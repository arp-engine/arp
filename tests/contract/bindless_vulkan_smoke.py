"""Public Vulkan BindlessCross compute and graphics execution smoke."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import time

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from bindless_codegen import compile_bindless_package, read_bindless_publication


def run_native_phase(command, env, output, phase, deadline):
    """Preserve current-run evidence even when a native phase times out."""
    pending = {"success": False, "phase": phase, "status": "Running",
               "runnerDeadlineSeconds": deadline}
    (output / "report.json").write_text(json.dumps(pending, indent=2), encoding="utf-8")
    started = time.monotonic()
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=deadline, env=env)
        timed_out = False
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
        result = subprocess.CompletedProcess(command, 124, decoded(error.stdout), decoded(error.stderr))
        timed_out = True
    record = dict(pending, success=result.returncode == 0, status="Finished",
                  timedOut=timed_out, returnCode=result.returncode,
                  elapsedSeconds=time.monotonic() - started)
    (output / (phase + "-stdout.log")).write_text(result.stdout, encoding="utf-8")
    (output / (phase + "-stderr.log")).write_text(result.stderr, encoding="utf-8")
    (output / (phase + "-result.json")).write_text(json.dumps(record, indent=2), encoding="utf-8")
    (output / "report.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--renderdoccmd", type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "report.json").write_text(json.dumps(
        {"success": False, "phase": "CompileArtifacts", "status": "Running"}), encoding="utf-8")
    suffix = ".exe" if sys.platform == "win32" else ""
    tool_dir = pathlib.Path(args.slangc).parent
    source = args.output / "fixture.slang"
    source.write_text((ROOT / "tests/fixtures/bindless_access.slang").read_text()
                      + (ROOT / "tests/fixtures/bindless_vertex.slang").read_text(), encoding="utf-8")
    schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_variant.yaml").read_text())
    schema["faultLatchBinding"] = 14
    package = compile_bindless_package(
        schema,
        source, ["computeMain", "fragmentMain", "vertexMain"],
        "Native", args.output, args.slangc, tool_dir / ("spirv-reflect" + suffix))
    public_schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_variant.yaml").read_text())
    public_schema["faultLatchBinding"] = 14
    public_schema["tables"] = [table for table in public_schema["tables"]
                                if table["stages"] == ["compute"]]
    public_output = args.output / "public-compute"
    compute_source = public_output / "bindless_compute_only.slang"
    public_output.mkdir(parents=True, exist_ok=True)
    compute_source.write_text(source.read_text(encoding="utf-8").split('[shader("fragment")]')[0],
                              encoding="utf-8")
    public_package = compile_bindless_package(
        public_schema, compute_source, ["computeMain"], "Native", public_output,
        args.slangc, tool_dir / ("spirv-reflect" + suffix))
    graphics_schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_variant.yaml").read_text())
    graphics_schema["faultLatchBinding"] = 14
    graphics_schema["tables"] = [table for table in graphics_schema["tables"]
                                 if table["name"] in ("Images", "Filters")]
    graphics_schema["tables"].append(dict(name="VertexImages", tableId=7,
        kind="SampledTexture", capacity=2, binding=4, stages=["vertex"],
        operations=["load", "query"]))
    graphics_output = args.output / "public-graphics"
    graphics_source = graphics_output / "bindless_graphics_only.slang"
    graphics_output.mkdir(parents=True, exist_ok=True)
    combined_source = source.read_text(encoding="utf-8")
    fragment_source = combined_source[combined_source.index('[shader("fragment")]'):]
    fragment_source = fragment_source.split('[shader("vertex")]')[0]
    fragment_source += "\n" + (ROOT / "tests/fixtures/bindless_vertex_indexed.slang").read_text(encoding="utf-8")
    fragment_source = fragment_source.replace("int3(0, 0, 1)", "int3(0, 0, 0)")
    graphics_source.write_text(fragment_source, encoding="utf-8")
    graphics_package = compile_bindless_package(
        graphics_schema, graphics_source, ["fragmentMain", "vertexMain"], "Native",
        graphics_output, args.slangc, tool_dir / ("spirv-reflect" + suffix))
    root_package = compile_bindless_package(
        yaml.safe_load((ROOT / "tests/fixtures/bindless_metal_compute.yaml").read_text()),
        ROOT / "tests/fixtures/bindless_metal_compute.slang", ["cleanMain", "faultMain"],
        "Native", args.output / "root-fence", args.slangc, tool_dir / ("spirv-reflect" + suffix))
    texture_root_package = compile_bindless_package(
        yaml.safe_load((ROOT / "tests/fixtures/bindless_completed_texture_root.yaml").read_text()),
        ROOT / "tests/fixtures/bindless_completed_texture_root.slang", ["cleanMain", "faultMain"],
        "Native", args.output / "texture-root", args.slangc, tool_dir / ("spirv-reflect" + suffix))
    array_root_package = compile_bindless_package(
        yaml.safe_load((ROOT / "tests/fixtures/bindless_completed_array_root.yaml").read_text()),
        ROOT / "tests/fixtures/bindless_completed_array_root.slang", ["cleanMain", "faultMain"],
        "Native", args.output / "array-root", args.slangc, tool_dir / ("spirv-reflect" + suffix))
    manifest = read_bindless_publication(package)
    public_manifest = read_bindless_publication(public_package)
    graphics_manifest = read_bindless_publication(graphics_package)
    # Validate the selected executable, including its non-uniform capabilities.
    for fixture_dir, entries in ((args.output, ("computeMain", "fragmentMain", "vertexMain")),
                                 (graphics_output, ("fragmentMain", "vertexMain"))):
        for entry in entries:
            subprocess.run([str(tool_dir / ("spirv-val" + suffix)), "--target-env", "vulkan1.2",
                            str(fixture_dir / f"BindlessFixture.native.{entry}.spv")], check=True)
    runner_args = [args.runner, str(package.resolve()), str(public_package.resolve()),
                   str(graphics_package.resolve())]
    runner_command = runner_args
    runner_env = os.environ.copy()
    runner_env["ARP_VULKAN_ROOT_FENCE_PACKAGE"] = str(root_package.resolve())
    runner_env["ARP_VULKAN_TEXTURE_ROOT_PACKAGE"] = str(texture_root_package.resolve())
    runner_env["ARP_VULKAN_ARRAY_ROOT_PACKAGE"] = str(array_root_package.resolve())
    capture_template = args.output / "public-bindless-vulkan"
    if args.renderdoccmd:
        runner_env["ARP_RENDERDOC_CAPTURE_PATH"] = str(capture_template)
        runner_command = [str(args.renderdoccmd), "capture", "--wait-for-exit",
                          "--capture-file", str(capture_template),
                          "--opt-api-validation", *runner_args]
    # All three invocations include the complete original Root/geometry matrix.
    # A cold positive run exceeded90s; same-input replay took80.25s. Reuse the
    # already bounded180s terminal-matrix ceiling, without dropping assertions.
    runner_deadline_seconds = 180
    result = run_native_phase(runner_command, runner_env, args.output, "positive", runner_deadline_seconds)
    report = dict(success=result.returncode == 0, returnCode=result.returncode,
                  runnerDeadlineSeconds=runner_deadline_seconds,
                  shaderPackageIdentity=manifest["shaderPackageIdentity"],
                  publicComputePackageIdentity=public_manifest["shaderPackageIdentity"],
                  publicGraphicsPackageIdentity=graphics_manifest["shaderPackageIdentity"],
                  publicGraphicsExecution=result.returncode == 0,
                  publicDepthPublication4=result.returncode == 0
                      and result.stdout.count("BINDLESS_VULKAN_PUBLIC_DEPTH_PUBLICATION4 ") == 1,
                  publicDepthPublicationEvidence="Source2/Publication4: single D32Float cell produced by the existing public Graphics view, Content9 original journal, real fence and fault0; cancel/retry, repeated clear/draw, stale proof refusal, descriptor refresh and quota retirement; publication alone grants no shader execution; separate Execution6 evidence follows",
                  publicDepthRootPreparation6=result.returncode == 0
                      and result.stdout.count("BINDLESS_VULKAN_PUBLIC_DEPTH_ROOT6 ") == 2,
                  publicDepthRootPreparationEvidence="Native cold Preparation6/ReadInput2/Closure9; original snapshot, authorization, prepared pipeline and canonical inventory; exact native descriptor proof, repeated physical reference deduplication, stale generation, bounded metadata and ordinary execution refusal",
                  publicDepthRootReadPlan12=result.returncode == 0
                      and result.stdout.count("BINDLESS_VULKAN_PUBLIC_DEPTH_READ_PLAN12 ") == 2,
                  publicDepthRootReadPlanEvidence="Content12 read-only control4 plan on the same Root/inventory, Sequence2 and RootState4 journal; bounded exact/one-less/exhausted admission, CPU ShaderRead claim/cancel, hot0, preserved content generation and retirement; CPU planning alone grants no native execution; separate Lifecycle14/Completion8 evidence follows",
                  publicDepthShaderExecution=result.returncode == 0
                      and result.stdout.count("BINDLESS_VULKAN_PUBLIC_DEPTH_EXECUTION6 ") == 2,
                  publicDepthShaderExecutionEvidence="Independent Native Execution6/Content12/Pending2/Lifecycle14/Completion8: original public Root/inventory, typed callback-free capacities, authentic single-cell depth load/sampleLevel/query and two non-uniform indices, same journal/fence, cancellation, fault1, numeric results and quota retirement; no Fragment/linear/comparison or scene-ready claim",
                  publicGraphicsEvidence="Vulkan fence completion, pixel readback, clean fault report",
                  preparedComputeRetirement=result.returncode == 0
                      and "BINDLESS_VULKAN_PREPARED_COMPUTE_RETIRED 1" in result.stdout,
                  preparedGraphicsRetirement=result.returncode == 0
                      and "BINDLESS_VULKAN_PREPARED_GRAPHICS_RETIRED 1" in result.stdout,
                  preparedUnsubmittedAbandonment=result.returncode == 0
                      and "BINDLESS_VULKAN_PREPARED_ABANDONED 1" in result.stdout,
                  preparedVertexOnlyDepth=result.returncode == 0
                      and "BINDLESS_VULKAN_PREPARED_VERTEX_DEPTH_RETIRED 1" in result.stdout,
                  preparedEvidence="factory-issued native owners retained through recording/finish/submit; exact quota return after real fence observation",
                  canonicalRootNativeClosure=result.returncode == 0
                      and "BINDLESS_VULKAN_ROOT_NATIVE_CLOSURE 1" in result.stdout,
                  canonicalRootNativeClosureEvidence="cold authentic Compute/Graphics six-class physical payloads, view-parent and original fault-readback child; exact metadata budget and allocation-free validation; no recording authority",
                  canonicalRootExecution=False,
                  canonicalRootGraphicsContentComposition=result.returncode == 0
                    and result.stdout.count("BINDLESS_VULKAN_ROOT_GRAPHICS_CONTENT9 composition") == 2,
                  canonicalRootGraphicsContentEvidence="CPU ContentPlan/9 with real Vulkan attachment/4 and uploaded sampled/3 owners; buffer-only and sampled Graphics Root/4, same journal, independent capacities, Pending rollback/recapture, unchanged completed reads and hot allocation0; no native draw/completion permission",
                  canonicalRootGraphicsExecution=result.returncode == 0
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_EXECUTION9 genuine-draw" in result.stdout,
                  canonicalRootGraphicsExecutionEvidence="scoped GraphicsRootFence/9 prototype: same original Root/inventory/stream/journal, retained cold pass, authentic nonuniform sampled draw with independent samplers, explicit LOD and coordinate gradients before indexed dispatch; real fence, fault0 readback and attachment content2 after abandoned generation1; typed capacity retirement; independent RenderDoc pixel acceptance is separate; no full scene/WOCC readiness",
                  canonicalRootGraphicsBuffers=result.returncode == 0
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_BUFFERS9 fault=0" in result.stdout
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_BUFFERS9 fault=1" in result.stdout,
                  canonicalRootGraphicsBuffersEvidence="same original GraphicsRootFence9 issuer, Vertex/Fragment uniform and readonly buffers, Fragment writable buffer query/load/store/atomic, actual mapped initialization and completion-gated side effects, nonuniform indexing, table/element bounds fault0/1; no full material ABI or WOCC readiness",
                  canonicalRootGraphicsArray=result.returncode == 0
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_ARRAY9 fault=0" in result.stdout
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_ARRAY9 fault=1" in result.stdout,
                  canonicalRootGraphicsArrayEvidence="same original GraphicsRootFence9, genuine two-layer/two-mip uploads, independent samplers, layer/mip/table/sampler fault0/1 and coordinate derivatives before nonuniform dispatch/discard; numerical output requires independent replay; no full terrain equivalence or WebGPU execution",
                  canonicalRootGraphicsRecords=result.returncode == 0
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_RECORDS9 fault=0" in result.stdout
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_RECORDS9 fault=1" in result.stdout,
                  canonicalRootGraphicsRecordsEvidence="VectorRecord1 material32, real pcg InstanceDataGpu64 and output48 on the same GraphicsRootFence9 issuer, independent runtime binary/WGSL type proof, exact initialized inputs and completion-gated outputs; static slices separately verified; no matrices/full material ABI/public WebGPU/WOCC readiness",
                  canonicalRootGeometryColdPreparation=result.returncode == 0
                      and "BINDLESS_VULKAN_ORIGINAL_ROOT_GEOMETRY_CLOSURE2 1" in result.stdout,
                  canonicalRootIndexedGeometryExecution1=result.returncode == 0
                      and result.stdout.count("BINDLESS_VULKAN_ROOT_GEOMETRY_EXECUTION1 fault=") == 4,
                  canonicalRootIndexedGeometryExecution1Evidence="Independent private GeometryExecution1: RootState5, Closure3, initialized Fetch2, Content9/Geometry1, Lifecycle9 and readback-binding3 share the original seal/journal; true Slang vertex/instance attributes, offset16/stride16, UInt16 offset2 and firstInstance1; cancel reuse, clean/fault real fence, caller-drop custody, hot submit and quota retirement; public scene and repeated completed attachment execution remain ungranted",
                  canonicalRootInitializedGeometry=result.returncode == 0
                      and "BINDLESS_VULKAN_ORIGINAL_ROOT_INITIALIZED_GEOMETRY1 1" in result.stdout,
                  canonicalRootInitializedGeometryEvidence="HostGeometryContent1: exact native allocation queried before admission, real mapped write/flush, private complete frozen bytes, host/GPU write seals, authentic canonical vertex/index leases consumed by the original Root/Stream Fetch2; quota-pressure rollback, forged-owner/foreign-lease rejection and hot0; no Pending custody or indexed command/completion authority",
                  canonicalRootInitializedGeometryClosure=result.returncode == 0
                      and "BINDLESS_VULKAN_ORIGINAL_ROOT_INITIALIZED_CLOSURE3 1" in result.stdout,
                  canonicalRootInitializedGeometryClosureEvidence="NativeClosure3: authentic initialized actors retained by the same canonical geometry entries; exact leases, independent deduplicated CPU quota owners, byte/actor/owner capacity refusal, caller-drop retention, failure rollback and hot0; cold custody only, no Pending or indexed execution authority",
                  canonicalRootGeometryContentComposition=result.returncode == 0
                      and "BINDLESS_VULKAN_ORIGINAL_ROOT_GEOMETRY_CONTENT9_1 1" in result.stdout,
                  canonicalRootGeometryContentCompositionEvidence="Explicit ContentPlan9/Geometry1: exact Inputs7/RootState5 and retained original geometry footprint/graph/stream; genuine completed sampled texture controls plus native attachment cells, independent quotas, one-less refusal and hot0; ordinary profiles remain isolated; Pending and indexed native execution unavailable",
                  canonicalRootGeometryColdEvidence="RootState5/Inputs7 share authentic prepared pipeline/snapshot generation, vertex/index tail and draw-consumption projection, canonical state/stream/inventory and deduplicated real native buffer owners; exact budgets and hot validation0; no initialized fetch, indexed command submission or scene execution authority",
                  canonicalRootGraphicsRecordRanges=result.returncode == 0
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_RECORD_RANGES9 fault=0" in result.stdout
                      and "BINDLESS_VULKAN_ROOT_GRAPHICS_RECORD_RANGES9 fault=1" in result.stdout,
                  canonicalRootGraphicsRecordRangesEvidence="same record ABI and GraphicsRootFence9 with explicit original static finite descriptors, queried alignment, view-relative element counts, exact outputs and unchanged poison prefix/suffix; no dynamic offsets, public WebGPU or complete workload ABI",
                  canonicalRootGraphicsColdPassPreparation=result.returncode == 0
                    and result.stdout.count("BINDLESS_VULKAN_ROOT_GRAPHICS_COLD_PASS1 native-create") == 2,
                  canonicalRootGraphicsColdPassEvidence="cold genuine VkRenderPass/image view/framebuffer for exact original Graphics Root/9 stream and transaction; independent CPU and physical capacity leases, exact/undersized/exhausted budgets, all-or-nothing retirement and hot validation0; no command emission or native completion authority",
                  canonicalRootCompletedTextureExecution=result.returncode == 0
                      and "BINDLESS_VULKAN_COMPLETED_TEXTURE_ROOT8 1" in result.stdout,
                  canonicalRootCompletedTextureRepeat=result.returncode == 0
                      and "BINDLESS_VULKAN_COMPLETED_TEXTURE_ROOT8_REPEAT 1" in result.stdout,
                  canonicalRootCompletedArrayExecution=result.returncode == 0
                      and "BINDLESS_VULKAN_COMPLETED_ARRAY_ROOT8 1" in result.stdout,
                  canonicalRootCompletedArrayRepeat=result.returncode == 0
                      and "BINDLESS_VULKAN_COMPLETED_ARRAY_ROOT8_REPEAT 1" in result.stdout,
                  canonicalRootCompletedTextureEvidence="scoped Compute Lifecycle8, genuine RGBA8 2D uploads, whole CopyDst-to-ShaderRead barriers, independent sampler/nonuniform sampleLevel/mips, fault map after real fence; graphics and scene readiness remain pending",
                  canonicalRootCompletedArrayEvidence="same Lifecycle8 and original inventory/journal, versioned package dimension checked against actual SPIR-V; two RGBA8 2DArray images, two layers and mips, independent sampler/nonuniform load-query-sampleLevel, layer/mip faults, repeated generation and real fence; fragment/gradient/storage/scene domains remain pending",
                  canonicalRootTextureViewProvenance=result.returncode == 0
                      and "BINDLESS_RHI_ORIGINAL_ROOT_TEXTURE_VIEW_PROVENANCE 1" in result.stdout,
                  canonicalRootTextureViewProvenanceEvidence="authentic Root cold declarations match the exact original view Source index, range and canonical parent lease; CPU state graph is separately tested on host/Wasm, not native completed texture sampling",
                  nativeResultLossSource=result.returncode == 0
                      and "BINDLESS_VULKAN_NATIVE_RESULT_LOSS_SOURCE 1" in result.stdout,
                  nativeResultLossEvidence="NativeResultLoss/1: actual runtime buffer/texture/view correspondence, weak subscription quota/identity/retirement, negative device-lost result after real idle; no actual hardware loss or queue-drain proof",
                  originalTextureCreation=result.returncode == 0
                      and "BINDLESS_VULKAN_ORIGINAL_TEXTURE_CREATION3 1" in result.stdout,
                  originalTextureCreationEvidence="ContentCreation/3: exact native image requirement and bounded real VMA allocation, Undefined/uninitialized metadata, exclusive legacy rejection and quota return; no content upload or completion",
                  originalAttachmentCreation=result.returncode == 0
                      and result.stdout.count("BINDLESS_VULKAN_ORIGINAL_ATTACHMENT_CREATION4 format=") == 4,
                  originalAttachmentCreationEvidence="ContentCreation/4: real RGBA8/RGBA16F/D32/depth-stencil allocation queries, original mip-layer-aspect cells, logical/native/CPU one-less refusal, Undefined/uninitialized content, ordinary render-pass refusal and shutdown quota return; no Root Graphics recording/completion permission",
                  originalUndefinedTextureUploadPlan=result.returncode == 0
                      and "BINDLESS_VULKAN_UNDEFINED_TEXTURE_UPLOAD_PLAN7 1" in result.stdout,
                  originalUndefinedTextureUploadPlanEvidence="ContentPlan/7: original Undefined control3, whole authored transition/upload, bounded same-journal CPU proposal/cancellation and drop; no staging initialization or actual GPU upload/completion",
                  originalTextureUploadFence=result.returncode == 0
                      and "BINDLESS_VULKAN_ORIGINAL_TEXTURE_UPLOAD_FENCE7 1" in result.stdout,
                    completedTexturePublication=result.returncode == 0
                        and "BINDLESS_VULKAN_COMPLETED_TEXTURE_PUBLICATION1 1" in result.stdout,
                    completedTexturePublicationEvidence="CompletedTextureDescriptor/1: authentic control3 fence completion, RGBA8 whole 2D/2DArray Source/view-parent custody, original descriptor loss registration9, actual VkImageView/write, bounded snapshot padding; shader texture Root execution remains closed",
                    originalSamplerDescriptorLossJoin=result.returncode == 0
                        and "sampler-loss-join2" in result.stdout,
                    originalSamplerDescriptorLossJoinEvidence="DescriptorLossRequest/2 and SamplerLossJoin/2: independent registered sampler capacity, same genuine runtime/device/registry payload and weak loss subscription, authentic original lease, descriptor metadata, allocation-free validation, retired-subscription rejection; no shader texture Root execution",
                  originalTextureUploadFenceEvidence="WholeOriginalUploadFence/7: frozen packing, genuinely written/flushed immutable registered staging, same inventory/state/seal/journal, RGBA8 2D/2DArray two-mip copies, real fence completion and unsubmitted rollback; texture Root/Graphics remain closed",
                  canonicalRootBufferExecution=result.returncode == 0
                      and "BINDLESS_VULKAN_CANONICAL_ROOT_BUFFER_EXECUTION 1" in result.stdout,
                  canonicalRootBufferEvidence="RootFaultFence/6 prototype: original /4 stream/seal/journal, mapped fault result after actual fence, numerical buffer/atomic side effects, unsubmitted abandonment and budget return; full resource/graphics execution remains closed",
                  publicIndirectExecution=result.returncode == 0
                      and "BINDLESS_VULKAN_INDIRECT_EXECUTED 1" in result.stdout,
                  publicIndirectEvidence="single indexed indirect draw, retained access, fence, clean fault report and independent pixel readback",
                  scope="public-vulkan-bindlesscross-compute-and-graphics; no WOCC readiness",
                  stdout=result.stdout, stderr=result.stderr)
    if args.renderdoccmd:
        report["renderDocCaptureTemplate"] = str(capture_template)
    if result.returncode == 0:
        # --partial changes the final logical table counts, but still executes
        # every original upload/Root/Graphics/lifecycle regression first. Use
        # the same bounded deadline as the two other full-matrix invocations.
        partial = run_native_phase([args.runner, str(package.resolve()), str(public_package.resolve()),
                                    str(graphics_package.resolve()), "--partial"], runner_env,
                                   args.output, "partial", runner_deadline_seconds)
        report["partialSnapshot"] = dict(success=partial.returncode == 0, stdout=partial.stdout, stderr=partial.stderr)
        report["success"] = partial.returncode == 0
        if partial.returncode: result = partial
    if report["success"]:
        negative_env = runner_env.copy()
        negative_env.pop("ARP_RENDERDOC_CAPTURE_PATH", None)
        negative_env["ARP_VULKAN_ROOT_UNCERTAIN_SUBMIT"] = "1"
        negative_env["ARP_VULKAN_ROOT_IDLE_DRAIN"] = "1"
        negative_env["ARP_VULKAN_TEXTURE_ROOT_TERMINAL_CASES"] = "1"
        # This process also replays texture/array/Graphics/idle/quarantine
        # terminal cases. A current run exhausted180s; isolated replay passed
        # in175.05s. Keep the complete assertions and give this larger matrix
        # its own bounded240s ceiling, separate from positive/partial180s.
        terminal_deadline_seconds = 240
        report["terminalRunnerDeadlineSeconds"] = terminal_deadline_seconds
        negative = run_native_phase(runner_args, negative_env, args.output, "negative", terminal_deadline_seconds)
        report["canonicalRootUncertainSubmission"] = dict(
            success=negative.returncode == 0
                and "BINDLESS_VULKAN_ROOT_UNCERTAIN_SUBMIT_RETIRED_UNPUBLISHED 1" in negative.stdout,
            scope="negative lifecycle injection after real vkQueueSubmit; real driver fence retires invalidated custody without result publication; no device-loss proof",
            stdout=negative.stdout, stderr=negative.stderr)
        report["success"] = report["canonicalRootUncertainSubmission"]["success"]
        report["canonicalRootDeviceIdleCompletion"] = dict(
            success=negative.returncode == 0
                and "BINDLESS_VULKAN_ROOT_DEVICE_IDLE_RETIRED 1" in negative.stdout,
            scope="real same-runtime vkDeviceWaitIdle with withheld fences: clean/fault publication and uncertain-submit unpublished retirement; foreign device idle and failed idle retain; no device-loss proof")
        report["success"] = report["success"] and report["canonicalRootDeviceIdleCompletion"]["success"]
        report["canonicalRootCompletedTextureTerminal"] = dict(
            success=negative.returncode == 0
                and "BINDLESS_VULKAN_COMPLETED_TEXTURE_ROOT8_TERMINAL 1" in negative.stdout,
            scope="Lifecycle8 genuine original uploads/reads, retained exact texture/view/sampler closure after caller release, withheld fence/shutdown, missing/failed/foreign idle, real same-runtime idle, unknown queue/fence observations invalidate state and retire without fault publication; no hardware device-loss proof")
        report["success"] = report["success"] and report["canonicalRootCompletedTextureTerminal"]["success"]
        report["canonicalRootCompletedArrayTerminal"] = dict(
            success=negative.returncode == 0
                and "BINDLESS_VULKAN_COMPLETED_ARRAY_ROOT8_TERMINAL 1" in negative.stdout,
            scope="same ten clean/fault terminal cases on genuine 2DArray uploads, exact read-only content stamps, conservative state invalidation and unpublished retirement; no hardware device-loss proof")
        report["success"] = report["success"] and report["canonicalRootCompletedArrayTerminal"]["success"]
        report["canonicalRootGraphicsTerminal"] = dict(
            success=negative.returncode == 0
                and "BINDLESS_VULKAN_ROOT_GRAPHICS_EXECUTION9_TERMINAL 1" in negative.stdout,
            scope="Lifecycle9 original Graphics: five real-driver clean cases, retained cold pass/attachment through withheld fence and shutdown; missing/failed/foreign idle refuse; same-runtime fence/idle commits; uncertain queue/fence invalidates writable attachment and retires unpublished; no hardware device-loss proof")
        report["success"] = report["success"] and report["canonicalRootGraphicsTerminal"]["success"]
        if not report["success"]: result = negative
    final_code = result.returncode or (0 if report["success"] else 1)
    report["returnCode"] = final_code
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return final_code


if __name__ == "__main__":
    raise SystemExit(main())
