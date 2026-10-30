#include "network/http/byte_read_transport.h"
#include "network/http/client.h"

#include <cassert>
#include <cstdio>

using namespace arp::network::http;

namespace {

class Sink final : public ResponseStreamSink {
public:
    void on_headers(const Response&) override { ++headers; }

    void on_body(arp::network::StringView chunk) override
    {
        body.append(chunk.data(), chunk.size());
    }

    bool shouldContinue() const noexcept override { return proceed; }

    arp::core::pmr::String body{&arp::core::defaultCpuMemoryResource()};
    unsigned headers = 0;
    bool proceed = true;
};

} // namespace

int main()
{
#if !defined(__EMSCRIPTEN__)
    ByteReadDesc boundedDns;
    boundedDns.maxDnsAddresses = 8;
    assert(ByteReadTransport::resolverBudget(boundedDns, SIZE_MAX) == UINT64_MAX);
#endif
    const auto uri = Uri::parse("http://127.0.0.1/a%2Fb%20%23%25?q=a%2Bb#ignored");
    assert(uri.success() && uri.uri.path() == "/a/b #%");
    assert(uri.uri.request_target() == "/a%2Fb%20%23%25?q=a%2Bb");
    const auto request = ClientRequest::get("http://127.0.0.1/a%2Fb%20%23%25?q=a%2Bb#ignored");
    assert(request.success());
    assert(request.message().target() == uri.uri.request_target());
    auto dns = ClientRequest::get("http://localhost:54321/a%2Fb");
    assert(dns.set_connection_endpoint(arp::network::endpoint("127.0.0.1", 54321))
        == DiagnosticCode::none);
    assert(dns.connection_endpoint() && dns.host_header() == "localhost:54321");
    assert(dns.message().target() == "/a%2Fb" && dns.uri().host() == "localhost");
    assert(dns.set_uri("http://localhost:54322/new") == DiagnosticCode::none);
    assert(!dns.connection_endpoint());
    assert(dns.set_connection_endpoint(arp::network::endpoint("127.0.0.1", 54321))
        == DiagnosticCode::malformed_uri);
    assert(Uri::parse("http://127.0.0.1?q=%20").uri.request_target() == "/?q=%20");

    ParserLimits limits;
    limits.allow_close_delimited_body = true;
    limits.max_body_bytes = 6;
    ResponseStreamParser close(limits);
    Sink sink;
    assert(close.feed("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabc", sink).status
        == ParseStatus::need_more);
    assert(close.feed("def", sink).status == ParseStatus::need_more);
    assert(close.finish(sink).status == ParseStatus::done && sink.body == "abcdef");
    ResponseStreamParser large(limits);
    Sink overflow;
    assert(large.feed("HTTP/1.1 200 OK\r\n\r\n1234567", overflow).diagnostic
        == DiagnosticCode::body_too_large);
    assert(overflow.body.empty());
    ResponseStreamParser partial(limits);
    Sink truncated;
    assert(partial.feed("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nabc", truncated).status
        == ParseStatus::need_more);
    assert(partial.finish(truncated).diagnostic == DiagnosticCode::connection_closed);

    limits.prioritize_header_observer = true;
    for (const auto* framing : {"Content-Length: 6", "Transfer-Encoding: chunked"}) {
        ResponseStreamParser aborted(limits);
        Sink reject;
        reject.proceed = false;
        arp::core::pmr::String header(
            "HTTP/1.1 404 Not Found\r\n", &arp::core::defaultCpuMemoryResource());
        header += framing;
        header += "\r\n\r\n";
        assert(aborted.feed(header, reject).diagnostic == DiagnosticCode::response_aborted);
        assert(reject.headers == 1 && reject.body.empty());
    }
    ResponseStreamParser oversized(limits);
    Sink reject;
    reject.proceed = false;
    assert(oversized.feed("HTTP/1.1 404 Missing\r\nContent-Length: 9999\r\n\r\n", reject).diagnostic
        == DiagnosticCode::response_aborted);
    assert(reject.headers == 1);
    limits.allow_close_delimited_body = false;
    ResponseStreamParser legacy(limits);
    Sink ordinary;
    assert(legacy.feed("HTTP/1.1 200 OK\r\n\r\n", ordinary).status == ParseStatus::done);
    std::puts("Engine HTTP encoded target, streaming rejection, EOF and budget contracts PASS");
}
