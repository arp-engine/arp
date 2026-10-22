"""Replay cloud dispatches and export linear-HDR composite evidence."""
import json
import math
import os
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tools"))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, write_bmp, rgba8_pixels


def main():
    path = os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(path, "", None)
    assert (status.code == rd.ResultCode.Succeeded if hasattr(status, "code") else status == rd.ReplayStatus.Succeeded), str(status)
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert (status.code == rd.ResultCode.Succeeded if hasattr(status, "code") else status == rd.ReplayStatus.Succeeded) and controller
    try:
        flags = rd.ActionFlags if hasattr(rd, "ActionFlags") else rd.DrawFlags
        actions = list(flatten(controller.GetRootActions() if hasattr(controller, "GetRootActions") else controller.GetDrawcalls()))
        work = [a for a in actions if a.flags & (flags.Drawcall | flags.Dispatch | flags.Copy)]
        dispatches = [a for a in work if a.flags & flags.Dispatch]
        assert dispatches
        report = {"contract": "VolumetricCloud/1", "capture": path, "passes": [], "images": [],
            "readbacks": [], "temporalChains": []}
        resources = {r.resourceId: r.name for r in controller.GetResources()}
        textures = {t.resourceId: t for t in controller.GetTextures()}
        exported_raymarch = set()
        outputs = {}
        debug_only = os.environ.get("ARP_CLOUD_RENDERDOC_DEBUG_ONLY") == "1"
        require_state = os.environ.get("ARP_CLOUD_RENDERDOC_REQUIRE_RETAINED_STATE") == "1"
        require_texture = os.environ.get("ARP_CLOUD_RENDERDOC_REQUIRE_RETAINED_TEXTURE") == "1"
        buffer_barriers = []
        image_barriers = []
        dispatch_commands = {}
        if (require_state or require_texture) and not debug_only:
            structured = controller.GetStructuredFile()
            visited = set()
            def fields(node):
                return {node.GetChild(i).name: node.GetChild(i) for i in range(node.NumChildren())}
            def number(node):
                return int(node.data.basic.u)
            for action in actions:
                for event in action.events:
                    if event.eventId in visited:
                        continue
                    visited.add(event.eventId)
                    chunk = structured.chunks[event.chunkIndex]
                    if chunk.name == "vkCmdDispatch":
                        dispatch_commands[event.eventId] = number(fields(chunk)["commandBuffer"])
                    if chunk.name != "vkCmdPipelineBarrier":
                        continue
                    native = fields(chunk)
                    array = native["pBufferMemoryBarriers"]
                    for i in range(array.NumChildren()):
                        barrier = fields(array.GetChild(i))
                        buffer_barriers.append({"event": event.eventId,
                            "sourceStages": number(native["srcStageMask"]),
                            "destinationStages": number(native["destStageMask"]),
                            **{key: number(barrier[key]) for key in
                               ("buffer", "srcAccessMask", "dstAccessMask", "offset", "size")}})
                    array = native["pImageMemoryBarriers"]
                    for i in range(array.NumChildren()):
                        barrier = fields(array.GetChild(i))
                        image_barriers.append({"event": event.eventId,
                            "commandBuffer": number(native["commandBuffer"]),
                            "sourceStages": number(native["srcStageMask"]),
                            "destinationStages": number(native["destStageMask"]),
                            **{key: number(barrier[key]) for key in
                               ("image", "srcAccessMask", "dstAccessMask", "oldLayout", "newLayout")},
                            "range": {key: number(value) for key, value in
                                fields(barrier["subresourceRange"]).items()}})
        def dependency(before, after, resource, transfer):
            if not require_state:
                return None
            # Vulkan specification: COMPUTE=0x800, TRANSFER=0x1000,
            # SHADER_WRITE=0x40, SHADER_READ=0x20, TRANSFER_READ=0x800.
            selected = [b for b in buffer_barriers if before < b["event"] < after
                and b["buffer"] == int(resource) and b["sourceStages"] == 0x800
                and b["destinationStages"] == (0x1000 if transfer else 0x800)
                and b["srcAccessMask"] == 0x40
                and b["dstAccessMask"] == (0x800 if transfer else 0x20)
                and b["offset"] == 0 and b["size"] == (1 << 64) - 1]
            assert len(selected) == 1, (before, after, str(resource), selected)
            return selected[0]
        if debug_only:
            controller.SetFrameEvent(actions[-1].eventId, True)
            report["scope"] = "final-event replay and debug messages only"
            work = []
        for action in work:
            controller.SetFrameEvent(action.eventId, True)
            if action.flags & flags.Copy:
                produced = outputs.get(action.copySource)
                if produced:
                    # Compare at this exact copy event: staging may be reused
                    # or destroyed later in the capture.
                    size = len(produced[1])
                    source = bytes(controller.GetBufferData(action.copySource, 0, size))
                    destination = bytes(controller.GetBufferData(action.copyDestination, 0, size))
                    assert produced[0] < action.eventId and source == destination == produced[1]
                    report["readbacks"].append({"event": action.eventId,
                        "dispatchEvent": produced[0], "source": str(action.copySource),
                        "destination": str(action.copyDestination), "bytes": size,
                        "exactDispatchBytes": True,
                        "dependency": dependency(produced[0], action.eventId, action.copySource, True)})
                continue
            pipeline = controller.GetPipelineState()
            compute = bool(action.flags & flags.Dispatch)
            stage = rd.ShaderStage.Compute if compute else rd.ShaderStage.Fragment
            reflection = pipeline.GetShaderReflection(stage)
            assert reflection
            entry = {"event": action.eventId, "entry": reflection.entryPoint, "compute": compute}
            if compute:
                uniform = next(b.descriptor for b in pipeline.GetConstantBlocks(stage)
                    if reflection.constantBlocks[b.access.index].fixedBindNumber == 0)
                constants = bytes(controller.GetBufferData(uniform.resource, uniform.byteOffset, uniform.byteSize))
                assert len(constants) == 368
                width, height = struct.unpack_from("<2I", constants, 272)
                output = next(b.descriptor for b in pipeline.GetReadWriteResources(stage)
                    if reflection.readWriteResources[b.access.index].fixedBindNumber == 3)
                output_name = next(reflection.readWriteResources[b.access.index].name
                    for b in pipeline.GetReadWriteResources(stage)
                    if reflection.readWriteResources[b.access.index].fixedBindNumber == 3)
                data = bytes(controller.GetBufferData(output.resource, output.byteOffset, width * height * 32))
                records = list(struct.iter_unpack("<8f", data))
                assert len(records) == width * height
                assert all(all(math.isfinite(v) for v in r) and min(r[:3]) >= 0 and 0 <= r[3] <= 1 for r in records)
                entry.update(resource=resources.get(output.resource, str(output.resource)),
                    minT=min(r[3] for r in records), maxT=max(r[3] for r in records),
                    maxL=max(max(r[:3]) for r in records), finite=True)
                entry["output"] = output_name
                if output_name == "CloudResolved":
                    # Vulkan readonly StructuredBuffer is a storage-buffer
                    # descriptor; RenderDoc can expose it in readWriteResources.
                    raw_bindings = [b.descriptor for bindings, reflected in (
                        (pipeline.GetReadOnlyResources(stage), reflection.readOnlyResources),
                        (pipeline.GetReadWriteResources(stage), reflection.readWriteResources))
                        for b in bindings if reflected[b.access.index].fixedBindNumber == 1]
                    assert len(raw_bindings) == 1
                    raw = raw_bindings[0]
                    producer = outputs.get(raw.resource)
                    if producer and producer[2] == "CloudRaw":
                        assert struct.unpack_from("<f", constants, 348)[0] == 0
                        raw_data = bytes(controller.GetBufferData(raw.resource, raw.byteOffset, len(data)))
                        assert producer[0] < action.eventId and producer[1] == raw_data == data
                        report["temporalChains"].append({"raymarchEvent": producer[0],
                            "temporalEvent": action.eventId, "raw": str(raw.resource),
                            "resolved": str(output.resource), "bytes": len(data),
                            "historyReset": True, "exactRaymarchBytes": True,
                            "dependency": dependency(producer[0], action.eventId, raw.resource, False)})
                outputs[output.resource] = (action.eventId, data, output_name)
                if output_name == "CloudRaw":
                    depth = next(b.descriptor for b in pipeline.GetReadOnlyResources(stage)
                        if reflection.readOnlyResources[b.access.index].fixedBindNumber == 2)
                    texture = textures[depth.resource]
                    assert "D32" in texture.format.Name().upper(), texture.format.Name()
                    depth_values = [v[0] for v in struct.iter_unpack("<f", texture_bytes(controller, depth.resource))]
                    assert len(depth_values) == texture.width * texture.height
                    assert all(math.isfinite(v) and 0 <= v <= 1 for v in depth_values)
                    entry["sampledDepth"] = {"resource": resources.get(depth.resource, str(depth.resource)),
                        "format": texture.format.Name(), "width": texture.width, "height": texture.height,
                        "minimum": min(depth_values), "maximum": max(depth_values)}
                    if require_texture:
                        # Vulkan: SHADER_READ_ONLY=5, DEPTH_ATTACHMENT=3;
                        # COMPUTE=0x800, EARLY|LATE_TESTS=0x300,
                        # SHADER_READ=0x20, DEPTH_WRITE=0x400.
                        candidates = [b for b in image_barriers
                            if b["commandBuffer"] == dispatch_commands[action.eventId]
                            and b["image"] == int(depth.resource) and b["event"] < action.eventId
                            and b["oldLayout"] == 5 and b["newLayout"] == 3
                            and b["sourceStages"] == 0x800 and b["destinationStages"] == 0x300
                            and b["srcAccessMask"] == 0x20 and b["dstAccessMask"] == 0x400]
                        assert candidates, (action.eventId, str(depth.resource))
                        enter = candidates[-1]
                        leave = [b for b in image_barriers
                            if b["commandBuffer"] == dispatch_commands[action.eventId]
                            and b["image"] == int(depth.resource) and enter["event"] < b["event"] < action.eventId
                            and b["oldLayout"] == 3 and b["newLayout"] == 5
                            and b["sourceStages"] == 0x300 and b["destinationStages"] == 0x800
                            and b["srcAccessMask"] == 0x400 and b["dstAccessMask"] == 0x20]
                        assert len(leave) == 1
                        expected_range = {"aspectMask": 2, "baseMipLevel": 0, "levelCount": 1,
                            "baseArrayLayer": 0, "layerCount": 1}
                        assert enter["range"] == leave[0]["range"] == expected_range
                        entry["sampledDepth"]["sameCommandStateRoundTrip"] = [enter, leave[0]]
                    key = (constants, min(depth_values), max(depth_values))
                    if key not in exported_raymarch:
                        exported_raymarch.add(key)
                        # Diagnostic display only: the captured L/T is composed
                        # over an explicit constant background on CPU, then mapped.
                        def cloud_display(value):
                            linear = value / (1 + value)
                            return int(round(255 * (12.92 * linear if linear <= .0031308
                                else 1.055 * linear ** (1 / 2.4) - .055)))
                        pixels = [tuple(cloud_display(r[i] + r[3] * (.3, .5, .8)[i])
                            for i in range(3)) + (255,) for r in records]
                        image = os.path.splitext(path)[0] + ".raymarch.event" + str(action.eventId) + ".bmp"
                        write_bmp(image, width, height, pixels)
                        report["images"].append({"event": action.eventId, "path": image,
                            "width": width, "height": height, "uniquePixels": len(set(pixels)),
                            "displayTransform": "captured L+T*(.3,.5,.8), CPU diagnostic Reinhard exposure1 then sRGB"})
                if output_name == "CloudSlice":
                    pixels = [(int(round(r[0] * 255)),) * 3 + (255,) for r in records]
                    image = os.path.splitext(path)[0] + ".density.event" + str(action.eventId) + ".bmp"
                    write_bmp(image, width, height, pixels)
                    report["images"].append({"event": action.eventId, "path": image,
                        "width": width, "height": height, "uniquePixels": len(set(pixels)),
                        "displayTransform": "linear density times 255"})
            else:
                output = descriptor_resource(pipeline.GetOutputTargets()[0])
                texture = textures[output]
                hdr = "16" in texture.format.Name() and "FLOAT" in texture.format.Name().upper()
                values = list(struct.iter_unpack("<4e", texture_bytes(controller, output))) if hdr else []
                assert all(all(math.isfinite(c) and c >= 0 for c in value) for value in values)
                # Explicit display transform for the diagnostic screenshot:
                # Reinhard followed by linear-to-sRGB, exposure = 1.
                def display(value):
                    linear = value / (1 + value)
                    return int(round(255 * (12.92 * linear if linear <= .0031308 else 1.055 * linear ** (1 / 2.4) - .055)))
                pixels = [(display(v[0]), display(v[1]), display(v[2]), 255) for v in values] if hdr else rgba8_pixels(texture_bytes(controller, output), texture.format.Name())
                image = os.path.splitext(path)[0] + ".event" + str(action.eventId) + ".bmp"
                write_bmp(image, texture.width, texture.height, pixels)
                report["images"].append({"event": action.eventId, "path": image,
                    "width": texture.width, "height": texture.height, "uniquePixels": len(set(pixels)),
                    "displayTransform": "Reinhard exposure1 then sRGB" if hdr else "native tonemap output"})
            report["passes"].append(entry)
        if not debug_only and require_state:
            assert report["readbacks"] and len(report["temporalChains"]) == 2
        # Force final-event replay before inspecting every debug severity.
        controller.SetFrameEvent(actions[-1].eventId, True)
        messages = [{"severity": str(message.severity), "description": message.description}
            for message in controller.GetDebugMessages()]
        errors = [message["description"] for message in messages
            if message["severity"].split(".")[-1] in ("High", "Error")]
        assert not errors, errors
        report["errors"] = errors
        report["debugMessages"] = messages
        counters = [] if debug_only else controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
        times = {c.eventId: c.value.d * 1000 for c in counters
            if c.counter == rd.GPUCounter.EventGPUDuration}
        for entry in report["passes"]:
            entry["replayGpuMs"] = times.get(entry["event"])
        report["replayGpuMs"] = sum(c.value.d for c in counters if c.counter == rd.GPUCounter.EventGPUDuration) * 1000
        with open(os.path.splitext(path)[0] + (".debug.json" if debug_only else ".json"), "w") as output:
            json.dump(report, output, indent=2)
    finally:
        controller.Shutdown()
        capture.Shutdown()


try:
    main()
    os._exit(0)
except Exception:
    with open(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"] + ".error.txt", "w") as output:
        output.write(traceback.format_exc())
    os._exit(1)
