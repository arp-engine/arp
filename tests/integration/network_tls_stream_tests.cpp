#include "network/tls/ssl_stream.h"
#include "network/tls/tls_context.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <functional>

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#endif

namespace {

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)

struct PemPair {
    arp::core::pmr::String certificate{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String private_key{&arp::core::defaultCpuMemoryResource()};
};

arp::core::pmr::String bioToString(BIO* bio)
{
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(bio, &memory);
    return arp::core::pmr::String(
        memory->data, memory->length, &arp::core::defaultCpuMemoryResource());
}

PemPair createSelfSignedCertificate()
{
    PemPair pair;
    EVP_PKEY* key = EVP_RSA_gen(2048);
    assert(key != nullptr);

    X509* cert = X509_new();
    assert(cert != nullptr);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 3600);
    X509_set_pubkey(cert, key);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(
        name, "C", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("CN"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(
        name, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("arp-test"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    assert(X509_sign(cert, key, EVP_sha256()) > 0);

    BIO* certBio = BIO_new(BIO_s_mem());
    BIO* keyBio = BIO_new(BIO_s_mem());
    assert(certBio != nullptr);
    assert(keyBio != nullptr);
    assert(PEM_write_bio_X509(certBio, cert) == 1);
    assert(PEM_write_bio_PrivateKey(keyBio, key, nullptr, nullptr, 0, nullptr, nullptr) == 1);
    pair.certificate = bioToString(certBio);
    pair.private_key = bioToString(keyBio);

    BIO_free(certBio);
    BIO_free(keyBio);
    X509_free(cert);
    EVP_PKEY_free(key);
    return pair;
}

class MemoryEndpoint {
public:
    void connect(MemoryEndpoint& peer) { peer_ = &peer; }

    template <typename Handler>
    void async_send(const void* data, std::size_t size, Handler&& handler)
    {
        assert(peer_ != nullptr);
        peer_->receive(static_cast<const std::uint8_t*>(data), size);
        arp::network::error_code ec;
        handler(ec, size);
    }

    template <typename Handler>
    void async_recv(arp::network::byte_buffer& buffer, Handler&& handler)
    {
        if (!incoming_.empty()) {
            complete(buffer, std::forward<Handler>(handler));
            return;
        }
        pending_buffer_ = &buffer;
        pending_handler_ = std::forward<Handler>(handler);
    }

private:
    template <typename Handler> void complete(arp::network::byte_buffer& buffer, Handler&& handler)
    {
        const std::size_t bytes =
            incoming_.size() < buffer.size() ? incoming_.size() : buffer.size();
        std::memcpy(buffer.data(), incoming_.data(), bytes);
        incoming_.erase(incoming_.begin(), incoming_.begin() + static_cast<std::ptrdiff_t>(bytes));
        arp::network::error_code ec;
        handler(ec, bytes);
    }

    void receive(const std::uint8_t* data, std::size_t size)
    {
        incoming_.insert(incoming_.end(), data, data + size);
        if (pending_buffer_ != nullptr && pending_handler_) {
            auto* buffer = pending_buffer_;
            auto handler = std::move(pending_handler_);
            pending_buffer_ = nullptr;
            complete(*buffer, handler);
        }
    }

    MemoryEndpoint* peer_ = nullptr;
    arp::network::byte_buffer incoming_{&arp::core::defaultCpuMemoryResource()};
    arp::network::byte_buffer* pending_buffer_ = nullptr;
    std::function<void(const arp::network::error_code&, std::size_t)> pending_handler_;
};

#endif

class GuardedStream {
public:
    template <typename Handler> void async_send(const void*, std::size_t, Handler&& handler)
    {
        (void)handler;
        ++send_count_;
    }

    template <typename Handler> void async_recv(arp::network::byte_buffer&, Handler&& handler)
    {
        (void)handler;
        ++recv_count_;
    }

    int send_count() const noexcept { return send_count_; }

    int recv_count() const noexcept { return recv_count_; }

private:
    int send_count_ = 0;
    int recv_count_ = 0;
};

void configStoresEndpointAndVerificationPolicy()
{
    using namespace arp::network;

    tls::TlsConfig client;
    client.set_endpoint_role(tls::TlsEndpointRole::client);
    client.set_server_name("example.com");
    client.set_certificate_verification(tls::CertificateVerification::system_trust);

    assert(client.endpoint_role() == tls::TlsEndpointRole::client);
    assert(client.server_name() == StringView("example.com"));
    assert(client.certificate_verification() == tls::CertificateVerification::system_trust);

    tls::TlsConfig server;
    server.set_endpoint_role(tls::TlsEndpointRole::server);
    server.set_certificate_verification(tls::CertificateVerification::disabled_for_test);

    assert(server.endpoint_role() == tls::TlsEndpointRole::server);
    assert(server.certificate_verification() == tls::CertificateVerification::disabled_for_test);
}

void disabledBackendFailsWithoutPlaintextFallback()
{
    using namespace arp::network;

    assert(!tls::backendAvailable());

    tls::TlsConfig config;
    config.set_endpoint_role(tls::TlsEndpointRole::client);
    config.set_server_name("example.com");
    tls::TlsContext context(config);
    tls::SslStream stream(context);
    GuardedStream tcp;

    assert(stream.state() == tls::TlsStreamState::idle);

    bool handshook = false;
    stream.async_handshake(tcp, [&](const tls::TlsStatus& status) {
        assert(!status.success());
        assert(status.diagnostic == tls::TlsDiagnosticCode::disabled);
        handshook = true;
    });
    assert(handshook);
    assert(stream.state() == tls::TlsStreamState::failed);
    assert(tcp.send_count() == 0);
    assert(tcp.recv_count() == 0);

    byte_buffer buffer{&arp::core::defaultCpuMemoryResource()};
    buffer.resize(8);
    bool read = false;
    stream.async_recv(tcp, buffer, [&](const tls::TlsStatus& status, std::size_t bytes) {
        assert(!status.success());
        assert(status.diagnostic == tls::TlsDiagnosticCode::disabled);
        assert(bytes == 0);
        read = true;
    });
    assert(read);
    assert(tcp.recv_count() == 0);

    const char payload[] = "ping";
    bool wrote = false;
    stream.async_send(
        tcp, payload, sizeof(payload) - 1, [&](const tls::TlsStatus& status, std::size_t bytes) {
            assert(!status.success());
            assert(status.diagnostic == tls::TlsDiagnosticCode::disabled);
            assert(bytes == 0);
            wrote = true;
        });
    assert(wrote);
    assert(tcp.send_count() == 0);

    bool shutDown = false;
    stream.async_shutdown(tcp, [&](const tls::TlsStatus& status) {
        assert(!status.success());
        assert(status.diagnostic == tls::TlsDiagnosticCode::disabled);
        shutDown = true;
    });
    assert(shutDown);
    assert(stream.state() == tls::TlsStreamState::shutdown);
}

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
void opensslBackendHandshakesAndEncryptsMemoryStream()
{
    using namespace arp::network;

    assert(tls::backendAvailable());
    const auto pem = createSelfSignedCertificate();

    tls::TlsConfig clientConfig;
    clientConfig.set_endpoint_role(tls::TlsEndpointRole::client);
    clientConfig.set_server_name("localhost");
    clientConfig.set_certificate_verification(tls::CertificateVerification::disabled_for_test);
    tls::TlsContext clientContext(clientConfig);
    assert(clientContext.status().success());

    tls::TlsConfig serverConfig;
    serverConfig.set_endpoint_role(tls::TlsEndpointRole::server);
    serverConfig.set_certificate_verification(tls::CertificateVerification::disabled_for_test);
    serverConfig.set_certificate_pem(pem.certificate);
    serverConfig.set_private_key_pem(pem.private_key);
    tls::TlsContext serverContext(serverConfig);
    assert(serverContext.status().success());

    tls::SslStream clientTls(clientContext);
    tls::SslStream serverTls(serverContext);
    MemoryEndpoint clientWire;
    MemoryEndpoint serverWire;
    clientWire.connect(serverWire);
    serverWire.connect(clientWire);

    bool clientHandshake = false;
    bool serverHandshake = false;
    clientTls.async_handshake(clientWire, [&](const tls::TlsStatus& status) {
        assert(status.success());
        clientHandshake = true;
    });
    serverTls.async_handshake(serverWire, [&](const tls::TlsStatus& status) {
        assert(status.success());
        serverHandshake = true;
    });
    assert(clientHandshake);
    assert(serverHandshake);
    assert(clientTls.state() == tls::TlsStreamState::established);
    assert(serverTls.state() == tls::TlsStreamState::established);

    byte_buffer serverBuffer{&arp::core::defaultCpuMemoryResource()};
    serverBuffer.resize(16);
    bool serverRead = false;
    serverTls.async_recv(
        serverWire, serverBuffer, [&](const tls::TlsStatus& status, std::size_t bytes) {
            assert(status.success());
            assert(bytes == 4);
            assert(std::memcmp(serverBuffer.data(), "ping", 4) == 0);
            serverRead = true;
        });

    bool clientWrite = false;
    clientTls.async_send(
        clientWire, "ping", 4, [&](const tls::TlsStatus& status, std::size_t bytes) {
            assert(status.success());
            assert(bytes == 4);
            clientWrite = true;
        });
    assert(clientWrite);
    assert(serverRead);
}
#endif

} // namespace

int main()
{
    configStoresEndpointAndVerificationPolicy();
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    opensslBackendHandshakesAndEncryptsMemoryStream();
#else
    disabledBackendFailsWithoutPlaintextFallback();
#endif
    return 0;
}
