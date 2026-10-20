#include "network/http/session.h"

#include <cassert>
#include <cstring>

namespace {

class ScriptedStream {
public:
    ScriptedStream() : chunks_(&arp::core::defaultCpuMemoryResource()) {}

    void add_read_chunk(arp::network::StringView text)
    {
        arp::core::pmr::String chunk{&arp::core::defaultCpuMemoryResource()};
        chunk.assign(text.data(), text.size());
        chunks_.push_back(chunk);
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

private:
    arp::core::pmr::Vector<arp::core::pmr::String> chunks_;
    std::size_t read_index_ = 0;
};

void appends(arp::core::pmr::String& out, arp::network::StringView text)
{
    out.append(text.data(), text.size());
}

void streamsContentLengthBodyInChunks()
{
    using namespace arp::network::http;

    ScriptedStream stream;
    stream.add_read_chunk(
        "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nContent-Type: text/plain\r\n\r\nhello");
    stream.add_read_chunk(" ");
    stream.add_read_chunk("world");

    Session session;
    bool headers = false;
    bool completed = false;
    arp::core::pmr::String body{&arp::core::defaultCpuMemoryResource()};

    session.async_read_response_stream(
        stream,
        [&](const Response& response) {
            assert(response.status().value() == 200);
            assert(response.header("content-type") == "text/plain");
            assert(response.body().empty());
            headers = true;
        },
        [&](arp::network::StringView chunk) {
            assert(headers);
            appends(body, chunk);
        },
        [&](const SessionStatus& status) {
            assert(status.success());
            completed = true;
        });

    assert(headers);
    assert(completed);
    assert(body == "hello world");
}

void streamsChunkedBodyInChunks()
{
    using namespace arp::network::http;

    ScriptedStream stream;
    stream.add_read_chunk("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n");
    stream.add_read_chunk("6\r\n world\r\n");
    stream.add_read_chunk("0\r\n\r\n");

    Session session;
    bool headers = false;
    bool completed = false;
    arp::core::pmr::String body{&arp::core::defaultCpuMemoryResource()};

    session.async_read_response_stream(
        stream,
        [&](const Response& response) {
            assert(response.status().value() == 200);
            assert(response.header("transfer-encoding") == "chunked");
            headers = true;
        },
        [&](arp::network::StringView chunk) {
            assert(headers);
            appends(body, chunk);
        },
        [&](const SessionStatus& status) {
            assert(status.success());
            completed = true;
        });

    assert(headers);
    assert(completed);
    assert(body == "hello world");
}

void reportsBodyLimitBeforeStreamingUnboundedData()
{
    using namespace arp::network::http;

    ScriptedStream stream;
    stream.add_read_chunk("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");

    SessionConfig config;
    config.parser_limits.max_body_bytes = 4;
    Session session(config);
    bool headers = false;
    bool body = false;
    bool completed = false;

    session.async_read_response_stream(
        stream,
        [&](const Response&) {
            headers = true;
        },
        [&](arp::network::StringView) {
            body = true;
        },
        [&](const SessionStatus& status) {
            assert(!status.success());
            assert(status.diagnostic == DiagnosticCode::body_too_large);
            completed = true;
        });

    assert(!headers);
    assert(!body);
    assert(completed);
}

} // namespace

int main()
{
    streamsContentLengthBodyInChunks();
    streamsChunkedBodyInChunks();
    reportsBodyLimitBeforeStreamingUnboundedData();
    return 0;
}
