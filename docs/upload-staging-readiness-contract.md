# UploadStagingReadiness/1

Status: Contract Complete after the required common and four-backend tests pass.
Native backend maturity is reported separately; a Metal result does not promote
Vulkan or WebGPU native status.

## L2 semantics

An upload allocation can fail because the request is invalid or unsupported, or
because the bounded backend-owned staging capacity is still retained by accepted
GPU work. These outcomes are not interchangeable:

- `DiagnosticFailure` is terminal for the unchanged request.
- `RetryAfterCompletion` means the unchanged request may be retried only after a
  backend completion boundary advances. It is not success and does not permit a
  busy wait, capacity increase, dummy allocation, CPU replay, or silent fallback.

The stable retryable diagnostic IDs are
`ARP-RHI-STAGING-PAGE-COUNT-EXCEEDED` and
`ARP-RHI-STAGING-IN-FLIGHT-CAPACITY`. Request-too-large, zero-size, invalid
alignment, missing budget, page creation, native record, and flush failures remain
`DiagnosticFailure`. Callers consume the typed command status; they must not parse
diagnostic text or inspect a backend name.

Queue acceptance transfers provisional staging ownership to backend completion.
The backend must retain a monotonically increasing submission serial and retire
accepted slices only after the native completion for that serial. Recording
abandonment still performs exact cancellation under `RecordedStagingUpload/1`.

## Lowering

| Backend | Lowering |
| --- | --- |
| Null | Equivalent validator; no physical staging pressure is claimed. |
| Vulkan | Native completion serial drives pool retirement; bounded in-flight exhaustion returns `Deferred`. |
| Metal | A backend submission serial is attached to each encoder; the native command-buffer completion advances the completed serial; allocation reads it before deciding readiness. |
| WebGPU persistent | Existing queue completion serial drives retirement; bounded live-ring exhaustion returns `Deferred`. |
| WebGPU queue-write | The immediate reservation is released after the queue API call; any overlapping bounded reservation failure is classified explicitly and never retained as a persistent slice. |

No L2 result exposes page indices, mapped pointers, native handles, or backend
enums. Runtime storage remains backend-owned and uses core allocation facilities.

## Required tests

Fast tests cover both retryable IDs and every terminal allocator diagnostic.
Backend pool tests cover exact-budget success, in-flight exhaustion, completion
retirement, retry success, and terminal oversized requests. Metal native tests
must prove that accepted uploads retire from a real completion callback without
`waitIdle` or a later logical frame serial.
