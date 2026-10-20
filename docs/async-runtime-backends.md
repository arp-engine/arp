# Async Runtime Backend Design Contracts

Status: Contract Complete.

Prototype Complete is not Backend Validated. Backend Validated is not Platform
Validated. Platform Validated is not Production Ready.

This document defines backend lowering contracts for AsyncRuntime,
NetworkService, and FileService before native backend implementation. It keeps
ThreadPoolFallback and LegacySocketPoller status honest and describes
deterministic failure behavior for unavailable native paths.

## Shared L2 Contract

L2 exposes domain requests and results only:

- `NetworkIoRequest` / `NetworkIoResult` for socket-domain operations.
- `FileReadRequest` / `FileReadResult` for host file reads.
- `WorkItem` / `WorkResult` for bounded generic background work.
- `CompletionQueue` for explicit callback dispatch.

Backends own native handles, file descriptors, completion ports, queues,
threads, and platform objects privately. Renderer, resource, asset, UI, sample,
game, and protocol facade layers must observe only typed results, capability
reports, status labels, diagnostics, and telemetry.

Unsupported or unavailable backend paths must fail deterministically with
capability/status diagnostics. They must not silently fall back to a different
native behavior, a guessed compatibility path, blocking IO, or backend defaults.

## Windows Lowering

Windows socket IOCP:

- L2 connect, accept, read, write, datagram, close, and cancel map to
  backend-owned overlapped socket operations associated with an IOCP.
- Completion keys, `SOCKET`, `OVERLAPPED`, and Winsock-specific error codes stay
  backend-private.
- Cancellation maps to explicit backend request ids and deterministic cancelled
  results. Late IOCP completions after cancellation are discarded or recorded as
  telemetry, never delivered twice.
- If socket IOCP is not configured or MinGW support is incomplete, capability
  reports unavailable status and requests fail deterministically.

Windows overlapped file IO:

- L2 file read maps to backend-owned file handles opened for overlapped IO.
- Host filesystem path scope, offset, size, partial-read policy, cancellation,
  and priority remain L2 request fields; backend defaults must come from the
  lowering result, not Win32 implicit defaults.
- `HANDLE`, `OVERLAPPED`, completion events, and native allocation state are
  backend-private.
- If overlapped file IO cannot be supported, FileService must report an
  unavailable Windows backend and may select ThreadPoolFallback only when the
  caller configured that fallback explicitly.

## Linux Lowering

Linux epoll socket backend:

- L2 socket readiness maps to backend-owned nonblocking sockets and epoll file
  descriptors.
- epoll readiness is not ordinary file IO support. Capability reports must keep
  socket readiness and file IO separate.
- Retryable would-block, peer close, cancellation, and timeout statuses must be
  mapped deterministically from native readiness and socket errors.

Linux io_uring file backend:

- L2 file reads map to backend-owned io_uring submission/completion queues.
- Request id, cancellation, offset, size, partial-read policy, and priority are
  encoded in backend-owned user data.
- Socket and file completions may share io_uring in a future design only after a
  separate capability contract proves equivalent socket semantics. Until then,
  Linux epoll socket and Linux io_uring file backends are separate capabilities.

## Darwin Lowering

Darwin kqueue network backend:

- L2 socket readiness maps to backend-owned kqueue registrations and
  nonblocking sockets.
- kqueue identifiers, file descriptors, and platform event structs stay private.
- Cancellation removes backend registrations or marks request ids cancelled, and
  late readiness is converted to telemetry or deterministic cancelled results.

Darwin file backend:

- Native async file support is not claimed. A dispatch/thread file fallback may
  be implemented as an explicit fallback backend with Prototype Complete status.
- Thread fallback is not native async IO and must not be reported as Backend
  Validated.

## Web/Browser Lowering

Web/browser network and file behavior is platform mediated:

- Browser networking maps to fetch/WebSocket-style platform APIs and cannot
  expose raw sockets.
- Local files require browser file handles or package/mount abstractions and
  cannot be treated as host filesystem paths silently.
- Capability reports must identify unsupported sockets, restricted filesystem
  access, cancellation limitations, page lifecycle suspension, and security
  diagnostics.

## Mobile Lifecycle

Android:

- Pause/resume may cancel or defer socket, file, timer, and work operations.
- App sandbox path scope and storage permission are explicit file diagnostics.
- Memory pressure must drive bounded queues, cancellation, and resource release.

iOS:

- App backgrounding may suspend network and file callbacks.
- Surface/layer lifecycle is renderer-owned, but async runtime shutdown must not
  block on platform UI lifecycle callbacks.
- Sandbox file access, memory pressure, and request cancellation are explicit
  diagnostics or capability limits.

## Current Fallback Capabilities

NullFileBackend:

- Status: Contract Complete.
- Native async: false.
- Purpose: validate FileService request shape and complete deterministic
  unavailable results through CompletionQueue.

ThreadPoolFallbackFileBackend:

- Status: Prototype Complete.
- Native async: false.
- Purpose: run blocking file reads through WorkService as an implementation
  detail and complete typed FileReadResult values through CompletionQueue.

LegacySocketPollerBackend:

- Status: Prototype Complete.
- Native async completion: false.
- Purpose: report the current socket-oriented poller boundary and deterministic
  unsupported typed operations while completing validation through
  CompletionQueue.

## Deterministic Failure

Every backend selection must produce one of these outcomes:

- selected backend capability supports the requested L2 operation;
- selected backend capability explicitly rejects the operation with diagnostics;
- selected backend is unavailable and the configured fallback is used with its
  own status label;
- selected backend is unavailable and the request fails deterministically.

Backend code must not rely on native defaults for observable state such as
blocking mode, descriptor ownership, cancellation, priority, queue size, memory
policy, callback thread, or lifecycle behavior. Defaults must come from L2
request semantics or lowering rules.

## Smoke Path

Null/ThreadPoolFallback capability smoke:

1. Create a CompletionQueue and WorkService.
2. Query NullFileBackend and ThreadPoolFallbackFileBackend capabilities.
3. Query LegacySocketPollerBackend capability.
4. Verify status labels remain Contract Complete or Prototype Complete only.
5. Verify native async/backend/platform/production readiness is not claimed.
