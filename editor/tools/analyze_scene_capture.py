import os
import sys
import math
import struct
from pathlib import Path

repository_root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repository_root / "tests" / "tools"))
import renderdoc as rd
from analyze_ui_text_renderdoc import action_name, descriptor_resource, flatten, rgba8_pixels, texture_bytes, write_bmp

capture_path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else
                               "build/arp-editor-evidence/scene-editing/arp-editor-scene_capture.rdc")
capture_stem = os.path.splitext(capture_path)[0]
capture = rd.OpenCaptureFile()
controller = None
try:
    if capture.OpenFile(capture_path, "", None) != rd.ResultCode.Succeeded:
        raise RuntimeError("Cannot open scene editor capture")
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError("Cannot replay scene editor capture: " + str(status))
    actions = controller.GetDrawcalls() if hasattr(controller, "GetDrawcalls") else controller.GetRootActions()
    draws = [action for action in flatten(actions) if "DrawIndexed" in action_name(action, controller)]
    controller.SetFrameEvent(draws[-1].eventId, True)
    target = next(t for t in controller.GetTextures()
                  if t.resourceId == descriptor_resource(controller.GetPipelineState().GetOutputTargets()[0]))
    raw_pixels = texture_bytes(controller, target.resourceId)
    format_name = target.format.Name()
    luminance_values = []
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
            luminance_values.append(0.2126 * red + 0.7152 * green + 0.0722 * blue)
            pixels.append((*channels, max(0, min(255, round(alpha * 255.0)))))
    else:
        pixels = rgba8_pixels(raw_pixels, format_name)
    write_bmp(capture_stem + ".bmp", target.width, target.height, pixels)
    errors = [message for message in controller.GetDebugMessages()
              if "Error" in str(message.severity) or "High" in str(message.severity)]
    with open(capture_stem + "_analysis.txt", "w") as output:
        output.write("draws=%d\nextent=%dx%d\nreplay_high_or_error=%d\n" %
                     (len(draws), target.width, target.height, len(errors)))
        if luminance_values:
            output.write("linear_luminance_min=%.6f\nlinear_luminance_max=%.6f\n" %
                         (min(luminance_values), max(luminance_values)))
        for error in errors:
            output.write(str(error) + "\n")
    print("draws=%d extent=%dx%d replay_high_or_error=%d" %
          (len(draws), target.width, target.height, len(errors)))
except Exception as error:
    with open(capture_stem + "_analysis_error.txt", "w") as output:
        output.write(str(error) + "\n")
    raise
finally:
    if controller:
        controller.Shutdown()
    capture.Shutdown()
