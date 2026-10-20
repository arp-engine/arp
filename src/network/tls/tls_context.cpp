#include "network/tls/tls_context.h"

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#endif

namespace arp::network::tls {

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)

class TlsContext::Impl final {
public:
    ~Impl()
    {
        if (ctx != nullptr) {
            SSL_CTX_free(ctx);
        }
    }

    SSL_CTX* ctx = nullptr;
};

namespace {

TlsDiagnosticCode loadCertificate(SSL_CTX* ctx, StringView pem)
{
    if (pem.empty()) {
        return TlsDiagnosticCode::missing_certificate;
    }
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return TlsDiagnosticCode::backend_error;
    }
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (cert == nullptr) {
        return TlsDiagnosticCode::invalid_certificate;
    }
    const int used = SSL_CTX_use_certificate(ctx, cert);
    X509_free(cert);
    return used == 1 ? TlsDiagnosticCode::none : TlsDiagnosticCode::invalid_certificate;
}

TlsDiagnosticCode loadPrivateKey(SSL_CTX* ctx, StringView pem)
{
    if (pem.empty()) {
        return TlsDiagnosticCode::missing_private_key;
    }
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return TlsDiagnosticCode::backend_error;
    }
    EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (key == nullptr) {
        return TlsDiagnosticCode::invalid_private_key;
    }
    const int used = SSL_CTX_use_PrivateKey(ctx, key);
    EVP_PKEY_free(key);
    if (used != 1 || SSL_CTX_check_private_key(ctx) != 1) {
        return TlsDiagnosticCode::invalid_private_key;
    }
    return TlsDiagnosticCode::none;
}

TlsDiagnosticCode loadTrustStore(SSL_CTX* ctx, StringView pem)
{
    if (pem.empty()) {
        return TlsDiagnosticCode::missing_trust_store;
    }
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return TlsDiagnosticCode::backend_error;
    }
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (cert == nullptr) {
        return TlsDiagnosticCode::invalid_trust_store;
    }
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    const int added = store != nullptr ? X509_STORE_add_cert(store, cert) : 0;
    X509_free(cert);
    return added == 1 ? TlsDiagnosticCode::none : TlsDiagnosticCode::invalid_trust_store;
}

} // namespace

#endif

bool TlsStatus::success() const noexcept
{
    return diagnostic == TlsDiagnosticCode::none;
}

TlsConfig::TlsConfig()
    : server_name_(&core::defaultCpuMemoryResource()),
      certificate_pem_(&core::defaultCpuMemoryResource()),
      private_key_pem_(&core::defaultCpuMemoryResource()),
      trusted_certificate_pem_(&core::defaultCpuMemoryResource())
{}

void TlsConfig::set_server_name(StringView serverName)
{
    server_name_.assign(serverName.data(), serverName.size());
}

StringView TlsConfig::server_name() const noexcept
{
    return StringView(server_name_.data(), server_name_.size());
}

void TlsConfig::set_endpoint_role(TlsEndpointRole role) noexcept
{
    endpoint_role_ = role;
}

TlsEndpointRole TlsConfig::endpoint_role() const noexcept
{
    return endpoint_role_;
}

void TlsConfig::set_certificate_verification(CertificateVerification verification) noexcept
{
    certificate_verification_ = verification;
}

CertificateVerification TlsConfig::certificate_verification() const noexcept
{
    return certificate_verification_;
}

void TlsConfig::set_certificate_pem(StringView certificatePem)
{
    certificate_pem_.assign(certificatePem.data(), certificatePem.size());
}

StringView TlsConfig::certificate_pem() const noexcept
{
    return StringView(certificate_pem_.data(), certificate_pem_.size());
}

void TlsConfig::set_private_key_pem(StringView privateKeyPem)
{
    private_key_pem_.assign(privateKeyPem.data(), privateKeyPem.size());
}

StringView TlsConfig::private_key_pem() const noexcept
{
    return StringView(private_key_pem_.data(), private_key_pem_.size());
}

void TlsConfig::set_trusted_certificate_pem(StringView trustedCertificatePem)
{
    trusted_certificate_pem_.assign(trustedCertificatePem.data(), trustedCertificatePem.size());
}

StringView TlsConfig::trusted_certificate_pem() const noexcept
{
    return StringView(trusted_certificate_pem_.data(), trusted_certificate_pem_.size());
}

bool backendAvailable() noexcept
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    return true;
#else
    return false;
#endif
}

StringView backendName() noexcept
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    return "OpenSSL";
#else
    return "None";
#endif
}

TlsContext::TlsContext(TlsConfig config) : config_(config)
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    OPENSSL_init_ssl(0, nullptr);
    const SSL_METHOD* method = config_.endpoint_role() == TlsEndpointRole::server
        ? TLS_server_method()
        : TLS_client_method();
    impl_ = core::makeUnique<Impl>();
    impl_->ctx = SSL_CTX_new(method);
    if (impl_->ctx == nullptr) {
        status_.diagnostic = TlsDiagnosticCode::backend_error;
        return;
    }
    SSL_CTX_set_min_proto_version(impl_->ctx, TLS1_2_VERSION);

    if (config_.certificate_verification() == CertificateVerification::disabled_for_test) {
        SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_PEER, nullptr);
        if (config_.certificate_verification() == CertificateVerification::explicit_trust) {
            status_.diagnostic = loadTrustStore(impl_->ctx, config_.trusted_certificate_pem());
            if (!status_.success()) {
                return;
            }
        } else {
            SSL_CTX_set_default_verify_paths(impl_->ctx);
        }
    }

    if (config_.endpoint_role() == TlsEndpointRole::server) {
        status_.diagnostic = loadCertificate(impl_->ctx, config_.certificate_pem());
        if (!status_.success()) {
            return;
        }
        status_.diagnostic = loadPrivateKey(impl_->ctx, config_.private_key_pem());
        if (!status_.success()) {
            return;
        }
    }
#else
    status_.diagnostic = TlsDiagnosticCode::disabled;
#endif
}

TlsContext::~TlsContext() = default;

const TlsConfig& TlsContext::config() const noexcept
{
    return config_;
}

const TlsStatus& TlsContext::status() const noexcept
{
    return status_;
}

void* TlsContext::native_handle() const noexcept
{
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    return impl_ ? impl_->ctx : nullptr;
#else
    return nullptr;
#endif
}

} // namespace arp::network::tls
