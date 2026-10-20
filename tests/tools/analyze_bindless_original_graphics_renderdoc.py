"""Independently replay the original GraphicsRootFence/9 color output.

Run with qrenderdoc --python. Shader hash identifies the genuine Root draw;
expected pixels follow the authored two-mip uploads and independent samplers.
No application readback or alternate resource journal is used as evidence.
"""
import hashlib
import json
import os
import struct
import traceback

import renderdoc as rd


output_dir = os.environ["ARP_BINDLESS_CAPTURE_OUTPUT"]
report = {"scope": "original-graphics-root9-single-rgba8-level-and-coordinate-grad"}
capture = None
controller = None
try:
    with open(os.environ["ARP_BINDLESS_ORIGINAL_GRAPHICS_SHADER"], "rb") as source:
        expected_hash = hashlib.sha256(source.read()).hexdigest()
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ["ARP_BINDLESS_CAPTURE_PATH"], "", None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))

    def actions(nodes):
        for action in nodes:
            yield action
            yield from actions(action.children)

    all_actions = list(actions(controller.GetRootActions()))
    draw_hashes = {}
    evidence = []
    for action in all_actions:
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.eventId, True)
        pipeline = controller.GetPipelineState()
        reflection = pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
        if reflection is None:
            continue
        draw_hashes[action.eventId] = hashlib.sha256(bytes(reflection.rawBytes)).hexdigest()
        if draw_hashes[action.eventId] != expected_hash:
            continue
        targets = [target for target in pipeline.GetOutputTargets() if target.resource != rd.ResourceId.Null()]
        if len(targets) != 1:
            raise RuntimeError("original Graphics requires exactly one color output")
        target = targets[0].resource
        pixels = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
        # SV_Position.x alternates table indices 1,0; sampler indices 0,1.
        # load.r=(32,16), nearest/linear=(128,40), coordinate-grad mip1.r=(64,32).
        expected = bytes([32, 128, 64, 255, 16, 40, 32, 255] * 2)
        if pixels != expected:
            raise RuntimeError("original Graphics pixel mismatch: " + str(list(pixels)))
        image = os.path.join(output_dir, "original-root-graphics9.png")
        save = rd.TextureSave()
        save.resourceId = target
        save.destType = rd.FileType.PNG
        controller.SaveTexture(save, image)
        evidence.append(dict(eventId=action.eventId, fragmentSpirvSha256=expected_hash,
                             outputResourceId=str(target), pixels=list(pixels), image=image))
    if len(evidence) != 1:
        raise RuntimeError("expected one submitted original Graphics draw: " + str(len(evidence)))
    report["draws"] = evidence
    buffer_lengths = {buffer.resourceId: buffer.length for buffer in controller.GetBuffers()}

    def descriptor_view_size(descriptor):
        # Vulkan preserves VK_WHOLE_SIZE in a captured descriptor. Resolve it
        # only against that same captured allocation; finite sizes stay exact.
        return buffer_lengths[descriptor.resource] - descriptor.byteOffset \
            if descriptor.byteSize == (1 << 64) - 1 else descriptor.byteSize

    def sampled_ordinary_buffers(array=False):
        # Observe the actual descriptor resources at the identified draw. These
        # are the same three scalar ordinary buffers used by both shader stages.
        descriptors = controller.GetPipelineState().GetAllUsedDescriptors()
        resources = {used.descriptor.resource for used in descriptors
            if used.access.stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment)
            and buffer_lengths.get(used.descriptor.resource) == 16}
        observed = {resource: bytes(controller.GetBufferData(resource, 0, 16)) for resource in resources}
        expected = [struct.pack('<4f', 32., 0., 0., 0.), struct.pack('<4I', 17, 0, 0, 0),
                    struct.pack('<4I', 98, 0 if array else 98, 98, 98)]
        if sorted(observed.values()) != sorted(expected):
            raise RuntimeError('Sampled ordinary descriptor bytes differ: ' + str(
                {str(resource): list(struct.unpack('<4I', data)) for resource, data in observed.items()}))
        return [{'resource': str(resource), 'words': list(struct.unpack('<4I', data))}
                for resource, data in observed.items()]

    if os.environ.get('ARP_BINDLESS_SAMPLED_ORDINARY') == '1':
        report['scope'] = 'public Root2 sampled Graphics with scalar ordinary buffers; scoped RGBA8 only'
        for draw in evidence:
            controller.SetFrameEvent(draw['eventId'], True)
            draw['ordinaryBuffers'] = sampled_ordinary_buffers()

    def record_descriptors():
        pipeline = controller.GetPipelineState()
        return [used.descriptor for used in pipeline.GetAllUsedDescriptors()
                if used.access.stage == rd.ShaderStage.Fragment
                and used.descriptor.type == rd.DescriptorType.ReadWriteBuffer
                and descriptor_view_size(used.descriptor) == 96]

    def original_draw(entry, directory, family, finite_ranges=None):
        shader = os.path.join(directory, family + ".native." + entry + ".spv")
        with open(shader, "rb") as source:
            shader_hash = hashlib.sha256(source.read()).hexdigest()
        matches = []
        for action in all_actions:
            if draw_hashes.get(action.eventId) != shader_hash:
                continue
            if finite_ranges is not None:
                controller.SetFrameEvent(action.eventId, True)
                descriptors = record_descriptors()
                if len(descriptors) != 2 or any(bool(d.byteOffset) != finite_ranges for d in descriptors):
                    continue
            matches.append(action)
        if len(matches) != 1:
            raise RuntimeError("expected one original draw for " + family + "/" + entry)
        controller.SetFrameEvent(matches[0].eventId, True)
        return matches[0], shader_hash

    def original_fault_copy(draw, fault_bits):
        copy = next((action for action in all_actions if action.eventId > draw.eventId
                     and action.flags & rd.ActionFlags.Copy), None)
        if copy is None:
            raise RuntimeError("original fault copy absent")
        controller.SetFrameEvent(copy.eventId, True)
        actual_fault = struct.unpack("<I", bytes(controller.GetBufferData(copy.copyDestination, 0, 4)))[0]
        if actual_fault != fault_bits:
            raise RuntimeError("original fault mismatch: " + str(actual_fault))
        return copy.eventId, actual_fault

    buffer_directory = os.environ.get("ARP_BINDLESS_ORIGINAL_GRAPHICS_BUFFER_SHADER_DIR")
    if buffer_directory:
        buffer_evidence = []
        for entry, fault_bits in (("cleanFragment", 0), ("faultFragment", 1)):
            draw, expected_buffer_hash = original_draw(entry, buffer_directory, "RootGraphicsBuffers")
            target = controller.GetPipelineState().GetOutputTargets()[0].resource
            pixels = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
            # Authored quarter values land exactly halfway between UNORM codes
            # for 0.5. Accept either adjacent code there, bounded to half an LSB;
            # keep 0.25, 0.75 and alpha exact. Integer arithmetic avoids imposing
            # Python's ties-to-even rule on the native attachment conversion.
            expected_quarters = [1, 2, 3, 4, 3, 2, 1, 4] * 2
            if len(pixels) != len(expected_quarters) or any(
                    abs(actual * 4 - quarter * 255) > 2
                    for actual, quarter in zip(pixels, expected_quarters)):
                raise RuntimeError(entry + " buffer Graphics color mismatch: " + str(list(pixels)))
            counters = []
            for buffer in controller.GetBuffers():
                if buffer.length != 12 or not any(usage.eventId == draw.eventId
                        and usage.usage == rd.ResourceUsage.PS_RWResource for usage in controller.GetUsage(buffer.resourceId)):
                    continue
                counters.append(list(struct.unpack("<III", bytes(controller.GetBufferData(buffer.resourceId, 0, 12)))))
            if sorted(counters) != [[102, 10, 10], [202, 20, 20]]:
                raise RuntimeError(entry + " buffer Graphics side effects mismatch: " + str(counters))
            fault_copy_event, actual_fault = original_fault_copy(draw, fault_bits)
            image = os.path.join(output_dir, "original-root-buffers-" + entry + ".png")
            save = rd.TextureSave()
            save.resourceId = target
            save.destType = rd.FileType.PNG
            controller.SaveTexture(save, image)
            buffer_evidence.append(dict(entry=entry, eventId=draw.eventId, fragmentSpirvSha256=expected_buffer_hash,
                                        pixels=list(pixels), counters=sorted(counters), faultBits=actual_fault,
                                        faultCopyEventId=fault_copy_event, image=image))
        report["bufferDraws"] = buffer_evidence
    array_directory = os.environ.get("ARP_BINDLESS_ORIGINAL_GRAPHICS_ARRAY_SHADER_DIR")
    if array_directory:
        array_evidence = []
        for entry, fault_bits in (("cleanFragment", 0), ("faultFragment", 1)):
            draw, shader_hash = original_draw(entry, array_directory, "RootGraphicsArray")
            target = controller.GetPipelineState().GetOutputTargets()[0].resource
            pixels = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
            # x chooses texture1/0 and sampler0/1; y chooses layer0/1. Top
            # right discards to the authored zero clear. At layer1 the original
            # upload adds 8 to each level0 texel and to level1 before scaling.
            expected = bytes([32, 128, 64, 255, 0, 0, 0, 0,
                              48, 144, 80, 255, 24, 48, 40, 255])
            if pixels != expected:
                raise RuntimeError(entry + " array Graphics pixel mismatch: " + str(list(pixels)))
            ordinary = sampled_ordinary_buffers(True) if os.environ.get('ARP_BINDLESS_SAMPLED_ORDINARY') == '1' else None
            fault_copy_event, actual_fault = original_fault_copy(draw, fault_bits)
            image = os.path.join(output_dir, "original-root-array-" + entry + ".png")
            save = rd.TextureSave()
            save.resourceId = target
            save.destType = rd.FileType.PNG
            controller.SaveTexture(save, image)
            array_evidence.append(dict(entry=entry, eventId=draw.eventId, fragmentSpirvSha256=shader_hash,
                                       pixels=list(pixels), faultBits=actual_fault,
                                       faultCopyEventId=fault_copy_event, image=image, ordinaryBuffers=ordinary))
        report["arrayDraws"] = array_evidence
    record_directory = os.environ.get("ARP_BINDLESS_ORIGINAL_GRAPHICS_RECORD_SHADER_DIR")
    if record_directory:
        range_modes = (False, True) if os.environ.get("ARP_BINDLESS_ORIGINAL_GRAPHICS_RECORD_RANGES") == "1" else (False,)
        for finite_ranges in range_modes:
            record_evidence = []
            for entry, fault_bits in (("cleanFragment", 0), ("faultFragment", 1)):
                draw, shader_hash = original_draw(entry, record_directory, "RootGraphicsRecords", finite_ranges)
                pipeline = controller.GetPipelineState()
                target = pipeline.GetOutputTargets()[0].resource
                pixels = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
                quarters = [1, 2, 3, 4, 3, 2, 1, 4] * 2
                if len(pixels) != len(quarters) or any(abs(actual * 4 - quarter * 255) > 2
                        for actual, quarter in zip(pixels, quarters)):
                    raise RuntimeError(entry + " record Graphics color mismatch: " + str(list(pixels)))
                expected_views = {32: [], 128: [], 96: []}
                for index in (0, 1):
                    color = (0.25, 0.5, 0.75, 1.0) if index == 0 else (0.75, 0.5, 0.25, 1.0)
                    expected_views[32].append(struct.pack("<4f4I", *color, index, 5, 7, 9))
                    expected_views[128].append(b"".join(struct.pack("<12f4I",
                        float(index + 1), float(element + 2), 3.0, 1.0,
                        0.125, 0.25, float(index), float(element), 5.0, 6.0, 7.0, 8.0,
                        0x00040302 + index + element, 0x87654321, 0, 0) for element in (0, 1)))
                    expected_views[96].append(b"".join(struct.pack("<4f4I4i", *color,
                        0x00040302 + index + element, 0x87654321, 0, 0,
                        -1 - index, -2 - element, 7, -9) for element in (0, 1)))
                actual_views = {size: [] for size in expected_views}
                descriptor_evidence = []
                for used in pipeline.GetAllUsedDescriptors():
                    descriptor = used.descriptor
                    if used.access.stage != rd.ShaderStage.Fragment or descriptor.resource not in buffer_lengths:
                        continue
                    size, offset, resource = descriptor_view_size(descriptor), descriptor.byteOffset, descriptor.resource
                    if size not in expected_views:
                        continue
                    # Vulkan uses one storage descriptor type for readonly and
                    # writable buffers. Exact shader hashes prove signedness,
                    # record layout and access; descriptors prove bound views.
                    required_type = {32: rd.DescriptorType.ConstantBuffer, 128: rd.DescriptorType.ReadWriteBuffer,
                                     96: rd.DescriptorType.ReadWriteBuffer}[size]
                    if descriptor.type != required_type or bool(offset) != finite_ranges:
                        raise RuntimeError("record descriptor type/offset mismatch")
                    if finite_ranges and descriptor.byteSize != size:
                        raise RuntimeError("record finite descriptor requires an explicit size")
                    allocation = bytes(controller.GetBufferData(resource, 0, buffer_lengths[resource]))
                    if offset + size > len(allocation):
                        raise RuntimeError("record descriptor view exceeds allocation")
                    if finite_ranges and (not allocation[:offset] or not allocation[offset + size:]
                            or any(byte != 0xcd for byte in allocation[:offset] + allocation[offset + size:])):
                        raise RuntimeError("record finite-view prefix/suffix changed")
                    actual_views[size].append(allocation[offset:offset + size])
                    descriptor_evidence.append(dict(resource=str(resource), type=str(descriptor.type),
                        byteOffset=offset, byteSize=size, rawByteSize=descriptor.byteSize,
                        allocationBytes=len(allocation), poisonUnchanged=finite_ranges))
                if any(sorted(actual_views[size]) != sorted(expected_views[size]) for size in expected_views):
                    raise RuntimeError(entry + " record Graphics input/output bytes mismatch: " + str(
                        {size: [data.hex() for data in values] for size, values in actual_views.items()}))
                fault_copy_event, actual_fault = original_fault_copy(draw, fault_bits)
                image = os.path.join(output_dir, "original-root-records-" + ("ranges-" if finite_ranges else "") + entry + ".png")
                save = rd.TextureSave()
                save.resourceId = target
                save.destType = rd.FileType.PNG
                controller.SaveTexture(save, image)
                record_evidence.append(dict(entry=entry, eventId=draw.eventId, fragmentSpirvSha256=shader_hash,
                    pixels=list(pixels), outputBytes=sorted(data.hex() for data in actual_views[96]),
                    descriptors=descriptor_evidence, faultBits=actual_fault, faultCopyEventId=fault_copy_event, image=image))
            report["recordRangeDraws" if finite_ranges else "recordDraws"] = record_evidence
    report["validationMessages"] = [message.description for message in controller.GetDebugMessages()]
    if report["validationMessages"]:
        raise RuntimeError("RenderDoc reported Vulkan validation messages")
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
    report["success"] = False
finally:
    with open(os.path.join(output_dir, "original-graphics-renderdoc-report.json"), "w") as destination:
        json.dump(report, destination, indent=2)
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    os._exit(0 if report.get("success") else 1)
