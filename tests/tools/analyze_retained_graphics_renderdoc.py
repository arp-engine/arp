"""Verify real retained cloud Graphics, dependencies and padded HDR readback."""
import json
import math
import os
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tools"))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, write_bmp


def fields(node):
    return {node.GetChild(i).name: node.GetChild(i) for i in range(node.NumChildren())}


def number(node):
    return int(node.data.basic.u)


def succeeded(status):
    return status.code == rd.ResultCode.Succeeded if hasattr(status, "code") else status == rd.ReplayStatus.Succeeded


def resource(pipeline, reflection, binding):
    matches = [b.descriptor for entries, reflected in (
        (pipeline.GetReadOnlyResources(rd.ShaderStage.Fragment), reflection.readOnlyResources),
        (pipeline.GetReadWriteResources(rd.ShaderStage.Fragment), reflection.readWriteResources))
        for b in entries if reflected[b.access.index].fixedBindNumber == binding]
    assert len(matches) == 1, (binding, matches)
    return matches[0]


def display(value):
    linear = value / (1 + value)
    return round(255 * (12.92 * linear if linear <= .0031308 else 1.055 * linear ** (1 / 2.4) - .055))


def main():
    path = os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]
    cap = rd.OpenCaptureFile()
    assert succeeded(cap.OpenFile(path, "", None))
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    assert succeeded(status) and controller
    try:
        actions = list(flatten(controller.GetRootActions()))
        structured = controller.GetStructuredFile()
        textures = {t.resourceId: t for t in controller.GetTextures()}
        images, buffers, native_draws, native_dispatches, native_copies, visited = [], [], {}, {}, {}, set()
        for action in actions:
            for event in action.events:
                if event.eventId in visited:
                    continue
                visited.add(event.eventId)
                chunk = structured.chunks[event.chunkIndex]
                if chunk.name not in ("vkCmdPipelineBarrier", "vkCmdDraw", "vkCmdDispatch", "vkCmdCopyImageToBuffer"):
                    continue
                native = fields(chunk)
                cb = number(native["commandBuffer"])
                if chunk.name == "vkCmdDraw":
                    native_draws[event.eventId] = {"commandBuffer": cb, **{k: number(native[k]) for k in ("vertexCount", "instanceCount", "firstVertex", "firstInstance")}}
                elif chunk.name == "vkCmdDispatch":
                    native_dispatches[event.eventId] = cb
                elif chunk.name == "vkCmdCopyImageToBuffer":
                    array = native["pRegions"]
                    assert array.NumChildren() == 1
                    region = fields(array.GetChild(0))
                    native_copies[event.eventId] = {"commandBuffer": cb,
                        "source": number(native["srcImage"]), "destination": number(native["destBuffer"]),
                        "layout": number(native["srcImageLayout"]),
                        **{k: number(region[k]) for k in ("bufferOffset", "bufferRowLength", "bufferImageHeight")}}
                else:
                    common = {"event": event.eventId, "commandBuffer": cb,
                        "sourceStages": number(native["srcStageMask"]), "destinationStages": number(native["destStageMask"])}
                    for array_name, output, names in (
                        ("pImageMemoryBarriers", images, ("image", "oldLayout", "newLayout", "srcAccessMask", "dstAccessMask")),
                        ("pBufferMemoryBarriers", buffers, ("buffer", "offset", "size", "srcAccessMask", "dstAccessMask"))):
                        array = native[array_name]
                        for i in range(array.NumChildren()):
                            item = fields(array.GetChild(i))
                            output.append({**common, **{k: number(item[k]) for k in names}})
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
        assert len(draws) == len(native_draws) == 36, (len(draws), len(native_draws))
        dispatches = [a for a in actions if a.flags & rd.ActionFlags.Dispatch]
        copies = [a for a in actions if a.flags & rd.ActionFlags.Copy and a.copySource in textures]
        assert len(copies) == len(native_copies) == len(draws)
        report = {"contract": "PrivateRetainedFullscreenGraphics/1", "capture": path,
            "scope": "same-command raymarch -> Fragment read -> actual cloud_composite draw -> padded HDR readback; private Vulkan only",
            "actualDrawCalls": len(draws), "actualDispatches": len(dispatches), "draws": []}
        image_output = []
        for ordinal, draw in enumerate(draws):
            native = native_draws[draw.eventId]
            assert native == {"commandBuffer": native["commandBuffer"], "vertexCount": 3, "instanceCount": 1, "firstVertex": 0, "firstInstance": 0}
            controller.SetFrameEvent(draw.eventId, True)
            pipeline = controller.GetPipelineState()
            fs = pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
            vs = pipeline.GetShaderReflection(rd.ShaderStage.Vertex)
            assert fs.entryPoint == "fragmentMain" and vs.entryPoint == "vertexMain"
            constants = [b.descriptor for b in pipeline.GetConstantBlocks(rd.ShaderStage.Fragment)
                if fs.constantBlocks[b.access.index].fixedBindNumber == 0]
            assert len(constants) == 1
            c = constants[0]
            data = bytes(controller.GetBufferData(c.resource, c.byteOffset, c.byteSize))
            assert len(data) == 368
            width, height, full_width, full_height = struct.unpack_from("<4I", data, 272)
            assert (width, height, full_width, full_height) == (16, 12, 16, 12)
            raw = resource(pipeline, fs, 1)
            raw_bytes = bytes(controller.GetBufferData(raw.resource, raw.byteOffset, width * height * 32))
            samples = list(struct.iter_unpack("<8f", raw_bytes))
            assert len(samples) == width * height and all(all(math.isfinite(v) for v in s) for s in samples)
            scene, depth = resource(pipeline, fs, 2), resource(pipeline, fs, 3)
            scene_tex, depth_tex = textures[scene.resource], textures[depth.resource]
            assert scene_tex.format.Name() == "R16G16B16A16_FLOAT" and depth_tex.format.Name() == "D32"
            scene_bytes = texture_bytes(controller, scene.resource)
            assert scene_bytes == struct.pack("<4e", .25, .5, 1.5, .625) * (width * height)
            depth_values = [v[0] for v in struct.iter_unpack("<f", texture_bytes(controller, depth.resource))]
            assert len(depth_values) == width * height and min(depth_values) == max(depth_values)
            target = descriptor_resource(pipeline.GetOutputTargets()[0])
            assert target not in (scene.resource, depth.resource)
            output = texture_bytes(controller, target)
            pixels = list(struct.iter_unpack("<4e", output))
            assert len(pixels) == len(samples)
            worst = 0
            for sample, pixel in zip(samples, pixels):
                reference = [sample[i] + sample[3] * (.25, .5, 1.5)[i] for i in range(3)] + [.625]
                assert all(math.isfinite(v) for v in pixel) and pixel[3] == .625
                worst = max(worst, max(abs(a-b) for a,b in zip(reference, pixel)))
            assert worst < .002, (draw.eventId, worst)
            producer = max((a for a in dispatches if a.eventId < draw.eventId), key=lambda a: a.eventId)
            assert native_dispatches[producer.eventId] == native["commandBuffer"]
            dependency = [b for b in buffers if producer.eventId < b["event"] < draw.eventId
                and b["commandBuffer"] == native["commandBuffer"] and b["buffer"] == int(raw.resource)
                and b["sourceStages"] == 0x800 and b["destinationStages"] == 0x80
                and b["srcAccessMask"] == 0x40 and b["dstAccessMask"] == 0x20
                and b["offset"] == 0 and b["size"] == (1 << 64) - 1]
            assert len(dependency) == 1, (draw.eventId, dependency)
            controller.SetFrameEvent(producer.eventId, True)
            compute = controller.GetPipelineState()
            cr = compute.GetShaderReflection(rd.ShaderStage.Compute)
            produced = [b.descriptor for b in compute.GetReadWriteResources(rd.ShaderStage.Compute)
                if cr.readWriteResources[b.access.index].fixedBindNumber == 3]
            assert len(produced) == 1 and produced[0].resource == raw.resource
            assert bytes(controller.GetBufferData(raw.resource, raw.byteOffset, len(raw_bytes))) == raw_bytes
            copy = next(a for a in copies if a.eventId > draw.eventId and a.copySource == target)
            nc = native_copies[copy.eventId]
            assert nc == {"commandBuffer": native["commandBuffer"], "source": int(target), "destination": int(copy.copyDestination),
                "layout": 6, "bufferOffset": 256, "bufferRowLength": 32, "bufferImageHeight": height + 2}
            transition = [b for b in images if draw.eventId < b["event"] < copy.eventId
                and b["commandBuffer"] == native["commandBuffer"] and b["image"] == int(target)
                and b["oldLayout"] == 2 and b["newLayout"] == 6 and b["sourceStages"] == 0x400
                and b["destinationStages"] == 0x1000 and b["srcAccessMask"] == 0x100 and b["dstAccessMask"] == 0x800]
            assert len(transition) == 1
            staging_barrier = [b for b in buffers if draw.eventId < b["event"] < copy.eventId
                and b["commandBuffer"] == native["commandBuffer"] and b["buffer"] == int(copy.copyDestination)
                and b["sourceStages"] == b["destinationStages"] == 0x1000
                and b["srcAccessMask"] == b["dstAccessMask"] == 0x1000
                and b["offset"] == 0 and b["size"] == (1 << 64) - 1]
            assert len(staging_barrier) == 1
            controller.SetFrameEvent(copy.eventId, True)
            checked_bytes = (height + 4) * 256
            actual = bytes(controller.GetBufferData(copy.copyDestination, 0, checked_bytes))
            expected = bytearray(b"\xa5" * checked_bytes)
            for y in range(height):
                expected[256+y*256:256+y*256+width*8] = output[y*width*8:(y+1)*width*8]
            assert actual == expected
            display_pixels = [tuple(display(v) for v in pixel[:3]) + (255,) for pixel in pixels]
            image_path = os.path.splitext(path)[0] + ".draw%d.bmp" % draw.eventId
            write_bmp(image_path, width, height, display_pixels)
            image_output.append(image_path)
            report["draws"].append({"event": draw.eventId, "raymarchEvent": producer.eventId, "copyEvent": copy.eventId,
                "nativeDraw": native, "constants": str(c.resource), "raw": str(raw.resource), "scene": str(scene.resource),
                "depth": str(depth.resource), "target": str(target), "sampledDepth": depth_values[0],
                "computeFragmentDependency": dependency[0], "colorTransferBarrier": transition[0],
                "stagingWriteTransaction": staging_barrier[0],
                "maxHdrError": worst, "preservedAlpha": .625, "checkedReadbackBytes": checked_bytes,
                "exactCapturedReadback": True, "image": image_path,
                "displayTransform": "captured Graphics HDR output; diagnostic Reinhard exposure1 then sRGB"})
        controller.SetFrameEvent(actions[-1].eventId, True)
        report["debugMessages"] = [{"severity": str(m.severity), "description": m.description} for m in controller.GetDebugMessages()]
        assert not report["debugMessages"], report["debugMessages"]
        report["success"] = True
        with open(os.path.splitext(path)[0] + ".json", "w") as output:
            json.dump(report, output, indent=2)
    finally:
        controller.Shutdown()
        cap.Shutdown()


try:
    main()
    os._exit(0)
except Exception:
    with open(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"] + ".error.txt", "w") as output:
        output.write(traceback.format_exc())
    os._exit(1)
