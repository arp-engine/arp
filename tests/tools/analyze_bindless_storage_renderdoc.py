"""Independently replay public storage or mixed depth/copy/reduce/read streams."""
import hashlib
import json
import os
from pathlib import Path
import struct
import traceback

import renderdoc as rd

mixed = os.environ.get('ARP_BINDLESS_MIXED_CAPTURE') == '1'
report = {'scope': 'public Native Execution11 D32/copy/reduce/read Content15' if mixed
          else 'public Native Execution9 RGBA16F clear/store/read Content13'}
capture = controller = None
try:
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ['ARP_BINDLESS_CAPTURE_PATH'], '', None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    package = Path(os.environ['ARP_BINDLESS_STORAGE_PACKAGE_DIR'])
    entry_names = ['depthPyramidCopyMain', 'depthPyramidReduceMain', 'depthPyramidReadMain'] if mixed else ['storeMain', 'readMain']
    prefix = 'DepthPyramid' if mixed else 'StorageVector'
    hashes = {hashlib.sha256((package / (prefix + '.native.' + entry + '.spv')).read_bytes()).hexdigest(): entry
              for entry in entry_names}

    def walk(actions):
        for action in actions:
            yield action
            yield from walk(action.children)

    actions = list(walk(controller.GetRootActions()))
    buffers = {b.resourceId: b.length for b in controller.GetBuffers()}
    textures = {t.resourceId: t for t in controller.GetTextures()}
    evidence = []
    entries = []
    source_depths = None
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:
            continue
        controller.SetFrameEvent(action.eventId, True)
        pipe = controller.GetPipelineState()
        shader = pipe.GetShaderReflection(rd.ShaderStage.Compute)
        digest = hashlib.sha256(bytes(shader.rawBytes)).hexdigest() if shader else None
        if digest not in hashes:
            continue
        entry = hashes[digest]
        entries.append(entry)
        descriptors = [d.descriptor for d in pipe.GetAllUsedDescriptors() if d.access.stage == rd.ShaderStage.Compute]
        if mixed and entry == 'depthPyramidCopyMain':
            if list(action.dispatchDimension) != [1, 1, 1]:
                raise RuntimeError('Wrong copy dispatch dimensions')
            depth_images = {d.resource for d in descriptors if d.resource in textures and textures[d.resource].format.compType == rd.CompType.Depth}
            if len(depth_images) != 1:
                raise RuntimeError('Missing independent D32 producer')
            depth_image = next(iter(depth_images))
            depth_bytes = bytes(controller.GetTextureData(depth_image, rd.Subresource(0, 0, 0)))
            source_depths = struct.unpack('<4f', depth_bytes)
            if any(abs(value - expected) >= 1e-7 for value, expected in zip(source_depths, [.01125, .01375, .01125, .01375])):
                raise RuntimeError('D32 producer differs: ' + str(source_depths))
        if mixed and entry == 'depthPyramidReduceMain' and list(action.dispatchDimension) != [1, 1, 1]:
            raise RuntimeError('Wrong reduce dispatch dimensions')
        if entry != ('depthPyramidReadMain' if mixed else 'readMain'):
            continue
        dimensions = list(action.dispatchDimension)
        if dimensions not in [[2, 1, 1], [3, 1, 1]]:
            raise RuntimeError('Unexpected dispatch dimensions: ' + str(dimensions))
        fault = dimensions[0] - 2
        outputs = []
        result_size = 32 if mixed else 16
        for resource in set(d.resource for d in descriptors if buffers.get(d.resource) == result_size):
            data = bytes(controller.GetBufferData(resource, 0, result_size))
            values = list(struct.unpack('<8f' if mixed else '<4f', data))
            if not mixed and values not in [[.75, .125, .25, 1.], [.75, .125, 1.25, 1.]]:
                raise RuntimeError('Storage result differs: ' + str(values))
            outputs.append({'resource': str(resource), 'values': values, 'sha256': hashlib.sha256(data).hexdigest()})
        if (mixed and len(outputs) != 1) or (not mixed and sorted(o['values'][2] for o in outputs) != [.25, 1.25]):
            raise RuntimeError('Missing independent output buffers')
        images = set(d.resource for d in descriptors if d.resource in textures)
        if len(images) != 1:
            raise RuntimeError('Expected one original parent image')
        image = next(iter(images))
        shape = textures[image]
        if (shape.width, shape.height, shape.mips, shape.arraysize) != (4, 4, 3, 1):
            raise RuntimeError('Wrong original texture shape')
        packed = bytes(controller.GetTextureData(image, rd.Subresource(1, 0, 0)))
        packed += bytes(controller.GetTextureData(image, rd.Subresource(2, 0, 0)))
        if mixed:
            if source_depths is None:
                raise RuntimeError('Copy producer was not observed')
            halves = struct.unpack('<20H', packed)
            copied = []
            for pixel, depth in enumerate(source_depths):
                nearest = struct.unpack('<H', struct.pack('<e', depth))[0]
                toward_zero = nearest - int(struct.unpack('<e', struct.pack('<H', nearest))[0] > depth)
                value = halves[pixel * 4]
                if value not in (nearest, toward_zero) or halves[pixel * 4:pixel * 4 + 4] != (value, value, value, 0x3c00):
                    raise RuntimeError('Wrong exact format conversion at pixel ' + str(pixel))
                copied.append(value)
            expected = struct.pack('<20H', *(lane for value in copied for lane in (value, value, value, 0x3c00)),
                                   max(copied), min(copied), max(copied), 0x3c00)
            result_values = struct.unpack('<4e', expected[:8]) + struct.unpack('<4e', expected[32:])
            if outputs[0]['values'] != list(result_values):
                raise RuntimeError('Results do not consume the exact copy/reduce texels')
        else:
            expected = struct.pack('<4e', .75, .125, .25, 1.)
            expected += struct.pack('<4e', .0625, .25, .5, 1.) * 3
            expected += struct.pack('<4e', .75, .125, 1.25, 1.)
        if packed != expected:
            raise RuntimeError('RGBA16F written/untouched texels differ')
        copied = next((a for a in actions if a.eventId > action.eventId and a.flags & rd.ActionFlags.Copy), None)
        if copied is None or buffers.get(copied.copyDestination) != 4:
            raise RuntimeError('Missing original fault copy')
        controller.SetFrameEvent(copied.eventId, True)
        bits = struct.unpack('<I', bytes(controller.GetBufferData(copied.copyDestination, 0, 4)))[0]
        if bits != fault:
            raise RuntimeError('Wrong fault bits')
        evidence.append({'eventId': action.eventId, 'faultBits': bits, 'outputs': outputs,
                         'parentImage': str(image), 'texelsSha256': hashlib.sha256(packed).hexdigest(),
                         'texels': list(struct.unpack('<20e', packed)), 'faultCopyEventId': copied.eventId,
                         'sourceDepths': list(source_depths) if source_depths is not None else None})
    if entries != entry_names * 2:
        raise RuntimeError('Wrong authenticated entry order: ' + str(entries))
    if sorted(e['faultBits'] for e in evidence) != [0, 1]:
        raise RuntimeError('Missing clean/fault execution')
    report['executions'] = evidence
    report['validationMessages'] = [m.description for m in controller.GetDebugMessages()]
    if report['validationMessages']:
        raise RuntimeError('Native validation messages present')
    report['success'] = True
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
finally:
    Path(os.environ['ARP_BINDLESS_CAPTURE_OUTPUT']).write_text(json.dumps(report, indent=2), encoding='utf-8')
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    os._exit(0 if report.get('success') else 1)
