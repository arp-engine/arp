#include "network/websocket/frame.h"

#include <cassert>
#include <cstdint>

namespace {

arp::network::byte_buffer payloadOfSize(std::size_t size)
{
    arp::network::byte_buffer payload{&arp::core::defaultCpuMemoryResource()};
    payload.resize(size);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>('a' + (i % 26));
    }
    return payload;
}

void serializesAndParsesDataAndControlFrames()
{
    using namespace arp::network;

    websocket::Frame text = websocket::Frame::text("hello");
    text.set_mask(websocket::MaskingKey{0x37, 0xfa, 0x21, 0x3d});
    const auto encodedText = websocket::FrameSerializer::serialize(text);
    websocket::FrameParser parser;
    const auto parsedText = parser.parse(encodedText);
    assert(parsedText.success());
    assert(parsedText.frame.opcode() == websocket::Opcode::text);
    assert(parsedText.frame.masked());
    assert(parsedText.frame.payload().size() == 5);
    assert(parsedText.frame.payload()[0] == static_cast<std::uint8_t>('h'));
    assert(parsedText.bytes_consumed == encodedText.size());

    const auto binaryPayload = payloadOfSize(3);
    const auto encodedBinary =
        websocket::FrameSerializer::serialize(websocket::Frame::binary(binaryPayload));
    const auto parsedBinary = parser.parse(encodedBinary);
    assert(parsedBinary.success());
    assert(parsedBinary.frame.opcode() == websocket::Opcode::binary);
    assert(parsedBinary.frame.payload().size() == 3);

    const auto parsedPing =
        parser.parse(websocket::FrameSerializer::serialize(websocket::Frame::ping("hi")));
    assert(parsedPing.success());
    assert(parsedPing.frame.opcode() == websocket::Opcode::ping);

    const auto parsedPong =
        parser.parse(websocket::FrameSerializer::serialize(websocket::Frame::pong("hi")));
    assert(parsedPong.success());
    assert(parsedPong.frame.opcode() == websocket::Opcode::pong);

    const auto parsedClose =
        parser.parse(websocket::FrameSerializer::serialize(websocket::Frame::close(1000, "bye")));
    assert(parsedClose.success());
    assert(parsedClose.frame.opcode() == websocket::Opcode::close);
    const auto close = websocket::Frame::parseCloseStatus(parsedClose.frame);
    assert(close.success());
    assert(close.code == 1000);
    assert(close.reason == StringView("bye"));
}

void usesDeterministicLengthEncodings()
{
    using namespace arp::network;

    const auto small =
        websocket::FrameSerializer::serialize(websocket::Frame::binary(payloadOfSize(125)));
    assert((small[1] & 0x7f) == 125);

    const auto medium =
        websocket::FrameSerializer::serialize(websocket::Frame::binary(payloadOfSize(126)));
    assert((medium[1] & 0x7f) == 126);
    assert(medium[2] == 0);
    assert(medium[3] == 126);

    const auto large =
        websocket::FrameSerializer::serialize(websocket::Frame::binary(payloadOfSize(66000)));
    assert((large[1] & 0x7f) == 127);
    assert(large[8] == 0x01);
    assert(large[9] == 0xd0);
}

void enforcesFragmentationAndProtocolDiagnostics()
{
    using namespace arp::network;

    websocket::FrameParser parser;
    websocket::Frame start = websocket::Frame::text("he");
    start.set_fin(false);
    assert(parser.parse(websocket::FrameSerializer::serialize(start)).success());

    const auto continuation = parser.parse(
        websocket::FrameSerializer::serialize(websocket::Frame::continuation("llo", true)));
    assert(continuation.success());
    assert(continuation.frame.opcode() == websocket::Opcode::continuation);

    websocket::FrameParser fresh;
    assert(fresh
               .parse(websocket::FrameSerializer::serialize(
                   websocket::Frame::continuation("oops", true)))
               .diagnostic
        == websocket::DiagnosticCode::continuation_without_message);

    byte_buffer reserved{&arp::core::defaultCpuMemoryResource()};
    reserved.push_back(0xc1);
    reserved.push_back(0);
    assert(fresh.parse(reserved).diagnostic == websocket::DiagnosticCode::invalid_reserved_bits);

    byte_buffer fragmentedPing{&arp::core::defaultCpuMemoryResource()};
    fragmentedPing.push_back(0x09);
    fragmentedPing.push_back(0);
    assert(fresh.parse(fragmentedPing).diagnostic
        == websocket::DiagnosticCode::fragmented_control_frame);

    websocket::FrameLimits limits;
    limits.max_payload_bytes = 4;
    websocket::FrameParser limited(limits);
    assert(limited.parse(websocket::FrameSerializer::serialize(websocket::Frame::text("hello")))
               .diagnostic
        == websocket::DiagnosticCode::oversized_payload);
}

} // namespace

int main()
{
    serializesAndParsesDataAndControlFrames();
    usesDeterministicLengthEncodings();
    enforcesFragmentationAndProtocolDiagnostics();
    return 0;
}
