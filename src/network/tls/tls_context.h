#pragma once

#include "network/tls/tls_config.h"

namespace arp::network::tls {

ARP_API bool backendAvailable() noexcept;
ARP_API StringView backendName() noexcept;

class ARP_API TlsContext {
public:
    explicit TlsContext(TlsConfig config = {});
    ~TlsContext();
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    const TlsConfig& config() const noexcept;
    const TlsStatus& status() const noexcept;

private:
    friend class SslStream;
    class Impl;

    // backend-private native handle: only SslStream may use the TLS provider handle.
    void* native_handle() const noexcept;

    TlsConfig config_;
    TlsStatus status_;
    core::UniquePtr<Impl> impl_;
};

} // namespace arp::network::tls
