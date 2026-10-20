#include "network/http/client.h"
#include "network/http/session.h"

#include <cassert>
#include <cstring>

namespace {

arp::network::byte_buffer bodyFromText(arp::network::StringView text)
{
    arp::network::byte_buffer body{&arp::core::defaultCpuMemoryResource()};
    for (char c : text) {
        body.push_back(static_cast<std::uint8_t>(c));
    }
    return body;
}

class ScriptedStream {
public:
    ScriptedStream()
        : sent_(&arp::core::defaultCpuMemoryResource()),
          chunks_(&arp::core::defaultCpuMemoryResource())
    {}

    void add_read_chunk(arp::network::StringView text)
    {
        arp::core::pmr::String chunk{&arp::core::defaultCpuMemoryResource()};
        chunk.assign(text.data(), text.size());
        chunks_.push_back(chunk);
    }

    template <typename Handler>
    void async_send(const void* data, std::size_t size, Handler&& handler)
    {
        const auto* bytes = static_cast<const char*>(data);
        sent_.append(bytes, size);
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

    arp::network::StringView sent() const noexcept
    {
        return arp::network::StringView(sent_.data(), sent_.size());
    }

private:
    arp::core::pmr::String sent_;
    arp::core::pmr::Vector<arp::core::pmr::String> chunks_;
    std::size_t read_index_ = 0;
};

void writesRequestAndReadsPartialContentLengthResponse()
{
    using namespace arp::network::http;

    ScriptedStream stream;
    stream.add_read_chunk("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n");
    stream.add_read_chunk("\r\nok");

    Session session;
    Request request;
    request.set_method(Method::Post);
    request.set_target("/submit");
    request.set_body(bodyFromText("hi"));

    bool wrote = false;
    session.async_write_request(stream, request, [&](const SessionStatus& status) {
        assert(status.success());
        wrote = true;
    });
    assert(wrote);
    assert(stream.sent() == "POST /submit HTTP/1.1\r\nContent-Length: 2\r\n\r\nhi");

    bool read = false;
    session.async_read_response(stream, [&](const SessionStatus& status, const Response& response) {
        assert(status.success());
        assert(response.status().value() == 200);
        assert(response.body().size() == 2);
        assert(response.body()[1] == static_cast<std::uint8_t>('k'));
        read = true;
    });
    assert(read);
}

void readsChunkedResponseAndReportsParserFailure()
{
    using namespace arp::network::http;

    ScriptedStream chunked;
    chunked.add_read_chunk(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nok\r\n0\r\n\r\n");
    Session session;
    bool read = false;
    session.async_read_response(
        chunked, [&](const SessionStatus& status, const Response& response) {
            assert(status.success());
            assert(response.body().size() == 2);
            read = true;
        });
    assert(read);

    ParserLimits limits;
    limits.max_header_bytes = 8;
    SessionConfig config;
    config.parser_limits = limits;
    Session failing(config);
    ScriptedStream bad;
    bad.add_read_chunk("HTTP/1.1 200 OK\r\nHeader: too-large\r\n\r\n");
    bool failed = false;
    failing.async_read_response(bad, [&](const SessionStatus& status, const Response&) {
        assert(!status.success());
        assert(status.diagnostic == DiagnosticCode::header_too_large);
        failed = true;
    });
    assert(failed);
}

void reportsConnectionCloseAndClientSchemeContracts()
{
    using namespace arp::network::http;

    ScriptedStream closed;
    Session session;
    bool reported = false;
    session.async_read_response(closed, [&](const SessionStatus& status, const Response&) {
        assert(!status.success());
        assert(status.diagnostic == DiagnosticCode::connection_closed);
        reported = true;
    });
    assert(reported);

    assert(Client::prepareTransport(Uri::parse("http://example.com/").uri).success());
    assert(Client::prepareTransport(Uri::parse("ws://example.com/").uri).success());
    assert(Client::prepareTransport(Uri::parse("https://example.com/").uri).diagnostic
        == DiagnosticCode::tls_unavailable);
    assert(Client::prepareTransport(Uri::parse("wss://example.com/").uri).diagnostic
        == DiagnosticCode::tls_unavailable);
}

} // namespace

int main()
{
    writesRequestAndReadsPartialContentLengthResponse();
    readsChunkedResponseAndReportsParserFailure();
    reportsConnectionCloseAndClientSchemeContracts();
    return 0;
}
