# Network Typed Transport Governance

Status: Contract Complete. Production Ready: not claimed.

This document adds the NetworkTransportRequest cleanup to the network
governance plan. The goal is to recover the old network library's type-safety
benefit from template/generic operations without leaking templates through every
backend boundary.

## Problem

`NetworkTransportRequest` is a transitional tagged aggregate. It stores every
transport operation payload in one object and relies on `kind` to decide which
field is meaningful.

That shape creates these risks:

- `kind` can disagree with the populated payload.
- backend code repeats field-selection switches.
- every new operation grows the same aggregate and every switch site.
- buffer lifetime, timeout, cancellation, and handle rules are spread across
  call sites.
- the compiler cannot reject invalid request shapes.

The previous network layer avoided many of these mistakes by representing
operations as concrete typed objects. The new runtime/backend architecture
should keep that type-safety while preserving a stable NetworkService and
backend interface. It does not preserve the old public socket-template facade
as the target API.

`tcp::socket<ipv4/ipv6>` and `udp::socket<ipv4/ipv6>` are legacy facades. Their
template parameter looks like address-family type safety, but the current
operations still accept runtime `endpoint` values and the NetworkService-backed
async path can create a backend socket from the endpoint family instead of the
socket template parameter. Address family belongs to endpoint, resolver, listen, and datagram policy, not to public socket template parameters.

## Target Model

NetworkService should accept typed operation objects:

- `NetworkConnectOp`
- `NetworkListenOp`
- `NetworkAcceptOp`
- `NetworkReadOp`
- `NetworkWriteOp`
- `NetworkDatagramBindOp`
- `NetworkDatagramReceiveOp`
- `NetworkDatagramSendOp`
- `NetworkCloseOp`
- `NetworkCancelOp`

Each operation type owns only the fields that are legal for that operation.
Connect and datagram-send operations carry concrete endpoint values. Resolver,
listen, and datagram-bind operations carry explicit address-family and
dual-stack policies when they need to choose or create endpoints.
Shared rules are exposed through traits:

- `NetworkOperationTraits<Op>::kind`
- `NetworkOperationTraits<Op>::validate(op)`
- `NetworkOperationTraits<Op>::timeoutMilliseconds(op)`
- `NetworkOperationTraits<Op>::stream/listener/datagram(op)`
- `NetworkOperationTraits<Op>::cancellation(op)`

NetworkService may provide a templated front door for type-safety:

```cpp
template <typename Op>
NetworkIoStatus submit(Op request, NetworkTransportCompletion completion) noexcept;
```

The backend boundary should remain explicit and stable. Backend implementations
should receive concrete typed methods rather than one large request aggregate:

```cpp
virtual NetworkTransportSubmitResult submitConnect(
    const NetworkConnectOp& request,
    core::runtime::CompletionQueue& completions,
    NetworkTransportCompletion completion) noexcept;

virtual NetworkTransportSubmitResult submitRead(
    const NetworkReadOp& request,
    core::runtime::CompletionQueue& completions,
    NetworkTransportCompletion completion) noexcept;
```

This keeps templates at the facade/service layer, where they improve request
construction, and keeps backend ownership explicit.

## Migration Plan

Phase A: Define typed operation structs.

- Add operation structs beside the NetworkService contract.
- Keep fields allocator-neutral and non-owning unless ownership is explicit.
- Introduce explicit byte-view structs if the core module does not already have
  reusable byte span types.
- Add contract tests for construction and validation of each operation type.

Phase B: Add NetworkOperationTraits.

- Provide kind, timeout, cancellation, and handle accessors.
- Centralize per-operation validation.
- Add tests that invalid combinations cannot be expressed or fail validation
  deterministically.

Phase C: Add typed NetworkService submit path.

- Add `NetworkService::submit(Op, NetworkTransportCompletion)`.
- Keep the existing `submitTransport(NetworkTransportRequest, ...)` as a
  compatibility adapter only.
- Ensure timeout/cancellation/completion telemetry remains identical between
  typed and adapter paths.

Phase D: Add typed backend methods.

- Add explicit backend virtual methods for each operation.
- Route default implementations through deterministic UnsupportedOperation.
- Move Select/Epoll/Kqueue cores from request switches to typed method calls.

Current adapter status: `NetworkService::submit(Op, completion)` is the typed
front door. `NetworkBackend` exposes typed validate/submit virtual methods for
connect, listen, accept, read, write, datagram bind/receive/send, close, and
cancel. The default typed backend methods bridge through the existing
`NetworkTransportRequest` adapter while concrete backend cores are migrated.
This is a compatibility bridge, not the final Production Ready backend shape.

Phase E: Move callers.

- Move `NetworkStream`, future `NetworkListener`, future `NetworkDatagram`,
  HTTP, WebSocket, and TLS paths to typed operations.
- Quarantine `tcp::socket` and `udp::socket` behind legacy tests or
  backend-private compatibility paths instead of treating them as the public
  facade.
- Keep compatibility tests for the adapter until no production path uses it.

Phase F: Retire NetworkTransportRequest.

- Remove direct production construction of `NetworkTransportRequest`.
- Move any remaining adapter use under legacy tests or delete it.
- Update boundary tests so new production code cannot include or instantiate
  the aggregate request carrier.
- Delete or quarantine public legacy socket facades after protocol, sample, and
  mainline tests no longer depend on them.

## Status Rules

- Contract Complete means typed operation shapes, traits, validation, and
  adapter semantics are documented and tested.
- Prototype Complete means at least one backend and one facade path submit typed
  operations end to end.
- Backend Validated means Select, Epoll, and Kqueue typed methods pass focused
  transport tests on their backend paths.
- Platform Validated means those backend tests pass on their target platforms.
- Production Ready cannot be claimed while production callers still build raw
  `NetworkTransportRequest` values.
