"""Verify formal cooked payload and CPU/GPU periodic slices in original RHI."""
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ["ARP_REPO_ROOT"]) / "tests/tools"))
from analyze_ordinary_mixed_cloud_renderdoc import binding
from analyze_ui_text_renderdoc import flatten, write_bmp
from renderdoc_cloud_common import succeeded


def main():
    path = Path(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"])
    evidence = Path(os.environ["ARP_CLOUD_SLICE_EVIDENCE_DIR"])
    cells = (evidence / "cooked-cells.bin").read_bytes()
    capture = rd.OpenCaptureFile()
    assert succeeded(capture.OpenFile(str(path), "", None))
    options = rd.ReplayOptions()
    options.optimisation = rd.ReplayOptimisationLevel.NoOptimisation
    options.apiValidation = True
    status, controller = capture.OpenCapture(options, None)
    assert succeeded(status) and controller
    report = {"contract": "CloudAssetSlice/1", "scope": "Formal typed cooked resource; original Sources/Program/FrameGraph; periodic resource samples only, no scene quality or platform claim",
              "capture": str(path), "captureSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
              "cookedPayloadSha256": hashlib.sha256(cells).hexdigest(), "slices": []}
    try:
        dispatches = [a for a in flatten(controller.GetRootActions())
                      if a.flags & rd.ActionFlags.Dispatch]
        assert len(dispatches) == 3
        for kind, action in zip(("weather", "shape", "detail"), dispatches):
            controller.SetFrameEvent(action.eventId, True)
            pipeline = controller.GetPipelineState()
            stage = rd.ShaderStage.Compute
            reflection = pipeline.GetShaderReflection(stage)
            block, = pipeline.GetConstantBlocks(stage)
            descriptor = block.descriptor
            parameters = bytes(controller.GetBufferData(descriptor.resource, descriptor.byteOffset, 48))
            assert parameters == (evidence / (kind + ".parameters.bin")).read_bytes()
            width, height, offset, dim = struct.unpack_from("<4I", parameters)
            assert width == height == 4 * dim + 1
            assert struct.unpack_from("<4f", parameters, 16) == (-1, -1, 1 / (2 * dim), .375)
            assert struct.unpack_from("<4I", parameters, 32) == (int(kind == "weather"), 0, 0, 0)
            density = binding(pipeline, stage, reflection, 1)
            assert density.byteOffset == 0 and density.byteSize == len(cells)
            assert bytes(controller.GetBufferData(density.resource, 0, len(cells))) == cells
            output = binding(pipeline, stage, reflection, 3)
            expected = (evidence / (kind + ".expected.bin")).read_bytes()
            assert output.byteOffset == 0 and output.byteSize == width * height * 16 == len(expected)
            actual = bytes(controller.GetBufferData(output.resource, 0, len(expected)))
            assert actual == (evidence / (kind + ".actual.bin")).read_bytes()
            values = list(struct.iter_unpack("<4f", actual))
            reference = list(struct.iter_unpack("<4f", expected))
            assert all(math.isfinite(x) and 0 <= x <= 1 for v in values for x in v)
            error = max(abs(a - b) for v, r in zip(values, reference) for a, b in zip(v, r))
            assert error < .0001
            for y in range(height):
                for x in range(2 * dim + 1):
                    assert values[y * width + x] == values[y * width + x + 2 * dim]
                    assert values[x * width + y] == values[(x + 2 * dim) * width + y]
            minimum, maximum = min(v[0] for v in values), max(v[0] for v in values)
            assert maximum - minimum > .01
            image = path.with_name(path.stem + "-" + kind + ".bmp")
            pixels = [tuple(round(255 * x) for x in v[:3]) + (255,) if kind == "weather"
                      else (round(255 * v[0]),) * 3 + (255,) for v in values]
            write_bmp(str(image), width, height, pixels)
            report["slices"].append({"kind": kind, "event": action.eventId,
                "dimension": dim, "offsetCells": offset, "width": width, "height": height,
                "maxCpuGpuError": error, "minimum": minimum, "maximum": maximum,
                "exactRepeatXY": True, "outputSha256": hashlib.sha256(actual).hexdigest(),
                "image": str(image)})
        controller.SetFrameEvent(dispatches[-1].eventId, True)
        messages = controller.GetDebugMessages()
        report["debugMessages"] = [str(x.description) for x in messages]
        assert not messages, report["debugMessages"]
        report["success"] = True
        path.with_suffix(".json").write_text(json.dumps(report, indent=2))
    finally:
        controller.Shutdown()
        capture.Shutdown()


if __name__ == "__main__":
    try:
        main()
        os._exit(0)
    except Exception:
        Path(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"] + ".error.txt").write_text(traceback.format_exc())
        os._exit(1)
