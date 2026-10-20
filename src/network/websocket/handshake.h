#pragma once

#include "network/http/message.h"
#include "network/websocket/diagnostic.h"

namespace arp::network::websocket {

class ARP_API HandshakeValidationResult {
public:
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

class ARP_API SubprotocolSelection {
public:
    SubprotocolSelection();

    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
    StringView value() const noexcept;
    void set_value(StringView value);

private:
    core::pmr::String value_;
};

class ARP_API HandshakeRequest {
public:
    HandshakeRequest();

    void set_host(StringView host);
    void set_resource(StringView resource);
    void set_key(StringView key);
    void add_subprotocol(StringView subprotocol);

    http::Request toHttpRequest() const;

private:
    core::pmr::String host_;
    core::pmr::String resource_;
    core::pmr::String key_;
    core::pmr::Vector<core::pmr::String> subprotocols_;
};

class ARP_API HandshakeResponse {
public:
    static core::pmr::String acceptKey(StringView clientKey);
    static HandshakeValidationResult validateClientRequest(const http::Request& request);
    static SubprotocolSelection selectSubprotocol(
        const http::Request& request, StringView supported);
    static http::Response buildServerResponse(
        const http::Request& request, StringView selectedSubprotocol);
    static HandshakeValidationResult validateServerResponse(
        const http::Response& response, StringView clientKey, StringView expectedSubprotocol);
};

} // namespace arp::network::websocket
