#include "network/http/uri.h"

#include <cassert>

namespace {

void parsesAbsoluteHttpSchemes()
{
    using namespace arp::network::http;

    const auto http = Uri::parse("http://example.com/index.html?debug=1#top");
    assert(http.success());
    assert(http.diagnostic == DiagnosticCode::none);
    assert(http.uri.is_absolute());
    assert(http.uri.scheme() == "http");
    assert(http.uri.host() == "example.com");
    assert(http.uri.port() == 80);
    assert(!http.uri.has_explicit_port());
    assert(http.uri.path() == "/index.html");
    assert(http.uri.query() == "debug=1");
    assert(http.uri.fragment() == "top");

    const auto https = Uri::parse("https://example.com");
    assert(https.success());
    assert(https.uri.port() == 443);
    assert(https.uri.path() == "/");

    const auto ws = Uri::parse("ws://127.0.0.1:8080/chat");
    assert(ws.success());
    assert(ws.uri.scheme() == "ws");
    assert(ws.uri.host() == "127.0.0.1");
    assert(ws.uri.port() == 8080);
    assert(ws.uri.has_explicit_port());

    const auto wss = Uri::parse("wss://[::1]/socket");
    assert(wss.success());
    assert(wss.uri.scheme() == "wss");
    assert(wss.uri.host() == "::1");
    assert(wss.uri.port() == 443);
    assert(wss.uri.path() == "/socket");
}

void parsesOriginFormAndPercentDecodedComponents()
{
    using namespace arp::network::http;

    const auto parsed = Uri::parse("/asset%20path/file?q=a%2Bb#frag%2Dment");
    assert(parsed.success());
    assert(!parsed.uri.is_absolute());
    assert(parsed.uri.scheme().empty());
    assert(parsed.uri.host().empty());
    assert(parsed.uri.port() == 0);
    assert(parsed.uri.path() == "/asset path/file");
    assert(parsed.uri.query() == "q=a+b");
    assert(parsed.uri.fragment() == "frag-ment");
}

void rejectsInvalidInputWithDeterministicDiagnostics()
{
    using namespace arp::network::http;

    assert(Uri::parse("ftp://example.com/").diagnostic == DiagnosticCode::unsupported_scheme);
    assert(Uri::parse("http:///missing-host").diagnostic == DiagnosticCode::missing_host);
    assert(Uri::parse("http://example.com:abc/").diagnostic == DiagnosticCode::invalid_port);
    assert(Uri::parse("http://example.com:70000/").diagnostic == DiagnosticCode::invalid_port);
    assert(Uri::parse("/bad%XZ").diagnostic == DiagnosticCode::invalid_percent_encoding);
    assert(Uri::parse("relative-without-slash").diagnostic == DiagnosticCode::malformed_uri);
}

} // namespace

int main()
{
    parsesAbsoluteHttpSchemes();
    parsesOriginFormAndPercentDecodedComponents();
    rejectsInvalidInputWithDeterministicDiagnostics();
    return 0;
}
