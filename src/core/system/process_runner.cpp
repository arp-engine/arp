#include "core/system/process_runner.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace arp::core {
namespace {

#if defined(_WIN32) || defined(__CYGWIN__)
pmr::String quoteWindowsArgument(std::string_view value)
{
    if (!value.empty() && value.find_first_of(" \t\n\v\"") == std::string_view::npos)
        return pmr::String(value, &defaultCpuMemoryResource());
    pmr::String result("\"", &defaultCpuMemoryResource());
    std::size_t slashes = 0u;
    for (const char ch : value) {
        if (ch == '\\') {
            ++slashes;
            continue;
        }
        if (ch == '"') {
            result.append(slashes * 2u + 1u, '\\');
            result.push_back('"');
        } else {
            result.append(slashes, '\\');
            result.push_back(ch);
        }
        slashes = 0u;
    }
    result.append(slashes * 2u, '\\');
    result.push_back('"');
    return result;
}
#elif !defined(__EMSCRIPTEN__)
pmr::String posixError(const char* prefix, const int errorCode = errno)
{
    pmr::String result(prefix, &defaultCpuMemoryResource());
    result += std::strerror(errorCode);
    return result;
}
#endif

} // namespace

ProcessRunResult ProcessRunner::run(const ProcessRunRequest& request) const
{
    ProcessRunResult result;
#if !defined(_WIN32) && !defined(__CYGWIN__)
#if defined(__EMSCRIPTEN__)
    (void)request;
    result.diagnostic = "structured process execution is unavailable";
    return result;
#else
    if (request.executable.empty() || request.outputFile.empty()) {
        result.diagnostic = "process executable or output file is missing";
        return result;
    }

    const int output =
        ::open(request.outputFile.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        result.diagnostic = posixError("failed to open process output file: ");
        return result;
    }

    pmr::String executable(request.executable.string(), &defaultCpuMemoryResource());
    pmr::Vector<char*> arguments{&defaultCpuMemoryResource()};
    arguments.reserve(request.arguments.size() + 2u);
    arguments.push_back(executable.data());
    for (const auto& argument : request.arguments)
        arguments.push_back(const_cast<char*>(argument.c_str()));
    arguments.push_back(nullptr);

    // A successful exec closes the write end atomically. Before exec, the
    // child reports setup/exec errno through this pipe so callers can
    // distinguish a tool that ran and returned 127 from one that never
    // launched at all.
    int launchStatus[2]{-1, -1};
    if (::pipe(launchStatus) != 0) {
        ::close(output);
        result.diagnostic = posixError("failed to create process status pipe: ");
        return result;
    }
    if (::fcntl(launchStatus[1], F_SETFD, FD_CLOEXEC) != 0) {
        ::close(launchStatus[0]);
        ::close(launchStatus[1]);
        ::close(output);
        result.diagnostic = posixError("failed to configure process status pipe: ");
        return result;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(launchStatus[0]);
        ::close(launchStatus[1]);
        ::close(output);
        result.diagnostic = posixError("failed to launch process: ");
        return result;
    }
    if (child == 0) {
        ::close(launchStatus[0]);
        const auto reportLaunchFailure = [&launchStatus] {
            const int errorCode = errno;
            ssize_t bytesWritten = 0;
            do {
                bytesWritten = ::write(launchStatus[1], &errorCode, sizeof(errorCode));
            } while (bytesWritten < 0 && errno == EINTR);
            (void)bytesWritten;
            _exit(127);
        };
        if (!request.workingDirectory.empty()
            && ::chdir(request.workingDirectory.string().c_str()) != 0) {
            reportLaunchFailure();
        }
        if (::dup2(output, STDOUT_FILENO) < 0 || ::dup2(output, STDERR_FILENO) < 0) {
            reportLaunchFailure();
        }
        ::close(output);
        if (request.executable.has_parent_path())
            ::execv(executable.c_str(), arguments.data());
        else
            ::execvp(executable.c_str(), arguments.data());
        reportLaunchFailure();
    }

    ::close(output);
    ::close(launchStatus[1]);
    int launchError = 0;
    ssize_t statusBytes = 0;
    do {
        statusBytes = ::read(launchStatus[0], &launchError, sizeof(launchError));
    } while (statusBytes < 0 && errno == EINTR);
    const int statusReadError = statusBytes < 0 ? errno : 0;
    ::close(launchStatus[0]);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            result.diagnostic = posixError("failed to wait for process: ");
            return result;
        }
    }
    if (statusBytes != 0) {
        if (statusBytes == static_cast<ssize_t>(sizeof(launchError))) {
            result.diagnostic = posixError("failed to launch process: ", launchError);
        } else if (statusBytes < 0) {
            result.diagnostic =
                posixError("failed to read process launch status: ", statusReadError);
        } else {
            result.diagnostic = "process launch status was incomplete";
        }
        return result;
    }
    result.launched = true;
    if (WIFEXITED(status)) {
        result.exitCode = static_cast<std::uint32_t>(WEXITSTATUS(status));
        return result;
    }
    result.launched = false;
    result.diagnostic = "process terminated without an exit status";
    return result;
#endif
#else
    if (request.executable.empty() || request.outputFile.empty()) {
        result.diagnostic = "process executable or output file is missing";
        return result;
    }
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE output = CreateFileA(request.outputFile.string().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        result.diagnostic = "failed to open process output file";
        return result;
    }
    pmr::String command = quoteWindowsArgument(request.executable.string());
    for (const auto& argument : request.arguments) {
        command.push_back(' ');
        command += quoteWindowsArgument(std::string_view(argument.data(), argument.size()));
    }
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process{};
    const auto workingDirectory = request.workingDirectory.empty()
        ? pmr::String(&defaultCpuMemoryResource())
        : pmr::String(request.workingDirectory.string(), &defaultCpuMemoryResource());
    // Keep the executable as the first quoted command-line token so Windows
    // performs the normal PATH lookup for tool names such as `g++`. Passing
    // the same bare name as lpApplicationName bypasses that lookup.
    const BOOL created = CreateProcessA(nullptr,
        command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startup,
        &process);
    CloseHandle(output);
    if (!created) {
        result.diagnostic = "failed to launch process";
        return result;
    }
    result.launched = true;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0u;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        result.launched = false;
        result.diagnostic = "failed to query process exit code";
    }
    result.exitCode = static_cast<std::uint32_t>(exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
#endif
}

} // namespace arp::core
