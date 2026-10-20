#include "network/http/message.h"

#include <cassert>

namespace {

void parsesMethodsAndStatusCodes()
{
    using namespace arp::network::http;

    const auto get = parseMethod("GET");
    assert(get.success());
    assert(get.method == Method::Get);

    const auto patch = parseMethod("PATCH");
    assert(patch.success());
    assert(patch.method == Method::Patch);

    assert(parseMethod("").diagnostic == DiagnosticCode::invalid_method);
    assert(parseMethod("BAD METHOD").diagnostic == DiagnosticCode::invalid_method);

    const auto ok = StatusCode::parse(200);
    assert(ok.success());
    assert(ok.status.value() == 200);

    assert(StatusCode::parse(99).diagnostic == DiagnosticCode::invalid_status_code);
    assert(StatusCode::parse(600).diagnostic == DiagnosticCode::invalid_status_code);
}

void preservesRepeatedHeadersAndCaseInsensitiveLookup()
{
    using namespace arp::network::http;

    HeaderList headers;
    assert(headers.add("Set-Cookie", "a=1") == DiagnosticCode::none);
    assert(headers.add("set-cookie", "b=2") == DiagnosticCode::none);
    assert(headers.add("Content-Type", "text/plain") == DiagnosticCode::none);

    assert(headers.size() == 3);
    assert(headers.count("SET-cookie") == 2);
    assert(headers.at(0).name() == "Set-Cookie");
    assert(headers.at(1).value() == "b=2");
    assert(headers.firstValue("content-type") == "text/plain");
    assert(headers.firstValue("missing").empty());

    assert(headers.add("Bad Header", "value") == DiagnosticCode::invalid_header_name);
    assert(headers.add("X-Test", "bad\r\nvalue") == DiagnosticCode::invalid_header_value);
}

void storesRequestAndResponseValueObjects()
{
    using namespace arp::network::http;

    byte_buffer requestBody{&arp::core::defaultCpuMemoryResource()};
    requestBody.push_back(static_cast<std::uint8_t>('o'));
    requestBody.push_back(static_cast<std::uint8_t>('k'));

    Request request;
    request.set_method(Method::Post);
    request.set_target("/submit");
    request.set_version(Version{1, 1});
    assert(request.headers().add("Content-Length", "2") == DiagnosticCode::none);
    request.set_body(requestBody);

    assert(request.method() == Method::Post);
    assert(request.target() == "/submit");
    assert(request.version().major == 1);
    assert(request.version().minor == 1);
    assert(request.headers().firstValue("content-length") == "2");
    assert(request.body().size() == 2);
    assert(request.body()[0] == static_cast<std::uint8_t>('o'));

    Response response;
    response.set_status(StatusCode(201));
    response.set_reason("Created");
    response.set_version(Version{1, 0});
    assert(response.headers().add("Server", "arp") == DiagnosticCode::none);
    response.set_body(requestBody);

    assert(response.status().value() == 201);
    assert(response.reason() == "Created");
    assert(response.version().minor == 0);
    assert(response.headers().firstValue("server") == "arp");
    assert(response.body().size() == 2);
}

} // namespace

int main()
{
    parsesMethodsAndStatusCodes();
    preservesRepeatedHeadersAndCaseInsensitiveLookup();
    storesRequestAndResponseValueObjects();
    return 0;
}
