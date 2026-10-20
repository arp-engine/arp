"""Analyze public Vulkan bindless direct and indexed-indirect draws."""
import hashlib
import json
import os
import struct
import traceback

import renderdoc as rd


capture_path = os.environ["ARP_BINDLESS_CAPTURE_PATH"]
shader_path = os.environ["ARP_BINDLESS_GRAPHICS_SHADER"]
output_dir = os.environ["ARP_BINDLESS_CAPTURE_OUTPUT"]
report = {"scope": "public-vulkan-bindlesscross-direct-and-indirect-draws"}
capture = None
controller = None
try:
    with open(shader_path, "rb") as shader_file:
        expected_shader_hash = hashlib.sha256(shader_file.read()).hexdigest()
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(capture_path, "", None)
    report["openFile"] = str(status)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    report["replay"] = str(status)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))

    actions = []

    def collect(nodes):
        for action in nodes:
            actions.append(action)
            collect(action.children)

    collect(controller.GetRootActions())
    matching_draws = []
    for action in actions:
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.eventId, True)
        reflection = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Fragment)
        if reflection is None or reflection.entryPoint != "fragmentMain":
            continue
        shader_hash = hashlib.sha256(bytes(reflection.rawBytes)).hexdigest()
        if shader_hash == expected_shader_hash:
            matching_draws.append(action)
    if len(matching_draws) < 2:
        raise RuntimeError("public direct and indirect bindless draws were not both found")
    matching_draw = matching_draws[0]
    indirect_draw = matching_draws[1]

    controller.SetFrameEvent(matching_draw.eventId, True)
    pipe = controller.GetPipelineState()
    outputs = pipe.GetOutputTargets()
    if not outputs:
        raise RuntimeError("public bindless draw has no color output")
    target = outputs[0].resource
    pixels = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
    report["eventId"] = matching_draw.eventId
    report["draw"] = matching_draw.customName or "Draw"
    report["fragmentEntry"] = "fragmentMain"
    report["fragmentSpirvSha256"] = expected_shader_hash
    report["outputResourceId"] = str(target)
    report["firstPixels"] = [list(pixels[i:i + 4]) for i in range(0, min(16, len(pixels)), 4)]
    report["validationMessages"] = [message.description for message in controller.GetDebugMessages()]
    if report["validationMessages"]:
        raise RuntimeError("RenderDoc reported Vulkan validation messages")
    if len(pixels) < 16:
        raise RuntimeError("public bindless output readback is shorter than 2x2 RGBA8")
    report["nonClearOutput"] = any(pixels[i] != 0 for i in (0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14))
    if not report["nonClearOutput"]:
        raise RuntimeError("public bindless draw left the output clear")
    save = rd.TextureSave()
    save.resourceId = target
    save.destType = rd.FileType.PNG
    image_path = os.path.join(output_dir, "public-bindless-fragment.png")
    controller.SaveTexture(save, image_path)
    report["image"] = image_path

    controller.SetFrameEvent(indirect_draw.eventId, True)
    indirect_outputs = controller.GetPipelineState().GetOutputTargets()
    if not indirect_outputs:
        raise RuntimeError("public bindless indirect draw has no color output")
    indirect_target = indirect_outputs[0].resource
    indirect_pixels = bytes(controller.GetTextureData(indirect_target, rd.Subresource(0, 0, 0)))
    report["indirectEventId"] = indirect_draw.eventId
    report["indirectDraw"] = indirect_draw.customName or "Draw"
    report["indirectOutputResourceId"] = str(indirect_target)
    report["indirectFirstPixels"] = [list(indirect_pixels[i:i + 4])
        for i in range(0, min(16, len(indirect_pixels)), 4)]
    if "Indirect" not in report["indirectDraw"]:
        raise RuntimeError("second public bindless draw is not an indirect command")
    if indirect_target == target:
        raise RuntimeError("indirect draw did not use an independent output target")
    if len(indirect_pixels) < 16 or not any(indirect_pixels[i] != 0
            for i in (0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14)):
        raise RuntimeError("public bindless indirect draw left the output clear")
    indirect_image_path = os.path.join(output_dir, "public-bindless-indirect.png")
    indirect_save = rd.TextureSave()
    indirect_save.resourceId = indirect_target
    indirect_save.destType = rd.FileType.PNG
    controller.SaveTexture(indirect_save, indirect_image_path)
    report["indirectImage"] = indirect_image_path
    # Optional selected Vertex-only evidence for the prepared-depth increment.
    # Identify the exact generated shader, rather than accepting any depth draw.
    vertex_path = os.environ.get("ARP_BINDLESS_VERTEX_SHADER")
    if vertex_path:
        with open(vertex_path, "rb") as vertex_file:
            vertex_hash = hashlib.sha256(vertex_file.read()).hexdigest()
        depth_draws = []
        for action in actions:
            if not action.flags & rd.ActionFlags.Drawcall:
                continue
            controller.SetFrameEvent(action.eventId, True)
            depth_pipe = controller.GetPipelineState()
            vertex = depth_pipe.GetShaderReflection(rd.ShaderStage.Vertex)
            fragment = depth_pipe.GetShaderReflection(rd.ShaderStage.Fragment)
            if (vertex is not None and fragment is None
                    and hashlib.sha256(bytes(vertex.rawBytes)).hexdigest() == vertex_hash):
                depth_draws.append(action)
        if len(depth_draws) != 1:
            raise RuntimeError("exactly one current public prepared Vertex-only depth draw is required")
        controller.SetFrameEvent(depth_draws[0].eventId, True)
        depth_target = controller.GetPipelineState().GetDepthTarget().resource
        depth_bytes = bytes(controller.GetTextureData(depth_target, rd.Subresource(0, 0, 0)))
        if len(depth_bytes) != 16:
            raise RuntimeError("public prepared depth readback is not 2x2 D32")
        values = struct.unpack("<4f", depth_bytes)
        if any(value != 0.0 for value in values):
            raise RuntimeError("public prepared depth output disagrees with the selected vertex shader")
        report["preparedVertexDepth"] = dict(eventId=depth_draws[0].eventId,
            vertexSpirvSha256=vertex_hash, depthResourceId=str(depth_target),
            depthValues=list(values), fragmentStagePresent=False)
        depth_save = rd.TextureSave()
        depth_save.resourceId = depth_target
        depth_save.destType = rd.FileType.PNG
        depth_image_path = os.path.join(output_dir, "public-prepared-depth.png")
        controller.SaveTexture(depth_save, depth_image_path)
        report["preparedVertexDepth"]["image"] = depth_image_path
    root_dir = os.environ.get("ARP_BINDLESS_ROOT_FENCE_SHADER_DIR")
    if root_dir:
        root_evidence = []
        buffers = controller.GetBuffers()
        for entry, fault in (("cleanMain", 0), ("faultMain", 1)):
            with open(os.path.join(root_dir, f"MetalComputeBuffers.native.{entry}.spv"), "rb") as shader_file:
                expected = hashlib.sha256(shader_file.read()).hexdigest()
            matches = []
            for action in actions:
                if not action.flags & rd.ActionFlags.Dispatch:
                    continue
                controller.SetFrameEvent(action.eventId, True)
                compute = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
                if compute and hashlib.sha256(bytes(compute.rawBytes)).hexdigest() == expected:
                    matches.append(action)
            if len(matches) != 1:
                raise RuntimeError(f"exactly one canonical {entry} dispatch is required")
            dispatch = matches[0]
            copy = next((action for action in actions if action.eventId > dispatch.eventId
                         and action.flags & rd.ActionFlags.Copy), None)
            if copy is None:
                raise RuntimeError(f"canonical {entry} fault copy is absent")
            controller.SetFrameEvent(copy.eventId, True)
            fault_bytes = bytes(controller.GetBufferData(copy.copyDestination, 0, 4))
            if len(fault_bytes) != 4 or struct.unpack("<I", fault_bytes)[0] != fault:
                raise RuntimeError(f"canonical {entry} fault copy disagrees with the native result")
            counter_values = []
            for buffer in buffers:
                if buffer.length != 4 or buffer.resourceId == copy.copySource:
                    continue
                if any(usage.eventId == dispatch.eventId and usage.usage == rd.ResourceUsage.CS_RWResource
                       for usage in controller.GetUsage(buffer.resourceId)):
                    data = bytes(controller.GetBufferData(buffer.resourceId, 0, 4))
                    counter_values.append(struct.unpack("<I", data)[0])
            # Vulkan reports structured storage inputs under CS_RWResource as
            # well. Verify all four one-word storage resources: two unchanged
            # inputs (10,11) and two updated counters (14,16), excluding latch.
            if sorted(counter_values) != [10, 11, 14, 16]:
                raise RuntimeError(f"canonical {entry} storage values disagree: {counter_values}")
            root_evidence.append(dict(entry=entry, eventId=dispatch.eventId,
                spirvSha256=expected, faultCopyEventId=copy.eventId,
                faultBits=fault, storageValues=sorted(counter_values)))
        report["canonicalRootBufferExecution"] = root_evidence
    texture_root_dir = os.environ.get("ARP_BINDLESS_TEXTURE_ROOT_SHADER_DIR")
    array_root_dir = os.environ.get("ARP_BINDLESS_ARRAY_ROOT_SHADER_DIR")
    for directory, family, array in ((texture_root_dir, "CompletedTextureRoot", False),
                                      (array_root_dir, "CompletedArrayRoot", True)):
        if not directory:
            continue
        evidence = []
        for entry, fault in (("cleanMain", 0), ("faultMain", 1)):
            with open(os.path.join(directory, f"{family}.native.{entry}.spv"), "rb") as source:
                expected_hash = hashlib.sha256(source.read()).hexdigest()
            matches = []
            for action in actions:
                if not action.flags & rd.ActionFlags.Dispatch:
                    continue
                controller.SetFrameEvent(action.eventId, True)
                shader = controller.GetPipelineState().GetShaderReflection(rd.ShaderStage.Compute)
                if shader and hashlib.sha256(bytes(shader.rawBytes)).hexdigest() == expected_hash:
                    matches.append(action)
            expected_count = 2 if entry == "cleanMain" else 1
            if len(matches) != expected_count:
                raise RuntimeError(f"expected {expected_count} current {family} {entry} dispatches, got {len(matches)}")
            for dispatch in matches:
                copy = next((action for action in actions if action.eventId > dispatch.eventId
                             and action.flags & rd.ActionFlags.Copy), None)
                if copy is None:
                    raise RuntimeError(f"{family} Root fault copy is absent")
                controller.SetFrameEvent(copy.eventId, True)
                actual_fault = struct.unpack("<I", bytes(controller.GetBufferData(copy.copyDestination, 0, 4)))[0]
                if actual_fault != fault:
                    raise RuntimeError(f"{family} Root fault disagrees")
                outputs = []
                count = 16 if array else 6
                for buffer in controller.GetBuffers():
                    if buffer.length != count*4 or not any(usage.eventId == dispatch.eventId
                            and usage.usage == rd.ResourceUsage.CS_RWResource for usage in controller.GetUsage(buffer.resourceId)):
                        continue
                    words = struct.unpack(f"<{count}I", bytes(controller.GetBufferData(buffer.resourceId, 0, count*4)))
                    floats = struct.unpack(f"<{count}f", struct.pack(f"<{count}I", *words))
                    outputs.append(dict(resourceId=str(buffer.resourceId), words=list(words), values=list(floats)))
                outputs.sort(key=lambda output: output["values"][0])
                if len(outputs) != 2:
                    raise RuntimeError(f"{family} Root requires two actual result buffers")
                for index, output in enumerate(outputs):
                    for layer in range(2 if array else 1):
                        base = layer*8 if array else 0
                        words, floats = output["words"], output["values"]
                        if words[base+1] != 2 or words[base+3] != 2 or floats[base+4] != 1:
                            raise RuntimeError(f"{family} Root query/alpha result disagrees")
                        if array and (words[base+6] != 2 or floats[base+7] != floats[base+5]):
                            raise RuntimeError(f"{family} Root array query/mip-load result disagrees")
                        for actual, expected in ((floats[base], (index+1)*(16+8*layer)/255),
                                (floats[base+2], ((40 if index == 0 else 128)+(index+1)*8*layer)/255),
                                (floats[base+5], (index+1)*(32+8*layer)/255)):
                            if abs(actual-expected) > 0.00001:
                                raise RuntimeError(f"{family} Root layer {layer} numerical result disagrees")
                evidence.append(dict(entry=entry, eventId=dispatch.eventId, spirvSha256=expected_hash,
                    faultCopyEventId=copy.eventId, faultBits=actual_fault, outputs=outputs))
        report["canonicalRootCompletedArrayExecution" if array else "canonicalRootCompletedTextureExecution"] = evidence
    if os.environ.get("ARP_BINDLESS_ORIGINAL_UPLOAD_FENCE"):
        # Inspect destination subresources after the last actual copy for each
        # original image. CPU journal completion alone is not numerical evidence.
        textures = {texture.resourceId: texture for texture in controller.GetTextures()}
        buffers = {buffer.resourceId: buffer for buffer in controller.GetBuffers()}
        uploads = {}
        for action in actions:
            if not action.flags & rd.ActionFlags.Copy:
                continue
            texture = textures.get(action.copyDestination)
            staging = buffers.get(action.copySource)
            if (texture and texture.width == 2 and texture.height == 2
                    and texture.depth == 1 and texture.mips == 2
                    and texture.arraysize in (1, 2) and staging
                    # Normative packing: mip0 two 256-byte rows, mip1 one
                    # 256-byte row per array layer; exact dedicated VkBuffer.
                    and staging.length == 3 * 256 * texture.arraysize):
                uploads.setdefault(action.copyDestination, []).append(action)
        for directory, array in ((texture_root_dir, False), (array_root_dir, True)):
            if not directory:
                continue
            root_uploads = []
            for target, copies in list(uploads.items()):
                texture = textures[target]
                if texture.arraysize != (2 if array else 1):
                    continue
                controller.SetFrameEvent(copies[-1].eventId, True)
                actual = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
                for index in (0, 1):
                    expected = bytes(value for cell in range(4) for value in ((index+1)*16*(cell+1), 0, 0, 255))
                    if actual != expected:
                        continue
                    if len(copies) != 2:
                        raise RuntimeError("completed Root requires two original mip copies")
                    subresources = []
                    for layer in range(texture.arraysize):
                        for mip in (0, 1):
                            pixels = bytes(controller.GetTextureData(target, rd.Subresource(mip, layer, 0)))
                            expected = bytes(value for cell in range(1 if mip else 4)
                                for value in ((index+1)*((32 if mip else 16*(cell+1))+8*layer), 0, 0, 255))
                            if pixels != expected:
                                raise RuntimeError(f"completed Root original mip {mip} layer {layer} disagrees")
                            subresources.append(dict(layer=layer, mip=mip, pixels=list(pixels)))
                    root_uploads.append(dict(resourceId=str(target), index=index, copyEvents=[copy.eventId for copy in copies],
                        subresources=subresources))
                    del uploads[target]
                    if len(root_uploads) == 1:
                        save = rd.TextureSave(); save.resourceId = target; save.destType = rd.FileType.PNG
                        image_path = os.path.join(output_dir, "completed-root-array.png" if array else "completed-root-texture.png")
                        controller.SaveTexture(save, image_path)
                        report["completedRootArrayImage" if array else "completedRootTextureImage"] = image_path
                    break
            if sorted(upload["index"] for upload in root_uploads) != [0, 0, 0, 1, 1, 1]:
                raise RuntimeError("completed Root clean/fault/abandon original uploads are incomplete")
            report["completedRootOriginalArrayUploads" if array else "completedRootOriginalUploads"] = root_uploads
        expected_uploads = 4 if os.environ.get("ARP_BINDLESS_COMPLETED_TEXTURE_PUBLICATION") else 2
        if len(uploads) != expected_uploads:
            raise RuntimeError(f"expected {expected_uploads} original uploaded 2D/array images, got {len(uploads)}")
        layers = sorted(textures[target].arraysize for target in uploads)
        if layers != [1] * (expected_uploads // 2) + [2] * (expected_uploads // 2):
            raise RuntimeError(f"original upload layer coverage disagrees: {layers}")
        upload_evidence = []
        for target, copies in uploads.items():
            if len(copies) != 2:
                raise RuntimeError("original whole upload requires exactly two native mip copies")
            texture = textures[target]
            controller.SetFrameEvent(copies[-1].eventId, True)
            subresources = []
            for mip, byte_count in ((0, 16), (1, 4)):
                for layer in range(texture.arraysize):
                    start = (layer * 16 if mip == 0 else 16 * texture.arraysize + layer * 4) + 1
                    expected = bytes(range(start, start + byte_count))
                    actual = bytes(controller.GetTextureData(target, rd.Subresource(mip, layer, 0)))
                    if actual != expected:
                        raise RuntimeError(f"original upload mismatch {target} mip={mip} layer={layer}: {list(actual)}")
                    subresources.append(dict(mip=mip, layer=layer, pixels=list(actual)))
            upload_evidence.append(dict(resourceId=str(target), layers=texture.arraysize,
                copyEvents=[copy.eventId for copy in copies], subresources=subresources))
        if sorted(item["layers"] for item in upload_evidence) != layers:
            raise RuntimeError("original upload evidence requires both 2D and 2DArray")
        report["originalTextureUploadFence"] = upload_evidence
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
    report["success"] = False
finally:
    with open(os.path.join(output_dir, "public-bindless-renderdoc-report.json"), "w") as report_file:
        json.dump(report, report_file, indent=2)
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    os._exit(0 if report.get("success") else 1)
