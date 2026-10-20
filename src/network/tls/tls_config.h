#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "network/compat.h"

namespace arp::network::tls {

enum class TlsDiagnosticCode {
    none,
    disabled,
    unsupported_backend,
    backend_error,
    missing_certificate,
    invalid_certificate,
    missing_private_key,
    invalid_private_key,
    missing_trust_store,
    invalid_trust_store,
    invalid_state,
    transport_error,
    connection_closed,
    handshake_failed,
};

enum class TlsEndpointRole {
    client,
    server,
};

enum class CertificateVerification {
    system_trust,
    explicit_trust,
    disabled_for_test,
};

struct TlsStatus {
    TlsDiagnosticCode diagnostic = TlsDiagnosticCode::none;

    bool success() const noexcept;
};

class ARP_API TlsConfig {
public:
    TlsConfig();

    void set_server_name(StringView serverName);
    StringView server_name() const noexcept;

    void set_endpoint_role(TlsEndpointRole role) noexcept;
    TlsEndpointRole endpoint_role() const noexcept;

    void set_certificate_verification(CertificateVerification verification) noexcept;
    CertificateVerification certificate_verification() const noexcept;

    void set_certificate_pem(StringView certificatePem);
    StringView certificate_pem() const noexcept;

    void set_private_key_pem(StringView privateKeyPem);
    StringView private_key_pem() const noexcept;

    void set_trusted_certificate_pem(StringView trustedCertificatePem);
    StringView trusted_certificate_pem() const noexcept;

private:
    TlsEndpointRole endpoint_role_ = TlsEndpointRole::client;
    CertificateVerification certificate_verification_ = CertificateVerification::system_trust;
    core::pmr::String server_name_;
    core::pmr::String certificate_pem_;
    core::pmr::String private_key_pem_;
    core::pmr::String trusted_certificate_pem_;
};

} // namespace arp::network::tls
