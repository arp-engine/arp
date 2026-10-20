import math
import os
import struct
import sys
import traceback

import renderdoc as rd


def flatten(actions):
    for action in actions:
        yield action
        yield from flatten(action.children)


def write_bmp(path, width, height, pixels):
    row_size = width * 3
    padding = (4 - row_size % 4) % 4
    image_size = (row_size + padding) * height
    with open(path, "wb") as output:
        output.write(b"BM")
        output.write(struct.pack("<IHHI", 54 + image_size, 0, 0, 54))
        output.write(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
            image_size, 2835, 2835, 0, 0))
        for y in range(height - 1, -1, -1):
            for red, green, blue in pixels[y * width:(y + 1) * width]:
                output.write(bytes((blue, green, red)))
            output.write(b"\0" * padding)


def analyze(capture_path):
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        status = capture.OpenFile(capture_path, "", None)
        if status.code != rd.ResultCode.Succeeded:
            raise RuntimeError("RenderDoc could not open capture: " + str(status))
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if status.code != rd.ResultCode.Succeeded or controller is None:
            raise RuntimeError("RenderDoc replay failed: " + str(status))

        actions = list(flatten(controller.GetRootActions()))
        draws = [action for action in actions if action.flags & rd.ActionFlags.Drawcall]
        scene_draws = []
        shadow_draws = []
        skinned_draws = []
        skinned_shadow_draws = []
        scene_diagnostics = []
        shadow_depth_target = rd.ResourceId.Null()
        scene_depth_target = rd.ResourceId.Null()
        scene_color_target = rd.ResourceId.Null()
        hud_draw = None
        for draw in draws:
            controller.SetFrameEvent(draw.eventId, True)
            pipeline = controller.GetPipelineState()
            depth = pipeline.GetDepthTarget().resource
            color_targets = [target.resource for target in pipeline.GetOutputTargets()
                if target.resource != rd.ResourceId.Null()]
            if depth != rd.ResourceId.Null() and not color_targets:
                shadow_draws.append(draw)
                shadow_depth_target = depth
                shader = pipeline.GetShaderReflection(rd.ShaderStage.Vertex)
                reflected = [] if shader is None else shader.constantBlocks
                if any(getattr(block, "name", "") == "gSkinning" for block in reflected):
                    skinned_shadow_draws.append(draw)
            elif depth != rd.ResourceId.Null() and color_targets:
                scene_draws.append(draw)
                scene_depth_target = depth
                scene_color_target = color_targets[0]
                shader = pipeline.GetShaderReflection(rd.ShaderStage.Vertex)
                reflected = [] if shader is None else shader.constantBlocks
                scene_diagnostics.append((draw.customName, draw.numIndices,
                    [getattr(block, "name", "?") for block in reflected],
                    [vb.byteStride for vb in pipeline.GetVBuffers()]))
                has_skin_palette = any(
                    getattr(block, "name", "") == "gSkinning" for block in reflected)
                if has_skin_palette:
                    skinned_draws.append(draw)
            else:
                hud_draw = draw
        if len(scene_draws) < 2 or len(shadow_draws) < 2 or hud_draw is None:
            raise RuntimeError("Expected shadow casters, runway/character geometry, and a HUD draw")
        scene_draw = scene_draws[0]
        scene_receiver_draw = None
        sampled_diagnostics = []
        for candidate in scene_draws:
            controller.SetFrameEvent(candidate.eventId, True)
            candidate_pipeline = controller.GetPipelineState()
            resources = candidate_pipeline.GetReadOnlyResources(rd.ShaderStage.Fragment, True)
            resource_ids = []
            for entry in resources:
                if hasattr(entry, "resourceId"):
                    resource_ids.append(entry.resourceId)
                elif hasattr(entry, "descriptor") and hasattr(entry.descriptor, "resource"):
                    resource_ids.append(entry.descriptor.resource)
                elif hasattr(entry, "resources"):
                    resource_ids.extend(resource.resourceId for resource in entry.resources)
            sampled_diagnostics.append("%s:%s" % (str(candidate.eventId),
                ",".join(str(resource) for resource in resource_ids)))
            if shadow_depth_target in resource_ids:
                scene_receiver_draw = candidate
                break
        shadow_draw = shadow_draws[0]
        if not shadow_depth_target or shadow_depth_target == rd.ResourceId.Null():
            raise RuntimeError("Depth-only shadow pass has no D32 attachment")
        if not skinned_shadow_draws or not skinned_draws:
            raise RuntimeError("Animated character pose was not submitted to both caster and receiver passes")
        if scene_color_target == rd.ResourceId.Null() or scene_depth_target == rd.ResourceId.Null():
            raise RuntimeError("Visible scene pass is missing its color or depth attachment")
        if hud_draw.eventId <= scene_draws[-1].eventId:
            raise RuntimeError("Expected all depth-tested 3D draws before the color-only HUD draw")
        expected_skinned = int(os.environ.get("ARP_QUEEN_EXPECT_SKINNED_DRAWS", "1"))
        if len(skinned_draws) != expected_skinned:
            raise RuntimeError("Expected %d animated character draws with gSkinning, got %d" % (
                expected_skinned, len(skinned_draws)) + "; scene=" + repr(scene_diagnostics))

        controller.SetFrameEvent(shadow_draw.eventId, True)
        shadow_pipeline = controller.GetPipelineState()
        if shadow_pipeline.GetOutputTargets():
            raise RuntimeError("Shadow depth pass unexpectedly has a color output")
        if shadow_pipeline.GetDepthTarget().resource != shadow_depth_target:
            raise RuntimeError("Shadow depth draws do not share their declared depth attachment")
        if scene_receiver_draw is None:
            raise RuntimeError("No visible scene draw samples shadow depth resource %s; draws=%s" %
                (str(shadow_depth_target), ";".join(sampled_diagnostics)))
        controller.SetFrameEvent(scene_receiver_draw.eventId, True)
        scene_pipeline = controller.GetPipelineState()
        scene_targets = [target.resource for target in scene_pipeline.GetOutputTargets()
            if target.resource != rd.ResourceId.Null()]
        if len(scene_targets) != 1:
            raise RuntimeError("Expected one 3D color output target")
        depth_target = scene_pipeline.GetDepthTarget().resource
        if depth_target == rd.ResourceId.Null():
            raise RuntimeError("3D pass has no bound depth attachment")
        scene_shader = scene_pipeline.GetShaderReflection(rd.ShaderStage.Vertex)
        if scene_shader is None or scene_shader.entryPoint != "vertexMain":
            raise RuntimeError("Expected Build A Queen vertex shader in 3D draw")
        scene_fragment = scene_pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
        if scene_fragment is None or scene_fragment.entryPoint != "fragmentMain":
            raise RuntimeError("Expected Build A Queen fragment shader in 3D draw")

        controller.SetFrameEvent(hud_draw.eventId, True)
        hud_pipeline = controller.GetPipelineState()
        hud_targets = [target.resource for target in hud_pipeline.GetOutputTargets()
            if target.resource != rd.ResourceId.Null()]
        if hud_targets != scene_targets:
            raise RuntimeError("HUD did not load and composite into the 3D color output")
        if hud_pipeline.GetDepthTarget().resource != rd.ResourceId.Null():
            raise RuntimeError("HUD unexpectedly uses the 3D depth attachment")
        hud_fragment = hud_pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
        if hud_fragment is None or hud_fragment.entryPoint != "fragmentMain":
            raise RuntimeError("Expected GUI framework fragment shader in HUD draw")
        hud_resources = hud_pipeline.GetReadOnlyResources(rd.ShaderStage.Fragment, True)
        if not hud_resources:
            raise RuntimeError("HUD draw has no bound atlas texture")

        textures = {texture.resourceId: texture for texture in controller.GetTextures()}
        color_texture = textures[hud_targets[0]]
        depth_texture = textures[depth_target]
        shadow_texture = textures[shadow_depth_target]
        color_format = color_texture.format.Name()
        if "B8G8R8A8" in color_format:
            channel_order = (2, 1, 0, 3)
        elif "R8G8B8A8" in color_format:
            channel_order = (0, 1, 2, 3)
        else:
            raise RuntimeError("Unexpected color format: " + color_format)

        color_data = bytes(controller.GetTextureData(hud_targets[0], rd.Subresource(0, 0, 0)))
        expected_bytes = color_texture.width * color_texture.height * 4
        if len(color_data) != expected_bytes:
            raise RuntimeError("Unexpected color readback size")
        pixels = []
        for offset in range(0, len(color_data), 4):
            sample = color_data[offset:offset + 4]
            pixels.append(tuple(sample[channel_order[index]] for index in range(3)))
        background = pixels[0]
        colored = sum(max(pixel) - min(pixel) > 24 for pixel in pixels)
        changed = sum(sum(abs(pixel[i] - background[i]) for i in range(3)) > 36
            for pixel in pixels)
        if colored < 10000 or changed < 20000:
            raise RuntimeError("Backbuffer does not contain enough rendered 3D color variation")

        depth_data = bytes(controller.GetTextureData(depth_target, rd.Subresource(0, 0, 0)))
        if len(depth_data) != depth_texture.width * depth_texture.height * 4:
            raise RuntimeError("Unexpected D32 depth readback size")
        depths = struct.unpack("<%df" % (len(depth_data) // 4), depth_data)
        valid_depths = [value for value in depths if math.isfinite(value) and 0.0 <= value < 1.0]
        if not valid_depths or max(valid_depths) - min(valid_depths) < 0.001:
            raise RuntimeError("Depth attachment contains no useful geometry depth values")

        shadow_data = bytes(controller.GetTextureData(
            shadow_depth_target, rd.Subresource(0, 0, 0)))
        if len(shadow_data) != shadow_texture.width * shadow_texture.height * 4:
            raise RuntimeError("Unexpected shadow D32 depth readback size")
        shadow_depths = struct.unpack("<%df" % (len(shadow_data) // 4), shadow_data)
        valid_shadow_depths = [value for value in shadow_depths
            if math.isfinite(value) and 0.0 <= value < 1.0]
        if len(valid_shadow_depths) < 64 or max(valid_shadow_depths) - min(valid_shadow_depths) < 0.001:
            raise RuntimeError("Shadow map contains no useful caster depth values")

        bmp_path = os.path.splitext(capture_path)[0] + ".bmp"
        write_bmp(bmp_path, color_texture.width, color_texture.height, pixels)
        report_path = os.path.splitext(capture_path)[0] + ".yaml"
        with open(report_path, "w", encoding="utf-8") as report:
            report.write("type: arp.build-a-queen-renderdoc-evidence\n")
            report.write("contractVersion: 1\nbackend: Vulkan\nstatus: BackendValidated\n")
            report.write("renderPasses: 3\nshadowDrawCalls: %d\nsceneDrawCalls: %d\n" % (
                len(shadow_draws), len(scene_draws)))
            report.write("skinnedShadowDrawCalls: %d\nskinnedCharacterDrawCalls: %d\nhudDrawCalls: 1\n" % (
                len(skinned_shadow_draws), len(skinned_draws)))
            report.write("sceneVertices: %d\nhudVertices: %d\n" % (
                sum(draw.numIndices for draw in scene_draws), hud_draw.numIndices))
            report.write("skinnedCharacterVertices: [%s]\n" % ", ".join(
                str(draw.numIndices) for draw in skinned_draws))
            report.write("colorFormat: %s\n" % color_format)
            report.write("width: %d\nheight: %d\n" % (color_texture.width, color_texture.height))
            report.write("depthFormat: %s\ndepthSamples: %d\n" % (
                depth_texture.format.Name(), len(valid_depths)))
            report.write("depthRange: [%.7f, %.7f]\n" % (min(valid_depths), max(valid_depths)))
            report.write("shadowDepthFormat: %s\nshadowDepthSamples: %d\n" % (
                shadow_texture.format.Name(), len(valid_shadow_depths)))
            report.write("shadowDepthRange: [%.7f, %.7f]\n" % (
                min(valid_shadow_depths), max(valid_shadow_depths)))
            report.write("coloredPixels: %d\nchangedPixels: %d\n" % (colored, changed))
            report.write("screenshot: %s\n" % bmp_path.replace("\\", "/"))
            report.write("capture: %s\n" % capture_path.replace("\\", "/"))

        print("Queen RenderDoc analysis passed: scene=%d vertices, HUD=%d vertices, %d colored pixels, depth [%.5f, %.5f]" % (
            sum(draw.numIndices for draw in scene_draws), hud_draw.numIndices,
            colored, min(valid_depths), max(valid_depths)))
    finally:
        if controller is not None:
            controller.Shutdown()
        capture.Shutdown()


try:
    analyze(os.environ["ARP_QUEEN_CAPTURE"])
    error_path = os.environ["ARP_QUEEN_CAPTURE"] + ".error.txt"
    if os.path.exists(error_path):
        os.remove(error_path)
    os._exit(0)
except Exception:
    with open(os.environ["ARP_QUEEN_CAPTURE"] + ".error.txt", "w", encoding="utf-8") as error:
        error.write(traceback.format_exc())
    os._exit(1)
