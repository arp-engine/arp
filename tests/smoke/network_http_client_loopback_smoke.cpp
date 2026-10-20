#include "network/endpoint.h"
#include "network/http/client.h"
#include "network/http/session.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket.h"
#include "network/socket_runtime.h"
#include "network/transport.h"

#include <cassert>
#include <thread>

namespace {

bool contains(arp::network::StringView text, arp::network::StringView fragment)
{
    return text.find(fragment) != arp::network::StringView::npos;
}

} // namespace

int main()
{
    using namespace arp::network;
    using namespace arp::network::http;

    SocketRuntime runtime;
    assert(runtime.valid());

    io_service service;
    io_context context(&service);
    error_code ec;

    tcp::socket<ipv4> acceptor(&context);
    tcp::socket<ipv4> serverClient(&context);

    assert(acceptor.open(ec) == 0);
    assert(acceptor.set_reuseaddr(ec, true) == 0);
    assert(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    endpoint listenEndpoint(acceptor.local_endpoint());
    assert(listenEndpoint.port() != 0);
    assert(acceptor.listen(ec, 4) == 0);

    Session serverSession;
    bool accepted = false;
    bool requestValidated = false;
    bool responseValidated = false;

    acceptor.async_accept(serverClient, [&](const error_code& acceptEc) {
        assert(!acceptEc);
        accepted = true;
        serverSession.async_read_request(
            serverClient, [&](const SessionStatus& readStatus, const Request& request) {
                assert(readStatus.success());
                assert(request.method() == Method::Get);
                assert(request.target() == "/anet-style.html");
                assert(request.header("host") == listenEndpoint.to_string());
                assert(request.header("accept") == "text/html");
                requestValidated = true;

                Response response;
                response.set_status(StatusCode(200));
                response.set_reason("OK");
                assert(response.set_content_type(ContentType::Html) == DiagnosticCode::none);
                response.set_body_text(
                    "<!doctype html><html><head><title>Client Facade</title></head>"
                    "<body><h1>anet-style client</h1></body></html>");
                serverSession.async_write_response(
                    serverClient, response, [&](const SessionStatus& writeStatus) {
                        assert(writeStatus.success());
                    });
            });
    });

    Client client(&context);
    arp::core::pmr::String uri{&arp::core::defaultCpuMemoryResource()};
    uri += "http://";
    uri += listenEndpoint.to_string();
    uri += "/anet-style.html";
    auto request = ClientRequest::get(uri);
    assert(request.add_header("Accept", "text/html") == DiagnosticCode::none);

    client.async_send(request, [&](const ClientStatus& status, const Response& response) {
        assert(status.success());
        assert(response.status().value() == 200);
        assert(response.header("content-type") == "text/html");
        assert(contains(response.body_text(), "<title>Client Facade</title>"));
        assert(contains(response.body_text(), "<h1>anet-style client</h1>"));
        responseValidated = true;
        context.shutdown();
    });

    std::thread reactor([&]() {
        service.run();
    });

    while (!service.quit()) {
        context.dispatch();
    }

    reactor.join();
    context.dispatch();
    assert(accepted);
    assert(requestValidated);
    assert(responseValidated);

    return 0;
}
