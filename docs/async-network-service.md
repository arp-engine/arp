# Async NetworkService Contract

Status: Contract Complete.

This document defines the backend-neutral L2 contract for engine network IO.
It does not claim Prototype Complete, Backend Validated, Platform Validated, or
Production Ready status for a NetworkService implementation.

## L2 Model

NetworkService is the public network IO facade. NetworkBackend is the private
backend interface that owns socket readiness, native completion handles, web
platform handles, and backend-specific polling state.

The future public network facade is not the old template socket API.
NetworkService owns transport submission and completion; `NetworkStream`,
NetworkListener, NetworkDatagram, and NetworkResolver are the intended public
typed facades over NetworkService. Legacy `tcp::socket<ipv4/ipv6>`,
`udp::socket<ipv4/ipv6>`, public `io_context` socket behavior, `native_handle`,
and public `deadline_timer` are migration debt and must not be used as new
production evidence.

NetworkService accepts NetworkIoRequest objects and completes with
NetworkIoResult objects. Every result carries a NetworkIoStatus and, when
applicable, deterministic diagnostics. NetworkBackendKind identifies the
backend family used for capability reporting and diagnostics.

Callbacks complete through the shared AsyncRuntime CompletionQueue unless a
future typed request explicitly declares a different completion mode and the
backend capability report supports it.

## Request Types

NetworkIoRequest and NetworkTransportRequest are transitional request carriers.
The target transport API is a set of concrete typed operation objects governed
by `docs/network-typed-transport-governance.md`. The aggregate
NetworkTransportRequest remains only as a staged adapter while facade and
backend paths migrate.

The typed operation model covers these domain requests. The final operation
names are `NetworkConnectOp`, `NetworkListenOp`, `NetworkAcceptOp`,
`NetworkReadOp`, `NetworkWriteOp`, `NetworkDatagramBindOp`,
`NetworkDatagramReceiveOp`, `NetworkDatagramSendOp`, `NetworkCloseOp`, and
`NetworkCancelOp`. `NetworkOperationTraits` owns kind mapping, validation,
timeout lookup, handle access, and transitional conversion to
`NetworkTransportRequest` while the aggregate adapter remains.

- AcceptRequest: accepts one incoming stream connection from a listening socket
  owned by NetworkService.
- ConnectRequest: connects to a typed endpoint with an explicit connect timeout
  policy.
- ReadRequest: reads bytes from a stream into caller-provided or service-owned
  buffer storage described by the request contract.
- WriteRequest: writes bytes to a stream and reports how many bytes were
  accepted by the transport.
- DatagramReceiveRequest: receives one datagram and returns a typed endpoint
  value owned by the result.
- DatagramSendRequest: sends one datagram to a typed endpoint.
- CloseRequest: closes a NetworkService-owned stream, listener, or datagram
  endpoint.
- CancelRequest: cancels a previously accepted request by stable request id.

NetworkResolveRequest is the typed resolver contract. It resolves host/service
text into owned endpoint values under explicit address-family, ordering,
timeout, cancellation, socket-kind, and diagnostic policy. The current contract
path deterministically resolves numeric IPv4/IPv6 host text and numeric service
text through CompletionQueue. Named-host DNS still returns deterministic no-DNS
diagnostics through CompletionQueue until a WorkService or native resolver
backend owns real DNS execution. DNS must not be guessed or hidden inside HTTP,
WebSocket, TLS, or connect fallback behavior.

Requests must not expose native sockets, file handles, platform descriptors,
TLS library handles, or backend poller keys to renderer, asset, resource, game,
sample, or UI callers.

## Typed Transport Handles

NetworkStreamHandle, NetworkListenerHandle, and NetworkDatagramHandle are
opaque NetworkService-owned identifiers. They are not socket descriptors, poller
keys, OS handles, TLS handles, or backend allocation ids. The backend may keep
socket/native data internally, but those details are backend-private only and
must never be observable by protocol, renderer, asset, resource, game, sample,
or UI code.

NetworkConnectRequest creates or completes a NetworkStreamHandle.
NetworkListenRequest binds/listens a NetworkListenerHandle to a typed local
endpoint and backlog. NetworkAcceptRequest consumes a NetworkListenerHandle and
returns a NetworkStreamHandle. NetworkReadRequest and NetworkWriteRequest
operate on NetworkStreamHandle. NetworkDatagramBindRequest binds a
NetworkDatagramHandle to a typed local endpoint. NetworkDatagramReceiveRequest
and NetworkDatagramSendRequest operate on NetworkDatagramHandle; send requests
carry an explicit typed remote endpoint. NetworkCloseRequest and
NetworkCancelRequest use typed handles only.

`NetworkStream`, `NetworkListener`, and `NetworkDatagram` are thin public
facades over these handles. `NetworkListener` accepts `NetworkListenOptions`
with endpoint, backlog, cancellation, timeout, and `DualStackPolicy`.
`NetworkDatagram` accepts `NetworkDatagramBindOptions` with endpoint,
address-family policy, and cancellation, then uses typed datagram receive/send
operations for packet IO. These facades must not expose `socket_t`,
`native_handle`, poller keys, `io_service`, or public `deadline_timer`.

Buffer ownership is explicit. The buffer ownership contract says read buffers
are caller-owned writable spans that
must remain valid until completion; write buffers are caller-owned readable
spans that must remain valid until completion unless a future request flag says
NetworkService has copied the bytes. No production path may infer buffer
lifetime from protocol type or backend behavior.

Partial transfer behavior is explicit. The partial transfer contract says read
and write completions report
bytes-transferred and a short-transfer flag when fewer bytes than requested were
completed without treating the operation as an implicit failure. Peer close is a
stable result condition and must be reported through typed status/diagnostics,
not guessed from zero bytes by upper layers.

Connect, read, write, accept, and datagram timeout policy uses TimerService
semantics. TimerService semantics are the observable timeout contract for
network transport operations. Timeouts are scheduled and completed through the runtime timer
contract, not public `io_context` timers. A backend may use private wait
mechanisms internally, but the observable timeout status and callback delivery
must match TimerService and CompletionQueue rules.

TimerService timeout integration status: Contract Complete for
NetworkService-owned connect/read/write timeout scheduling. A transport result
that remains Pending with an explicit timeout is completed as Timeout through
TimerService and the shared CompletionQueue, never inline from submission.
Late completion ownership uses exactly-once state so a timeout result cannot
call the user callback twice. Backend Validated, Platform Validated, and
Production Ready remain blocked until native/event backend phases provide
backend-specific evidence.

## Result And Status

NetworkIoResult includes:

- request id
- request kind
- NetworkIoStatus
- transferred byte count for read, write, and datagram operations
- typed local and peer endpoint values when the operation defines them
- deterministic diagnostic code and message label

NetworkIoStatus must distinguish:

- success
- short transfer
- cancelled
- timeout
- peer close
- retryable would-block
- connection refused
- address unavailable
- network unreachable
- permission denied
- backend unavailable
- unsupported operation
- invalid request
- queue full
- service closed
- backend failure
- pending

Partial stream reads and writes may complete as short transfer only when the
request policy allows it. Exact-transfer requests must complete as success only
when the requested byte count is met; otherwise they fail with a deterministic
short transfer diagnostic. Datagram send/receive does not silently split a
datagram.

Peer close is reported as an explicit status. Retryable would-block is reported
only for request modes that permit retryable readiness semantics; completion
backends should otherwise keep the operation pending or fail deterministically.

## Cancellation And Timeouts

AcceptRequest supports accept cancellation through CancelRequest. ConnectRequest
supports connect timeout as an explicit duration or deadline in the request.
Cancelling before backend submission, while queued, while running, and after
completion must be deterministic and callback exactly once when the original
request was accepted.

Late backend completions after cancellation must be discarded or converted into
diagnostic telemetry; they must not call the user callback a second time.

Accepted connect/read/write timeout completions are scheduled with
TimerService. TimerService submission failures map deterministically to
NetworkIoStatus::QueueFull, ServiceClosed, Cancelled, or InvalidRequest.
Accepted timeout completions report NetworkIoStatus::Timeout through
CompletionQueue dispatch. A pending transport request without a TimerService
dependency or without an explicit timeout fails deterministically with
BackendUnavailable and a diagnostic instead of silently falling back to a
legacy public timer.

## Datagram Endpoint Ownership

DatagramReceiveRequest and DatagramSendRequest use typed endpoint values. A
datagram result owns the endpoint metadata needed by upper layers. There is no
native handle exposure through datagram request or result types. Datagram bind
completion returns the bound local endpoint, including the OS-assigned port when
binding port 0. Datagram receive completion returns the remote endpoint that
sent the packet.

Listener results also use typed endpoint metadata. Listen completion returns the
bound local endpoint, including the deterministic OS-assigned port when the
request binds port 0. Accept completion returns typed local and remote endpoint
metadata for the accepted stream. These endpoint values are data, not native
socket handles.

## Address Family And Dual Stack

IPv4 and IPv6 are endpoint and policy data, not backend identities. Select,
Epoll, Kqueue, IOCP, and Web platform remain event-model backends. Resolver,
listen, connect, and datagram operations carry address-family intent through
`AddressFamilyPolicy`, concrete endpoint values, or `DualStackPolicy`.

`AddressFamilyPolicy` includes `IPv4Only`, `IPv6Only`, `AnySystemOrder`,
`AnyIPv6First`, and `AnyIPv4First`. Resolver ordering and family filtering must
follow the selected policy and report deterministic diagnostics for unsupported
or unavailable families.

`DualStackPolicy` includes `IPv4Only`, `IPv6Only`,
`SeparateIPv4IPv6Sockets`, and `IPv6DualStackSocket`. Backend code must not
depend on native `IPV6_V6ONLY` defaults. A requested dual-stack shape that the
backend cannot implement must fail deterministically or report an explicit
capability downgrade.

Endpoint text formatting is stable. IPv4 uses `address:port`; IPv6 uses
`[address]:port`. Protocol code must not parse ambiguous IPv6 text such as
`::1:8080`.

## TLS And DNS Scope

TLS is an adapter above NetworkService. TLS, HTTPS, and WSS code may consume
NetworkService stream read/write/connect results, but TLS is not backend magic
and must not be hidden inside NetworkBackend capability.

DNS is out of scope for backend execution in this phase. Resolver submission is
typed and completes numeric address results or unsupported/no-DNS named-host
results through CompletionQueue with deterministic diagnostics. Future resolver
work must use explicit WorkService or native resolver backend ownership,
capability reporting, cancellation, timeout, and diagnostics.

## Protocol Stack Integration

Status: Prototype Complete.

`network::http::NetworkServiceHttpFacade` provides a narrow preflight adapter
for HTTP client requests. Cleartext HTTP requests submit a `ValidateOnly`
`NetworkIoRequest` to `NetworkService` and complete through the shared
`CompletionQueue`. TLS schemes remain an adapter concern above socket IO; this
prototype returns deterministic `tls_unavailable` diagnostics and does not fall
back to plaintext. Unsupported schemes complete with deterministic diagnostics
without touching backend socket state.

`network::http::Client` and `network::websocket::Client` also expose
`NetworkService&` constructors for protocol-stack migration. In that mode,
selected connect/send entry points submit typed `NetworkResolveRequest`
host/service intent first, then connect through `NetworkStream::async_connect`
and complete only through the shared `CompletionQueue`. Protocol code must not
construct `NetworkTransportRequest` for NetworkService-mode cleartext connects.

The current `LegacySocketPollerBackend` implements typed connect, read, write,
listen, accept, and close transport submission through a backend-private event
worker that reuses the legacy `io_service`, `io_operation`, `poller`, and
`socket_ops` boundary. Direct `validateTransport` does not execute transport
syscalls; real transport work is accepted through the backend submission hook
and completed through the shared `CompletionQueue`.

This is Prototype Complete / Backend Validated for the focused MinGW loopback
event backend stream and listener paths only. It is not native
IOCP/io_uring/kqueue completion, not Platform Validated, and not Production
Ready. Datagram bind/send/receive/close is also focused Backend Validated on
MinGW UDP loopback. The current `LegacySocketPollerBackend` capability reports
`PrototypeComplete`, `nativeAsyncCompletion = false`, and socket-oriented
capability only.

## Backend Capability Reports

Each NetworkBackendKind report includes supported request kinds, cancellation
support, timeout support, datagram support, whether completion is native async,
status level, and deterministic diagnostics for unsupported operations.

- NullNetworkBackend: Contract Complete validation backend. It validates request
  shape, reports unsupported real IO, and posts deterministic results through
  CompletionQueue.
- LegacySocketPollerBackend: future adapter around the current socket poller.
  It is socket-oriented, not native scalable completion IO, and must not claim
  ordinary file handle support. The typed stream connect/read/write/close event
  path and listener bind/listen/accept/close path are backend-private and
  complete through CompletionQueue. Datagram bind/send/receive/close uses typed
  endpoint metadata and also completes through CompletionQueue.
- WindowsIocpSocketBackend: future native Windows socket completion backend.
  It owns IOCP handles privately and reports unsupported/unavailable status
  deterministically when not configured.
- SelectSocketBackend: runtime default facade for the current Windows MinGW
  socket path. It reports select/poller-style socket readiness separately from
  epoll and kqueue, and lazily delegates real transport to the legacy poller
  only after typed transport submission.
- EpollSocketBackend: Linux socket readiness backend selected by
  `makeDefaultSocketBackend()` on Linux. It owns `EpollSocketPollerCore` for
  typed transport submission. Historical docs may call this LinuxEpollBackend;
  the contract enum name is EpollSocketBackend. Platform validation must run on
  Linux before claiming Backend Validated or Platform Validated.
- KqueueSocketBackend: Darwin socket readiness backend selected by
  `makeDefaultSocketBackend()` on Apple platforms. It owns
  `KqueueSocketPollerCore` for typed transport submission. Historical docs may
  call this DarwinKqueueBackend; the contract enum name is KqueueSocketBackend.
  Platform validation must run on Darwin before claiming Backend Validated or
  Platform Validated.
- WebPlatformBackend: future browser/platform network facade. It reports web
  security, protocol, and lifecycle limits explicitly.

Backend differences are visible only through capability reports, status labels,
diagnostics, or backend-private implementation. Upper layers observe typed
NetworkIoResult data and no native handle exposure.

## Smoke Path

NullNetworkBackend request validation smoke:

1. Initialize a CompletionQueue.
2. Create a NullNetworkBackend capability report.
3. Submit one invalid ConnectRequest and one unsupported ReadRequest.
4. Verify both accepted validation paths complete through CompletionQueue.
5. Verify NetworkIoStatus is deterministic and no native handle is exposed.
6. Shutdown and reject late submissions with service closed diagnostics.

This phase documents the smoke path only. Executable NullNetworkBackend smoke
starts when NetworkService and NullNetworkBackend are implemented.

TimerService timeout smoke:

1. Initialize CompletionQueue and TimerService.
2. Create NetworkService with an explicit TimerService dependency.
3. Submit pending Connect, Read, and Write transport requests with explicit
   timeouts.
4. Verify callbacks are not invoked by submit or TimerService::pollDue.
5. Dispatch CompletionQueue and verify NetworkIoStatus::Timeout exactly once
   for each request.

Current evidence: direct MinGW focused executable
`arp_network_service_transport_contract_phase25_check.exe` and no-window smoke
`arp_network_service_timeout_smoke_phase25_check.exe` pass locally. This is
Contract Complete timeout scheduler evidence, not native backend or platform
validation.
