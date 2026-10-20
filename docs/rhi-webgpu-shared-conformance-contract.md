# WebGpuSharedConformance/1

Status: Clear/Resize, Sampled and PingPong Contract Complete / Prototype Complete / bounded Backend Validated on the tested Windows Chrome/Dawn configuration (3/4 shared scenes). Storage/MRT remains unimplemented; no mini-program Platform Validated or Production Ready claim.

P4-14 remains one package: shared Clear/Resize, Sampled, PingPong and Storage/MRT recorder intent -> original backend-owned command -> actual owned outputs -> exact original acquired canvas presentation -> sealed audit. This package does not alter production factories or NativeSync. The existing private general-output issuer remains authoritative for its original finite stream; its RGBA16F snapshot or caller reports cannot certify another stream.

## L2 and lowering

The explicit cold request reserves immutable workload, CPU output and native padded staging budgets before recording. Only the issuing original device/loss observation and issuer epoch authorize the run. WebGPU maps logical barriers to validated order within one original encoder; compute/storage publication requires matching source/destination stage/access and identity. Original selected WGSL/artifact bytes, complete state and reflection govern pipeline creation; no path-based authority or expected-pixel upload is permitted.

All acquired canvases receive backend-issued identity with image id, frame slot, serial and surface epoch. A conformance issuer claims that identity once. Its original command journals all executable work and denies ordinary direct queue submission. The same command copies the original images to distinct issuer-owned staging. Queue completion and all three native error scopes must succeed. Map uses the existing WebGpuNativeBufferMapReceipt, checking actual browser and C map state, exact binding and unmap. Bytes are published only after genuine delivery; padding is removed by declared layout. Canvas RGBA8 and optional Color1 RGBA8/Storage little-endian RGBA16F are audited independently. Present consumes that same original acquired identity only after observed bytes. Surface/device retirement and original quota/artifact mutation invalidate admission.

Cold callbacks retain their exact resource/command owners before native invocation. Unknown native invocation, timeout, map failure, scope error or device loss cannot release accepted GPU work early, fabricate completion, replay, rollback or retry. Callback userdata is preallocated and drained or genuinely suppressed using existing receipt contracts. No caller-accessible mutation, completion flag or native handle authorizes output.

`ConformanceSubmissionCompletion/1` distinguishes the existing external native fence from issuer-owned cold observation. Vulkan retains its external native fence. The WebGPU issuer supplies the latter: its exact queue/scope/map receipts and retained graph own completion, including accepted-work failure. The shared recorder must not request an unavailable general WebGPU fence or manufacture a signaled one. Submission observers requiring a public fence reject before submission for this mode. This cold-only choice does not open production NativeSync, generic commands, or general fences; Null, Metal, WebGL and unknown issuers retain their existing rejection/entry contracts.

`CanvasTexture/1` uses one descriptor for the actual configured surface and wrapped normalized state: RenderAttachment | CopySrc, Undefined acquisition state, one mip/layer/sample, explicit format/extent. CopyDst is not advertised. Repeated acquisition of the same actual JS GPUTexture is rejected using a bounded retained original native identity. ColorAttachment barriers accept write or read/write scope with a required write bit; Clear/Store remains exact and no preservation is inferred.

| Backend | This package's execution decision |
|---|---|
| Null | Validator only; no native output |
| Vulkan | Existing explicit cold issuers and owned native output preserved |
| Metal | Deterministic shared-entry diagnostic until implementation decision and target evidence |
| WebGPU | Original shared Clear/Resize, Sampled and PingPong issuers validated; ordinary and Storage/MRT branches reject |
| WebGL1 | Existing Clear/Sampled/PingPong issuer preserved; compute/storage/MRT rejects |
| Unknown | Deterministic diagnostic failure |

## Implementation and acceptance order

1. Original canvas identity and cold Clear/Resize shared execution, reusing native completion/map receipts. Fast L2 contract plus actual shared-recorder browser smoke and native negatives. Do not mark the package complete here.
2. Original immutable inputs/artifact pipelines and complete Sampled/PingPong draw journal; exact UBO/push/index/texture ordering and resize. Reuse existing artifact factories and recorders.
3. Storage/MRT compute/fragment dependency and three-output join. Explicit aliasing capability remains authoritative.
4. Final Debug/Release host contracts, Null smoke, all shared browser scenarios, private issuer regressions, independent RenderDoc evidence and frozen source/binary/output custody. Only then update the WebGPU package coverage and P4-14 acceptance.

## Clear/Resize checkpoint and next execution package

The shared FrameGraph now executes three original acquired canvases (64x64, 48x32, 64x64), reads 38,912 actual RGBA8 bytes through real queue/error-scope/map/unmap receipts, presents those exact identities and seals the read-only audit. Fifteen native negative cases include cold observer refusal and accepted-deadline retention. A separate fresh-device browser run destroys the actual device after accepted-work timeout, then verifies callback drain, quota retirement and denied output audit.

Independent RenderDoc replay uses Dawn's `enable_renderdoc_process_injection` toggle on the default D3D12 adapter, without selecting another adapter or changing capabilities. One valid warmup present registers Dawn's native canvas before the authored capture gate. The authored three captures contain clear -> exact original texture-to-owned-buffer copy -> same image present. D3D12 row pitch and offset come from the recorded `CopyTextureRegion` footprint; all 9,728 pixels and three hashes agree with runtime output. AI inspected `build/webgl1-evidence/p4/p4h-webgpu-shared-clear-final-capture/three-frame-contact.png`. Compositor-only and missing-first-frame attempts are preserved as failures, not merged into acceptance.

Next package is Sampled and PingPong together: reuse `WebGpuOriginalArtifactGraphicsCreation` / `WebGpuOriginalArtifactGraphicsFactory`, immutable original pipeline inputs, existing initialized upload sources, descriptor/resource bindings and shared recorders. Extend the finite command journal with complete vertex/index/UBO/push/pipeline/descriptor/draw identity and source-target transition ordering. General ordinary recording remains closed. The existing scalar-storage triangle proof describes another shader and cannot authorize these sampled draws. Actual creation scopes, accepted-work owners, row-major shader artifacts, output/present and independent capture must complete together before advancing those scene rows.

Dependencies remain intent/source -> native recording/submission -> actual observation/presentation -> audit. Readiness is never an input to execution.

## Sampled/PingPong execution package (bounded backend acceptance)

`OriginalGraphicsArtifactConsumption/2` preserves sparse L2 vertex slots. Version 1
continues to reject sparse slots. Version 2 validates unique stream slots and materializes
WebGPU positional layouts through max(slot)+1, bounded by the actual device's
maxVertexBuffers; absent slots use the WebGPU C API's Undefined step-mode null layout.
Logical slot 3 remains native slot 3. Null validates shape only; Vulkan's existing binding
indices and Metal's existing slot lowering are unchanged; no new native issuer is advertised
for those factories. Production/ordinary command admission and NativeSync remain closed.

`ConformanceSampledArtifactBudget/1` explicitly reserves independent input, creation-owner,
native-metadata, callback and wrapper quota domains plus maximum selected WGSL bytes. Zero
budgets reject the WebGPU entry before allocation. Existing Vulkan and validator requests
do not acquire this optional backend payload. These cold Prototype budgets do not enable a
production factory or ordinary command recording.

Inputs are uploaded once from the immutable workload, tracked with InitializedByteBuffer,
then sealed against host/GPU/transfer writes. Uint16 index content occupies six authored
bytes and an explicitly zero-initialized eight-byte WebGPU write (four-byte API alignment);
padding never changes the indexed range. Original graphics input selects the actual combined
WGSL artifact; complete pipeline identity, original layout and three real creation scopes
must validate before recording. Accepted creation callbacks retain their owners on timeout.

Each frame owns a frozen draw schedule and retains input/pipeline owners independently of
the public issuer. The finite journal admits exactly two source clears followed by the
authored draws. Intermediate texture identities are discovered only from original retained
command resources, with exact device/extent/format/usage/subresource metadata. Color-write
publication to fragment sampling is validated before ordered WebGPU lowering. Every draw
requires the exact pipeline, full viewport/scissor, slot-3 vertex input, optional Uint16
index input, five descriptor elements, frame UBO and authored 32-byte push block. Duplicate,
missing, foreign, reordered or extra work poisons the frame. No generic recording gate is
opened. Output copy, real callback drain, same-canvas present and audit reuse Clear/Resize.

Four-backend outcome: Null remains validator-only; Vulkan retains its original native
issuer; Metal deterministically rejects this shared entry; WebGPU admits only this explicit
issuer after all original artifact/native gates succeed. WebGL1 keeps its existing native
sampled/pingpong path. Storage/MRT stays closed until its separate complete package passes.

Pipeline creation has an independently declared bounded deadline; an accepted-work completion deadline never implicitly limits cold pipeline creation. Push ring capacity in the browser fixture is derived from authored maximum draw count and the queried native uniform-offset alignment. Same-state geometry dependencies validate exact original sealed buffers and stage/access through the finite journal, without opening ordinary WebGPU geometry barriers.

Final package evidence: `build/webgl1-evidence/p4/p4i-webgpu-shared-sampled-pingpong-checkpoint.yaml` and its custody index. Each scene has three independent original output/copy/present joins and complete constants/Mv replay. Dawn DXIL bytes are hashed separately; exact original WGSL module/pipeline/draw observation is recorded independently. No production/platform promotion follows. The next package is shared Storage/MRT three-output execution.
