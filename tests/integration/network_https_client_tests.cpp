#include "network/http/client.h"
#include "network/http/session.h"
#include "network/tls/tls_context.h"

#include <cassert>
#include <cstring>

namespace {

class ScriptedTlsReadStream {
public:
    explicit ScriptedTlsReadStream(arp::network::StringView text)
        : text_(text.data(), text.size(), &arp::core::defaultCpuMemoryResource())
    {}

    template <typename Handler>
    void async_recv(arp::network::byte_buffer& buffer, Handler&& handler)
    {
        assert(buffer.size() >= text_.size());
        std::memcpy(buffer.data(), text_.data(), text_.size());
        arp::network::error_code ec;
        handler(ec, text_.size());
    }

private:
    arp::core::pmr::String text_;
};

void preparesHttpsTlsConfigAndRejectsDisabledBackend()
{
    using namespace arp::network;

    const auto parsed = http::Uri::parse("https://secure.example:8443/path");
    assert(parsed.success());

    const auto prepared = http::Client::prepareSecureTransport(parsed.uri);
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    assert(prepared.success());
    assert(prepared.transport == http::ClientTransportKind::tls_required);
    assert(prepared.diagnostic == http::DiagnosticCode::none);
    assert(prepared.tls_diagnostic == tls::TlsDiagnosticCode::none);
#else
    assert(!prepared.success());
    assert(prepared.transport == http::ClientTransportKind::tls_required);
    assert(prepared.diagnostic == http::DiagnosticCode::tls_unavailable);
    assert(prepared.tls_diagnostic == tls::TlsDiagnosticCode::disabled);
#endif
    assert(prepared.tls_config.endpoint_role() == tls::TlsEndpointRole::client);
    assert(prepared.tls_config.server_name() == StringView("secure.example"));
    assert(prepared.tls_config.certificate_verification()
        == tls::CertificateVerification::system_trust);

    const auto cleartext = http::Client::prepareTransport(parsed.uri);
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    assert(cleartext.success());
#else
    assert(!cleartext.success());
    assert(cleartext.diagnostic == http::DiagnosticCode::tls_unavailable);
#endif
}

void propagatesTlsHandshakeFailureDeterministically()
{
    using namespace arp::network;

    tls::TlsStatus tlsStatus;
    tlsStatus.diagnostic = tls::TlsDiagnosticCode::disabled;

    const auto clientStatus = http::Client::propagateTlsFailure(tlsStatus);
    assert(!clientStatus.success());
    assert(clientStatus.diagnostic == http::DiagnosticCode::tls_unavailable);
    assert(clientStatus.tls_diagnostic == tls::TlsDiagnosticCode::disabled);
}

void parserFailureAfterTlsReadUsesHttpSessionPath()
{
    using namespace arp::network;

    http::Session session;
    ScriptedTlsReadStream stream("not-a-response\r\n\r\n");

    bool completed = false;
    session.async_read_response(
        stream, [&](const http::SessionStatus& status, const http::Response&) {
            assert(!status.success());
            assert(status.diagnostic == http::DiagnosticCode::malformed_start_line);
            completed = true;
        });
    assert(completed);
}

} // namespace

int main()
{
    preparesHttpsTlsConfigAndRejectsDisabledBackend();
    propagatesTlsHandshakeFailureDeterministically();
    parserFailureAfterTlsReadUsesHttpSessionPath();
    return 0;
}
