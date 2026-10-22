"""Run via qrenderdoc --python; ARP_EDITOR_TERRAIN_CAPTURE and SOURCE are explicit."""
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_ui_text_renderdoc import action_name, descriptor_resource, flatten, texture_bytes, write_bmp

capture_path = Path(os.environ["ARP_EDITOR_TERRAIN_CAPTURE"])
source = Path(os.environ["ARP_EDITOR_TERRAIN_SOURCE"])
stem = str(capture_path.with_suffix(""))
report = {"capture": str(capture_path), "source": str(source), "tiles": []}
controller = None
capture = rd.OpenCaptureFile()
try:
    assert capture.OpenFile(str(capture_path), "", None) == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status == rd.ResultCode.Succeeded, str(status)
    roots = controller.GetDrawcalls() if hasattr(controller, "GetDrawcalls") else controller.GetRootActions()
    actions = flatten(roots)
    draws = [a for a in actions if "DrawIndexed" in action_name(a, controller) and a.numIndices == 98304]
    assert len(draws) == 16, len(draws)
    textures = {t.resourceId: t for t in controller.GetTextures()}
    for index, draw in enumerate(draws):
        controller.SetFrameEvent(draw.eventId, True)
        pipe = controller.GetPipelineState()
        vb = pipe.GetVBuffers()[0]
        vertices = bytes(controller.GetBufferData(vb.resourceId, vb.byteOffset, 16641 * 48))
        heights = [struct.unpack_from("<f", vertices, i * 48 + 4)[0] for i in range(16641)]
        expected_bytes = (source / "tiles" / ("%d_%d.height.bin" % (index % 4, index // 4))).read_bytes()
        expected = [v[0] for v in struct.iter_unpack("<f", expected_bytes)]
        assert len(expected) == len(heights)
        error = max(abs(a - b) for a, b in zip(expected, heights))
        assert error < 0.0001, error
        uvs = [struct.unpack_from("<2f", vertices, i * 48 + 24) for i in (0, 128, 16512, 16640)]
        assert abs(uvs[0][0] - 0.5 / 512) < 1e-7
        assert abs(uvs[-1][1] - (511.5 / 512)) < 1e-7
        resources = []
        for entry in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment, True):
            if hasattr(entry, "resources"):
                resources.extend(descriptor_resource(v) for v in entry.resources)
            else:
                resources.append(descriptor_resource(entry.descriptor))
        assert len(resources) == 5, resources
        material = []
        for rid in resources:
            texture = textures[rid]
            assert texture.width == 512 and texture.height == 512
            raw = texture_bytes(controller, rid)
            material.append({"id": str(rid), "format": texture.format.Name(),
                             "unique_rgba8": len(set(struct.iter_unpack("<4B", raw)))})
        report["tiles"].append({"eid": draw.eventId, "indices": draw.numIndices,
            "vertices": len(heights), "height_range": [min(heights), max(heights)],
            "max_height_error": error, "textures": material})
    last = draws[-1]
    controller.SetFrameEvent(last.eventId, True)
    pipe = controller.GetPipelineState()
    color = descriptor_resource(pipe.GetOutputTargets()[0])
    depth = descriptor_resource(pipe.GetDepthTarget())
    target = textures[color]
    rgba = list(struct.iter_unpack("<4e", texture_bytes(controller, color)))
    assert all(math.isfinite(v) for pixel in rgba for v in pixel)
    depth_values = [v[0] for v in struct.iter_unpack("<f", texture_bytes(controller, depth))]
    written = [v for v in depth_values if math.isfinite(v) and v < 1]
    assert written
    pixels = []
    for pixel in rgba:
        rgb = []
        for channel in pixel[:3]:
            mapped = max(0, channel) / (1 + max(0, channel))
            encoded = mapped * 12.92 if mapped <= 0.0031308 else 1.055 * mapped ** (1 / 2.4) - 0.055
            rgb.append(round(max(0, min(1, encoded)) * 255))
        pixels.append((*rgb, 255))
    write_bmp(stem + "-terrain.bmp", target.width, target.height, pixels)
    errors = [str(m) for m in controller.GetDebugMessages()
              if "High" in str(m.severity) or "Error" in str(m.severity)]
    assert not errors, errors
    report.update({"result": "PASS", "draws": len(draws), "extent": [target.width, target.height],
        "depth_pixels": len(written), "depth_range": [min(written), max(written)],
        "unique_color": len(set(pixels)), "replay_high_or_error": errors})
except Exception:
    report.update({"result": "FAIL", "error": traceback.format_exc()})
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
    Path(stem + "-terrain-analysis.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
os._exit(0 if report.get("result") == "PASS" else 1)
