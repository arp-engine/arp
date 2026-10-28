#pragma once

#include "editor/remote/editor_remote_server.h"

#include <chrono>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace arp::editor::tests {
// Launch the actual Interpreter CLI and pump the owning loopback server.
// Shared by native and ScriptComponent protocol integration tests.
#if defined(_WIN32)
inline std::string quoteWindowsArgument(std::string_view value)
{
    std::string result("\"");
    std::size_t backslashes = 0;
    for (const auto character : value) {
        if (character == '\\') {
            ++backslashes;
            continue;
        }
        if (character == '"')
            result.append(backslashes * 2 + 1, '\\');
        else
            result.append(backslashes, '\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}
#endif

inline bool runEditorCli(arp::editor::EditorRemoteServer& remote,
    std::string_view endpoint,
    std::string_view token,
    std::initializer_list<std::string_view> arguments)
{
#if defined(ARP_EDITOR_CLI_EXECUTABLE) && defined(_WIN32)
    const auto command = quoteWindowsArgument(ARP_EDITOR_CLI_EXECUTABLE) + " --url "
        + quoteWindowsArgument(endpoint) + " --token " + quoteWindowsArgument(token);
    std::string completeCommand = command;
    for (const auto argument : arguments)
        completeCommand += " " + quoteWindowsArgument(argument);
    auto mutableCommand =
        arp::core::pmr::String(completeCommand, &arp::core::defaultCpuMemoryResource());
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        remote.pump();
        if (WaitForSingleObject(process.hProcess, 1) == WAIT_OBJECT_0)
            break;
        Sleep(1);
    }
    const bool completed = WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0;
    if (!completed)
        TerminateProcess(process.hProcess, 1);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return completed && exitCode == 0;
#elif defined(ARP_EDITOR_CLI_EXECUTABLE)
    std::vector<std::string> command{
        ARP_EDITOR_CLI_EXECUTABLE, "--url", std::string(endpoint), "--token", std::string(token)};
    for (const auto argument : arguments)
        command.emplace_back(argument);
    std::vector<char*> argv;
    argv.reserve(command.size() + 1u);
    for (auto& argument : command)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    const auto child = fork();
    if (child < 0)
        return false;
    if (child == 0) {
        execv(ARP_EDITOR_CLI_EXECUTABLE, argv.data());
        _exit(127);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        remote.pump();
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (result < 0)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    return false;
#else
    (void)remote;
    (void)endpoint;
    (void)token;
    (void)arguments;
    return false;
#endif
}
} // namespace arp::editor::tests
