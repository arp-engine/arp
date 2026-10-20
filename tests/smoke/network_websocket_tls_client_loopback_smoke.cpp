#include "network/endpoint.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket.h"
#include "network/socket_runtime.h"
#include "network/tls/ssl_stream.h"
#include "network/tls/tls_context.h"
#include "network/transport.h"
#include "network/websocket/client.h"

#include <cassert>
#include <thread>

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

class ServerTlsWsTransport {
public:
    ServerTlsWsTransport(
        arp::network::tls::SslStream* tls, arp::network::tcp::socket<arp::network::ipv4>* socket)
        : tls_(tls), socket_(socket)
    {}

    template <typename Handler>
    void async_recv(arp::network::byte_buffer& buffer, Handler&& handler)
    {
        tls_->async_recv(*socket_,
            buffer,
            [handler](const arp::network::tls::TlsStatus& status, std::size_t bytes) mutable {
                auto ec = status.success()
                    ? arp::network::error_code()
                    : arp::network::make_error_code(arp::network::inet_errors::operation_aborted);
                handler(ec, bytes);
            });
    }

    template <typename Handler>
    void async_send(const void* data, std::size_t size, Handler&& handler)
    {
        tls_->async_send(*socket_,
            data,
            size,
            [handler](const arp::network::tls::TlsStatus& status, std::size_t bytes) mutable {
                auto ec = status.success()
                    ? arp::network::error_code()
                    : arp::network::make_error_code(arp::network::inet_errors::operation_aborted);
                handler(ec, bytes);
            });
    }

private:
    arp::network::tls::SslStream* tls_ = nullptr;
    arp::network::tcp::socket<arp::network::ipv4>* socket_ = nullptr;
};

#endif

} // namespace

int main()
{
    using namespace arp::network;

    SocketRuntime runtime;
    assert(runtime.valid());

#if !defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    io_context context;
    websocket::Client client(&context);
    auto request = websocket::ClientRequest::connect("wss://127.0.0.1/chat");

    bool completed = false;
    client.async_connect(request, [&](const websocket::ClientStatus& status) {
        assert(!status.success());
        assert(status.http_diagnostic == http::DiagnosticCode::tls_unavailable);
        assert(status.tls_diagnostic == tls::TlsDiagnosticCode::disabled);
        completed = true;
    });
    assert(completed);
#else
    const auto pem = createSelfSignedCertificate();

    io_service service;
    io_context context(&service);
    error_code ec;

    tcp::socket<ipv4> acceptor(&context);
    tcp::socket<ipv4> serverClient(&context);

    assert(acceptor.open(ec) == 0);
    assert(acceptor.set_reuseaddr(ec, true) == 0);
    assert(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    endpoint listenEndpoint(acceptor.local_endpoint());
    assert(listenEndpoint.port() != 0);
    assert(acceptor.listen(ec, 4) == 0);

    tls::TlsConfig serverConfig;
    serverConfig.set_endpoint_role(tls::TlsEndpointRole::server);
    serverConfig.set_certificate_verification(tls::CertificateVerification::disabled_for_test);
    serverConfig.set_certificate_pem(pem.certificate);
    serverConfig.set_private_key_pem(pem.private_key);
    tls::TlsContext serverContext(serverConfig);
    assert(serverContext.status().success());
    tls::SslStream serverTls(serverContext);
    ServerTlsWsTransport serverTransport(&serverTls, &serverClient);
    websocket::Stream server(websocket::StreamRole::server);

    bool accepted = false;
    bool completed = false;

    acceptor.async_accept(serverClient, [&](const error_code& acceptEc) {
        assert(!acceptEc);
        accepted = true;
        serverTls.async_handshake(serverClient, [&](const tls::TlsStatus& handshakeStatus) {
            assert(handshakeStatus.success());
            server.async_server_accept(
                serverTransport, StringView(), [&](const websocket::StreamStatus& acceptStatus) {
                    assert(acceptStatus.success());
                    server.async_read_frame(serverTransport,
                        [&](const websocket::StreamStatus& readStatus,
                            const websocket::Frame& frame) {
                            assert(readStatus.success());
                            assert(frame.opcode() == websocket::Opcode::text);
                            server.async_write_text(serverTransport,
                                "secure pong",
                                [&](const websocket::StreamStatus& writeStatus) {
                                    assert(writeStatus.success());
                                });
                        });
                });
        });
    });

    arp::core::pmr::String uri{&arp::core::defaultCpuMemoryResource()};
    uri += "wss://";
    uri += listenEndpoint.to_string();
    uri += "/secure-chat";
    auto request = websocket::ClientRequest::connect(uri);
    request.tls_config().set_certificate_verification(
        tls::CertificateVerification::disabled_for_test);
    websocket::Client client(&context, request);

    client.connect([&](const websocket::ClientStatus& connectStatus) {
        assert(connectStatus.success());
        client.send("secure ping", [&](const websocket::ClientStatus& writeStatus) {
            assert(writeStatus.success());
            client.receive(
                [&](const websocket::ClientStatus& readStatus, const websocket::Frame& frame) {
                    assert(readStatus.success());
                    assert(frame.opcode() == websocket::Opcode::text);
                    assert(frame.payload_text() == "secure pong");
                    completed = true;
                    context.shutdown();
                });
        });
    });

    std::thread reactor([&]() {
        service.run();
    });

    while (!service.quit()) {
        context.dispatch();
    }

    reactor.join();
    context.dispatch();
    assert(accepted);
    assert(completed);
#endif

    return 0;
}
