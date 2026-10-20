#include "network/websocket/frame.h"

namespace arp::network::websocket {

namespace {

byte_buffer bytesFromText(StringView text)
{
    byte_buffer bytes{&core::defaultCpuMemoryResource()};
    bytes.reserve(text.size());
    for (char c : text) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
}

bool isControlOpcode(Opcode opcode) noexcept
{
    return opcode == Opcode::close || opcode == Opcode::ping || opcode == Opcode::pong;
}

bool validOpcode(std::uint8_t opcode) noexcept
{
    return opcode == 0x0U || opcode == 0x1U || opcode == 0x2U || opcode == 0x8U || opcode == 0x9U
        || opcode == 0xAU;
}

FrameParseResult parseFailure(DiagnosticCode diagnostic)
{
    FrameParseResult result;
    result.diagnostic = diagnostic;
    return result;
}

void appendUint16(byte_buffer& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendUint64(byte_buffer& bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(
            static_cast<std::uint8_t>((value >> static_cast<std::uint32_t>(shift)) & 0xffU));
    }
}

std::uint16_t readUint16(const byte_buffer& bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U)
        | static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::uint64_t readUint64(const byte_buffer& bytes, std::size_t offset) noexcept
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8U; ++i) {
        value = (value << 8U) | bytes[offset + i];
    }
    return value;
}

} // namespace

bool CloseStatusResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

Frame::Frame() : payload_(&core::defaultCpuMemoryResource())
{}

Frame Frame::fromPayload(Opcode opcode, const byte_buffer& payload, bool fin)
{
    Frame frame;
    frame.opcode_ = opcode;
    frame.fin_ = fin;
    frame.payload_ = payload;
    return frame;
}

Frame Frame::text(StringView payload)
{
    return fromPayload(Opcode::text, bytesFromText(payload), true);
}

Frame Frame::binary(const byte_buffer& payload)
{
    return fromPayload(Opcode::binary, payload, true);
}

Frame Frame::continuation(StringView payload, bool fin)
{
    return fromPayload(Opcode::continuation, bytesFromText(payload), fin);
}

Frame Frame::ping(StringView payload)
{
    return fromPayload(Opcode::ping, bytesFromText(payload), true);
}

Frame Frame::pong(StringView payload)
{
    return fromPayload(Opcode::pong, bytesFromText(payload), true);
}

Frame Frame::close(std::uint16_t code, StringView reason)
{
    byte_buffer payload{&core::defaultCpuMemoryResource()};
    appendUint16(payload, code);
    for (char c : reason) {
        payload.push_back(static_cast<std::uint8_t>(c));
    }
    return fromPayload(Opcode::close, payload, true);
}

CloseStatusResult Frame::parseCloseStatus(const Frame& frame)
{
    CloseStatusResult result;
    if (frame.opcode_ != Opcode::close) {
        result.diagnostic = DiagnosticCode::invalid_opcode;
        return result;
    }
    if (frame.payload_.empty()) {
        result.code = 1005;
        return result;
    }
    if (frame.payload_.size() == 1U) {
        result.diagnostic = DiagnosticCode::invalid_close_payload;
        return result;
    }
    result.code = readUint16(frame.payload_, 0);
    result.reason.assign(
        reinterpret_cast<const char*>(frame.payload_.data() + 2U), frame.payload_.size() - 2U);
    return result;
}

bool Frame::fin() const noexcept
{
    return fin_;
}

void Frame::set_fin(bool fin) noexcept
{
    fin_ = fin;
}

Opcode Frame::opcode() const noexcept
{
    return opcode_;
}

bool Frame::masked() const noexcept
{
    return masked_;
}

void Frame::set_mask(MaskingKey key) noexcept
{
    masked_ = true;
    mask_ = key;
}

const MaskingKey& Frame::mask() const noexcept
{
    return mask_;
}

const byte_buffer& Frame::payload() const noexcept
{
    return payload_;
}

StringView Frame::payload_text() const noexcept
{
    return StringView(reinterpret_cast<const char*>(payload_.data()), payload_.size());
}

void Frame::set_payload(const byte_buffer& payload)
{
    payload_ = payload;
}

bool FrameParseResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

FrameParser::FrameParser() = default;

FrameParser::FrameParser(FrameLimits limits) : limits_(limits)
{}

FrameParseResult FrameParser::parse(const byte_buffer& bytes)
{
    if (bytes.size() < 2U) {
        return parseFailure(DiagnosticCode::incomplete_frame);
    }

    const std::uint8_t first = bytes[0];
    if ((first & 0x70U) != 0U) {
        return parseFailure(DiagnosticCode::invalid_reserved_bits);
    }

    const bool fin = (first & 0x80U) != 0U;
    const std::uint8_t rawOpcode = first & 0x0fU;
    if (!validOpcode(rawOpcode)) {
        return parseFailure(DiagnosticCode::invalid_opcode);
    }
    const auto opcode = static_cast<Opcode>(rawOpcode);
    const bool control = isControlOpcode(opcode);
    if (control && !fin) {
        return parseFailure(DiagnosticCode::fragmented_control_frame);
    }

    const std::uint8_t second = bytes[1];
    const bool masked = (second & 0x80U) != 0U;
    std::uint64_t payloadSize = second & 0x7fU;
    std::size_t offset = 2U;
    if (payloadSize == 126U) {
        if (bytes.size() < offset + 2U) {
            return parseFailure(DiagnosticCode::incomplete_frame);
        }
        payloadSize = readUint16(bytes, offset);
        offset += 2U;
    } else if (payloadSize == 127U) {
        if (bytes.size() < offset + 8U) {
            return parseFailure(DiagnosticCode::incomplete_frame);
        }
        payloadSize = readUint64(bytes, offset);
        offset += 8U;
    }
    if (control && payloadSize > 125U) {
        return parseFailure(DiagnosticCode::oversized_payload);
    }
    if (payloadSize > limits_.max_payload_bytes) {
        return parseFailure(DiagnosticCode::oversized_payload);
    }

    MaskingKey mask{};
    if (masked) {
        if (bytes.size() < offset + 4U) {
            return parseFailure(DiagnosticCode::incomplete_frame);
        }
        for (std::size_t i = 0; i < 4U; ++i) {
            mask.bytes[i] = bytes[offset + i];
        }
        offset += 4U;
    }

    if (bytes.size() < offset + payloadSize) {
        return parseFailure(DiagnosticCode::incomplete_frame);
    }

    if (opcode == Opcode::continuation) {
        if (!fragmented_message_open_) {
            return parseFailure(DiagnosticCode::continuation_without_message);
        }
        if (fin) {
            fragmented_message_open_ = false;
        }
    } else if (opcode == Opcode::text || opcode == Opcode::binary) {
        if (fragmented_message_open_) {
            return parseFailure(DiagnosticCode::invalid_fragment_start);
        }
        if (!fin) {
            fragmented_message_open_ = true;
        }
    }

    byte_buffer payload{&core::defaultCpuMemoryResource()};
    payload.resize(static_cast<std::size_t>(payloadSize));
    for (std::size_t i = 0; i < payload.size(); ++i) {
        std::uint8_t byte = bytes[offset + i];
        if (masked) {
            byte = static_cast<std::uint8_t>(byte ^ mask.bytes[i % 4U]);
        }
        payload[i] = byte;
    }

    Frame frame = Frame::fromPayload(opcode, payload, fin);
    if (masked) {
        frame.set_mask(mask);
    }

    FrameParseResult result;
    result.frame = frame;
    result.bytes_consumed = offset + static_cast<std::size_t>(payloadSize);
    return result;
}

byte_buffer FrameSerializer::serialize(const Frame& frame)
{
    byte_buffer bytes{&core::defaultCpuMemoryResource()};
    const auto payloadSize = static_cast<std::uint64_t>(frame.payload_.size());
    bytes.reserve(frame.payload_.size() + 14U);
    std::uint8_t first = static_cast<std::uint8_t>(frame.opcode_);
    if (frame.fin_) {
        first = static_cast<std::uint8_t>(first | 0x80U);
    }
    bytes.push_back(first);

    const std::uint8_t maskBit = frame.masked_ ? 0x80U : 0U;
    if (payloadSize <= 125U) {
        bytes.push_back(static_cast<std::uint8_t>(maskBit | payloadSize));
    } else if (payloadSize <= 0xffffU) {
        bytes.push_back(static_cast<std::uint8_t>(maskBit | 126U));
        appendUint16(bytes, static_cast<std::uint16_t>(payloadSize));
    } else {
        bytes.push_back(static_cast<std::uint8_t>(maskBit | 127U));
        appendUint64(bytes, payloadSize);
    }

    if (frame.masked_) {
        for (std::uint8_t byte : frame.mask_.bytes) {
            bytes.push_back(byte);
        }
    }
    for (std::size_t i = 0; i < frame.payload_.size(); ++i) {
        std::uint8_t byte = frame.payload_[i];
        if (frame.masked_) {
            byte = static_cast<std::uint8_t>(byte ^ frame.mask_.bytes[i % 4U]);
        }
        bytes.push_back(byte);
    }
    return bytes;
}

} // namespace arp::network::websocket
