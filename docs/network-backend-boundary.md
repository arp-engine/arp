# Network Backend Boundary

Status: Contract Complete. Production Ready: not claimed.

This document defines the current network backend ownership boundary while the
old event-loop implementation is being retired from protocol and app paths.

Gate summary: LegacySocketPollerBackend lives under `src/network/backends`;
legacy event loop types are backend-private; Production Ready: not claimed.

## Current Boundary

`NetworkService` owns the public L2 request/result contract. Protocol, app,
sample, and resource code should express network intent through typed operation
objects, `NetworkStream`, `CompletionQueue`, and `TimerService`.
`NetworkTransportRequest` is a transitional aggregate carrier and is governed by
`docs/network-typed-transport-governance.md`; it should not be treated as the
final typed transport API.

Backends are split by event model rather than hidden behind one generic poller
facade. The explicit event-model facade names are `SelectSocketBackend`,
`EpollSocketBackend`, and `KqueueSocketBackend`, reporting
`NetworkBackendKind::SelectSocketBackend`,
`NetworkBackendKind::EpollSocketBackend`, and
`NetworkBackendKind::KqueueSocketBackend` respectively. `SelectSocketBackend`
is the default runtime-backed `io_context` socket backend on the current
Windows MinGW path. During this transition it lazily reuses the legacy polling
implementation for real transport submission, but capability and validation no
longer construct or expose the legacy backend worker.

The default socket backend is selected by `makeDefaultSocketBackend()`: Linux
selects `EpollSocketBackend`, Darwin selects `KqueueSocketBackend`, and other
current builds select `SelectSocketBackend`. This default selection is a
backend implementation detail. The future public facade is NetworkService plus typed transport facades, not the old public socket-template API.

`SelectSocketBackend`, `EpollSocketBackend`, and `KqueueSocketBackend` each own
their own backend-private poller core:

- `SelectSocketPollerCore`
- `EpollSocketPollerCore`
- `KqueueSocketPollerCore`

This is source-level implementation separation. Current local evidence is
MinGW/Windows-only, so only the select path has local smoke evidence here.
Linux epoll and Darwin kqueue still need platform/backend validation before
claiming Backend Validated or Platform Validated.

Each socket poller core owns its transitional worker, socket maps, `io_service`,
`SocketRuntime`, and low-level socket operation path. The concrete backend
lazily creates its core for real transport submission.
`LegacySocketPollerBackend` remains only as a compatibility adapter under
`src/network/backends` and forwards to the same core; it no longer owns the
worker implementation directly.

`LegacySocketPollerBackend` lives under `src/network/backends`. Its public
header exposes the `NetworkBackend` implementation only. The legacy event loop
types are backend-private implementation details:

- `io_service`
- `io_operation`
- `poller`
- `socket_ops`
- `SocketRuntime`
- `socket_t`
- `deadline_timer`

The current consolidation step keeps backend files flat under `backends` and
moves the worker thread, socket maps, event service, and socket runtime out of
the legacy facade into `SelectSocketPollerCore`. This prevents callers from
including the legacy facade and accidentally depending on legacy poller headers.

## Retained Legacy Surface

The old socket/event APIs still exist under `src/network` for staged migration
and legacy tests. They do not count as protocol-stack production evidence.
Existing legacy tests may include them directly while they are being moved or
renamed into backend-owned test coverage. `tcp::socket<ipv4/ipv6>` is legacy migration debt, and `udp::socket<ipv4/ipv6>`, public `io_context` socket
behavior, `native_handle`, and public `deadline_timer` follow the same
retirement path.

New protocol evidence must use:

- `NetworkService`
- `NetworkStream`
- future `NetworkListener`
- future `NetworkDatagram`
- future `NetworkResolver`
- `CompletionQueue`
- `TimerService`
- typed transport operation objects
- endpoint, resolver, listen, and datagram policies

The retained template socket types are not type-safe enough for the target
architecture. `tcp::socket<ipv6>` chooses an address family at `open()` time,
but later `connect(endpoint)` and the runtime-backed async path can observe a
different endpoint family. Address family must therefore be expressed through
explicit endpoint, resolver, connect, listen, and datagram policies, not public
socket template parameters.

New production protocol/app/sample paths must not depend on public
`io_context`, `io_service`, `socket_t`, `native_handle`, `deadline_timer`, or
poller headers. New production paths also must not treat `tcp::socket` or
`udp::socket` as the future public facade.

## Next Consolidation Steps

Future phases should continue moving the retained legacy event-loop files into
`src/network/backends` or an equivalent backend-private module. Each move
must keep deterministic diagnostics and focused tests:

- move the remaining shared legacy event-loop pieces (`io_service`, `poller`,
  `socket_ops`, and `SocketRuntime`) behind the concrete backend cores so each
  platform backend owns its event driver directly;
- delete the `LegacySocketPollerBackend` adapter after remaining direct tests
  and compatibility includes move to concrete event-model backends;
- move `io_service`, `io_operation`, and `poller` into the legacy backend
  module;
- move `socket_ops`, `socket_runtime`, and low-level socket helpers behind the
  backend implementation boundary;
- keep legacy socket tests explicitly named as legacy or backend-private
  compatibility coverage; they must not count as new public facade evidence;
- migrate `tcp::socket::async_accept`, listener bind/listen ownership, and
  `udp::socket` datagram operations to NetworkService-backed
  `NetworkListener`, `NetworkDatagram`, and typed transport facades;
- migrate raw `NetworkTransportRequest` construction to typed transport
  operation objects, then retire the aggregate request carrier from production
  paths;
- delete or quarantine public `tcp::socket`, `udp::socket`, public
  `native_handle`, and public `deadline_timer` after protocol, sample, and
  mainline tests move to the typed facade path;
- keep legacy-only tests clearly named so they do not count as Production Ready
  or platform-native evidence.

This consolidation is not a native backend implementation. Windows IOCP, Linux
epoll/io_uring, Darwin kqueue, and web platform backends must still implement
the `NetworkBackend` contract independently before any Platform Validated or
Production Ready claim.
