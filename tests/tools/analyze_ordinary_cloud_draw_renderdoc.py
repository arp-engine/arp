"""Check the original ordinary Preparation/5 cloud Draw, bindings and HDR output."""
import json
import math
import os
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tools"))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, write_bmp
from renderdoc_cloud_common import fields, number, succeeded, resource, display


def main():
    path = os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]
    cap = rd.OpenCaptureFile()
    assert succeeded(cap.OpenFile(path, "", None))
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    assert succeeded(status) and controller
    try:
        actions = list(flatten(controller.GetRootActions()))
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
        assert len(draws) == 1, len(draws)
        assert not [a for a in actions if a.flags & rd.ActionFlags.Dispatch]
        structured = controller.GetStructuredFile()
        visited, native_draws, barriers, begins, ends = set(), [], [], [], []
        command_buffers, descriptor_binds, pipeline_binds = set(), [], []
        for action in actions:
            for event in action.events:
                if event.eventId in visited:
                    continue
                visited.add(event.eventId)
                chunk = structured.chunks[event.chunkIndex]
                if chunk.name in ("vkCmdBeginRenderPass", "vkCmdEndRenderPass", "vkCmdDraw",
                        "vkCmdPipelineBarrier", "vkCmdBindPipeline", "vkCmdBindDescriptorSets",
                        "vkCmdSetViewport", "vkCmdSetScissor"):
                    command_buffers.add(number(fields(chunk)["commandBuffer"]))
                if chunk.name == "vkCmdBeginRenderPass":
                    begins.append(event.eventId)
                elif chunk.name == "vkCmdEndRenderPass":
                    ends.append(event.eventId)
                elif chunk.name == "vkCmdDraw":
                    native = fields(chunk)
                    native_draws.append({"event": event.eventId,
                        **{k: number(native[k]) for k in ("commandBuffer", "vertexCount", "instanceCount", "firstVertex", "firstInstance")}})
                elif chunk.name == "vkCmdBindDescriptorSets":
                    binding = fields(chunk)
                    descriptor_binds.append({"event": event.eventId,
                        **{k: number(binding[k]) for k in ("pipelineBindPoint", "firstSet", "dynamicOffsetCount")},
                        "descriptorSetCount": number(binding["setCount"])})
                elif chunk.name == "vkCmdBindPipeline":
                    pipeline_binds.append(number(fields(chunk)["pipelineBindPoint"]))
                elif chunk.name == "vkCmdPipelineBarrier":
                    native = fields(chunk)
                    array = native["pImageMemoryBarriers"]
                    for index in range(array.NumChildren()):
                        barrier = fields(array.GetChild(index))
                        barriers.append({"event": event.eventId,
                            **{k: number(barrier[k]) for k in ("image", "oldLayout", "newLayout", "srcAccessMask", "dstAccessMask")}})
        assert len(begins) == len(ends) == 2 and len(barriers) == 5
        assert len(native_draws) == 1
        native = native_draws[0]
        assert command_buffers == {native["commandBuffer"]}, command_buffers
        assert pipeline_binds == [0]
        assert len(descriptor_binds) == 1
        binding = descriptor_binds[0]
        assert [binding[k] for k in ("pipelineBindPoint", "firstSet", "descriptorSetCount", "dynamicOffsetCount")] == [0, 0, 1, 0]
        assert [native[k] for k in ("vertexCount", "instanceCount", "firstVertex", "firstInstance")] == [3, 1, 0, 0]
        draw = draws[0]
        assert begins[0] < ends[0] < begins[1] < draw.eventId < ends[1]
        assert begins[1] < binding["event"] < draw.eventId
        controller.SetFrameEvent(draw.eventId, True)
        pipeline = controller.GetPipelineState()
        fs = pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
        vs = pipeline.GetShaderReflection(rd.ShaderStage.Vertex)
        assert fs.entryPoint == "fragmentMain" and vs.entryPoint == "vertexMain"
        constants = [b.descriptor for b in pipeline.GetConstantBlocks(rd.ShaderStage.Fragment)
            if fs.constantBlocks[b.access.index].fixedBindNumber == 0]
        assert len(constants) == 1
        cb = constants[0]
        constant_bytes = bytes(controller.GetBufferData(cb.resource, cb.byteOffset, cb.byteSize))
        assert len(constant_bytes) == 368
        assert struct.unpack_from("<4I", constant_bytes, 272) == (1, 1, 4, 4)
        samples = resource(pipeline, fs, 1)
        sample_bytes = bytes(controller.GetBufferData(samples.resource, samples.byteOffset, 32))
        assert struct.unpack_from("<4f", sample_bytes) == (.25, .5, .75, .5)
        scene, depth = resource(pipeline, fs, 2), resource(pipeline, fs, 3)
        target = descriptor_resource(pipeline.GetOutputTargets()[0])
        assert len({target, scene.resource, depth.resource}) == 3
        assert texture_bytes(controller, scene.resource) == struct.pack("<4e", .125, .375, .75, .625) * 16
        assert texture_bytes(controller, depth.resource) == struct.pack("<f", 1.) * 16
        for sampled, attachment_layout in ((scene, 2), (depth, 3)):
            dependency = [b for b in barriers if b["image"] == int(sampled.resource)
                and ends[0] < b["event"] < begins[1]
                and b["oldLayout"] == attachment_layout and b["newLayout"] == 5
                and b["dstAccessMask"] == 0x20]
            assert len(dependency) == 1, (sampled.resource, dependency)
        pixels = list(struct.iter_unpack("<4e", texture_bytes(controller, target)))
        assert len(pixels) == 16
        expected = (.3125, .6875, 1.125, .625)
        error = max(abs(a - b) for pixel in pixels for a, b in zip(pixel, expected))
        assert error == 0.0 and all(all(math.isfinite(v) for v in p) for p in pixels), (pixels, error)
        output = os.path.splitext(path)[0] + ".output.bmp"
        # Enlarge the actual pixels for inspection, using the same explicit
        # Reinhard/sRGB display transform as the retained-reference analyzer.
        image = [tuple(display(v) for v in pixels[(y // 64) * 4 + x // 64][:3]) + (255,)
            for y in range(256) for x in range(256)]
        write_bmp(output, 256, 256, image)
        controller.SetFrameEvent(actions[-1].eventId, True)
        messages = [{"severity": str(m.severity), "description": m.description}
            for m in controller.GetDebugMessages()]
        assert not messages, messages
        report = {"contract": "OriginalRenderPassPreparation/5", "capture": path,
            "scope": "original ordinary cloud composite Draw; seeded immutable sample, not raymarch or complete cloud workload",
            "success": True, "actualDrawCalls": 1, "nativeDraw": native,
            "commandBuffers": sorted(command_buffers), "descriptorBinds": descriptor_binds,
            "beginEvents": begins, "endEvents": ends, "barriers": barriers,
            "bindings": {"constantBytes": len(constant_bytes), "constantOffset": cb.byteOffset,
                "sampleOffset": samples.byteOffset, "scene": str(scene.resource), "depth": str(depth.resource)},
            "output": str(target), "checkedTexels": 16, "checkedBytes": 128,
            "expected": expected, "maxError": error, "debugMessages": messages,
            "image": output, "displayTransform": "Reinhard then sRGB; nearest 64x enlargement"}
        with open(os.path.splitext(path)[0] + ".json", "w", encoding="utf-8") as destination:
            json.dump(report, destination, indent=2)
        if os.path.exists(path + ".error.txt"):
            os.remove(path + ".error.txt")
    finally:
        controller.Shutdown()
        cap.Shutdown()


if __name__ == "__main__":
    try:
        main()
        os._exit(0)
    except Exception:
        with open(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"] + ".error.txt", "w", encoding="utf-8") as destination:
            destination.write(traceback.format_exc())
        os._exit(1)
