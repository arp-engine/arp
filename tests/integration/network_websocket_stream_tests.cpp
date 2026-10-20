#include "network/websocket/stream.h"

#include <cassert>
#include <cstring>

namespace {

arp::network::byte_buffer bytesFromText(arp::network::StringView text)
{
    arp::network::byte_buffer bytes{&arp::core::defaultCpuMemoryResource()};
    for (char c : text) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
}

class ScriptedStream {
public:
    ScriptedStream()
        : sent_(&arp::core::defaultCpuMemoryResource()),
          chunks_(&arp::core::defaultCpuMemoryResource())
    {}

    void add_read_bytes(const arp::network::byte_buffer& bytes) { chunks_.push_back(bytes); }

    void add_read_text(arp::network::StringView text) { add_read_bytes(bytesFromText(text)); }

    template <typename Handler>
    void async_send(const void* data, std::size_t size, Handler&& handler)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        sent_.insert(sent_.end(), bytes, bytes + size);
        arp::network::error_code ec;
        handler(ec, size);
    }

    template <typename Handler>
    void async_recv(arp::network::byte_buffer& buffer, Handler&& handler)
    {
        arp::network::error_code ec;
        if (read_index_ >= chunks_.size()) {
            handler(ec, 0);
            return;
        }
        const auto& chunk = chunks_[read_index_++];
        assert(buffer.size() >= chunk.size());
        std::memcpy(buffer.data(), chunk.data(), chunk.size());
        handler(ec, chunk.size());
    }

    const arp::network::byte_buffer& sent() const noexcept { return sent_; }

    arp::core::pmr::String sent_text() const
    {
        return arp::core::pmr::String(reinterpret_cast<const char*>(sent_.data()),
            sent_.size(),
            &arp::core::defaultCpuMemoryResource());
    }

private:
    arp::network::byte_buffer sent_;
    arp::core::pmr::Vector<arp::network::byte_buffer> chunks_;
    std::size_t read_index_ = 0;
};

void performsClientAndServerUpgrade()
{
    using namespace arp::network;

    const auto key = StringView("dGhlIHNhbXBsZSBub25jZQ==");
    websocket::HandshakeRequest request;
    request.set_host("example.com");
    request.set_resource("/chat");
    request.set_key(key);
    const auto response =
        websocket::HandshakeResponse::buildServerResponse(request.toHttpRequest(), StringView());

    http::SerializationOptions options;
    const auto serializedResponse = http::Serializer::serializeResponse(response, options);
    ScriptedStream clientTransport;
    clientTransport.add_read_text(serializedResponse.text);

    websocket::Stream client(websocket::StreamRole::client);
    bool clientDone = false;
    client.async_client_upgrade(
        clientTransport, "example.com", "/chat", key, [&](const websocket::StreamStatus& status) {
            assert(status.success());
            clientDone = true;
        });
    assert(clientDone);
    assert(client.state() == websocket::StreamState::open);
    assert(clientTransport.sent_text().find("GET /chat HTTP/1.1") != arp::core::pmr::String::npos);

    const auto serializedRequest =
        http::Serializer::serializeRequest(request.toHttpRequest(), options);
    ScriptedStream serverTransport;
    serverTransport.add_read_text(serializedRequest.text);
    websocket::Stream server(websocket::StreamRole::server);
    bool serverDone = false;
    server.async_server_accept(
        serverTransport, StringView(), [&](const websocket::StreamStatus& status) {
            assert(status.success());
            serverDone = true;
        });
    assert(serverDone);
    assert(server.state() == websocket::StreamState::open);
    assert(serverTransport.sent_text().find("HTTP/1.1 101 Switching Protocols")
        != arp::core::pmr::String::npos);
}

void writesMaskedClientFramesAndReadsPartialFramesInOrder()
{
    using namespace arp::network;

    websocket::StreamConfig config;
    config.client_mask_key = websocket::MaskingKey{1, 2, 3, 4};
    websocket::Stream client(websocket::StreamRole::client, config);
    ScriptedStream transport;

    int order = 0;
    client.async_write_text(transport, "hello", [&](const websocket::StreamStatus& status) {
        assert(status.success());
        assert(++order == 1);
    });
    client.async_write_binary(
        transport, bytesFromText("bin"), [&](const websocket::StreamStatus& status) {
            assert(status.success());
            assert(++order == 2);
        });
    assert(order == 2);

    websocket::FrameParser parser;
    const auto parsed = parser.parse(transport.sent());
    assert(parsed.success());
    assert(parsed.frame.masked());
    assert(parsed.frame.opcode() == websocket::Opcode::text);

    websocket::Frame fragmented = websocket::Frame::text("he");
    fragmented.set_fin(false);
    const auto encoded = websocket::FrameSerializer::serialize(fragmented);
    byte_buffer first{&arp::core::defaultCpuMemoryResource()};
    first.push_back(encoded[0]);
    byte_buffer second{&arp::core::defaultCpuMemoryResource()};
    for (std::size_t i = 1; i < encoded.size(); ++i) {
        second.push_back(encoded[i]);
    }
    const auto continuationBytes =
        websocket::FrameSerializer::serialize(websocket::Frame::continuation("llo", true));
    second.insert(second.end(), continuationBytes.begin(), continuationBytes.end());

    ScriptedStream readerTransport;
    readerTransport.add_read_bytes(first);
    readerTransport.add_read_bytes(second);
    websocket::Stream server(websocket::StreamRole::server);

    bool readStart = false;
    server.async_read_frame(
        readerTransport, [&](const websocket::StreamStatus& status, const websocket::Frame& frame) {
            assert(status.success());
            assert(frame.opcode() == websocket::Opcode::text);
            assert(!frame.fin());
            readStart = true;
        });
    assert(readStart);

    bool readEnd = false;
    server.async_read_frame(
        readerTransport, [&](const websocket::StreamStatus& status, const websocket::Frame& frame) {
            assert(status.success());
            assert(frame.opcode() == websocket::Opcode::continuation);
            assert(frame.fin());
            readEnd = true;
        });
    assert(readEnd);
}

void reportsProtocolErrorsAndCloseState()
{
    using namespace arp::network;

    websocket::StreamConfig config;
    config.frame_limits.max_payload_bytes = 4;
    websocket::Stream limited(websocket::StreamRole::server, config);
    ScriptedStream oversized;
    oversized.add_read_bytes(
        websocket::FrameSerializer::serialize(websocket::Frame::text("hello")));
    bool failed = false;
    limited.async_read_frame(
        oversized, [&](const websocket::StreamStatus& status, const websocket::Frame&) {
            assert(!status.success());
            assert(status.diagnostic == websocket::DiagnosticCode::oversized_payload);
            failed = true;
        });
    assert(failed);
    assert(limited.state() == websocket::StreamState::failed);

    byte_buffer reserved{&arp::core::defaultCpuMemoryResource()};
    reserved.push_back(0xc1);
    reserved.push_back(0);
    ScriptedStream bad;
    bad.add_read_bytes(reserved);
    websocket::Stream stream(websocket::StreamRole::server);
    bool protocolError = false;
    stream.async_read_frame(
        bad, [&](const websocket::StreamStatus& status, const websocket::Frame&) {
            assert(!status.success());
            assert(status.diagnostic == websocket::DiagnosticCode::invalid_reserved_bits);
            protocolError = true;
        });
    assert(protocolError);

    ScriptedStream closeTransport;
    websocket::Stream closer(websocket::StreamRole::server);
    bool closeSent = false;
    closer.async_write_close(
        closeTransport, 1000, "bye", [&](const websocket::StreamStatus& status) {
            assert(status.success());
            closeSent = true;
        });
    assert(closeSent);
    assert(closer.state() == websocket::StreamState::close_sent);
}

} // namespace

int main()
{
    performsClientAndServerUpgrade();
    writesMaskedClientFramesAndReadsPartialFramesInOrder();
    reportsProtocolErrorsAndCloseState();
    return 0;
}
