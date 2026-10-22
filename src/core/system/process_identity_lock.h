#pragma once

#include "core/api.h"

#include <filesystem>
#include <string_view>

namespace arp::core {

// Serializes publication for one content identity across threads and process
// boundaries. The caller owns the cache directory and must create it before
// constructing the lock.
class ARP_API ProcessIdentityLock final {
public:
    ProcessIdentityLock(const std::filesystem::path& lockPath, std::string_view identity);
    ~ProcessIdentityLock();

    ProcessIdentityLock(const ProcessIdentityLock&) = delete;
    ProcessIdentityLock& operator=(const ProcessIdentityLock&) = delete;

    bool locked() const noexcept { return locked_; }

private:
#if defined(_WIN32) || defined(__CYGWIN__)
    void* handle_ = nullptr;
#else
    int descriptor_ = -1;
#endif
    bool locked_ = false;
};

} // namespace arp::core
