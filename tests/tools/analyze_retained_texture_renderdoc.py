"""Verify the private retained texture slice using real Vulkan capture events."""
import json
import os
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tools"))
from analyze_ui_text_renderdoc import flatten, texture_bytes, write_bmp


def fields(node):
    return {node.GetChild(i).name: node.GetChild(i) for i in range(node.NumChildren())}


def number(node):
    return int(node.data.basic.u)


def succeeded(status):
    return status.code == rd.ResultCode.Succeeded if hasattr(status, "code") else status == rd.ReplayStatus.Succeeded


def main():
    path = os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]
    capture = rd.OpenCaptureFile()
    assert succeeded(capture.OpenFile(path, "", None))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert succeeded(status) and controller
    try:
        structured = controller.GetStructuredFile()
        actions = list(flatten(controller.GetRootActions()))
        textures = {t.resourceId: t for t in controller.GetTextures()}
        barriers, copies, begins, visited = [], {}, {}, set()
        require_clear = os.environ.get("ARP_RETAINED_TEXTURE_REQUIRE_CLEAR") == "1"
        for action in actions:
            for event in action.events:
                if event.eventId in visited:
                    continue
                visited.add(event.eventId)
                chunk = structured.chunks[event.chunkIndex]
                if chunk.name == "vkCmdPipelineBarrier":
                    native = fields(chunk)
                    array = native["pImageMemoryBarriers"]
                    for i in range(array.NumChildren()):
                        item = fields(array.GetChild(i))
                        barriers.append({"event": event.eventId,
                            "commandBuffer": number(native["commandBuffer"]),
                            "sourceStages": number(native["srcStageMask"]),
                            "destinationStages": number(native["destStageMask"]),
                            **{k: number(item[k]) for k in
                                ("image", "srcAccessMask", "dstAccessMask", "oldLayout", "newLayout")},
                            "range": {k: number(v) for k, v in fields(item["subresourceRange"]).items()}})
                if chunk.name == "vkCmdCopyImageToBuffer":
                    native = fields(chunk)
                    regions = native["pRegions"]
                    assert regions.NumChildren() == 1
                    region = fields(regions.GetChild(0))
                    copies[event.eventId] = {"commandBuffer": number(native["commandBuffer"]),
                        "source": number(native["srcImage"]),
                        "destination": number(native["destBuffer"]),
                        "sourceLayout": number(native["srcImageLayout"]),
                        **{k: number(region[k]) for k in
                            ("bufferOffset", "bufferRowLength", "bufferImageHeight")},
                        **{k: {key: number(value) for key, value in fields(region[k]).items()}
                            for k in ("imageSubresource", "imageOffset", "imageExtent")}}
                if chunk.name == "vkCmdBeginRenderPass":
                    begins[event.eventId] = number(fields(chunk)["commandBuffer"])
        copied = [a for a in actions if a.flags & rd.ActionFlags.Copy and a.copySource in textures]
        assert len(copied) == len(copies) == 4, (len(copied), len(copies))
        report = {"contract": "PrivateRetainedTexture/1", "capture": path,
            "scope": ("cold prepared private Clear/Store + transitions/readback; no Graphics shader/draw claim"
                if require_clear else "cold ordinary clear; bounded private texture transitions/readback only; no bounded Graphics claim"),
            "copies": []}
        assert not any(a.flags & rd.ActionFlags.Drawcall for a in actions)
        report["actualDrawCalls"] = 0
        formats = {}
        for action in copied:
            controller.SetFrameEvent(action.eventId, True)
            native = copies[action.eventId]
            texture = textures[action.copySource]
            depth = texture.format.Name() == "D32"
            assert depth or texture.format.Name() == "R16G16B16A16_FLOAT", texture.format.Name()
            assert (texture.width, texture.height, texture.depth, texture.mips, texture.arraysize) == (5, 3, 1, 2, 2)
            bpp = 4 if depth else 8
            pixel = (struct.pack("<f", .75 if require_clear else .25) if depth else
                struct.pack("<4e", *(.5, .25, 1, .75) if require_clear else (.25, .5, .75, 1)))
            image = texture_bytes(controller, action.copySource)
            assert image == pixel * 15
            data = bytes(controller.GetBufferData(action.copyDestination, 0, 1024))
            expected = bytearray(b"\xa5" * 1024)
            for y in range(3):
                offset = 256 + y * 256
                expected[offset:offset + 5 * bpp] = pixel * 5
            assert data == expected, (action.eventId, len(data))
            aspect = 2 if depth else 1
            assert native == {"commandBuffer": native["commandBuffer"],
                "source": int(action.copySource), "destination": int(action.copyDestination),
                "sourceLayout": 6, "bufferOffset": 256, "bufferRowLength": 256 // bpp, "bufferImageHeight": 4,
                "imageSubresource": {"aspectMask": aspect, "mipLevel": 0, "baseArrayLayer": 0, "layerCount": 1},
                "imageOffset": {"x": 0, "y": 0, "z": 0}, "imageExtent": {"width": 5, "height": 3, "depth": 1}}
            expected_range = {"aspectMask": aspect, "baseMipLevel": 0, "levelCount": 1,
                "baseArrayLayer": 0, "layerCount": 1}
            enter = [b for b in barriers if b["image"] == int(action.copySource) and b["event"] < action.eventId
                and b["oldLayout"] == 5 and b["newLayout"] == 6
                and b["sourceStages"] == 0x800 and b["destinationStages"] == 0x1000
                and b["srcAccessMask"] == 0x20 and b["dstAccessMask"] == 0x800
                and b["range"] == expected_range]
            leave = [b for b in barriers if b["image"] == int(action.copySource) and b["event"] > action.eventId
                and b["oldLayout"] == 6 and b["newLayout"] == 5
                and b["sourceStages"] == 0x1000 and b["destinationStages"] == 0x800
                and b["srcAccessMask"] == 0x800 and b["dstAccessMask"] == 0x20
                and b["range"] == expected_range]
            assert len(enter) == len(leave) == 1
            assert enter[0]["commandBuffer"] == leave[0]["commandBuffer"] == native["commandBuffer"]
            clear_evidence = None
            if require_clear:
                writer_stage, writer_access, layout = (0x300, 0x400, 3) if depth else (0x400, 0x100, 2)
                before_clear = [b for b in barriers if b["image"] == int(action.copySource)
                    and b["commandBuffer"] == native["commandBuffer"] and b["event"] < enter[0]["event"]
                    and b["oldLayout"] == 5 and b["newLayout"] == layout
                    and b["sourceStages"] == 0x800 and b["destinationStages"] == writer_stage
                    and b["srcAccessMask"] == 0x20 and b["dstAccessMask"] == writer_access
                    and b["range"] == expected_range]
                assert len(before_clear) == 1
                # Native command buffers are reused after actual completion.
                # Bound this invocation by its exact original image barriers.
                prepared = [a for a in actions if before_clear[0]["event"] < a.eventId < enter[0]["event"]
                    and begins.get(a.eventId) == native["commandBuffer"]]
                assert len(prepared) == 1, (action.eventId, native["commandBuffer"], begins,
                    [(a.eventId, str(a.flags), a.GetName(structured)) for a in prepared])
                clear = prepared[0]
                controller.SetFrameEvent(clear.eventId, True)
                pipeline = controller.GetPipelineState()
                selected = pipeline.GetDepthTarget() if depth else pipeline.GetOutputTargets()[0]
                assert selected.resource == action.copySource
                assert texture_bytes(controller, action.copySource) == pixel * 15
                after_clear = [b for b in barriers if b["image"] == int(action.copySource)
                    and b["commandBuffer"] == native["commandBuffer"] and clear.eventId < b["event"] < action.eventId
                    and b["oldLayout"] == layout and b["newLayout"] == 5
                    and b["sourceStages"] == writer_stage and b["destinationStages"] == 0x800
                    and b["srcAccessMask"] == writer_access and b["dstAccessMask"] == 0x20
                    and b["range"] == expected_range]
                assert len(before_clear) == len(after_clear) == 1
                clear_evidence = {"event": clear.eventId, "exactClearedTexels": True,
                    "before": before_clear[0], "after": after_clear[0]}
            name = texture.format.Name()
            formats[name] = formats.get(name, 0) + 1
            values = [v * 3 if depth else v[:3] for v in struct.iter_unpack("<f" if depth else "<4e", image)]
            pixels = [tuple(round(c * 255) for c in v) + (255,) for v in values]
            output = os.path.splitext(path)[0] + ".event%d.bmp" % action.eventId
            write_bmp(output, 5, 3, pixels)
            report["copies"].append({"event": action.eventId, "format": name,
                "native": native, "enter": enter[0], "leave": leave[0],
                "preparedClear": clear_evidence,
                "exactKnownTexels": True, "paddingAndOffsetPreserved": True,
                "checkedBufferBytes": 1024, "copiedTexelBytes": len(image),
                "image": output, "displayTransform": "linear values times 255; D32 grayscale"})
        assert sorted(formats.values()) == [2, 2]
        controller.SetFrameEvent(actions[-1].eventId, True)
        report["debugMessages"] = [{"severity": str(m.severity), "description": m.description}
            for m in controller.GetDebugMessages()]
        assert not report["debugMessages"], report["debugMessages"]
        report["success"] = True
        with open(os.path.splitext(path)[0] + ".json", "w") as output:
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
