#include "core/callback.h"
#include "network/http/parser.h"
#include "network/http/router.h"
#include "network/http/serializer.h"

#include <cassert>

using namespace arp::network::http;

int main()
{
    arp::core::Callback<int(int)> fn([](int value) {
        return value + 1;
    });
    auto moved = std::move(fn);
    assert(!fn && moved(2) == 3);
    int calls = 0;
    arp::core::SharedCallback<int()> shared([&] {
        return ++calls;
    });
    auto copied = shared;
    shared = nullptr;
    assert(!shared && copied() == 1 && copied() == 2);
    Router router;
    assert(router.route(Method::Get, "/users/:id") == DiagnosticCode::none);
    assert(router.route(Method::Get, "/users/me") == DiagnosticCode::none);
    assert(router.route(Method::Post, "/users/:id") == DiagnosticCode::none);
    assert(router.route(Method::Get, "/users/:name") == DiagnosticCode::duplicate_route);
    assert(router.route(Method::Get, "/:id/:id") == DiagnosticCode::invalid_route);
    assert(router.route(Method::Get, "/bad/*") == DiagnosticCode::invalid_route);
    Request request;
    request.set_target("/users/me?q=1");
    assert(router.match(request).pattern == "/users/me");
    request.set_target("/users/a%2Fb?x=2");
    auto match = router.match(request);
    assert(match.param("id") == "a%2Fb" && match.query == "x=2");
    request.set_target("/users/me/");
    assert(router.match(request).status == 404);
    request.set_target("/Users/me");
    assert(router.match(request).status == 404);
    request.set_target("/users/me");
    request.set_method(Method::Delete);
    assert(router.match(request).status == 405 && router.match(request).allow == "GET, POST");
    request.set_target("/users/%X1");
    assert(router.match(request).status == 400);
    router.freeze();
    assert(router.route(Method::Get, "/later") == DiagnosticCode::routes_frozen);
    Router reverse;
    assert(reverse.route(Method::Get, "/users/me") == DiagnosticCode::none);
    assert(reverse.route(Method::Get, "/users/:id") == DiagnosticCode::none);
    request.set_method(Method::Get);
    request.set_target("/users/me");
    assert(reverse.match(request).pattern == router.match(request).pattern);
    Router limited(1);
    assert(limited.route(Method::Get, "/") == DiagnosticCode::none);
    assert(limited.route(Method::Get, "/next") == DiagnosticCode::route_limit);
    Router encoded;
    assert(encoded.route(Method::Get, "/encoded/a%2Fb") == DiagnosticCode::none);
    request.set_target("/encoded/a%2Fb?raw=%20");
    assert(encoded.match(request).status == 200);
    request.set_target("/encoded/a%2Fb?raw=%XY");
    assert(encoded.match(request).status == 400);
    Router shortPattern(2, 4);
    assert(shortPattern.route(Method::Get, "/1234") == DiagnosticCode::route_limit);

    ParserLimits limits;
    limits.server_request_validation = true;
    RequestParser parser(limits);
    assert(
        parser.feed("POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\nx").status
        == ParseStatus::need_more);
    assert(parser.feed("\ny").success());
    assert(parser.request().body_text() == "x\ny");
    parser.reset();
    assert(
        parser
            .feed(
                "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n0\r\n\r\n")
            .diagnostic
        == DiagnosticCode::ambiguous_framing);
    parser.reset();
    assert(
        parser.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 184467440737095516160\r\n\r\n")
            .diagnostic
        == DiagnosticCode::invalid_content_length);
    parser.reset();
    assert(parser.feed("GET / HTTP/1.1\r\n\r\n").diagnostic == DiagnosticCode::invalid_host_header);
    parser.reset();
    assert(
        parser
            .feed("POST / HTTP/1.1\r\nHost: x\r\nExpect: 100-continue\r\nContent-Length: 2\r\n\r\n")
            .diagnostic
        == DiagnosticCode::expectation_not_supported);
    parser.reset();
    assert(parser
            .feed(
                "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nx\ny\r\n0\r\n\r\n")
            .success());
    parser.reset();
    assert(parser
            .feed(
                "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n3; tag=\"a;b\";flag\r\nabc\r\n0\r\n\r\n")
            .success());
    parser.reset();
    assert(
        parser
            .feed(
                "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n3;tag=\"unterminated\r\nabc\r\n0\r\n\r\n")
            .diagnostic
        == DiagnosticCode::invalid_chunk_size);
    parser.reset();
    assert(
        parser
            .feed(
                "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n3;=invalid\r\nabc\r\n0\r\n\r\n")
            .diagnostic
        == DiagnosticCode::invalid_chunk_size);
    parser.reset();
    assert(parser.feed("GET / HTTP/01.01\r\nHost: x\r\n\r\n").diagnostic
        == DiagnosticCode::malformed_start_line);
    parser.reset();
    assert(
        parser.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\na")
            .diagnostic
        == DiagnosticCode::conflicting_content_length);
    parser.reset();
    assert(
        parser
            .feed(
                "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFFF\r\n")
            .diagnostic
        == DiagnosticCode::invalid_chunk_size);
    parser.reset();
    assert(parser.feed("GET / HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n").diagnostic
        == DiagnosticCode::invalid_host_header);
    parser.reset();
    assert(
        parser.feed("GET / HTTP/1.1\r\nHost: x\r\n\r\nGET / HTTP/1.1\r\nHost: x\r\n\r\n").diagnostic
        == DiagnosticCode::ambiguous_framing);
    ParserLimits small;
    small.max_body_bytes = 2;
    assert(RequestParser(small).feed("POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\n").diagnostic
        == DiagnosticCode::body_too_large);
    assert(RequestParser(small)
               .feed("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n")
               .diagnostic
        == DiagnosticCode::body_too_large);
    small.max_header_count = 1;
    assert(RequestParser(small).feed("GET / HTTP/1.1\r\nHost: x\r\nX: y\r\n\r\n").diagnostic
        == DiagnosticCode::too_many_headers);
    small.max_header_bytes = 16;
    assert(RequestParser(small).feed("GET / HTTP/1.1\r\nHost: x\r\n\r\n").diagnostic
        == DiagnosticCode::header_too_large);
    limits.max_buffer_bytes = 8;
    RequestParser bounded(limits);
    assert(bounded.feed("123456789").diagnostic == DiagnosticCode::buffer_too_large);
    ResponseParser binaryResponse;
    assert(binaryResponse.feed("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nx\ny").success());
    assert(binaryResponse.response().body_text() == "x\ny");
    assert(
        ResponseParser().feed("HTTP/1.1 304 Not Modified\r\nContent-Length: 99\r\n\r\n").success());
    ParserLimits head;
    head.head_response = true;
    assert(ResponseParser(head).feed("HTTP/1.1 200 OK\r\nContent-Length: 99\r\n\r\n").success());
    assert(ResponseParser(limits).feed("123456789").diagnostic == DiagnosticCode::buffer_too_large);

    Response response;
    response.set_body_text("hello");
    response.add_header("Connection", "keep-alive");
    SerializationOptions options;
    options.head_response = true;
    options.close_connection = true;
    auto wire = Serializer::serializeResponse(response, options);
    assert(wire.success());
    assert(wire.text.find("Content-Length: 5") != wire.text.npos);
    assert(wire.text.find("hello") == wire.text.npos);
    assert(wire.text.find("keep-alive") == wire.text.npos);
    assert(wire.text.find("Connection: close") != wire.text.npos);
    options.head_response = false;
    response.set_status(StatusCode(204));
    wire = Serializer::serializeResponse(response, options);
    assert(wire.text.find("Content-Length") == wire.text.npos
        && wire.text.find("hello") == wire.text.npos);
    response.set_status(StatusCode(304));
    assert(Serializer::serializeResponse(response, options).text.find("hello") == wire.text.npos);
    response.set_reason("Bad\r\nInjected");
    assert(!Serializer::serializeResponse(response).success());
}
