#include "network/http/client.h"
#include "network/tls/tls_context.h"

#include <cassert>

int main()
{
    using namespace arp::network;

    const auto parsed = http::Uri::parse("https://localhost/");
    assert(parsed.success());

    const auto prepared = http::Client::prepareSecureTransport(parsed.uri);
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    assert(tls::backendAvailable());
    assert(prepared.success());
    assert(prepared.tls_config.server_name() == StringView("localhost"));
#else
    assert(!tls::backendAvailable());
    assert(!prepared.success());
    assert(prepared.diagnostic == http::DiagnosticCode::tls_unavailable);
    assert(prepared.tls_diagnostic == tls::TlsDiagnosticCode::disabled);
#endif
    return 0;
}
