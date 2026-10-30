#pragma once

#include "network/http/byte_read_transport.h"

#include <atomic>

namespace arp::network::http {

// Main-browser-thread adapter. The JS registry uses both address and operation
// identity; detached promises never dereference a reused C++ address.
class FetchByteTransfer final {
public:
    using CompleteFn = void (*)(void*);
    static bool supported() noexcept;
    ByteReadStatus start(const ByteReadDesc& desc,
        const ByteReadRequest& request,
        ByteReadObserver& receiver,
        const std::atomic_bool& closed,
        CompleteFn complete,
        void* context);
    void cancel() noexcept;
    bool cancelled() const noexcept;
    bool headers(unsigned status, std::size_t length) noexcept;
    bool append(std::size_t length) noexcept;
    void terminal(unsigned reason) noexcept;

private:
    const ByteReadRequest* request_ = nullptr;
    ByteReadObserver* receiver_ = nullptr;
    const std::atomic_bool* closed_ = nullptr;
    CompleteFn complete_ = nullptr;
    void* context_ = nullptr;
    core::pmr::Vector<std::uint8_t> scratch_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<char> header_{&core::defaultCpuMemoryResource()};
    bool active_ = false;
};

} // namespace arp::network::http
