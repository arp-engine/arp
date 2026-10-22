#include "core/io/staged_directory.h"

#include "core/io/filesystem_path.h"
#include "core/text/number_format.h"

#include <cerrno>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif

namespace arp::core::io {
namespace {
bool absent(const std::filesystem::path& path)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return status.type() == std::filesystem::file_type::not_found
        && (!error || error == std::errc::no_such_file_or_directory);
}
} // namespace

StagedDirectory::~StagedDirectory()
{
    if (!owned_)
        return;
    std::error_code error;
    const auto resolved = std::filesystem::canonical(staging_, error);
    if (!error && resolved == staging_ && resolved.parent_path() == parent_
        && FilePath::contains(parent_, resolved))
        std::filesystem::remove_all(resolved, error);
}

bool StagedDirectory::begin(const std::filesystem::path& destination)
{
    if (owned_) {
        diagnostic_ = "ARP-DIRECTORY-STAGING-ACTIVE";
        return false;
    }
    const auto name = FilePath::utf8(destination.filename());
    if (!FilePath::isSafeRelativePath(name) || destination.filename().has_parent_path()) {
        diagnostic_ = "ARP-DIRECTORY-DESTINATION-INVALID";
        return false;
    }
    std::error_code error;
    parent_ = std::filesystem::canonical(
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::current_path(),
        error);
    if (error || !std::filesystem::is_directory(parent_, error) || error) {
        diagnostic_ = "ARP-DIRECTORY-PARENT-INVALID";
        return false;
    }
    destination_ = parent_ / destination.filename();
    if (!absent(destination_)) {
        diagnostic_ = "ARP-DIRECTORY-DESTINATION-EXISTS";
        return false;
    }
    staging_ = destination_;
    staging_ += ".arp-staging";
    if (!std::filesystem::create_directory(staging_, error) || error) {
        diagnostic_ = "ARP-DIRECTORY-STAGING-CREATE";
        return false;
    }
    owned_ = true;
    diagnostic_.clear();
    return true;
}

bool StagedDirectory::publish()
{
    if (!owned_) {
        diagnostic_ = "ARP-DIRECTORY-STAGING-INACTIVE";
        return false;
    }
    std::error_code error;
    if (std::filesystem::canonical(staging_, error) != staging_ || error
        || std::filesystem::canonical(parent_, error) != parent_ || error) {
        diagnostic_ = "ARP-DIRECTORY-STAGING-PATH-CHANGED";
        return false;
    }
    bool published = false;
    std::error_code nativeError;
#ifdef _WIN32
    published = MoveFileW(staging_.c_str(), destination_.c_str()) != 0;
    if (!published)
        nativeError = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#elif defined(__linux__) && defined(SYS_renameat2)
    published = syscall(SYS_renameat2,
                    AT_FDCWD,
                    staging_.c_str(),
                    AT_FDCWD,
                    destination_.c_str(),
                    RENAME_NOREPLACE)
        == 0;
    if (!published)
        nativeError = std::error_code(errno, std::generic_category());
#elif defined(__APPLE__)
    published = renamex_np(staging_.c_str(), destination_.c_str(), RENAME_EXCL) == 0;
    if (!published)
        nativeError = std::error_code(errno, std::generic_category());
#else
    diagnostic_ = "ARP-DIRECTORY-EXCLUSIVE-PUBLISH-UNSUPPORTED";
    return false;
#endif
    if (!published) {
        diagnostic_ = "ARP-DIRECTORY-EXCLUSIVE-PUBLISH-FAILED";
        diagnostic_ += " native=";
        diagnostic_ += nativeError.category().name();
        diagnostic_ += ':';
        appendUnsigned(diagnostic_, nativeError.value());
        return false;
    }
    owned_ = false;
    diagnostic_.clear();
    return true;
}
} // namespace arp::core::io
