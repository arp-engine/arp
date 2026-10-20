#include "tests/fixtures/http_server_fixture.h"

int main()
{
    using namespace arp::network;
    using namespace arp::network::http;
    test::ServerFixture fixture;
    auto& server = fixture.make_server();
    assert(server.route(Method::Get, "/health") == DiagnosticCode::none);
    assert(server.route(Method::Get, "/users/:id") == DiagnosticCode::none);
    assert(server.route(Method::Post, "/echo") == DiagnosticCode::none);
    assert(server.route(Method::Head, "/health") == DiagnosticCode::none);
    assert(server.route(Method::Get, "/no-content") == DiagnosticCode::none);
    assert(server.route(Method::Get, "/cached") == DiagnosticCode::none);
    assert(server.start(endpoint::localhost<ipv4>(0)).success());
    assert(server.local_endpoint().port() != 0);
    for (int i = 0; i < 3; ++i) {
        auto& client = fixture.client(server,
            i == 0 ? "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n"
                : i == 1
                ? "GET /users/42?q=ok HTTP/1.1\r\nHost: localhost\r\n\r\n"
                : "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\necho");
        bool sent = false;
        server.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
            assert(status.success() && request);
            Response response;
            response.set_reason("OK");
            response.set_content_type(ContentType::Plain);
            response.add_header("X-ARP", "server");
            if (i == 0)
                response.set_body_text("healthy");
            if (i == 1) {
                assert(request->param("id") == "42" && request->route_match().query == "q=ok");
                response.set_body_text(request->param("id"));
            }
            if (i == 2)
                response.set_body(request->request().body());
            request->async_respond(response, [&](const SessionStatus& write) {
                assert(write.success());
                sent = true;
            });
        });
        fixture.until([&] {
            return sent && client.done;
        });
        assert(client.response.find("HTTP/1.1 200 OK") != client.response.npos);
        assert(client.response.find("X-ARP: server") != client.response.npos);
        assert(client.response.find("Connection: close") != client.response.npos);
        assert(client.response.find(i == 0 ? "healthy"
                       : i == 1            ? "42"
                                           : "echo")
            != client.response.npos);
    }
    const StringView requests[] = {"HEAD /health HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "GET /no-content HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "GET /cached HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n2;tag=ok\r\nec\r\n2\r\nho\r\n0\r\n\r\n"};
    for (int i = 0; i < 4; ++i) {
        auto& client = fixture.client(server, requests[i]);
        bool sent = false;
        server.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
            assert(status.success() && request);
            Response response;
            response.set_status(StatusCode(i == 1 ? 204 : i == 2 ? 304 : 200));
            response.set_reason(i == 1 ? "No Content" : i == 2 ? "Not Modified" : "OK");
            response.set_body_text(i == 3 ? request->request().body_text() : "healthy");
            request->async_respond(response, [&](const SessionStatus& write) {
                assert(write.success());
                sent = true;
            });
        });
        fixture.until([&] {
            return sent && client.done;
        });
        assert(!client.error);
        assert(client.response.find("Connection: close") != client.response.npos);
        const auto headerEnd = client.response.find("\r\n\r\n");
        assert(headerEnd != client.response.npos);
        if (i == 3)
            assert(client.response.substr(headerEnd + 4) == "echo");
        else
            assert(client.response.size() == headerEnd + 4);
        if (i == 0)
            assert(client.response.find("Content-Length: 7") != client.response.npos);
        if (i == 1)
            assert(client.response.find("Content-Length") == client.response.npos);
    }
}
