#include "network/websocket/handshake.h"

#include <cassert>

namespace {

void generatesClientUpgradeAndKnownAcceptVector()
{
    using namespace arp::network;

    assert(websocket::HandshakeResponse::acceptKey("dGhlIHNhbXBsZSBub25jZQ==")
        == StringView("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

    websocket::HandshakeRequest request;
    request.set_host("example.com");
    request.set_resource("/chat");
    request.set_key("dGhlIHNhbXBsZSBub25jZQ==");
    request.add_subprotocol("chat");
    request.add_subprotocol("superchat");

    const auto httpRequest = request.toHttpRequest();
    assert(httpRequest.method() == http::Method::Get);
    assert(httpRequest.target() == StringView("/chat"));
    assert(httpRequest.headers().firstValue("Host") == StringView("example.com"));
    assert(httpRequest.headers().firstValue("Upgrade") == StringView("websocket"));
    assert(httpRequest.headers().firstValue("Connection") == StringView("Upgrade"));
    assert(httpRequest.headers().firstValue("Sec-WebSocket-Version") == StringView("13"));
    assert(httpRequest.headers().firstValue("Sec-WebSocket-Key")
        == StringView("dGhlIHNhbXBsZSBub25jZQ=="));
    assert(httpRequest.headers().firstValue("Sec-WebSocket-Protocol")
        == StringView("chat, superchat"));

    const auto validation = websocket::HandshakeResponse::validateClientRequest(httpRequest);
    assert(validation.success());

    const auto selected = websocket::HandshakeResponse::selectSubprotocol(httpRequest, "superchat");
    assert(selected.success());
    assert(selected.value() == StringView("superchat"));

    const auto response =
        websocket::HandshakeResponse::buildServerResponse(httpRequest, selected.value());
    assert(response.status().value() == 101);
    assert(response.headers().firstValue("Sec-WebSocket-Accept")
        == StringView("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    assert(response.headers().firstValue("Sec-WebSocket-Protocol") == StringView("superchat"));

    const auto serverValidation = websocket::HandshakeResponse::validateServerResponse(
        response, "dGhlIHNhbXBsZSBub25jZQ==", "superchat");
    assert(serverValidation.success());
}

arp::network::http::Request validClientRequest()
{
    arp::network::websocket::HandshakeRequest request;
    request.set_host("example.com");
    request.set_resource("/chat");
    request.set_key("dGhlIHNhbXBsZSBub25jZQ==");
    return request.toHttpRequest();
}

void rejectsInvalidClientRequestsAndServerResponses()
{
    using namespace arp::network;

    auto missingKey = validClientRequest();
    missingKey = http::Request();
    missingKey.set_method(http::Method::Get);
    missingKey.set_target("/chat");
    missingKey.headers().add("Host", "example.com");
    missingKey.headers().add("Upgrade", "websocket");
    missingKey.headers().add("Connection", "Upgrade");
    missingKey.headers().add("Sec-WebSocket-Version", "13");
    assert(websocket::HandshakeResponse::validateClientRequest(missingKey).diagnostic
        == websocket::DiagnosticCode::missing_key);

    auto unsupportedVersion = http::Request();
    unsupportedVersion.set_method(http::Method::Get);
    unsupportedVersion.set_target("/chat");
    unsupportedVersion.headers().add("Host", "example.com");
    unsupportedVersion.headers().add("Upgrade", "websocket");
    unsupportedVersion.headers().add("Connection", "Upgrade");
    unsupportedVersion.headers().add("Sec-WebSocket-Version", "12");
    unsupportedVersion.headers().add("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    assert(websocket::HandshakeResponse::validateClientRequest(unsupportedVersion).diagnostic
        == websocket::DiagnosticCode::unsupported_version);

    auto invalidKey = http::Request();
    invalidKey.set_method(http::Method::Get);
    invalidKey.set_target("/chat");
    invalidKey.headers().add("Host", "example.com");
    invalidKey.headers().add("Upgrade", "websocket");
    invalidKey.headers().add("Connection", "Upgrade");
    invalidKey.headers().add("Sec-WebSocket-Version", "13");
    invalidKey.headers().add("Sec-WebSocket-Key", "abc");
    assert(websocket::HandshakeResponse::validateClientRequest(invalidKey).diagnostic
        == websocket::DiagnosticCode::invalid_key);

    http::Response badAccept;
    badAccept.set_status(http::StatusCode(101));
    badAccept.headers().add("Upgrade", "websocket");
    badAccept.headers().add("Connection", "Upgrade");
    badAccept.headers().add("Sec-WebSocket-Accept", "wrong");
    assert(websocket::HandshakeResponse::validateServerResponse(
               badAccept, "dGhlIHNhbXBsZSBub25jZQ==", StringView())
               .diagnostic
        == websocket::DiagnosticCode::accept_mismatch);
}

} // namespace

int main()
{
    generatesClientUpgradeAndKnownAcceptVector();
    rejectsInvalidClientRequestsAndServerResponses();
    return 0;
}
