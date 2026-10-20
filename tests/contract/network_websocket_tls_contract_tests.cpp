#include "network/http/client.h"
#include "network/websocket/stream.h"

#include <cassert>

int main()
{
    using namespace arp::network;

    const auto wss = http::Uri::parse("wss://secure.example/chat");
    assert(wss.success());

    const auto prepared = websocket::Stream::prepareSecureClient(wss.uri);
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    assert(tls::backendAvailable());
    assert(prepared.success());
    assert(!prepared.plaintext_fallback_allowed);
    assert(prepared.http_diagnostic == http::DiagnosticCode::none);
    assert(prepared.tls_diagnostic == tls::TlsDiagnosticCode::none);
#else
    assert(!tls::backendAvailable());
    assert(!prepared.success());
    assert(!prepared.plaintext_fallback_allowed);
    assert(prepared.http_diagnostic == http::DiagnosticCode::tls_unavailable);
    assert(prepared.tls_diagnostic == tls::TlsDiagnosticCode::disabled);
#endif
    assert(prepared.tls_config.server_name() == StringView("secure.example"));
    assert(prepared.tls_config.endpoint_role() == tls::TlsEndpointRole::client);

    const auto genericTransport = http::Client::prepareTransport(wss.uri);
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    assert(genericTransport.success());
#else
    assert(!genericTransport.success());
#endif
    assert(genericTransport.transport == http::ClientTransportKind::tls_required);
#if !defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    assert(genericTransport.diagnostic == http::DiagnosticCode::tls_unavailable);
#endif

    const auto ws = http::Uri::parse("ws://secure.example/chat");
    assert(ws.success());
    assert(http::Client::prepareTransport(ws.uri).success());
    return 0;
}
