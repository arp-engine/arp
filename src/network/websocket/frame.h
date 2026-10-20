#pragma once

#include "network/websocket/diagnostic.h"

namespace arp::network::websocket {

enum class Opcode : std::uint8_t {
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA,
};

struct MaskingKey {
    std::uint8_t bytes[4]{};
};

struct FrameLimits {
    std::uint64_t max_payload_bytes = 65536;
};

struct CloseStatusResult {
    DiagnosticCode diagnostic = DiagnosticCode::none;
    std::uint16_t code = 1005;
    core::pmr::String reason{&core::defaultCpuMemoryResource()};

    bool success() const noexcept;
};

class ARP_API Frame {
public:
    Frame();

    static Frame text(StringView payload);
    static Frame binary(const byte_buffer& payload);
    static Frame continuation(StringView payload, bool fin);
    static Frame ping(StringView payload);
    static Frame pong(StringView payload);
    static Frame close(std::uint16_t code, StringView reason);
    static CloseStatusResult parseCloseStatus(const Frame& frame);

    bool fin() const noexcept;
    void set_fin(bool fin) noexcept;
    Opcode opcode() const noexcept;
    bool masked() const noexcept;
    void set_mask(MaskingKey key) noexcept;
    const MaskingKey& mask() const noexcept;
    const byte_buffer& payload() const noexcept;
    StringView payload_text() const noexcept;
    void set_payload(const byte_buffer& payload);

private:
    friend class FrameParser;
    friend class FrameSerializer;

    static Frame fromPayload(Opcode opcode, const byte_buffer& payload, bool fin);

    bool fin_ = true;
    Opcode opcode_ = Opcode::text;
    bool masked_ = false;
    MaskingKey mask_{};
    byte_buffer payload_;
};

struct FrameParseResult {
    Frame frame;
    std::size_t bytes_consumed = 0;
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

class ARP_API FrameParser {
public:
    FrameParser();
    explicit FrameParser(FrameLimits limits);

    FrameParseResult parse(const byte_buffer& bytes);

private:
    FrameLimits limits_{};
    bool fragmented_message_open_ = false;
};

class ARP_API FrameSerializer {
public:
    static byte_buffer serialize(const Frame& frame);
};

} // namespace arp::network::websocket
