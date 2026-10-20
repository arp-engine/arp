"""Independently replay composed public Root/2 workloads with qrenderdoc."""
import hashlib
import json
import os
import struct
import traceback

import renderdoc as rd

graphics = bool(os.environ.get("ARP_BINDLESS_COMPOSED_GRAPHICS_DIRECTORY"))
color_gi = bool(os.environ.get("ARP_BINDLESS_COLOR_GI_DIRECTORY"))
report = {"scope": "Root/2 ordinary color GI" if color_gi else
          "Root/2 " + ("Graphics" if graphics else "Compute") + " scalar buffers across three original sets"}
capture = None
controller = None
try:
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ["ARP_BINDLESS_CAPTURE_PATH"], "", None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))

    def walk(actions):
        for action in actions:
            yield action
            yield from walk(action.children)

    if color_gi:
        expected_hashes = {}
        for entry, fault in [("cleanMain", 0), ("faultMain", 1)]:
            path = os.path.join(os.environ["ARP_BINDLESS_COLOR_GI_DIRECTORY"],
                                "ColorGi.native." + entry + ".spv")
            with open(path, "rb") as source:
                expected_hashes[hashlib.sha256(source.read()).hexdigest()] = fault
    elif graphics:
        expected_hashes = {}
        for entry, fault in [("cleanFragment", 0), ("faultFragment", 1)]:
            path = os.path.join(os.environ["ARP_BINDLESS_COMPOSED_GRAPHICS_DIRECTORY"],
                                "GraphicsDomains.native." + entry + ".spv")
            with open(path, "rb") as source:
                expected_hashes[hashlib.sha256(source.read()).hexdigest()] = fault
    else:
        with open(os.environ["ARP_BINDLESS_COMPOSED_SPIRV"], "rb") as source:
            expected_hash = hashlib.sha256(source.read()).hexdigest()
    actions = list(walk(controller.GetRootActions()))
    buffers = {buffer.resourceId: buffer.length for buffer in controller.GetBuffers()}
    textures = {texture.resourceId: texture for texture in controller.GetTextures()}
    dispatches = []
    for action in actions:
        if not action.flags & (rd.ActionFlags.Drawcall if graphics else rd.ActionFlags.Dispatch):
            continue
        controller.SetFrameEvent(action.eventId, True)
        pipe = controller.GetPipelineState()
        shader = pipe.GetShaderReflection(rd.ShaderStage.Fragment if graphics else rd.ShaderStage.Compute)
        shader_hash = hashlib.sha256(bytes(shader.rawBytes)).hexdigest() if shader else None
        if (shader_hash not in expected_hashes if graphics or color_gi else shader_hash != expected_hash):
            continue
        if not graphics and list(action.dispatchDimension) != [2, 1, 1]:
            raise RuntimeError("Expected two invocations of the original Compute entry")
        all_used = [item for item in pipe.GetAllUsedDescriptors()
                if item.access.stage in ((rd.ShaderStage.Vertex, rd.ShaderStage.Fragment) if graphics
                                         else (rd.ShaderStage.Compute,))]
        used = [item for item in all_used if item.descriptor.resource in buffers]
        stores = {str(item.access.descriptorStore) for item in all_used}
        if len(stores) != 3:
            raise RuntimeError("Expected three distinct bound descriptor sets: " + str(stores))
        observed = []
        graphics_target = pipe.GetOutputTargets()[0].resource if graphics else None
        for item in used:
            descriptor = item.descriptor
            data = bytes(controller.GetBufferData(descriptor.resource, 0, buffers[descriptor.resource]))
            observed.append(dict(resource=str(descriptor.resource), store=str(item.access.descriptorStore),
                                 type=str(descriptor.type), offset=descriptor.byteOffset,
                                 descriptorBytes=descriptor.byteSize, data=data.hex()))
        copy = next((item for item in actions if item.eventId > action.eventId
                     and item.flags & rd.ActionFlags.Copy), None)
        if copy is None:
            raise RuntimeError("Missing original fault readback copy")
        # Every recorded native dispatch executes; uncertain submit cases may
        # execute but intentionally withhold public results. Replay checks GPU
        # data only and does not confer publication authority on those cases.
        controller.SetFrameEvent(copy.eventId, True)
        fault = struct.unpack("<I", bytes(controller.GetBufferData(copy.copyDestination, 0, 4)))[0]
        if fault not in (0, 1):
            raise RuntimeError("Unexpected fault bits: " + str(fault))
        if color_gi:
            if fault != expected_hashes[shader_hash]:
                raise RuntimeError("GI entry and original fault result disagree")
            expected = []
            for face in range(6):
                expected += [17 + (40 + 8 * face) / 255, 17 + (32 + 8 * face) / 255]
            expected += [17 + 32 / 255, 17 + 64 / 255,
                         17 + 48 / 255, 17 + 96 / 255, 17 + 72 / 255, 17 + 120 / 255]
            outputs = {item["resource"]: struct.unpack("<18f", bytes.fromhex(item["data"]))
                       for item in observed if len(bytes.fromhex(item["data"])) == 72}
            if len(outputs) != 2 or any(abs(actual - reference) >= 0.00001
                    for values in outputs.values() for actual, reference in zip(values, expected)):
                raise RuntimeError("GI replay outputs differ from original uploaded texels: " + str(outputs))
            if not any(bytes.fromhex(item["data"])[:4] == struct.pack("<f", 17) for item in observed):
                raise RuntimeError("GI Frame17 missing")
            images = {}
            for item in all_used:
                resource = item.descriptor.resource
                if resource in textures:
                    images[resource] = textures[resource]
            if len(images) != 3:
                raise RuntimeError("GI cube/LUT/array descriptor closure missing")
            image_data = []
            shapes = set()
            for resource, description in images.items():
                layers = description.arraysize
                shapes.add(layers)
                if description.width != 2 or description.height != 2 or description.mips != 2 or layers not in (1, 2, 6):
                    raise RuntimeError("GI uploaded texture shape differs")
                index = {6: 0, 1: 1, 2: 2}[layers]
                subresources = []
                for mip in range(2):
                    for layer in range(layers):
                        actual = bytes(controller.GetTextureData(resource, rd.Subresource(mip, layer, 0)))
                        texels = [16 * (texel + 1) + 8 * layer for texel in range(4)] if mip == 0 else [32 + 8 * layer]
                        reference = bytes(value for red in texels for value in ((index + 1) * red, 0, 0, 255))
                        if actual != reference:
                            raise RuntimeError("GI original whole mip/face bytes differ: " + str((layers, mip, layer, actual.hex())))
                        subresources.append(dict(mip=mip, layer=layer, rgba=actual.hex()))
                image_data.append(dict(resource=str(resource), layers=layers, mips=2, subresources=subresources))
            if shapes != {1, 2, 6}:
                raise RuntimeError("GI image dimensions missing")
            dispatches.append(dict(eventId=action.eventId, spirvSha256=shader_hash,
                descriptorSets=sorted(stores), descriptors=observed, faultBits=fault,
                images=image_data, outputs=outputs))
            continue
        if graphics:
            if fault != expected_hashes[shader_hash]:
                raise RuntimeError("Fragment entry and original fault result disagree")
            values = [bytes.fromhex(item["data"]) for item in observed]
            for required in [struct.pack("<I", 777), struct.pack("<I", 778),
                             struct.pack("<IIII", 875, 876, 875, 876)]:
                if required not in values:
                    raise RuntimeError("Graphics indexed/ordinary buffer values disagree: " + str(observed))
            if not any(data[:4] == struct.pack("<f", 17.0) for data in values) or not any(
                    data[:4] == struct.pack("<I", 32) for data in values):
                raise RuntimeError("Graphics ordinary Frame/Shared values disagree")
            target = graphics_target
            pixels = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
            expected = bytes([255, 0, 0, 255, 0, 255, 0, 255] * 2)
            if pixels != expected:
                raise RuntimeError("Graphics output is not the exact red/green2x2: " + pixels.hex())
            save = rd.TextureSave()
            save.resourceId = target
            save.destType = rd.FileType.PNG
            output = os.path.join(os.environ["ARP_BINDLESS_CAPTURE_OUTPUT"], "graphics-" + str(fault) + ".png")
            controller.SaveTexture(save, output)
            dispatches.append(dict(eventId=action.eventId, spirvSha256=shader_hash,
                descriptorSets=sorted(stores), descriptors=observed, faultBits=fault,
                ordinaryOutput=[875, 876, 875, 876], rgba=pixels.hex(), image=output))
            continue
        indexed = set()
        frame = shared = output = False
        for item in observed:
            data = bytes.fromhex(item["data"])
            if len(data) == 4 and data == struct.pack("<I", 49):
                indexed.add(item["resource"])
            if data[:4] == struct.pack("<f", 17.0):
                frame = True
            if data[:4] == struct.pack("<I", 32):
                shared = True
            if data[:8] == struct.pack("<II", 49, 49 + fault):
                output = True
        if not frame or not shared or not output or len(indexed) != (1 if fault else 2):
            report["failedDescriptors"] = observed
            raise RuntimeError("Ordinary/indexed outputs differ from exact Root/2 inputs")
        dispatches.append(dict(eventId=action.eventId, spirvSha256=expected_hash,
                               descriptorSets=sorted(stores), descriptors=observed,
                               faultCopyEventId=copy.eventId, faultBits=fault,
                               ordinaryOutput=[49, 49 + fault], indexedResources=sorted(indexed)))
    if len(dispatches) != (7 if color_gi else 2 if graphics else 5) or sum(item["faultBits"] for item in dispatches) != 1:
        raise RuntimeError("Unexpected actual draw/dispatch or fault count")
    report["draws" if graphics else "dispatches"] = dispatches
    report["validationMessages"] = [message.description for message in controller.GetDebugMessages()]
    if report["validationMessages"]:
        raise RuntimeError("Vulkan validation messages present")
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
    report["success"] = False
finally:
    with open(os.path.join(os.environ["ARP_BINDLESS_CAPTURE_OUTPUT"], "renderdoc-report.json"), "w") as destination:
        json.dump(report, destination, indent=2)
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    os._exit(0 if report.get("success") else 1)
