#include "editor/app/game_preview.h"

#include "core/number_format.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <string_view>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
namespace arp::editor {
#if defined(_WIN32)
struct ProcessWindowSearch {
    DWORD processId = 0;
    HWND window = nullptr;
};

BOOL CALLBACK findProcessWindow(HWND window, LPARAM parameter)
{
    auto& search = *reinterpret_cast<ProcessWindowSearch*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != search.processId || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;
    wchar_t className[64]{};
    GetClassNameW(window, className, 64);
    if (std::wcscmp(className, L"ConsoleWindowClass") == 0)
        return TRUE;
    search.window = window;
    return FALSE;
}

HWND findProcessWindow(DWORD processId)
{
    ProcessWindowSearch search{processId, nullptr};
    EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

struct GamePreviewHost::Impl final {
public:
    ~Impl() { close(); }

    bool start(const std::filesystem::path& projectRoot, arp::core::pmr::String& error)
    {
        if (process_) {
            error = "A game process is already active.";
            return false;
        }
        const std::filesystem::path candidates[] = {
            projectRoot / "build/win-auto-release/arp_world_of_claudecraft.exe",
            projectRoot / "build/win-auto-debug/arp_world_of_claudecraft.exe",
            projectRoot / "build/arp_world_of_claudecraft.exe"};
        std::filesystem::path executable;
        for (const auto& candidate : candidates)
            if (std::filesystem::is_regular_file(candidate)) {
                executable = candidate;
                break;
            }
        if (executable.empty()) {
            error =
                "Cannot find build/win-auto-release/arp_world_of_claudecraft.exe under the project root.";
            return false;
        }
        arp::core::pmr::Vector<wchar_t> command{&arp::core::defaultCpuMemoryResource()};
        command.push_back(L'"');
        command.insert(command.end(), executable.native().begin(), executable.native().end());
        constexpr std::wstring_view resourceRootOption = L"\" --resource-root \"";
        command.insert(command.end(), resourceRootOption.begin(), resourceRootOption.end());
        command.insert(command.end(), projectRoot.native().begin(), projectRoot.native().end());
        constexpr std::wstring_view previewOptions =
            L"\" --rhi-backend vulkan --width 1280 --height 720";
        command.insert(command.end(), previewOptions.begin(), previewOptions.end());
        command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(executable.c_str(),
                command.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                projectRoot.c_str(),
                &startup,
                &processInfo)) {
            error = "CreateProcessW failed with Win32 error ";
            arp::core::appendUnsigned(error, GetLastError());
            error += '.';
            return false;
        }
        CloseHandle(processInfo.hThread);
        process_ = processInfo.hProcess;
        processId_ = processInfo.dwProcessId;
        return true;
    }

    bool requestStop()
    {
        if (!process_)
            return false;
        stopping_ = true;
        stopRequestedAt_ = std::chrono::steady_clock::now();
        if (window_ && IsWindow(window_))
            PostMessageW(window_, WM_CLOSE, 0, 0);
        return true;
    }

    bool hasEditorWindow() const { return editorWindow_ != nullptr; }

    bool hasProcess() const { return process_ != nullptr; }

    Update update(const arp::ui::LayoutRect* viewport,
        unsigned pixelWidth,
        unsigned pixelHeight,
        bool visible)
    {
        const auto editorWindow = editorWindow_;
        if (!process_)
            return Update::Idle;
        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(process_, &exitCode) || exitCode != STILL_ACTIVE) {
            releaseHandles();
            return Update::Exited;
        }
        if (stopping_) {
            if (std::chrono::steady_clock::now() - stopRequestedAt_ > std::chrono::seconds(12)) {
                TerminateProcess(process_, 1);
                WaitForSingleObject(process_, 3000);
                releaseHandles();
                return Update::Exited;
            }
            return Update::Starting;
        }
        if (!window_) {
            window_ = findProcessWindow(processId_);
            if (!window_)
                return Update::Starting;
            SetLastError(ERROR_SUCCESS);
            if (!SetParent(window_, editorWindow) && GetLastError() != ERROR_SUCCESS) {
                requestStop();
                return Update::Starting;
            }
            editorThreadId_ = GetWindowThreadProcessId(editorWindow, nullptr);
            gameThreadId_ = GetWindowThreadProcessId(window_, nullptr);
            if (editorThreadId_ != 0 && gameThreadId_ != 0 && editorThreadId_ != gameThreadId_)
                inputAttached_ = AttachThreadInput(editorThreadId_, gameThreadId_, TRUE) != FALSE;
            const auto parentStyle = GetWindowLongPtrW(editorWindow, GWL_STYLE);
            if ((parentStyle & static_cast<LONG_PTR>(WS_CLIPCHILDREN)) == 0) {
                SetWindowLongPtrW(
                    editorWindow, GWL_STYLE, parentStyle | static_cast<LONG_PTR>(WS_CLIPCHILDREN));
                SetWindowPos(editorWindow,
                    nullptr,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            auto style = GetWindowLongPtrW(window_, GWL_STYLE);
            style &= ~static_cast<LONG_PTR>(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU
                | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            style &= ~static_cast<LONG_PTR>(WS_VISIBLE);
            style |= static_cast<LONG_PTR>(WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
            SetWindowLongPtrW(window_, GWL_STYLE, style);
            SetWindowPos(window_,
                HWND_TOP,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            geometryValid_ = false;
            windowVisible_ = false;
        }
        if (!editorWindow || !viewport || pixelWidth == 0 || pixelHeight == 0)
            return Update::Running;
        if (!visible) {
            if (windowVisible_)
                ShowWindow(window_, SW_HIDE);
            windowVisible_ = false;
            return Update::Running;
        }
        const int x = static_cast<int>(std::lround(viewport->x * pixelWidth / 1920.0f));
        const int y = static_cast<int>(std::lround(viewport->y * pixelHeight / 1080.0f));
        const int width =
            std::max(1, static_cast<int>(std::lround(viewport->width * pixelWidth / 1920.0f)));
        const int height =
            std::max(1, static_cast<int>(std::lround(viewport->height * pixelHeight / 1080.0f)));
        const bool geometryChanged =
            !geometryValid_ || x != x_ || y != y_ || width != width_ || height != height_;
        if (geometryChanged || !windowVisible_) {
            UINT flags = SWP_NOACTIVATE;
            if (!geometryChanged)
                flags |= SWP_NOMOVE | SWP_NOSIZE;
            if (!windowVisible_)
                flags |= SWP_SHOWWINDOW;
            SetWindowPos(window_, nullptr, x, y, width, height, flags);
            x_ = x;
            y_ = y;
            width_ = width;
            height_ = height;
            geometryValid_ = true;
            windowVisible_ = true;
        }
        return Update::Running;
    }

private:
    void close()
    {
        if (process_) {
            if (window_ && IsWindow(window_))
                PostMessageW(window_, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(process_, 5000) == WAIT_TIMEOUT)
                TerminateProcess(process_, 1);
            releaseHandles();
        }
    }

    void releaseHandles()
    {
        if (inputAttached_)
            AttachThreadInput(editorThreadId_, gameThreadId_, FALSE);
        if (process_)
            CloseHandle(process_);
        process_ = nullptr;
        processId_ = 0;
        window_ = nullptr;
        stopping_ = false;
        editorThreadId_ = 0;
        gameThreadId_ = 0;
        inputAttached_ = false;
        geometryValid_ = false;
        windowVisible_ = false;
    }

    HWND editorWindow_ = findProcessWindow(GetCurrentProcessId());
    HANDLE process_ = nullptr;
    DWORD processId_ = 0;
    DWORD editorThreadId_ = 0, gameThreadId_ = 0;
    HWND window_ = nullptr;
    bool stopping_ = false;
    bool inputAttached_ = false;
    bool geometryValid_ = false;
    bool windowVisible_ = false;
    int x_ = 0, y_ = 0, width_ = 0, height_ = 0;
    std::chrono::steady_clock::time_point stopRequestedAt_{};
};
#elif !defined(__EMSCRIPTEN__)
// On platforms without a portable native child-window API, run the game as a
// normal top-level window. Keyboard and pointer input then follow the OS path.
struct GamePreviewHost::Impl final {
public:
    ~Impl() { close(); }

    bool start(const std::filesystem::path& projectRoot, arp::core::pmr::String& error)
    {
        if (process_ > 0) {
            error = "A game process is already active.";
            return false;
        }
        const std::filesystem::path candidates[] = {projectRoot / "build/arp_world_of_claudecraft",
            projectRoot / "build/linux-auto-release/arp_world_of_claudecraft",
            projectRoot / "build/linux-auto-debug/arp_world_of_claudecraft",
            projectRoot
                / "build/mac-auto-release/arp_world_of_claudecraft.app/Contents/MacOS/arp_world_of_claudecraft",
            projectRoot
                / "build/mac-auto-debug/arp_world_of_claudecraft.app/Contents/MacOS/arp_world_of_claudecraft"};
        std::filesystem::path executable;
        for (const auto& candidate : candidates)
            if (std::filesystem::is_regular_file(candidate)) {
                executable = candidate;
                break;
            }
        if (executable.empty()) {
            error =
                "Cannot find a native arp_world_of_claudecraft executable under the project root.";
            return false;
        }
        auto executableString = arp::core::toPmrString(executable.string());
        auto rootString = arp::core::toPmrString(projectRoot.string());
#if defined(__APPLE__)
        char* backend = const_cast<char*>("--rhi-backend");
        char* backendName = const_cast<char*>("metal");
#else
        char* backend = const_cast<char*>("--rhi-backend");
        char* backendName = const_cast<char*>("vulkan");
#endif
        char* argv[] = {executableString.data(),
            const_cast<char*>("--resource-root"),
            rootString.data(),
            backend,
            backendName,
            const_cast<char*>("--width"),
            const_cast<char*>("1280"),
            const_cast<char*>("--height"),
            const_cast<char*>("720"),
            nullptr};
        const int result =
            posix_spawn(&process_, executableString.c_str(), nullptr, nullptr, argv, environ);
        if (result != 0) {
            process_ = -1;
            error = "posix_spawn failed with error ";
            arp::core::appendUnsigned(error, static_cast<unsigned>(result));
            error += '.';
            return false;
        }
        stopping_ = false;
        return true;
    }

    bool requestStop()
    {
        if (process_ <= 0)
            return false;
        stopping_ = true;
        stopRequestedAt_ = std::chrono::steady_clock::now();
        kill(process_, SIGTERM);
        return true;
    }

    bool hasEditorWindow() const { return true; }

    bool hasProcess() const { return process_ > 0; }

    Update update(const arp::ui::LayoutRect*, unsigned, unsigned, bool)
    {
        if (process_ <= 0)
            return Update::Idle;
        int status = 0;
        const pid_t result = waitpid(process_, &status, WNOHANG);
        if (result == process_ || (result < 0 && errno == ECHILD)) {
            process_ = -1;
            stopping_ = false;
            return Update::Exited;
        }
        if (result < 0) {
            process_ = -1;
            stopping_ = false;
            return Update::Exited;
        }
        if (stopping_
            && std::chrono::steady_clock::now() - stopRequestedAt_ > std::chrono::seconds(12))
            kill(process_, SIGKILL);
        return Update::Running;
    }

private:
    void close()
    {
        if (process_ <= 0)
            return;
        kill(process_, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = waitpid(process_, nullptr, WNOHANG);
            if (result == process_ || (result < 0 && errno == ECHILD)) {
                process_ = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        kill(process_, SIGKILL);
        while (waitpid(process_, nullptr, 0) < 0 && errno == EINTR) {
        }
        process_ = -1;
    }

    pid_t process_ = -1;
    bool stopping_ = false;
    std::chrono::steady_clock::time_point stopRequestedAt_{};
};
#else
struct GamePreviewHost::Impl final {
public:
    bool start(const std::filesystem::path&, arp::core::pmr::String& error)
    {
        error = "Launching a native game process is unavailable in the browser build.";
        return false;
    }

    bool requestStop() { return false; }

    bool hasEditorWindow() const { return true; }

    bool hasProcess() const { return false; }

    Update update(const arp::ui::LayoutRect*, unsigned, unsigned, bool) { return Update::Idle; }
};
#endif

GamePreviewHost::GamePreviewHost() : impl_(core::makeUnique<Impl>())
{}

GamePreviewHost::~GamePreviewHost() = default;

bool GamePreviewHost::hasEditorWindow() const
{
    return impl_->hasEditorWindow();
}

bool GamePreviewHost::start(const std::filesystem::path& root, core::pmr::String& diagnostic)
{
    return impl_->start(root, diagnostic);
}

bool GamePreviewHost::requestStop()
{
    return impl_->requestStop();
}

GamePreviewHost::Update GamePreviewHost::update(
    const ui::LayoutRect* viewport, unsigned width, unsigned height, bool visible)
{
    return impl_->update(viewport, width, height, visible);
}

bool GamePreviewHost::synchronize(
    EditorShell& shell, unsigned width, unsigned height, bool minimized)
{
    bool dirty = false;
    const auto command = shell.consumeGamePreviewCommand();
    if (command == GamePreviewModel::Request::Start) {
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        if (start(shell.projectRoot(), diagnostic)) {
#if defined(_WIN32)
            shell.setGamePreviewState(false, true, "Waiting for the project game window...");
#else
            shell.setGamePreviewState(false, true, "Game launched in a separate native window.");
#endif
        } else
            shell.setGamePreviewState(false, false, diagnostic.c_str());
        dirty = true;
    } else if (command == GamePreviewModel::Request::Stop && !requestStop()) {
        shell.setGamePreviewState(false, false, "Game preview stopped.");
        dirty = true;
    }
    // An idle host has no window geometry to update. Avoid resolving the full
    // workspace layout on every iteration of the event-driven idle loop.
    if (!impl_->hasProcess())
        return dirty;
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(shell.id("viewport"));
    auto rect = viewport ? viewport->rect : ui::LayoutRect{};
    if (shell.outputOpen()) {
        if (const auto* output = layout.findWidget(shell.id("output_panel")))
            rect.height = std::max(1.0f, output->rect.y - rect.y);
    }
    const auto state = update(
        viewport ? &rect : nullptr, width, height, shell.workspace() == "Game" && !minimized);
#if defined(_WIN32)
    if (state == Update::Running && !shell.gamePreviewRunning()) {
        shell.setGamePreviewState(true, false, "Game running in the editor viewport.");
        dirty = true;
    }
#endif
    if (state == Update::Exited) {
        shell.setGamePreviewState(false, false, "Game stopped.");
        dirty = true;
    }
    return dirty;
}
} // namespace arp::editor
