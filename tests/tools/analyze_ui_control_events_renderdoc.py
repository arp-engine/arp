"""Replay Events/1 before/after captures using the shared UI text capture helpers."""
import os
import sys
sys.path.insert(0, os.environ["ARP_UI_EVENTS_TOOLS"])
with open(os.path.join(os.environ["ARP_UI_EVENTS_EVIDENCE"], "analysis_started.txt"), "w") as started:
    started.write("replay started\n")
import renderdoc as rd
from analyze_ui_text_renderdoc import (
    flatten, action_name, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp,
)


def replay(path):
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(path, "", None) != rd.ResultCode.Succeeded:
        raise RuntimeError("cannot open " + path)
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded or controller is None:
        capture.Shutdown()
        raise RuntimeError("cannot replay " + str(status))
    try:
        actions = (controller.GetDrawcalls() if hasattr(controller, "GetDrawcalls")
                   else controller.GetRootActions())
        draws = [a for a in flatten(actions) if "DrawIndexed" in action_name(a, controller)]
        if not draws:
            raise RuntimeError("capture contains no GUI draws")
        controller.SetFrameEvent(draws[-1].eventId, True)
        pipe = controller.GetPipelineState()
        rid = descriptor_resource(pipe.GetOutputTargets()[0])
        textures = {t.resourceId: t for t in controller.GetTextures()}
        target = textures[rid]
        pixels = rgba8_pixels(texture_bytes(controller, rid), str(target.format))
        if len(pixels) != target.width * target.height:
            raise RuntimeError("expected tightly packed RGBA8/BGRA8 target")
        reflection = pipe.GetShaderReflection(rd.ShaderStage.Fragment)
        if reflection is None or not reflection.readOnlyResources:
            raise RuntimeError("GUI fragment shader has no atlas resource")
        errors = [m for m in controller.GetDebugMessages()
                  if "Error" in str(m.severity) or "High" in str(m.severity)]
        if errors:
            raise RuntimeError("replay debug errors: " + str(len(errors)))
        screenshot = os.path.splitext(path)[0] + ".bmp"
        write_bmp(screenshot, target.width, target.height, pixels)
        return target.width, target.height, pixels, len(draws), screenshot
    finally:
        controller.Shutdown()
        capture.Shutdown()


def main():
    evidence = os.environ["ARP_UI_EVENTS_EVIDENCE"]
    before = replay(os.path.join(evidence, "before_capture.rdc"))
    after = replay(os.path.join(evidence, "after_capture.rdc"))
    if before[:2] != after[:2]:
        raise RuntimeError("capture extents differ")
    width, height = before[:2]
    # Regions come directly from examples/ui/control_events.arpui, not image heuristics.
    regions = [("button_status", 220, 20, 480, 36),
               ("slider_status", 220, 85, 480, 36),
               ("text_status", 220, 150, 480, 36),
               ("text_input", 20, 150, 160, 36)]
    lines = ["extent=%dx%d" % (width, height),
             "indexed_draws_before=%d after=%d" % (before[3], after[3]),
             "replay_debug_errors=0", "before_screenshot=" + before[4],
             "after_screenshot=" + after[4]]
    for name, x, y, w, h in regions:
        changed = sum(before[2][row * width + col] != after[2][row * width + col]
                      for row in range(y, y + h) for col in range(x, x + w))
        if not changed:
            raise RuntimeError("input did not change " + name)
        lines.append("%s changed_pixels=%d" % (name, changed))
    with open(os.path.join(evidence, "analysis.txt"), "w", encoding="utf-8") as output:
        output.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if os.environ.get("ARP_UI_EVENTS_EVIDENCE"):
    try:
        main()
    except Exception as error:
        with open(os.path.join(os.environ["ARP_UI_EVENTS_EVIDENCE"], "analysis_error.txt"),
                  "w", encoding="utf-8") as output:
            output.write(str(error))
        raise
