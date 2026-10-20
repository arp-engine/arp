#include "core/process_identity_lock.h"

#include "core/cpu_allocator.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace arp::core {

ProcessIdentityLock::ProcessIdentityLock(
    const std::filesystem::path& lockPath, std::string_view identity)
{
#if defined(_WIN32) || defined(__CYGWIN__)
    (void)identity;
    const auto path = lockPath.wstring();
    handle_ = CreateFileW(path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        return;
    }
    OVERLAPPED operation{};
    locked_ = LockFileEx(static_cast<HANDLE>(handle_),
                  LOCKFILE_EXCLUSIVE_LOCK,
                  0u,
                  MAXDWORD,
                  MAXDWORD,
                  &operation)
        != FALSE;
#else
    const auto path = lockPath.string();
    descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (descriptor_ < 0)
        return;
    for (;;) {
        if (::flock(descriptor_, LOCK_EX) == 0) {
            locked_ = true;
            return;
        }
        if (errno != EINTR)
            break;
    }
    ::close(descriptor_);
    descriptor_ = -1;
#endif
}

ProcessIdentityLock::~ProcessIdentityLock()
{
#if defined(_WIN32) || defined(__CYGWIN__)
    if (locked_) {
        OVERLAPPED operation{};
        (void)UnlockFileEx(static_cast<HANDLE>(handle_), 0u, MAXDWORD, MAXDWORD, &operation);
    }
    if (handle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(handle_));
#else
    if (descriptor_ >= 0) {
        if (locked_)
            (void)::flock(descriptor_, LOCK_UN);
        ::close(descriptor_);
    }
#endif
}

} // namespace arp::core
