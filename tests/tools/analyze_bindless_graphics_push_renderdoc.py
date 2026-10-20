"""Independently replay original public Graphics push bytes and both GPU stages."""
import hashlib
import json
import os
from pathlib import Path
import struct
import traceback

import renderdoc as rd

report = {'scope': 'original public Graphics push224 VS/FS numeric4 clean/fault'}
capture = controller = None
try:
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ['ARP_BINDLESS_CAPTURE_PATH'], '', None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    package = Path(os.environ['ARP_BINDLESS_MATRIX_PACKAGE_DIR'])
    hashes = {hashlib.sha256((package / ('GraphicsMatrixPush.native.' + entry + '.spv')).read_bytes()).hexdigest(): entry
              for entry in ['vertexMain', 'fragmentMain']}
    matrices = [
        [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
        [2, 1, 4, 3, 6, 5, 8, 7, 10, 9, 12, 11, 14, 13, 16, 15],
        [1, 0, 0, -.125, 0, 1, 0, -.125, 0, 0, 1, 0, 0, 0, 0, 1]]
    flags = [2, 4, 8, 16]
    expected_vs = [(-1.125, -1.125, 0., 1., 2., 4., 8., 16.),
                   (2.875, -1.125, 0., 1., 2., 4., 8., 16.),
                   (-1.125, 2.875, 0., 1., 2., 4., 8., 16.)]

    def walk(actions):
        for action in actions:
            yield action
            yield from walk(action.children)

    actions = list(walk(controller.GetRootActions()))
    lengths = {b.resourceId: b.length for b in controller.GetBuffers()}
    evidence = []
    for action in actions:
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.eventId, True)
        pipe = controller.GetPipelineState()
        reflections = [pipe.GetShaderReflection(stage) for stage in [rd.ShaderStage.Vertex, rd.ShaderStage.Fragment]]
        entries = [hashes.get(hashlib.sha256(bytes(shader.rawBytes)).hexdigest()) if shader else None for shader in reflections]
        if entries != ['vertexMain', 'fragmentMain']:
            raise RuntimeError('Unexpected selected original shader binaries: ' + str(entries))
        push = bytes(controller.GetVulkanPipelineState().pushconsts)
        if len(push) != 224:
            raise RuntimeError('Push block is not exactly224 bytes')
        cascade, fault, pad1, pad2 = struct.unpack_from('<4I', push, 208)
        if cascade != 2 or fault not in (0, 1) or pad1 or pad2:
            raise RuntimeError('Unexpected push scalar fields')
        expected_push = struct.pack('<52f4I', *(sum(matrices, []) + flags), 2, fault, 0, 0)
        if push != expected_push:
            raise RuntimeError('Frozen CPU row-memory push bytes differ')
        mesh = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
        if mesh.numIndices != 3 or mesh.vertexByteStride != 32:
            raise RuntimeError('Unexpected original Vertex output layout')
        vertices = bytes(controller.GetBufferData(mesh.vertexResourceId, mesh.vertexByteOffset,
                                                 mesh.vertexByteStride * mesh.numIndices))
        actual_vs = [struct.unpack_from('<8f', vertices, i * mesh.vertexByteStride) for i in range(3)]
        if actual_vs != expected_vs:
            raise RuntimeError('Vertex matrix arithmetic differs: ' + str(actual_vs))
        descriptors = [d.descriptor for d in pipe.GetAllUsedDescriptors()]
        results = {d.resource for d in descriptors if lengths.get(d.resource) == 16}
        if len(results) != 1:
            raise RuntimeError('Missing exact original Results buffer')
        values = list(struct.unpack('<4f', bytes(controller.GetBufferData(next(iter(results)), 0, 16))))
        if values != ([-999.] * 4 if fault else [2., 6., 16., 32.]):
            raise RuntimeError('Fragment result/poison differs: ' + str(values))
        targets = [t for t in pipe.GetOutputTargets() if t.resource != rd.ResourceId.Null()]
        if len(targets) != 1:
            raise RuntimeError('Missing original color target')
        color = bytes(controller.GetTextureData(targets[0].resource, rd.Subresource(0, 0, 0)))
        if color != bytes([255] * 16):
            raise RuntimeError('Color output differs: ' + color.hex())
        copied = next((a for a in actions if a.eventId > action.eventId and a.flags & rd.ActionFlags.Copy), None)
        if copied is None or lengths.get(copied.copyDestination) != 4:
            raise RuntimeError('Missing original fault readback')
        controller.SetFrameEvent(copied.eventId, True)
        bits = struct.unpack('<I', bytes(controller.GetBufferData(copied.copyDestination, 0, 4)))[0]
        if bits != fault:
            raise RuntimeError('Fault latch differs')
        evidence.append({'eventId': action.eventId, 'entries': entries,
                         'pushSha256': hashlib.sha256(push).hexdigest(), 'pushBytes': len(push),
                         'postVS': actual_vs, 'fragmentValues': values, 'faultBits': bits,
                         'colorRgba8': list(color)})
    if [e['faultBits'] for e in evidence] != [0, 1]:
        raise RuntimeError('Missing distinct clean/fault executions')
    report['executions'] = evidence
    report['validationMessages'] = [m.description for m in controller.GetDebugMessages()]
    if report['validationMessages']:
        raise RuntimeError('Vulkan validation reported messages')
    report['success'] = True
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
finally:
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    Path(os.environ['ARP_BINDLESS_CAPTURE_OUTPUT']).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    os._exit(0 if report.get('success') else 1)
