#include "network/http/serializer.h"

#include <cassert>

namespace {

arp::network::byte_buffer bodyFromText(arp::network::StringView text)
{
    arp::network::byte_buffer body{&arp::core::defaultCpuMemoryResource()};
    for (char c : text) {
        body.push_back(static_cast<std::uint8_t>(c));
    }
    return body;
}

void serializesRequestWithDeterministicHeadersAndContentLength()
{
    using namespace arp::network::http;

    Request request;
    request.set_method(Method::Post);
    request.set_target("/submit");
    assert(request.headers().add("X-Trace", "one") == DiagnosticCode::none);
    assert(request.headers().add("X-Trace", "two") == DiagnosticCode::none);
    request.set_body(bodyFromText("ok"));

    const auto serialized = Serializer::serializeRequest(request);
    assert(serialized.success());
    assert(serialized.text
        == "POST /submit HTTP/1.1\r\nX-Trace: one\r\nX-Trace: two\r\nContent-Length: 2\r\n\r\nok");
    assert(serialized.text.find("Connection:") == arp::core::pmr::String::npos);
}

void serializesResponseAndExplicitChunkedBody()
{
    using namespace arp::network::http;

    Response response;
    response.set_status(StatusCode(200));
    response.set_reason("OK");
    assert(response.headers().add("Server", "arp") == DiagnosticCode::none);
    response.set_body(bodyFromText("hello"));

    SerializationOptions options;
    options.transfer_mode = BodyTransferMode::chunked;
    const auto serialized = Serializer::serializeResponse(response, options);
    assert(serialized.success());
    assert(serialized.text
        == "HTTP/1.1 200 OK\r\nServer: arp\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
}

} // namespace

int main()
{
    serializesRequestWithDeterministicHeadersAndContentLength();
    serializesResponseAndExplicitChunkedBody();
    return 0;
}
