#include "network/http/client.h"

#include <cassert>

namespace {

void buildsRequestFromAbsoluteUri()
{
    using namespace arp::network::http;

    auto request = ClientRequest::post("http://127.0.0.1:8080/api/v1/items?debug=1");
    assert(request.success());
    assert(request.uri().scheme() == "http");
    assert(request.uri().host() == "127.0.0.1");
    assert(request.uri().port() == 8080);
    assert(request.host_header() == "127.0.0.1:8080");
    assert(request.message().method() == Method::Post);
    assert(request.message().target() == "/api/v1/items?debug=1");

    assert(request.add_header("Accept", "application/json") == DiagnosticCode::none);
    assert(request.set_content_type(ContentType::Json) == DiagnosticCode::none);
    request.set_body_text("{\"ok\":true}");

    assert(request.message().headers().firstValue("accept") == "application/json");
    assert(request.message().headers().firstValue("content-type") == "application/json");
    assert(request.message().body_text() == "{\"ok\":true}");
}

void exposesResponseConvenienceAccessors()
{
    using namespace arp::network::http;

    Response response;
    assert(response.add_header("Content-Type", "text/html") == DiagnosticCode::none);
    response.set_body_text("<html>ok</html>");

    assert(response.header("content-type") == "text/html");
    assert(response.body_text() == "<html>ok</html>");
}

void reportsSecureTransportWithoutPlaintextFallback()
{
    using namespace arp::network;
    using namespace arp::network::http;

    io_context context;
    Client client(&context);
    auto request = ClientRequest::get("https://127.0.0.1/");

    bool completed = false;
    client.async_send(request, [&](const ClientStatus& status, const Response&) {
        assert(!status.success());
        assert(status.diagnostic == DiagnosticCode::tls_unavailable);
        completed = true;
    });

    assert(completed);
}

} // namespace

int main()
{
    buildsRequestFromAbsoluteUri();
    exposesResponseConvenienceAccessors();
    reportsSecureTransportWithoutPlaintextFallback();
    return 0;
}
