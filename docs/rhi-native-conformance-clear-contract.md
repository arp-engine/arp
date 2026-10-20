# OriginalConformanceClearExecution/1

This separate cold Prototype execution domain joins the unchanged Clear/Resize
YAML and shared FrameGraph recorder to the original device's commands and actual
presented image. It does not grant production readiness or a generic readback API.

The issuer consumes an immutable parsed workload. Preparation reserves bounded
CPU output storage and backend-owned native staging for every authored frame,
before any submission. A caller supplies explicit native allocation and completion
deadline budgets. Failed preparation issues no executable domain. The domain is
single-threaded, single-use per frame and tied to the original device lifetime
and its already-armed original native-result-loss observation. Neither a logical
lifetime ticket nor an unarmed/synthetic runtime can grant native authority.

Only one Clear/Store color pass, no depth, no resolve, no subpass, no draw or other
GPU work is admitted. Original native command entry points enforce that language;
ignored record errors poison the domain. Transitions must name the same original
acquired image and cover its single color subresource. The clear, dimensions,
format and original acquisition/serial must match immutable workload intent.
Oracles never construct command arguments. Duplicate/foreign/missing/reordered
recordings fail before queue acceptance.

The backend inserts a color-write to transfer-read barrier, an exact image-to-owned
staging copy, and a transition to Present. These are equivalent observation
lowerings of the original final transition. It waits only on that original
submission's real fence, with the explicit cold deadline, invalidates mapped
memory, and converts only specified RGBA8/BGRA8 UNORM bytes to canonical RGBA8.
Every actual channel is checked against the fixture. No counter-derived hash,
caller byte import, borrowed readback, Dummy primitive or CPU replay is allowed.
Accepted-work facts survive subsequent timeout, read or oracle failure; failure
is terminal and retry is not automatic. Native staging remains command-owned
until real completion even when the domain is destroyed.

Certification requires original successful queue-present acceptance for the same
frame synchronization owner and serial, after pixel observation. This proves
acceptance, not display/presentation completion. Audit additionally requires a
current original device lifetime, live execution domain and unchanged CPU quota.
After finalization, history retains pixels and identity, not swapchain images or
frame synchronization owners. The original command independently retains native
resources through real completion. Resize preserves this history without reopening a lease. Audit is a
read-only consumer and never authorizes execution, resources or readiness.

| Backend | Version 1 lowering |
|---|---|
| Null | DiagnosticFailure; validator smoke remains available separately |
| Vulkan | Explicit Prototype opt-in; original ordinary command payload, owned staging, real fence, exact original present owner |
| Metal | DiagnosticFailure; no shared original output issuer |
| WebGPU | DiagnosticFailure in this entry; existing private issuer is not a shared-recorder join |
| WebGL | DiagnosticFailure in this entry; use existing OriginalConformanceOutput/1 ordered issuer |
| Unknown | DiagnosticFailure |

Sampled/PingPong and storage/MRT are excluded from this version, remain P4-14 work,
and cannot be certified through Clear/1. Tests cover all lowering decisions,
budgets, unforgeable run construction, actual native success and native negative
commands, plus Null hidden-frame. RenderDoc replay and AI inspection independently
validate the native output; captures cannot mint an L2 run.
