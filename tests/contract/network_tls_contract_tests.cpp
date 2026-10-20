#include "network/http/client.h"
#include "network/tls/tls_context.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

arp::core::pmr::String readText(const std::filesystem::path& file)
{
    std::ifstream input(file);
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String line{&arp::core::defaultCpuMemoryResource()};
    while (std::getline(input, line)) {
        text += line;
        text += '\n';
    }
    return text;
}

void publicHeadersDoNotExposeNativeTls()
{
    const auto root = projectRoot();
    const std::filesystem::path headers[] = {
        root / "src/network/tls/tls_config.h",
        root / "src/network/tls/tls_context.h",
        root / "src/network/tls/ssl_stream.h",
    };
    for (const auto& header : headers) {
        const auto text = readText(header);
        assert(text.find("openssl/") == arp::core::pmr::String::npos);
        assert(text.find("SSL_CTX") == arp::core::pmr::String::npos);
        assert(text.find("X509") == arp::core::pmr::String::npos);
    }
}

} // namespace

int main()
{
    using namespace arp::network;

#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    assert(tls::backendAvailable());
    assert(tls::backendName() == StringView("OpenSSL"));

    tls::TlsConfig client;
    client.set_endpoint_role(tls::TlsEndpointRole::client);
    client.set_server_name("example.com");
    client.set_certificate_verification(tls::CertificateVerification::disabled_for_test);
    tls::TlsContext clientContext(client);
    assert(clientContext.status().success());
    assert(clientContext.config().server_name() == StringView("example.com"));

    tls::TlsConfig server;
    server.set_endpoint_role(tls::TlsEndpointRole::server);
    server.set_certificate_verification(tls::CertificateVerification::disabled_for_test);
    tls::TlsContext serverContext(server);
    assert(!serverContext.status().success());
    assert(serverContext.status().diagnostic == tls::TlsDiagnosticCode::missing_certificate);
#else
    assert(!tls::backendAvailable());
    assert(tls::backendName() == StringView("None"));

    tls::TlsConfig config;
    config.set_server_name("example.com");
    tls::TlsContext context(config);
    assert(!context.status().success());
    assert(context.status().diagnostic == tls::TlsDiagnosticCode::disabled);
    assert(context.config().server_name() == StringView("example.com"));

    const auto https = http::Client::prepareTransport(http::Uri::parse("https://example.com/").uri);
    assert(!https.success());
    assert(https.diagnostic == http::DiagnosticCode::tls_unavailable);

    const auto wss = http::Client::prepareTransport(http::Uri::parse("wss://example.com/").uri);
    assert(!wss.success());
    assert(wss.diagnostic == http::DiagnosticCode::tls_unavailable);
#endif

    publicHeadersDoNotExposeNativeTls();
    return 0;
}
