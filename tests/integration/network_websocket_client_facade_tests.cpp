#include "network/websocket/client.h"

#include <cassert>

namespace {

void buildsClientRequestFromWsUri()
{
    using namespace arp::network;

    auto request = websocket::ClientRequest::connect("ws://127.0.0.1:8080/chat?room=1");
    assert(request.success());
    assert(request.uri().scheme() == "ws");
    assert(request.host_header() == "127.0.0.1:8080");
    assert(request.resource() == "/chat?room=1");
    assert(request.key() == "dGhlIHNhbXBsZSBub25jZQ==");
}

void rejectsWssWithoutPlaintextFallbackWhenTlsDisabled()
{
    using namespace arp::network;

    io_context context;
    websocket::Client client(&context);
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    auto request = websocket::ClientRequest::connect("wss://");
#else
    auto request = websocket::ClientRequest::connect("wss://127.0.0.1/chat");
#endif

    bool completed = false;
    client.async_connect(request, [&](const websocket::ClientStatus& status) {
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
        assert(!status.success());
        assert(status.http_diagnostic == http::DiagnosticCode::missing_host);
#else
        assert(!status.success());
        assert(status.http_diagnostic == http::DiagnosticCode::tls_unavailable);
        assert(status.tls_diagnostic == tls::TlsDiagnosticCode::disabled);
#endif
        completed = true;
    });
    assert(completed);
}

} // namespace

int main()
{
    buildsClientRequestFromWsUri();
    rejectsWssWithoutPlaintextFallbackWhenTlsDisabled();
    return 0;
}
