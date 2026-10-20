#pragma once

#include "network/http/header.h"

#include <cstdint>

namespace arp::network::http {

using arp::network::byte_buffer;

enum class ContentType {
    Html,
    Plain,
    Json,
    Xml,
};

enum class Method {
    Get,
    Post,
    Put,
    Delete,
    Patch,
    Head,
    Options,
    Trace,
    Connect,
};

struct MethodParseResult {
    Method method = Method::Get;
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

struct Version {
    std::uint16_t major = 1;
    std::uint16_t minor = 1;
};

struct StatusCodeParseResult;

class ARP_API StatusCode {
public:
    explicit StatusCode(std::uint16_t value = 200) noexcept;

    static StatusCodeParseResult parse(std::uint16_t value) noexcept;

    std::uint16_t value() const noexcept;

private:
    std::uint16_t value_ = 200;
};

struct StatusCodeParseResult {
    StatusCode status;
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

class ARP_API Request {
public:
    Request();

    Method method() const noexcept;
    void set_method(Method method) noexcept;
    StringView target() const noexcept;
    void set_target(StringView target);
    Version version() const noexcept;
    void set_version(Version version) noexcept;
    HeaderList& headers() noexcept;
    const HeaderList& headers() const noexcept;
    DiagnosticCode add_header(StringView name, StringView value);
    StringView header(StringView name) const noexcept;
    DiagnosticCode set_content_type(ContentType type);
    const byte_buffer& body() const noexcept;
    void set_body(const byte_buffer& body);
    void set_body_text(StringView text);
    StringView body_text() const noexcept;

private:
    Method method_ = Method::Get;
    core::pmr::String target_;
    Version version_{};
    HeaderList headers_;
    byte_buffer body_;
};

class ARP_API Response {
public:
    Response();

    StatusCode status() const noexcept;
    void set_status(StatusCode status) noexcept;
    StringView reason() const noexcept;
    void set_reason(StringView reason);
    Version version() const noexcept;
    void set_version(Version version) noexcept;
    HeaderList& headers() noexcept;
    const HeaderList& headers() const noexcept;
    DiagnosticCode add_header(StringView name, StringView value);
    StringView header(StringView name) const noexcept;
    DiagnosticCode set_content_type(ContentType type);
    const byte_buffer& body() const noexcept;
    void set_body(const byte_buffer& body);
    void set_body_text(StringView text);
    StringView body_text() const noexcept;

private:
    StatusCode status_{};
    core::pmr::String reason_;
    Version version_{};
    HeaderList headers_;
    byte_buffer body_;
};

ARP_API MethodParseResult parseMethod(StringView text) noexcept;
ARP_API StringView methodText(Method method) noexcept;
ARP_API StringView contentTypeText(ContentType type) noexcept;

} // namespace arp::network::http
