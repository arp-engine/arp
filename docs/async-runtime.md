# Async Runtime Contract

Status: Contract Complete.

This document defines the L2 engine contract for the shared async runtime. It
does not claim Backend Validated, Platform Validated, or Production Ready
status. Native file and socket backends remain future work until backend and
platform evidence exists.

## Goal

AsyncRuntime owns one completion dispatch surface shared by generic work and
file IO. Network keeps its own event system, and the runtime timer facade has
been removed from the active surface:

```text
WorkService completes    -> CompletionQueue
FileService completes    -> CompletionQueue
```

FileService keeps a typed domain request/result contract. Callers must not wrap
file reads as arbitrary work tasks.

## Ownership Model

AsyncRuntime owns the services that share dispatch state:

- CompletionQueue owns queued completion records until dispatch consumes them.
- WorkService owns bounded generic work execution and posts typed work
  completions.
- FileService owns file requests and delegates blocking or native IO to
  FileBackend implementations.

Completion callbacks are caller-provided callable objects with explicit
lifetime. A submitted operation either takes ownership of its callback until
exactly one terminal completion is dispatched, or rejects submission with a
deterministic diagnostic before taking ownership.

Production runtime implementation must store dynamic completion records,
strings, and queues in allocator-aware containers using `core::pmr` and
`core/cpu_allocator`. Owned runtime objects must use `core::makeUnique`,
`core::makeShared`, or an equivalent core allocation helper.

## CompletionQueue Contract

CompletionQueue is the only public dispatch surface for AsyncRuntime-owned
asynchronous domains.

- Completions are queued in FIFO order within the same priority and service
  class unless a later phase explicitly defines priority ordering.
- `dispatch(maxCallbacks)` applies a dispatch budget and returns the number of
  callbacks run.
- A budget of zero is invalid and must fail deterministically.
- Callback execution happens on the caller thread that invokes explicit
  AsyncRuntime dispatch.
- Callback exceptions, panics, or fatal diagnostics must not leave a completion
  record available for a second callback invocation.
- Once shutdown begins, new completion posts are rejected or converted to a
  documented shutdown diagnostic according to the shutdown policy.
- Queue overflow is deterministic. The submitting service must report the
  overflow as a rejected operation or a typed failed result; it must not silently
  drop production completions.

Completion records must not contain backend-native socket handles, file handles,
platform descriptors, renderer objects, or resource-loader private state.

## AsyncRuntime Lifecycle

AsyncRuntime has explicit lifecycle states:

- `Uninitialized`: no service accepts work.
- `Initialized`: services may validate and submit requests.
- `Running`: background work and backend polling may make progress.
- `Draining`: new submissions are rejected; accepted work may complete according
  to the selected drain policy.
- `Shutdown`: services are closed; dispatch may only run already accepted final
  completions when the selected policy allows it.
- `Failed`: fail-fast mode records a deterministic diagnostic and rejects new
  work.

Required lifecycle operations:

- `initialize(desc)`: validates queue capacity, worker count, enabled domains,
  backend kinds, and dispatch policy.
- `run` or `start`: starts services that need background execution.
- `pump`: advances services that require polling without running callbacks.
- `dispatch`: runs queued callbacks through the CompletionQueue budget.
- `shutdown`: rejects new submissions, requests cancellation for pending work,
  and transitions to Draining or Shutdown.
- `drain`: dispatches accepted terminal completions until the selected drain
  policy is satisfied.

Shutdown policy must be explicit: drain accepted completions, cancel pending
operations and dispatch cancellation results, or discard pending completions
only when a diagnostic makes that behavior observable.

## Completion Thread Contract

By default, callbacks run only through explicit AsyncRuntime dispatch. A request
may opt into a different completion thread only when that thread mode is part of
the typed request contract and the service capability report says it is
supported.

Backend worker threads, socket polling threads, native IO completion threads,
and file fallback threads must not call user callbacks directly. They may only
post typed completion records into CompletionQueue or fail with a deterministic
diagnostic when posting is impossible.

## Diagnostics

Invalid runtime state and submission failure must produce deterministic
diagnostics. Required diagnostic categories:

- invalid runtime state
- invalid runtime descriptor
- invalid dispatch budget
- shutdown submission
- queue overflow
- cancelled request
- backend unavailable
- unsupported backend capability
- callback ownership violation
- completion posted after cancellation
- completion dropped during shutdown

Diagnostics must include a stable status label, service domain, operation kind,
and whether the request was accepted before failure. Production paths must not
use guessed fallback behavior, magic numbers, or silent compatibility handling.

## Domain Capability/Status Reporting

Domain capability/status reporting is required for every AsyncRuntime service.
Reports must distinguish the current validation level:

- Contract Complete
- Prototype Complete
- Backend Validated
- Platform Validated
- Production Ready

Work capability report:

- worker count
- bounded queue capacity
- cancellation support for pending work
- telemetry counters

File capability report:

- backend kind, such as NullFileBackend, ThreadPoolFallbackFileBackend,
  WindowsOverlappedFileBackend, LinuxIoUringFileBackend, or DarwinFileBackend
- supported path scope and read policies
- cancellation support
- whether completion is native async
- deterministic diagnostics for unavailable native support

ThreadPoolFallbackFileBackend must report native async as false. Legacy socket
poller backends must not claim scalable native completion IO.

## Hot Path Rules

Runtime, asset streaming, renderer, game, sample, and UI hot paths must prove:

- no blocking IO
- no unbounded allocation
- no per-frame backend creation
- no per-draw pipeline creation
- no per-frame shader compilation
- no hidden native handle exposure
- no uncontrolled completion storm beyond the dispatch budget

File IO, staging, readback, native file handles, and backend-specific
completion primitives stay backend-owned. Upper layers express intent through
typed request contracts and observe typed results, diagnostics, capability
reports, and telemetry only.

## Smoke Path

Smoke path: initialize, post, dispatch, shutdown.

The minimal no-window smoke path for this contract is:

1. Initialize AsyncRuntime with one CompletionQueue and bounded WorkService
   settings.
2. Post one generic work completion through WorkService.
3. Post one file validation/read completion through FileService.
4. Dispatch completions on the caller thread with a finite dispatch budget.
5. Shutdown, drain accepted terminal completions, and reject late submissions
   with shutdown submission diagnostics.

This phase documents the smoke path only. Executable runtime smoke validation
starts when the required services exist.
