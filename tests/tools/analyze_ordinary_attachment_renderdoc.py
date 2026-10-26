"""Verify the real /4 ordinary encoder's exact attachment cells and barriers."""
import json
import os
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tools"))
from analyze_ui_text_renderdoc import flatten, write_bmp


def fields(node):
    return {node.GetChild(i).name: node.GetChild(i) for i in range(node.NumChildren())}


def number(node):
    return int(node.data.basic.u)


def succeeded(status):
    return status.code == rd.ResultCode.Succeeded if hasattr(status, "code") else status == rd.ReplayStatus.Succeeded


def main():
    path = os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]
    cap = rd.OpenCaptureFile()
    assert succeeded(cap.OpenFile(path, "", None))
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    assert succeeded(status) and controller
    try:
        actions = list(flatten(controller.GetRootActions()))
        structured = controller.GetStructuredFile()
        begins, ends, barriers, visited = [], [], [], set()
        for action in actions:
            for event in action.events:
                if event.eventId in visited:
                    continue
                visited.add(event.eventId)
                chunk = structured.chunks[event.chunkIndex]
                if chunk.name == "vkCmdBeginRenderPass":
                    begins.append(event.eventId)
                elif chunk.name == "vkCmdEndRenderPass":
                    ends.append(event.eventId)
                elif chunk.name == "vkCmdPipelineBarrier":
                    native = fields(chunk)
                    array = native["pImageMemoryBarriers"]
                    for i in range(array.NumChildren()):
                        barrier = fields(array.GetChild(i))
                        subresource = fields(barrier["subresourceRange"])
                        barriers.append({"event": event.eventId,
                            "sourceStages": number(native["srcStageMask"]),
                            "destinationStages": number(native["destStageMask"]),
                            **{k: number(barrier[k]) for k in ("image", "oldLayout", "newLayout", "srcAccessMask", "dstAccessMask")},
                            **{k: number(subresource[k]) for k in ("aspectMask", "baseMipLevel", "levelCount", "baseArrayLayer", "layerCount")}})
        assert len(begins) == len(ends) == 2, (begins, ends)
        assert len(barriers) == 4, barriers
        assert not [a for a in actions if a.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch)]
        assert max(b["event"] for b in barriers[:2]) < begins[0] < ends[0]
        assert ends[0] < min(b["event"] for b in barriers[2:]) <= max(b["event"] for b in barriers[2:]) < begins[1] < ends[1]
        for b in barriers:
            assert (b["baseMipLevel"], b["levelCount"], b["baseArrayLayer"], b["layerCount"]) == (1, 1, 2, 1)
            assert b["aspectMask"] in (1, 2)
            layout, access = (2, 0x180) if b["aspectMask"] == 1 else (3, 0x600)
            initial = b["event"] < begins[0]
            assert b["oldLayout"] == (0 if initial else layout) and b["newLayout"] == layout
            assert b["srcAccessMask"] == (0 if initial else access) and b["dstAccessMask"] == access
            assert b["sourceStages"] == (1 if initial else 0x700) and b["destinationStages"] == 0x700
        report = {"contract": "OriginalRenderPassPreparation/4",
            "scope": "private ordinary encoder, original Program/Inputs/Sources, two real clear/store passes; no draws or cloud feature",
            "capture": path, "beginEvents": begins, "endEvents": ends, "barriers": barriers, "cells": []}
        for begin, end in zip(begins, ends):
            controller.SetFrameEvent(begin, True)
            pipeline = controller.GetPipelineState()
            targets = [(t.resource, (0.125, 0.375, 0.75, 0.625), "<4e", 8, "color")
                for t in pipeline.GetOutputTargets() if t.resource != rd.ResourceId.Null()]
            targets.append((pipeline.GetDepthTarget().resource, (0.25,), "<f", 4, "depth"))
            assert len(targets) == 2
            controller.SetFrameEvent(end, True)
            for resource, expected, packing, stride, kind in targets:
                data = bytes(controller.GetTextureData(resource, rd.Subresource(1, 2, 0)))
                assert len(data) == 16 * stride, (kind, len(data))
                pixels = list(struct.iter_unpack(packing, data))
                error = max(abs(a - b) for pixel in pixels for a, b in zip(pixel, expected))
                assert error == 0.0, (kind, pixels, error)
                display = [tuple(round(v * 255) for v in (pixel[:3] if kind == "color" else pixel * 3)) + (255,) for pixel in pixels]
                image = os.path.splitext(path)[0] + ".%d.%s.bmp" % (end, kind)
                write_bmp(image, 4, 4, display)
                report["cells"].append({"event": end, "kind": kind, "resource": str(resource),
                    "mip": 1, "layer": 2, "texels": 16, "expected": expected,
                    "maxError": error, "checkedBytes": len(data), "image": image,
                    "displayTransform": "linear diagnostic RGB times255; depth replicated into RGB"})
        controller.SetFrameEvent(actions[-1].eventId, True)
        report["debugMessages"] = [{"severity": str(m.severity), "description": m.description} for m in controller.GetDebugMessages()]
        assert not report["debugMessages"], report["debugMessages"]
        report["success"] = True
        with open(os.path.splitext(path)[0] + ".json", "w", encoding="utf-8") as output:
            json.dump(report, output, indent=2)
    finally:
        controller.Shutdown()
        cap.Shutdown()


try:
    main()
    os._exit(0)
except Exception:
    with open(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"] + ".error.txt", "w", encoding="utf-8") as output:
        output.write(traceback.format_exc())
    os._exit(1)
