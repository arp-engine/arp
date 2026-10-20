# OriginalConformanceOutput/1

P4-14 execution admission precedes output observation; readiness is a consumer.
This is an explicitly opted-in **cold Prototype** observation path, never a
per-frame production readback/completion capability.

## L2 invariants

The original ordered issuer consumes exclusive ownership of an immutable parsed
Clear, Sampled/PingPong or Storage/MRT workload. A caller-supplied report, expected hash,
factory, native handle or RenderDoc capture cannot create a run. Preparation
reserves all frame/output storage against a separate, bounded CPU quota. No
allocation or file IO occurs in observation. Synchronous native pixel reads are
allowed only in this opt-in cold path; they confer no NativeSync semantics.

Before native submission the backend must compare the original sealed command
payload with the next workload frame: pass order, exact attachment identities,
Clear/Store, dimensions, no depth attachment, draw/index ranges, original shader
artifact source identity, vertex/index contents, UBO/push snapshots, full viewport
and scissor, and sampled-resource producer/consumer identity. Output oracles are
never used to construct commands. Native command failure never yields evidence.

After the original command succeeds the backend consumes the actual observation
defined by its lowering contract. WebGL reads the exact original Submitted
canvas in its current context, explicitly binding its framebuffer and
PACK_ALIGNMENT=1. Vulkan copies the exact original images into issuer-owned
staging in the same command, waits its genuine native fence, invalidates mapped
reads and stores actual bytes. Every channel is compared with the immutable
fixture. Only successful original present/retirement of that same Canvas
image/serial finalizes a frame.
Duplicate, missing, reordered, foreign or stale payloads fail closed. Any failure
after accepted work preserves accepted-work reporting and closes certification;
there is no replay, rollback or automatic retry. Budget failure precedes execution.

The sealed run is noncopyable and cannot be constructed by consumers. Its audit
requires all frames observed and presented, plus current device/context/domain
epochs. Surface resize may retain historical observations; it cannot authorize
another read or execute. Device/context retirement invalidates the audit. The
borrowed run is valid only while its original issuer lives. Pixels are read-only;
there is no caller byte import. Audit reports are summaries, never executable
authorities. Renderer/platform invoke L2; RHI never imports those layers.

## Versioned backend lowering

| Backend | Original shared-run output source | Result |
|---|---|---|
| Null | Validator only, no native output | DiagnosticFailure |
| Vulkan | Original Clear, Sampled/PingPong and Storage/MRT entries seal authored immutable inputs, original artifact SPIR-V, native commands, owned staging and exact original present; generic ordered entry remains closed | Four existing shared scenarios have explicit cold Prototype entries; no general production authority |
| Metal | No original shared recorder/output join on this host | DiagnosticFailure |
| WebGPU | Private Prototype output issuer is separate from shared recorder | DiagnosticFailure for this shared-run entry |
| WebGL | Original sealed ordered commands + original canvas GL_RGBA/GL_UNSIGNED_BYTE observation | Prototype, runtime/current owner gates required |
| Unknown | No contract | DiagnosticFailure |

Storage/MRT adds Color1 RGBA8 and Storage little-endian RGBA16F to the Canvas RGBA8 output. Each frame has three distinct owned staging buffers and a real command completion fence. Audit includes all three byte streams and the same acquired Canvas image/serial native present. See `rhi-native-conformance-storage-mrt-contract.md` for the compute-write to fragment-read and multi-output ownership contract.

These are explicit backend contracts for this optional observation entry, not
inference of device capabilities from backend names. Storage/MRT and aliasing
retain their existing capability/execution gates; this version cannot certify
unsupported workloads. No general backend/platform/production readiness is granted.

## Validation

Quick contracts cover opt-in/version/budgets, five-backend decisions, private
construction and incomplete-run rejection. Real browser tests run unchanged
Clear, Sampled and PingPong YAML through the shared recorder; altered original
commands must fail before native submission. Null hidden-frame remains the
validator smoke. RenderDoc remains independent visual evidence and cannot mint
an L2 run.
