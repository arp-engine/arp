# OriginalConformanceStorageMrtExecution/1

Status: Contract Complete / Prototype Complete / bounded Vulkan Backend Validated. MinGW Debug/Release scoped tests each pass 22/22, including the fast workload test, Null hidden-frame and actual native positive/30-negative smoke. Final Release RenderDoc independently verifies all nine original outputs; see `build/webgl1-evidence/p4/p4g-vulkan-storage-original-3/`. Platform Validated and Production Ready remain unclaimed. WebGL and existing WebGPU private native output browser regressions each pass 3/3. Source/build/capture custody and frozen binaries are recorded by `build/webgl1-evidence/p4/p4g-storage-original-checkpoint.yaml`; this does not authenticate the still-open WebGPU shared recorder join.

This explicit cold Prototype consumes the immutable `ConformanceStorageMrtWorkload/1`. The L2 dependency is intent and original input/artifacts -> native dispatch/barrier/draw -> owned actual output -> audit. Readiness never authorizes execution. Expected values are post-execution oracles and never construct uploads, shaders, commands or observed pixels.

## L2 invariants and versioned lowering

One original device creates and uploads one 48-byte readonly storage input per authored frame, records initialized coverage only after successful native mapped write/flush, and seals all writes. Original graphics and compute artifacts supply immutable SPIR-V bytes directly; complete reflection, entries, layouts, workgroup size and L2 pipeline state must match. Both pipelines consume the actual device-owned capability lowering contract before native creation. Caller paths or same-byte ordinary resources grant no authority.

The only accepted language is one compute pass, one exact (width,height,1) dispatch with workgroup size (1,1,1), an actual compute-write -> fragment-read dependency on that same RGBA16F image, then one Draw(3,1,0,0) into the acquired output and a distinct RGBA8 MRT attachment. The fragment descriptors must use the same original storage input and same compute-written image. No indexed/indirect/extra work, copies, push updates, aliasing, caller output bytes or direct queue bypass are accepted. Viewport, scissor, full attachments, formats, subresources, descriptor intervals and exact native pipeline owners are checked before submission.

Three output roles belong to one frame: Canvas (canonical RGBA8), Color1 (RGBA8) and Storage (little-endian IEEE binary16 RGBA). Each has owned staging created before execution against explicit cumulative native readback and CPU output budgets. The same command copies all three original images after the final draw and restores their declared state before the original acquired image transitions to Present. The real submitted-command fence and absolute cold deadline govern mapped invalidation/readback. Accepted-work failure cannot claim rollback or retry. All three byte streams must match their independent authored oracles, then the exact acquired image/serial must receive native present acceptance. Only that complete join advances the sealed run. Prior Clear/Sampled `pixels(frame)` remains Canvas; typed role access and audit include all three outputs for this workload.

The issuer owns bounded inputs, two pipelines, per-frame immutable intent and three staging buffers. Original device, loss observation, issuer epoch, CPU quota and both artifact quotas stay live through record/submit/audit. Retained encoders cannot record after issuer destruction. Finalized history retains output bytes and artifact identity, releases surface/native image ownership, and survives resize without authorizing another use. This cold path explicitly permits bounded synchronous readback; it grants neither NativeSync nor production hot-path capability.

| Backend | Decision for this original shared-output entry |
|---|---|
| Null | DiagnosticFailure, validator/hidden-frame only |
| Vulkan | Explicit cold Prototype, actual queried capabilities and original loss/lifetime gates; three native copies and real completion/present |
| Metal | DiagnosticFailure until a shared original issuer is implemented and target execution is verified |
| WebGPU | DiagnosticFailure; existing private native issuer does not authenticate this shared recorder |
| WebGL1 | DiagnosticFailure: compute/storage/MRT workload unsupported by this profile; no extension opt-in inferred |
| Unknown | Deterministic diagnostic failure |

## Required verification

Keep fast workload/output/lowering tests and the existing Null hidden-frame. Native positive smoke must destroy the shared recorder before final execution, verify all nine output streams across three frames/two extents, and audit original device/artifact/quota retirement. Native negatives cover substituted input/artifacts/pipelines/views, dispatch/draw/count/order, missing/wrong dependency scopes, duplicate/extra work, output budget/ownership, direct submit, retained encoder, oracle mismatch after accepted work and deadline failure. RenderDoc independently checks original graphics/compute shader bytes, actual input bytes, dispatch -> sample/MRT closure, three exact image-to-owned-buffer copies, actual mapped-buffer bytes, draw/copy/present order and all nine output hashes. Unsupported paths remain explicit failures. Existing P4-14 obligations are not moved to P5.
