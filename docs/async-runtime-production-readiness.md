# Async Runtime Production Readiness Gate

Status: Contract Complete. Production Ready: blocked.

This document is a gate, not a release claim. The async runtime, NetworkService,
FileService, fallback backends, and selected
resource/protocol integrations are not Production Ready until backend,
platform, lifecycle, telemetry, migration, and smoke evidence are complete.

## Status Boundaries

- Contract Complete: L2 semantics, diagnostics, and tests exist.
- Prototype Complete: first implementation or adapter exists with focused
  smoke evidence.
- Backend Validated: native backend behavior is proven against the L2 contract.
- Platform Validated: backend behavior is proven on the target OS/device
  lifecycle.
- Production Ready: backend, platform, migration, telemetry, lifetime,
  shutdown, cancellation, and final smoke evidence are all present.

Backend Validated is not Platform Validated. Platform Validated is not
Production Ready.

ThreadPoolFallback is not native async file IO.
SelectSocketBackend is not native scalable socket completion IO.
LegacySocketPollerBackend is a compatibility adapter only and is not a
production evidence target.

## Current Gate Result

Production Ready: blocked.

Current blockers:

- native Windows IOCP socket backend is unavailable;
- native Windows overlapped file backend is unavailable;
- Linux io_uring file backend is not implemented;
- Linux native socket backend is not implemented;
- Darwin native backend is not implemented;
- Web/browser backend is not validated;
- mobile Android/iOS lifecycle behavior is not validated;
- typed NetworkService listener/datagram and cleartext HTTP/WebSocket paths
  have focused backend evidence only; native backend and platform validation
  remain under Pending Confirmation;
- encrypted HTTPS/WSS over TLS-on-NetworkStream is Contract Complete at the
  disabled-path boundary in the non-TLS direct build, not Platform Validated;
- broad asset/resource loader migration is not complete;
- external platform/backend blockers remain under Pending Confirmation.

No async runtime component is Production Ready.

## Migration Evidence Refresh

The current migration evidence is intentionally split by status label:

Gate summary: TimerService Network Timeout Integration: Contract Complete;
SelectSocketBackend Event Pump: Prototype Complete / Backend Validated;
NetworkService Listener Contract And Adapter: Prototype Complete / Backend Validated;
NetworkService Datagram Contract And Adapter: Prototype Complete / Backend Validated;
HTTP NetworkService Hardening: Prototype Complete / Backend Validated;
WebSocket NetworkService Hardening: Prototype Complete / Backend Validated;
TLS On NetworkStream: Contract Complete;
Legacy Public Surface Retirement: Contract Complete;
native backend and platform validation remain under Pending Confirmation.

- TimerService Network Timeout Integration: Contract Complete. NetworkService
  timeout scheduling goes through TimerService and CompletionQueue contracts,
  with no claim of native backend or platform validation.
- SelectSocketBackend Event Pump: Prototype Complete / Backend Validated.
  The backend-private event pump reuses socket readiness machinery through
  SelectSocketPollerCore and returns typed NetworkService completions, but it
  is not native IOCP, io_uring, kqueue, or browser socket completion.
- NetworkService Listener Contract And Adapter: Prototype Complete / Backend
  Validated. Typed TCP listener bind/listen/accept/close has focused MinGW
  loopback evidence on SelectSocketBackend only.
- NetworkService Datagram Contract And Adapter: Prototype Complete / Backend
  Validated. Typed UDP bind/send/receive/close has focused MinGW loopback
  evidence on SelectSocketBackend only.
- HTTP NetworkService Hardening: Prototype Complete / Backend Validated.
  Cleartext HTTP request and streaming paths map queue-full, timeout,
  cancellation, peer-close, malformed URI, unsupported scheme, and no-DNS
  resolver-boundary behavior to deterministic diagnostics.
- WebSocket NetworkService Hardening: Prototype Complete / Backend Validated.
  Cleartext ws:// connect, frame exchange, close, and hardening diagnostics run
  through NetworkService and NetworkStream with focused evidence.
- TLS On NetworkStream: Contract Complete. TLS is an adapter above stream IO;
  the non-TLS direct build validates deterministic `tls_unavailable` boundary
  behavior and does not claim encrypted HTTPS/WSS loopback.
- Legacy Public Surface Retirement: Contract Complete. New HTTP/WebSocket/TLS
  migration paths are guarded against public `io_context`, `io_service`,
  `socket_t`, `native_handle`, `deadline_timer`, and public poller exposure;
  retained legacy constructors are staged migration only.

These items refresh the readiness gate; they do not change the result. Native
backend and platform validation remain under Pending Confirmation unless they
are explicitly scheduled.

## Runtime Behavior Requirements

Startup:

- Runtime configuration must declare bounded completion queue capacity, work
  queue capacity, worker count, completion budget, enabled timer/work/file and
  network validation surfaces.
- Invalid configuration fails with deterministic startup diagnostics.

Shutdown:

- Shutdown rejects new submissions.
- Pending work is drained or cancelled according to explicit shutdown policy.
- Final queued completions are dispatched or deterministically dropped according
  to policy.
- Late completions after cancellation must not call callbacks twice.

Request lifetime:

- Callbacks run through explicit runtime dispatch unless a future request
  declares another supported mode.
- Cancellation before scheduling, while queued, while running, and after
  completion must be deterministic.
- Queue overflow and backend failure must be observable as status/diagnostics.

## Hot Path Requirements

Production hot paths require evidence for:

- no blocking IO on selected hot paths;
- no unbounded queue;
- no native handle exposure;
- no per-frame shader compilation;
- no per-frame backend creation;
- no per-draw pipeline creation;
- no uncontrolled completion storm.

Current evidence is focused and prototype-level only. It is enough to keep the
contract honest, not enough to claim production readiness.

## Backend And Fallback Labels

Every backend must report explicit capability and status labels:

- NullFileBackend: Contract Complete, native async false.
- ThreadPoolFallbackFileBackend: Prototype Complete, native async false.
- LegacySocketPollerBackend: Prototype Complete, native async completion false.
- WindowsOverlappedFileBackend: future native backend contract name only; no
  production code placeholder is present.
- WindowsIocpSocketBackend: future native backend contract name only; no
  production code placeholder is present.

Fallback labels must stay visible to callers and telemetry. Fallback selection
must be explicit; no guessed fallback, silent compatibility path, or native
default may produce production results.

## Platform Lifecycle Requirements

Android and iOS validation must cover:

- pause and resume;
- app sandbox and permissions;
- surface/layer lifecycle impact on runtime shutdown;
- memory pressure and resource release;
- cancellation of queued and running work;
- callback dispatch after lifecycle transitions.

This validation is not available in the current Windows MinGW environment and
therefore remains under Pending Confirmation.

## Required Final Evidence

Before Production Ready can be claimed, focused evidence must include:

- unit and contract tests for runtime, file, network, backend capability,
  cancellation, budget, telemetry, resource integration, protocol facade
  integration, and protocol-stack NetworkService entry points;
- no-window smoke for async file and async network;
- backend-native smoke for each claimed native backend;
- platform lifecycle smoke for each claimed platform;
- telemetry snapshot proving bounded queues, dispatch budget, fallback labels,
  and dropped/late completion counters;
- documentation listing which components are Contract Complete, Prototype
  Complete, Backend Validated, Platform Validated, and Production Ready.

Until all items are present, Production Ready remains blocked.
