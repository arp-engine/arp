"""Check EditorViewportEnvironment/2 draws in an offscreen RenderDoc capture.

Run in qrenderdoc --python; set ARP_EDITOR_ENVIRONMENT_CAPTURE to the .rdc path.
The normal viewport analyzer exports the attachment image separately.
"""
import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests" / "tools"))
import renderdoc as rd
from analyze_ui_text_renderdoc import action_name, descriptor_resource, flatten, texture_bytes


def analyze(path):
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        assert capture.OpenFile(path, "", None) == rd.ResultCode.Succeeded
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded
        draws = [a for a in flatten(controller.GetRootActions())
                 if "Draw" in action_name(a, controller)]
        assert len(draws) >= 3, "Expected sky, scene and grid draws"
        assert "DrawIndexed" not in action_name(draws[0], controller)
        assert "DrawIndexed" not in action_name(draws[-1], controller)
        assert draws[0].numIndices == 3 and draws[-1].numIndices == 3

        controller.SetFrameEvent(draws[-2].eventId, True)
        pipe = controller.GetPipelineState()
        color = descriptor_resource(pipe.GetOutputTargets()[0])
        depth = descriptor_resource(pipe.GetDepthTarget())
        texture = next(t for t in controller.GetTextures() if t.resourceId == color)
        assert "R16G16B16A16" in texture.format.Name().upper()
        before = texture_bytes(controller, color)
        depth_before = texture_bytes(controller, depth)

        controller.SetFrameEvent(draws[-1].eventId, True)
        pipe = controller.GetPipelineState()
        vk = controller.GetVulkanPipelineState()
        assert vk.depthStencil.depthTestEnable and not vk.depthStencil.depthWriteEnable
        assert "LessEqual" in str(vk.depthStencil.depthFunction)
        assert pipe.GetColorBlends()[0].enabled
        push = struct.unpack_from("<44f", bytes(vk.pushconsts))
        assert push[24:] == (1.0, 10.0, 100.0, 1.0)
        after = texture_bytes(controller, color)
        depth_after = texture_bytes(controller, depth)
        assert depth_before == depth_after, "Grid changed scene depth"
        assert before != after, "Grid did not change any color pixels"

        blocked = 0
        visible_changes = 0
        for y in range(0, texture.height, 2):
            for x in range(0, texture.width, 2):
                uv = ((x + 0.5) / texture.width, (y + 0.5) / texture.height)
                ray = [push[4 + i] + push[8 + i] * uv[0] + push[12 + i] * uv[1]
                       for i in range(3)]
                if abs(ray[1]) < 1e-6:
                    continue
                distance = -push[1] / ray[1]
                if distance <= 0:
                    continue
                hit = [push[i] + ray[i] * distance for i in range(3)] + [1]
                denominator = sum(push[20 + i] * hit[i] for i in range(4))
                if denominator <= 0:
                    continue
                grid_depth = sum(push[16 + i] * hit[i] for i in range(4)) / denominator
                index = y * texture.width + x
                scene_depth = struct.unpack_from("<f", depth_before, index * 4)[0]
                a = struct.unpack_from("<4e", before, index * 8)
                b = struct.unpack_from("<4e", after, index * 8)
                assert all(math.isfinite(v) for v in b)
                changed = max(abs(a[i] - b[i]) for i in range(3)) > 0.001
                if scene_depth < grid_depth - 1e-6:
                    assert not changed, "Grid drew over foreground geometry at %d,%d" % (x, y)
                    blocked += 1
                elif changed:
                    visible_changes += 1
        assert blocked > 10, "No foreground occlusion samples"
        assert visible_changes > 100, "No visible grid samples"
        errors = [m for m in controller.GetDebugMessages()
                  if "Error" in str(m.severity) or "High" in str(m.severity)]
        assert not errors, str(errors)
        report = ["contract=EditorViewportEnvironment/2", "draws=%d" % len(draws),
                  "sky_event=%d" % draws[0].eventId, "grid_event=%d" % draws[-1].eventId,
                  "grid_push_bytes=176", "depth_test=LessEqual", "depth_write=false",
                  "alpha_blend=true", "depth_unchanged=true",
                  "foreground_occlusion_samples=%d" % blocked,
                  "visible_grid_samples=%d" % visible_changes, "replay_high_or_error=0"]
        Path(path).with_suffix(".environment.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()


if __name__ == "__main__":
    analyze(os.environ["ARP_EDITOR_ENVIRONMENT_CAPTURE"])
