#include "network/http/parser.h"

#include <cassert>

namespace {

void parsesIncrementalRequestWithContentLength()
{
    using namespace arp::network::http;

    RequestParser parser;
    auto result =
        parser.feed("POST /submit HTTP/1.1\r\nHost: example.com\r\nContent-Length: 2\r\n");
    assert(result.status == ParseStatus::need_more);
    assert(result.diagnostic == DiagnosticCode::none);

    result = parser.feed("\r\nok");
    assert(result.status == ParseStatus::done);
    assert(parser.request().method() == Method::Post);
    assert(parser.request().target() == "/submit");
    assert(parser.request().headers().firstValue("host") == "example.com");
    assert(parser.request().body().size() == 2);
    assert(parser.request().body()[0] == static_cast<std::uint8_t>('o'));
}

void parsesIncrementalResponseAndChunkedBody()
{
    using namespace arp::network::http;

    ResponseParser parser;
    auto result = parser.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n");
    assert(result.status == ParseStatus::need_more);

    result = parser.feed("Wiki\r\n5\r\npedia\r\n0\r\n\r\n");
    assert(result.status == ParseStatus::done);
    assert(parser.response().status().value() == 200);
    assert(parser.response().reason() == "OK");
    assert(parser.response().body().size() == 9);
    assert(parser.response().body()[8] == static_cast<std::uint8_t>('a'));
}

void enforcesLimitsAndDeterministicFailures()
{
    using namespace arp::network::http;

    ParserLimits headerBytes;
    headerBytes.max_header_bytes = 16;
    RequestParser smallHeaderParser(headerBytes);
    assert(smallHeaderParser.feed("GET / HTTP/1.1\r\nHost: example.com").diagnostic
        == DiagnosticCode::header_too_large);

    ParserLimits headerCount;
    headerCount.max_header_count = 1;
    RequestParser tooManyHeaders(headerCount);
    assert(tooManyHeaders.feed("GET / HTTP/1.1\r\nA: 1\r\nB: 2\r\n\r\n").diagnostic
        == DiagnosticCode::too_many_headers);

    RequestParser badLine;
    assert(badLine.feed("GET / HTTP/1.1\nHost: nope\n\n").diagnostic
        == DiagnosticCode::malformed_start_line);

    RequestParser conflictingLength;
    assert(
        conflictingLength.feed("POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nx")
            .diagnostic
        == DiagnosticCode::conflicting_content_length);

    RequestParser unsupportedTransfer;
    assert(unsupportedTransfer.feed("POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n").diagnostic
        == DiagnosticCode::unsupported_transfer_encoding);

    ResponseParser invalidChunk;
    assert(
        invalidChunk.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n").diagnostic
        == DiagnosticCode::invalid_chunk_size);
}

void resetsParserState()
{
    using namespace arp::network::http;

    RequestParser parser;
    assert(parser.feed("GET /one HTTP/1.1\r\n\r\n").status == ParseStatus::done);
    assert(parser.request().target() == "/one");
    parser.reset();
    assert(parser.feed("GET /two HTTP/1.1\r\n\r\n").status == ParseStatus::done);
    assert(parser.request().target() == "/two");
}

} // namespace

int main()
{
    parsesIncrementalRequestWithContentLength();
    parsesIncrementalResponseAndChunkedBody();
    enforcesLimitsAndDeterministicFailures();
    resetsParserState();
    return 0;
}
