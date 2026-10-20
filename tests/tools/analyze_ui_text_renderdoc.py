import os
import struct
import sys

import renderdoc as rd


def flatten(draws):
    result = []
    for draw in draws:
        result.append(draw)
        result.extend(flatten(draw.children))
    return result


def action_name(action, controller):
    if hasattr(action, "name"):
        return action.name
    if getattr(action, "customName", ""):
        return action.customName
    return action.GetName(controller.GetStructuredFile())


def descriptor_resource(descriptor):
    if hasattr(descriptor, "resourceId"):
        return descriptor.resourceId
    if hasattr(descriptor, "resource"):
        return descriptor.resource
    raise RuntimeError("output descriptor has no resource field: %r" % dir(descriptor))


def resource_name(controller, resource_id):
    if hasattr(controller, "GetResourceName"):
        return controller.GetResourceName(resource_id)
    if hasattr(controller, "GetResourceDesc"):
        return controller.GetResourceDesc(resource_id).name
    return ""


def texture_bytes(controller, resource_id):
    return bytes(controller.GetTextureData(resource_id, rd.Subresource(0, 0, 0)))


def rgba8_pixels(data, format_name):
    bgra = "B8G8R8A8" in format_name or "BGRA" in format_name.upper()
    pixels = []
    for offset in range(0, len(data) - 3, 4):
        first, green, third, alpha = data[offset:offset + 4]
        red, blue = (third, first) if bgra else (first, third)
        pixels.append((red, green, blue, alpha))
    return pixels


def write_bmp(path, width, height, pixels):
    row_bytes = width * 3
    padded_row_bytes = (row_bytes + 3) & ~3
    pixel_bytes = padded_row_bytes * height
    with open(path, "wb") as output:
        output.write(b"BM")
        output.write(struct.pack("<IHHI", 54 + pixel_bytes, 0, 0, 54))
        output.write(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24,
                                 0, pixel_bytes, 2835, 2835, 0, 0))
        padding = b"\0" * (padded_row_bytes - row_bytes)
        for y in range(height - 1, -1, -1):
            row = pixels[y * width:(y + 1) * width]
            for red, green, blue, _ in row:
                output.write(bytes((blue, green, red)))
            output.write(padding)


def row_segments(counts):
    active = [index for index, count in enumerate(counts) if count >= 2]
    if not active:
        return []
    segments = []
    start = active[0]
    previous = active[0]
    for row in active[1:]:
        if row - previous > 4:
            segments.append((start, previous))
            start = row
        previous = row
    segments.append((start, previous))
    return segments


def main():
    capture_value = os.environ.get("ARP_RENDERDOC_ANALYZE_CAPTURE")
    if capture_value is None:
        capture_value = sys.argv[-1]
    capture_path = os.path.abspath(capture_value)
    report_path = os.environ.get(
        "ARP_RENDERDOC_ANALYZE_REPORT",
        os.path.splitext(capture_path)[0] + "_analysis.txt")
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(capture_path, "", None) != rd.ResultCode.Succeeded:
        raise RuntimeError("failed to open capture: " + capture_path)
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded or controller is None:
        capture.Shutdown()
        raise RuntimeError("failed to replay capture: " + str(status))

    try:
        root_actions = (controller.GetDrawcalls() if hasattr(controller, "GetDrawcalls")
                        else controller.GetRootActions())
        draws = flatten(root_actions)
        indexed = [draw for draw in draws
                   if "DrawIndexed" in action_name(draw, controller)]
        if len(indexed) != 67:
            raise RuntimeError("expected 67 GUI indexed draws, got %d" % len(indexed))

        last_draw = indexed[-1]
        controller.SetFrameEvent(last_draw.eventId, True)
        output_targets = controller.GetPipelineState().GetOutputTargets()
        if not output_targets:
            raise RuntimeError("last GUI draw has no color output")
        backbuffer_id = descriptor_resource(output_targets[0])
        textures = {texture.resourceId: texture for texture in controller.GetTextures()}
        backbuffer = textures[backbuffer_id]
        format_name = str(backbuffer.format)
        data = texture_bytes(controller, backbuffer_id)
        pixels = rgba8_pixels(data, format_name)
        expected_pixels = backbuffer.width * backbuffer.height
        if len(pixels) != expected_pixels:
            raise RuntimeError("backbuffer is not tightly packed RGBA8/BGRA8")

        bright_counts = [0] * backbuffer.height
        for y in range(backbuffer.height):
            for red, green, blue, _ in pixels[y * backbuffer.width:(y + 1) * backbuffer.width]:
                luminance = (54 * red + 183 * green + 19 * blue) // 256
                if luminance >= 140:
                    bright_counts[y] += 1
        segments = row_segments(bright_counts)
        if len(segments) != 2:
            raise RuntimeError("expected two separated text bands, got %r" % (segments,))
        if any(end - start < 8 for start, end in segments):
            raise RuntimeError("captured text band is unexpectedly thin: %r" % (segments,))

        band_levels = []
        for start, end in segments:
            levels = set()
            for y in range(start, end + 1):
                for red, green, blue, _ in pixels[y * backbuffer.width:(y + 1) * backbuffer.width]:
                    luminance = (54 * red + 183 * green + 19 * blue) // 256
                    if 24 < luminance < 245:
                        levels.add(luminance)
            band_levels.append(len(levels))
        if any(level_count < 24 for level_count in band_levels):
            raise RuntimeError("text edge coverage has too few output levels: %r" % band_levels)

        atlas = None
        atlas_name = ""
        for texture in textures.values():
            name = resource_name(controller, texture.resourceId)
            if ("ui_text_visual_smoke" in name.lower()
                    or (texture.width == 1024 and texture.height == 260)):
                atlas = texture
                atlas_name = name
                break
        if atlas is None:
            raise RuntimeError("GUI text atlas resource was not captured")
        atlas_pixels = rgba8_pixels(texture_bytes(controller, atlas.resourceId), str(atlas.format))
        atlas_alpha_levels = {alpha for _, _, _, alpha in atlas_pixels if alpha != 0}
        if len(atlas_alpha_levels) < 32:
            raise RuntimeError("atlas coverage has too few alpha levels: %d" % len(atlas_alpha_levels))

        screenshot_path = os.path.splitext(capture_path)[0] + "_backbuffer.bmp"
        write_bmp(screenshot_path, backbuffer.width, backbuffer.height, pixels)
        debug_messages = controller.GetDebugMessages()
        errors = [message for message in debug_messages
                  if "Error" in str(message.severity) or "High" in str(message.severity)]
        if errors:
            raise RuntimeError("RenderDoc replay reported %d error messages" % len(errors))

        lines = [
            "capture=%s" % capture_path,
            "last_draw_eid=%d indexed_draws=%d" % (last_draw.eventId, len(indexed)),
            "backbuffer=%dx%d format=%s" %
                (backbuffer.width, backbuffer.height, format_name),
            "text_bands=%r coverage_levels=%r" % (segments, band_levels),
            "atlas=%s extent=%dx%d alpha_levels=%d" %
                (atlas_name, atlas.width, atlas.height, len(atlas_alpha_levels)),
            "debug_errors=0",
            "screenshot=%s" % screenshot_path,
        ]
        with open(report_path, "w", encoding="ascii") as report:
            report.write("\n".join(lines) + "\n")
        print("\n".join(lines))
    finally:
        controller.Shutdown()
        capture.Shutdown()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        report_path = os.environ.get(
            "ARP_RENDERDOC_ANALYZE_REPORT", "ui_text_renderdoc_analysis_error.txt")
        with open(report_path, "w", encoding="ascii") as report:
            report.write("error=%s\n" % error)
        raise
