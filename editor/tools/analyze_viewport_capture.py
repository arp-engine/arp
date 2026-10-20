import math
import os
import struct
import sys
import traceback

analysis_log = os.environ.get("ARP_EDITOR_RENDERDOC_ANALYSIS_LOG")


def log_step(value):
    if analysis_log:
        with open(analysis_log, "a", encoding="utf-8") as log:
            log.write(value + "\n")


if analysis_log:
    with open(analysis_log, "w", encoding="utf-8") as log:
        log.write("script-started\n")
log_step("standard-library-loaded")

repository_root = os.getcwd()
sys.path.insert(0, os.path.join(repository_root, "tests", "tools"))
log_step("repository-root=%s" % repository_root)

try:
    log_step("renderdoc-in-modules=%s" % ("renderdoc" in sys.modules))
    log_step("pyrenderdoc-in-globals=%s" % ("pyrenderdoc" in globals()))
    if "renderdoc" in sys.modules:
        rd = sys.modules["renderdoc"]
    elif "renderdoc" in globals():
        rd = globals()["renderdoc"]
    else:
        import renderdoc as rd
    log_step("renderdoc-imported")
    from analyze_ui_text_renderdoc import action_name, descriptor_resource, flatten, rgba8_pixels, resource_name, texture_bytes, write_bmp
    log_step("helpers-imported")
except Exception:
    if analysis_log:
        with open(analysis_log, "a", encoding="utf-8") as log:
            log.write(traceback.format_exc())
    raise


def main(capture_path):
    log_step("open-capture-start")
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        if capture.OpenFile(capture_path, "", None) != rd.ResultCode.Succeeded:
            raise RuntimeError("Cannot open viewport capture: " + capture_path)
        log_step("capture-opened")
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError("Cannot replay viewport capture: " + str(status))
        log_step("capture-replayed")

        actions = controller.GetDrawcalls() if hasattr(controller, "GetDrawcalls") else controller.GetRootActions()
        draws = [action for action in flatten(actions) if "Draw" in action_name(action, controller)]
        if not draws:
            raise RuntimeError("Viewport capture contains no draw action")
        log_step("draw-actions=%d" % len(draws))
        for action in draws:
            log_step("draw-event=%d name=%s" % (action.eventId, action_name(action, controller)))

        controller.SetFrameEvent(draws[-1].eventId, True)
        pipeline = controller.GetPipelineState()
        output_targets = pipeline.GetOutputTargets()
        if not output_targets:
            raise RuntimeError("Viewport draw has no color output target")
        resource_id = descriptor_resource(output_targets[0])
        texture = next((item for item in controller.GetTextures() if item.resourceId == resource_id), None)
        if texture is None:
            raise RuntimeError("Viewport color target is absent from the texture list")

        raw_pixels = texture_bytes(controller, resource_id)
        format_name = texture.format.Name()
        luminance = []
        if "R16G16B16A16" in format_name.upper() and "FLOAT" in format_name.upper():
            pixels = []
            for offset in range(0, len(raw_pixels) - 7, 8):
                red, green, blue, alpha = struct.unpack_from("<4e", raw_pixels, offset)
                channels = []
                for value in (red, green, blue):
                    linear = max(0.0, value) if math.isfinite(value) else 0.0
                    mapped = linear / (1.0 + linear)
                    encoded = 12.92 * mapped if mapped <= 0.0031308 else 1.055 * mapped ** (1.0 / 2.4) - 0.055
                    channels.append(max(0, min(255, round(encoded * 255.0))))
                luminance.append(0.2126 * red + 0.7152 * green + 0.0722 * blue)
                pixels.append((*channels, max(0, min(255, round(alpha * 255.0)))) )
        else:
            pixels = rgba8_pixels(raw_pixels, format_name)

        output_stem = os.path.splitext(capture_path)[0]
        write_bmp(output_stem + "_viewport.bmp", texture.width, texture.height, pixels)
        log_step("attachment-exported")
        messages = controller.GetDebugMessages()
        errors = [message for message in messages
                  if "Error" in str(message.severity) or "High" in str(message.severity)]
        depth_descriptor = pipeline.GetDepthTarget()
        depth_resource_id = descriptor_resource(depth_descriptor) if depth_descriptor else None
        depth_texture = next((item for item in controller.GetTextures()
                              if item.resourceId == depth_resource_id), None)
        depth_summary = "depth_target=none\n"
        if depth_texture is not None:
            depth_bytes = texture_bytes(controller, depth_resource_id)
            depth_summary = "depth_target=%s\ndepth_format=%s\ndepth_extent=%dx%d\n" % (
                resource_name(controller, depth_resource_id), depth_texture.format.Name(),
                depth_texture.width, depth_texture.height)
            if "D32" in depth_texture.format.Name().upper() and len(depth_bytes) >= 4:
                values = struct.unpack_from("<%df" % (len(depth_bytes) // 4), depth_bytes)
                finite = [value for value in values if math.isfinite(value)]
                if finite:
                    depth_summary += "depth_min=%.6f\ndepth_max=%.6f\n" % (
                        min(finite), max(finite))
        with open(output_stem + "_viewport_analysis.txt", "w", encoding="utf-8") as report:
            report.write("draws=%d\n" % len(draws))
            for action in draws:
                report.write("draw_event=%d name=%s\n" % (
                    action.eventId, action_name(action, controller)))
            report.write("extent=%dx%d\n" % (texture.width, texture.height))
            report.write("format=%s\n" % format_name)
            report.write("color_target=%s\n" % resource_name(controller, resource_id))
            report.write(depth_summary)
            report.write("replay_high_or_error=%d\n" % len(errors))
            if luminance:
                report.write("linear_luminance_min=%.6f\n" % min(luminance))
                report.write("linear_luminance_max=%.6f\n" % max(luminance))
            for error in errors:
                report.write(str(error) + "\n")
        print("draws=%d extent=%dx%d high_or_error=%d" %
              (len(draws), texture.width, texture.height, len(errors)))
        return 0
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()


try:
    capture_path = os.environ.get("ARP_EDITOR_RENDERDOC_ANALYSIS_CAPTURE")
    if not capture_path and len(getattr(sys, "argv", ())) > 1:
        capture_path = sys.argv[-1]
    if not capture_path:
        raise RuntimeError("Set ARP_EDITOR_RENDERDOC_ANALYSIS_CAPTURE to a viewport .rdc path")
    result = main(os.path.abspath(capture_path))
    log_step("analysis-completed")
    raise SystemExit(result)
except SystemExit:
    raise
except Exception:
    if analysis_log:
        with open(analysis_log, "a", encoding="utf-8") as log:
            log.write(traceback.format_exc())
    raise
