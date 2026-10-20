# HTTP server implementation plan

Contract: C++ and ArpScript HTTP/1.1 server, method/path routes with named
segments, raw encoded parameters, one response per connection, bounded queues.
No HTTPS, keep-alive, callback VM scheduler, wildcard or filesystem serving.

## Phases

- [x] 1. Router and parser/serializer contracts; fast tests and loopback smoke.
- [x] 2. Reactor-owned cancellation, server, timeouts and lifecycle tests/smoke.
- [x] 3. Reflected script objects, Interpreter/AOT smoke, examples and docs.

## Status

Contract Complete: route, framing, bounds, lifecycle, reflection and plugin
identity contracts are implemented and tested. Prototype Complete: native
Server, reflected ArpScript objects, examples and Interpreter/DirectNative AOT
integration. Platform Validated: Windows IPv4 loopback with MinGW Debug,
including actual generated AOT code. Backend Validated: not applicable to this
network change. Production Ready: not claimed; IPv6 and other platforms have
not been runtime validated. Browser/Wasm listening fails explicitly.

## Phase evidence (2026-10-14)

1. Fast path: `arp_network_http_router_tests` covers registration conflicts,
   specificity, raw paths/parameters, query/trailing slash, 404/405, parser
   fragmentation, framing/overflow/limits and HEAD/204/304 serialization.
   Smoke: existing `arp_network_http_loopback_smoke` plus the final server smoke
   confirm real TCP transport and protocol bytes. Chunk extensions are syntax
   validated with the existing HTTP token validator; invalid extensions and
   noncanonical HTTP-version syntax fail before business dispatch.
2. Fast path: `arp_network_http_server_tests` covers one pending waiter, one
   response, queue overload, deadlines, disconnect, stop during accept/read/
   write, facade destruction, restart, connection admission and independent
   servers sharing one context. It also fills a real TCP send buffer to verify
   cancellation of a pending write completes exactly once.
   Smoke: `arp_network_http_server_loopback_smoke` uses port zero and verifies
   health/users/echo, chunked input, HEAD/204/304 output and EOF.
3. Fast path: `arp_scriptvm_http_server_tests`, export-hash validation and
   `arp_scriptvm_resource_network_plugin_parity_tests` cover reflected ownership,
   diagnostics and legacy client methods. `MethodOnly` explicitly preserves the
   existing callable content_type/body getters when a setter is also reflected.
   Smoke: `arp_scriptvm_http_server_smoke` and
   `arp_scriptvm_http_server_aot_smoke` run route -> await request -> respond
   through the actual network plugin, with all three example routes and TCP EOF.
   `arp_http_server_example_smoke` launches the C++ executable hidden and
   verifies three requests before its finite lifetime ends.

The final focused CTest run passed **24/24** in **12.54 seconds**, including
existing HTTP URI/message/parser/serializer/session/streaming/client tests,
network allocator/protocol/architecture/TLS contracts, VM/AOT v5 tests and
reflection inheritance regression. Log:
`build/http-server-debug/http-server-validation.log`.

Build orchestration used `scripts/build.py` with a temporary user preset
inheriting `win-vulkan-debug`, using an isolated `build/http-server-debug`
directory and disabling editor targets to avoid disturbing concurrent work.
The temporary preset was removed after validation. Standard-preset build
commands are in `docs/network-protocols.md`; the retained evidence directory
can be checked directly with `ctest --test-dir build/http-server-debug`.

Current network export identity:
`sha256:3433e7df75178e644042cc414d4b128e77b0aa91e999acb88a127fd3cc9d57fd`.

## Defaults and ownership

16 connections, 16 queued requests, 64 routes; read/handler/write deadlines
10/30/10 seconds; 8192 header bytes, 64 headers, 1048576 body bytes, 2097152
wire-buffer bytes. Limits are explicit configuration, not semantic inference.
Server operations and dispatch run on the host dispatch thread; reactor owns
IO cancellation and closing descriptors. Host keeps io_context alive through
server cleanup. Stop does not shut down the shared context.
