#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

namespace arp::network {

class ARP_API SocketRuntime final {
public:
    SocketRuntime();
    ~SocketRuntime();
    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;

    bool valid() const;
    const arp::core::pmr::String& diagnostics() const;

private:
    bool valid_ = false;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::network
