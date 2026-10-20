"""Replay the original public D32Float producer; run with qrenderdoc --python."""
import hashlib
import json
import math
import os
import struct
import traceback
import renderdoc as rd

output = os.environ["ARP_BINDLESS_CAPTURE_OUTPUT"]
report = {"scope": "public Graphics depth producer; no depth shader consumer"}
capture = None
controller = None
try:
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ["ARP_BINDLESS_CAPTURE_PATH"], "", None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))

    def walk(actions):
        for action in actions:
            yield action
            yield from walk(action.children)

    expected_hashes = []
    for entry in ("vertexMain", "cleanFragment"):
        with open(os.path.join(os.environ["ARP_BINDLESS_DEPTH_PACKAGE_DIR"],
                               "RootVertexBuffer.native." + entry + ".spv"), "rb") as source:
            expected_hashes.append(hashlib.sha256(source.read()).hexdigest())
    evidence = []
    original = None
    for action in walk(controller.GetRootActions()):
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.eventId, True)
        pipe = controller.GetPipelineState()
        shaders = [pipe.GetShaderReflection(stage) for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment)]
        if any(shader is None for shader in shaders):
            continue
        actual = [hashlib.sha256(bytes(shader.rawBytes)).hexdigest() for shader in shaders]
        if actual != expected_hashes:
            continue
        depth = pipe.GetDepthTarget().resource
        colors = [target for target in pipe.GetOutputTargets() if target.resource != rd.ResourceId.Null()]
        if depth == rd.ResourceId.Null() or colors or action.numIndices != 3 or action.numInstances != 1:
            raise RuntimeError("Expected depth-only public direct Draw3")
        if original is None:
            original = depth
        elif depth != original:
            raise RuntimeError("Re-clear replaced original depth resource")
        raw = bytes(controller.GetTextureData(depth, rd.Subresource(0, 0, 0)))
        values = struct.unpack("<4f", raw)
        expected = (0.01125, 0.01375) * 2
        if any(abs(actual_value - expected_value) > 0.0000001 for actual_value, expected_value in zip(values, expected)):
            raise RuntimeError("Actual interpolated D32 draw differs: " + str(values))
        evidence.append(dict(eventId=action.eventId, resource=str(depth), values=list(values),
                             bytes=raw.hex(), vertexSpirvSha256=actual[0], fragmentSpirvSha256=actual[1]))
    ordinary_version = os.environ.get("ARP_BINDLESS_ORDINARY_DEPTH_SOURCE_VERSION", "0")
    if ordinary_version not in ("0", "6"):
        raise RuntimeError("Unsupported ordinary depth Source capture version: " + ordinary_version)
    expected_producers = 3 if ordinary_version == "6" else 2
    if len(evidence) != expected_producers:
        raise RuntimeError("Expected " + str(expected_producers)
                           + " real completed producer draws: " + str(len(evidence)))
    report["draws"] = evidence
    consumer_dir = os.environ.get("ARP_BINDLESS_NATIVE_DEPTH_SHADER_DIR")
    if consumer_dir:
        actions = list(walk(controller.GetRootActions()))
        consumers = []
        buffers = controller.GetBuffers()
        for entry, fault in (("nativeSingleCellMain", 0), ("faultMain", 1)):
            with open(os.path.join(consumer_dir, "SampledDepth.native." + entry + ".spv"), "rb") as source:
                expected_hash = hashlib.sha256(source.read()).hexdigest()
            matches = []
            for action in actions:
                if not action.flags & rd.ActionFlags.Dispatch:
                    continue
                controller.SetFrameEvent(action.eventId, True)
                shader = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
                if shader and hashlib.sha256(bytes(shader.rawBytes)).hexdigest() == expected_hash:
                    matches.append(action)
            if len(matches) != 1:
                raise RuntimeError("Expected one public Native depth " + entry + " dispatch: " + str(len(matches)))
            dispatch = matches[0]
            controller.SetFrameEvent(dispatch.eventId, True)
            descriptors = controller.GetPipelineState().GetAllUsedDescriptors()
            depth_descriptors = [used for used in descriptors
                                 if used.access.stage == rd.ShaderStage.Compute and used.descriptor.resource == original]
            # faultMain only references valid depth index 0; its index 2 call
            # must be rejected before descriptor access. Clean uses both.
            if len(depth_descriptors) != (1 if fault else 2):
                report["descriptorObservation"] = [dict(resource=str(used.descriptor.resource),
                    stage=str(used.access.stage), access=str(used.access), descriptor=str(used.descriptor))
                    for used in descriptors]
                raise RuntimeError("Unexpected descriptor indices into the original producer depth image in "
                                   + entry + ": " + str(len(depth_descriptors)))
            copy = next((action for action in actions if action.eventId > dispatch.eventId
                         and action.flags & rd.ActionFlags.Copy), None)
            if copy is None:
                raise RuntimeError("Missing original fault copy after " + entry)
            controller.SetFrameEvent(copy.eventId, True)
            actual_fault = struct.unpack("<I", bytes(controller.GetBufferData(copy.copyDestination, 0, 4)))[0]
            if actual_fault != fault:
                raise RuntimeError("Independent native depth fault mismatch: " + str(actual_fault))
            results = []
            if not fault:
                for buffer in buffers:
                    if buffer.length != 16 or not any(usage.eventId == dispatch.eventId
                            and usage.usage == rd.ResourceUsage.CS_RWResource for usage in controller.GetUsage(buffer.resourceId)):
                        continue
                    raw = bytes(controller.GetBufferData(buffer.resourceId, 0, 16))
                    loaded, filtered, width, height = struct.unpack("<ffII", raw)
                    if abs(loaded - .01125) > 1e-7 or abs(filtered - .01375) > 1e-7 or (width, height) != (2, 2):
                        raise RuntimeError("Independent Native depth output mismatch: " + raw.hex())
                    results.append(dict(resource=str(buffer.resourceId), loaded=loaded, filtered=filtered,
                                        width=width, height=height, bytes=raw.hex()))
                if len(results) != 2:
                    raise RuntimeError("Expected two independently indexed depth result buffers")
            consumers.append(dict(entry=entry, eventId=dispatch.eventId, spirvSha256=expected_hash,
                                  depthResource=str(original), depthDescriptors=len(depth_descriptors),
                                  faultBits=actual_fault, faultCopyEventId=copy.eventId, results=results))
        report["scope"] = "public Graphics producer and Native Compute depth consumer; single original D32 cell"
        report["consumers"] = consumers
    if ordinary_version == "6":
        if not consumer_dir or evidence[-1]["eventId"] <= max(item["eventId"] for item in consumers):
            raise RuntimeError("Source6 stale-proof probe must re-clear after both real depth consumers")
        report["ordinarySourceVersion"] = 6
        report["ordinarySourceReclearEventId"] = evidence[-1]["eventId"]
        report["ordinaryDepthComparisonExecutionClaimed"] = False
    graphics_dir = os.environ.get("ARP_BINDLESS_GRAPHICS_DEPTH_SHADER_DIR")
    if graphics_dir:
        actions = list(walk(controller.GetRootActions()))
        lengths = {b.resourceId: b.length for b in controller.GetBuffers()}
        comparisons = []
        for entry, fault in (("cleanFragment", 0), ("faultFragment", 1)):
            expected = []
            for shader_entry in ("vertexMain", entry):
                with open(os.path.join(graphics_dir, "GraphicsDepthColor.native." + shader_entry + ".spv"), "rb") as source:
                    expected.append(hashlib.sha256(source.read()).hexdigest())
            matches = []
            for action in actions:
                if not action.flags & rd.ActionFlags.Drawcall:
                    continue
                controller.SetFrameEvent(action.eventId, True)
                pipe = controller.GetPipelineState()
                shaders = [pipe.GetShaderReflection(s) for s in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment)]
                if all(shaders) and [hashlib.sha256(bytes(s.rawBytes)).hexdigest() for s in shaders] == expected:
                    matches.append(action)
            if len(matches) != 1:
                raise RuntimeError("Expected one submitted Graphics comparison " + entry + ": " + str(len(matches)))
            draw = matches[0]
            controller.SetFrameEvent(draw.eventId, True)
            pipe = controller.GetPipelineState()
            used = list(pipe.GetAllUsedDescriptors())
            depth_bindings = [d for d in used if d.access.stage == rd.ShaderStage.Fragment
                              and d.descriptor.resource == original]
            if not depth_bindings or draw.numIndices != 3 or draw.numInstances != 1:
                raise RuntimeError("Comparison must consume the same original depth with direct Draw3")
            raw_depth = bytes(controller.GetTextureData(original, rd.Subresource(0, 0, 0)))
            depths = struct.unpack('<4f', raw_depth)
            if any(abs(a - b) > 1e-7 for a, b in zip(depths, (0.01125, 0.01375) * 2)):
                raise RuntimeError("Comparison depth differs from independently validated producer")
            results = {d.descriptor.resource for d in used if d.access.stage == rd.ShaderStage.Fragment
                       and d.access.type == rd.DescriptorType.ReadWriteBuffer
                       and lengths.get(d.descriptor.resource) == 160}
            if len(results) != 1:
                raise RuntimeError("Missing exact comparison Results40 buffer")
            raw = bytes(controller.GetBufferData(next(iter(results)), 0, 160))
            values = struct.unpack('<40f', raw)
            for pixel in range(4):
                lanes = (16 * (pixel + 1) / 255, 0, 0, 1, 1, 0, 0, 0, 0, 1)
                for lane, wanted in enumerate(lanes):
                    if not math.isfinite(values[pixel * 10 + lane]) or abs(values[pixel * 10 + lane] - wanted) > 1e-6:
                        raise RuntimeError("Comparison mismatch pixel/lane " + str((pixel, lane, values[pixel * 10 + lane], wanted)))
            targets = [t.resource for t in pipe.GetOutputTargets() if t.resource != rd.ResourceId.Null()]
            if len(targets) != 1 or pipe.GetDepthTarget().resource != rd.ResourceId.Null():
                raise RuntimeError("Comparison must write an independent color attachment")
            rgba = bytes(controller.GetTextureData(targets[0], rd.Subresource(0, 0, 0)))
            if rgba != bytes(v for pixel in range(4) for v in (16 * (pixel + 1), 0, 0, 255)):
                raise RuntimeError("Comparison color attachment mismatch: " + rgba.hex())
            copy = next((a for a in actions if a.eventId > draw.eventId and a.flags & rd.ActionFlags.Copy), None)
            if copy is None:
                raise RuntimeError("Comparison missing original fault copy")
            controller.SetFrameEvent(copy.eventId, True)
            bits = struct.unpack('<I', bytes(controller.GetBufferData(copy.copyDestination, 0, 4)))[0]
            if bits != fault:
                raise RuntimeError("Comparison fault mismatch")
            comparisons.append(dict(entry=entry, eventId=draw.eventId, spirvSha256=expected,
                depthResource=str(original), depthBytes=raw_depth.hex(), resultsBytes=raw.hex(),
                numericLanes=40, colorBytes=rgba.hex(), faultBits=bits, faultCopyEventId=copy.eventId))
        if evidence[-1]['eventId'] <= max(item['eventId'] for item in comparisons):
            raise RuntimeError("Stale proof reclear must follow both real Graphics consumers")
        report['graphicsComparisons'] = comparisons
        report['ordinaryDepthComparisonExecutionClaimed'] = True
        report['scope'] = 'Same original D32 producer, Compute consumers and public Graphics2 color/depth comparison consumers'
    report["validationMessages"] = [message.description for message in controller.GetDebugMessages()]
    if report["validationMessages"]:
        raise RuntimeError("Vulkan validation messages present")
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
    report["success"] = False
finally:
    with open(os.path.join(output, "renderdoc-report.json"), "w") as destination:
        json.dump(report, destination, indent=2)
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    os._exit(0 if report.get("success") else 1)
