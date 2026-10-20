# Network protocol stack

## Current layering and status

TCP socket/event loop -> optional TLS stream -> HTTP session -> WebSocket upgrade/frame stream

The current implementation uses `io_service`, `io_context`, typed TCP sockets,
`http::Session`, Parser and Serializer. HTTP clients retain their existing
interfaces. There is no public NetworkService/NetworkStream migration in this
checkout; older descriptions of that migration do not describe current source.
TLS disabled/unavailable paths fail diagnostically and never downgrade to plaintext.

- HTTP server: Contract Complete and Prototype Complete. Focused Windows MinGW
  Platform Validated evidence is recorded in `http-server-plan.md`.
- HTTP client: existing URI/message/parser/serializer/session/streaming and
  TCP loopback tests remain regression paths.
- TLS/HTTPS: explicit `tls::SslStream` adapter; encrypted server is outside the
  HTTP server contract. This change does not claim encrypted transport validation.
- WebSocket: existing handshake/frame/client API; HTTP Server does not upgrade
  connections. WSS keeps the explicit TLS boundary.
- Backend Validated: not applicable; this is not an RHI backend.
- Platform Validated: focused Windows MinGW evidence only. Linux, macOS,
  Android/iOS and browser builds are not promoted by Windows tests.
- Production Ready: not claimed.

## Simple HTTP server contract

`http::Router::route(method, pattern)` registers exact origin paths and `:name`
segments. Static segments win lexicographically over parameter segments, within
matching methods, independent of registration order. Duplicate routes and
same-method parameter shapes, duplicate parameter names, malformed patterns,
wildcards and excessive route counts/bytes fail diagnostically. Parameter names
contain ASCII letters, digits or underscore. Route-pattern bytes are capped by
the configured header-byte budget. The table freezes after a successful start,
including subsequent stop/restart cycles.

Matching strips query for routing, preserves query text, percent encoding and
case, and distinguishes trailing `/`. No decoding, slash collapsing or dot
normalization occurs. For example `/users/a%2Fb` binds `id` to `a%2Fb`.
404 means no path matches; 405 means a path matches but the method does not,
and includes the deterministic `Allow` list. HEAD must be registered explicitly.

Only HTTP/1.1 origin-form requests with exactly one nonempty Host are accepted.
Content-Length and chunked framing reuse the existing parser. Conflicting
lengths, overflowing numbers, conflicting/duplicate Transfer-Encoding,
Transfer-Encoding plus Content-Length, malformed targets and over-limit
messages fail before business dispatch. Expectations are rejected with 417.
Pipelined requests are unsupported and rejected rather than silently consumed.
Chunk trailers are outside the parser contract and fail diagnostically.
Chunk extensions are syntax validated and ignored semantically; invalid token
or quoted-string syntax fails. HTTP-version uses the RFC single-digit grammar.

Each response sends `Connection: close`. Serializer owns wire framing and
replaces user Content-Length/Transfer-Encoding. HEAD sends representation
length without body; 204 omits body/framing; 304 sends representation length
without body. Response parsers accept an explicit `ParserLimits.head_response`
when consuming a HEAD reply; 204/304 are recognized from status.

## Limits and lifetime

| Setting | Default |
| --- | --- |
| Connections / queued requests / routes | 16 / 16 / 64 |
| Read / business response / write timeout | 10 / 30 / 10 seconds |
| Header bytes / header count | 8192 / 64 |
| Body bytes / receive cache bytes | 1048576 / 2097152 |

Use `ServerConfig` or reflected `HttpServerConfig` to configure limits. A full
request queue produces 503. At the connection limit, accept pauses until a
connection retires. Only one pending `async_next_request`/`next_request` is
allowed; a second fails with `server_busy`. A request can respond once. Invalid
responses can be corrected before the original business deadline; expired,
disconnected, stopped or already-responded requests cannot send another reply.

All public operations and callbacks run on the context dispatch thread.
The host owns the reactor thread and shared context. Socket closure is queued
to the reactor, which removes poller/pending operations before releasing the
descriptor. Stop is idempotent, cancels listener/connections/timers and completes
waiting business operations. It does not shut down the shared event loop.
Restart is allowed after `stopped()` becomes true; numeric IPv4/IPv6 endpoints
are explicit and port zero returns the actual bound port. Browser/Wasm start
returns `unsupported_platform`.

Native requests retain bounded connection state. The host must keep the context
alive until cleanup completes, including after destroying a Server facade.
ArpScript runtime owns server/request wrappers. `request.destroy()` abandons an
unanswered request and invalidates its reflected reference. `server.destroy()`
or runtime destruction stops the server and invalidates owned references;
retained request wrappers keep diagnostic state until explicitly destroyed.
Per-server retained script requests are capped at `max_connections`; destroy
completed requests to release those slots. Async state, callback targets,
strings, containers and owned objects use the core allocator.

## Usage and validation

Runnable examples: `examples/network/http_server.cpp` and `http_server.arp`,
both serving `/health`, `/users/:id` and POST `/echo`. Build with:

```powershell
python scripts/build.py --preset win-vulkan-debug configure
python scripts/build.py --preset win-vulkan-debug test --target arp_network_http_router_tests
python scripts/build.py --preset win-vulkan-debug test --target arp_network_http_server_tests
python scripts/build.py --preset win-vulkan-debug test --target arp_scriptvm_http_server_tests
python scripts/build.py --preset win-vulkan-debug test --target arp_scriptvm_http_server_aot_smoke
cmake --build --preset win-vulkan-debug --target arp_http_server_example
build/win-vulkan-debug/arp_http_server_example.exe --port 0 --duration-ms 10000
```

The C++ example reports `listening_port`; its default listener is explicitly
127.0.0.1. CTest `arp_http_server_example_smoke` invokes three real loopback
requests and verifies response bytes, headers and EOF with a finite deadline.

Script `setup()` returns the native server. A host invokes/resumes `serve_one`
and drives `io_context::dispatch`; the function awaits a reflected request,
builds `HttpResponse`, awaits `respond`, destroys the request and yields sent
bytes. It uses the existing generator frame contract for DirectNative AOT.
No script function callback or implicit scheduler is installed. Calling
`next_request`/`respond` itself synchronously creates a `ScriptAwaitable`; await
performs suspension. A failed `next_request` fails await diagnostically; respond
returns `NetworkResult` on both success and failure (`success`, `code`, `bytes`,
`message`). Request exposes `method()`, `target()`, `path()`, `query()`,
`headers()`, `header(name)`, `body()`, `route_pattern()` and `param(name)`.
Response exposes `status`, `reason`, `body`, `content_type` and `add_header`.
Existing client request `content_type()` and `body()` getters remain callable;
their explicit `AFUNC(..., MethodOnly)` marker prevents property synthesis.
AOT hosts install `describeNetworkScriptValue` from `network/network_aot.h` for
retained reflected values, plus their metadata-validated reflection call/export
bridge. The AOT smoke compiles generated native code and uses actual plugin
reflection identities; it does not replace Server/Router with fixtures.

Generated reflection is rebuilt by CMake. The YAML plugin export identity is
checked by `arp_network_http_server_reflection_contract`. To deliberately update
it after changing the public surface:

```powershell
python -X utf8 scripts/reflection_contract_hash.py --reflection build/win-vulkan-debug/generated/reflection/arp_network/arp_network.reflection.json --manifest modules/arp.network_module.arpplugin --update
```

TLS server, keep-alive, wildcard/regex routing, static directories, middleware,
WebSocket upgrade, named-host listening and cross-platform production readiness
are outside this version's claims.
