"""Replay the native Queen consumer after animated-glb-source/1 migration.

Run with qrenderdoc --python, ARP_REPO_ROOT, ARP_QUEEN_SOURCE_CAPTURE pointing
to the RDC and ARP_QUEEN_SOURCE_PHASE set explicitly to running or result.
The offsets below are checked against the actual reflected palette members
and vertex input descriptions before any captured bytes are interpreted.
"""
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import (
    descriptor_resource, flatten, rgba8_pixels, texture_bytes, write_bmp,
)


def main():
    path = Path(os.environ['ARP_QUEEN_SOURCE_CAPTURE'])
    report = dict(success=False, capture=str(path), draws=[], productionReady=False)
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        assert capture.OpenFile(str(path), '', None).code == rd.ResultCode.Succeeded
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status.code == rd.ResultCode.Succeeded
        assert controller.GetAPIProperties().pipelineType == rd.GraphicsAPI.Vulkan
        textures = {t.resourceId: t for t in controller.GetTextures()}
        final = None
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            targets = [descriptor_resource(t) for t in pipe.GetOutputTargets()]
            targets = [t for t in targets if t in textures]
            depth_target = descriptor_resource(pipe.GetDepthTarget())
            depth_only = not targets
            if depth_only and depth_target in textures:
                targets = [depth_target]
            if not targets:
                continue
            texture = textures[targets[0]]
            if not depth_only and (texture.width, texture.height) == (1280, 720):
                final = (action.eventId, targets[0])
            reflection = pipe.GetShaderReflection(rd.ShaderStage.Vertex)
            if not reflection:
                continue
            palette_blocks = []
            for index, block in enumerate(reflection.constantBlocks):
                if not block.bufferBacked or block.byteSize != 256 * 64 + 48:
                    continue
                palette_blocks.append((index, block))
            if not palette_blocks:
                continue
            assert len(palette_blocks) == 1
            index, block = palette_blocks[0]
            variables = block.variables
            while len(variables) == 1 and variables[0].type.members:
                variables = variables[0].type.members
            fields = {v.name: v for v in variables}
            assert fields['skinMatrices'].byteOffset == 0
            assert fields['jointCount'].byteOffset == 256 * 64 + 32
            descriptor = pipe.GetConstantBlock(rd.ShaderStage.Vertex, index, 0).descriptor
            palette = bytes(controller.GetBufferData(
                descriptor.resource, descriptor.byteOffset, block.byteSize))
            joint_count, = struct.unpack_from('<I', palette, fields['jointCount'].byteOffset)
            assert joint_count == 129, joint_count
            matrices = struct.unpack_from('<' + 'f' * joint_count * 16, palette)
            assert all(math.isfinite(value) for value in matrices)
            inputs = pipe.GetVertexInputs()
            assert len(inputs) == 5
            joints, weights = inputs[3:5]
            assert joints.vertexBuffer == weights.vertexBuffer
            stream = pipe.GetVBuffers()[joints.vertexBuffer]
            assert stream.byteStride == 76
            count = action.numIndices
            assert count in (18324, 19272), count
            data = bytes(controller.GetBufferData(stream.resourceId,
                stream.byteOffset + action.vertexOffset * stream.byteStride,
                count * stream.byteStride))
            used = set()
            for vertex in range(count):
                offset = vertex * stream.byteStride
                ids = struct.unpack_from('<4f', data, offset + joints.byteOffset)
                values = struct.unpack_from('<4f', data, offset + weights.byteOffset)
                assert all(math.isfinite(v) and 0 <= v <= 1 for v in values)
                assert abs(sum(values) - 1) <= 1.e-4
                assert all(math.isfinite(v) and v == int(v) and 0 <= v < joint_count for v in ids)
                used.update(int(v) for v, weight in zip(ids, values) if weight > 0)
            assert min(used) >= 67 and max(used) < 129
            post = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            assert post.numIndices == count and post.vertexByteStride >= 16
            post_bytes = bytes(controller.GetBufferData(post.vertexResourceId,
                post.vertexByteOffset, post.vertexByteStride * post.numIndices))
            positions = [struct.unpack_from('<4f', post_bytes, v * post.vertexByteStride)
                         for v in range(post.numIndices)]
            assert all(math.isfinite(v) for p in positions for v in p)
            assert any(abs(p[3]) > 0 for p in positions)
            report['draws'].append(dict(event=action.eventId, vertices=count,
                extent=[texture.width, texture.height], paletteJoints=joint_count,
                usedJointRange=[min(used), max(used)], checkedFinitePostVs=len(positions),
                vertexStride=stream.byteStride, paletteBytes=len(palette), depthOnly=depth_only))
        assert final is not None
        phase = os.environ['ARP_QUEEN_SOURCE_PHASE']
        assert phase in ('running', 'result')
        expected = {19272} if phase == 'running' else {18324, 19272}
        world = [d for d in report['draws'] if not d['depthOnly']]
        shadow = [d for d in report['draws'] if d['depthOnly']]
        assert {d['vertices'] for d in world} == expected
        assert {d['vertices'] for d in shadow} == expected
        controller.SetFrameEvent(final[0], True)
        texture = textures[final[1]]
        pixels = rgba8_pixels(texture_bytes(controller, final[1]), texture.format.Name())
        assert len(set(pixels[::97])) > 30
        image = path.with_suffix('.bmp')
        write_bmp(str(image), texture.width, texture.height, pixels)
        errors = [str(m.description) for m in controller.GetDebugMessages()
                  if m.severity in (rd.MessageSeverity.High, rd.MessageSeverity.Medium)]
        assert not errors, errors
        report.update(success=True, image=str(image), finalEvent=final[0], replayErrors=errors,
                      expectedPhase=phase, shadowAndColorGeometryMatch=True)
    except Exception:
        report['error'] = traceback.format_exc()
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()
        path.with_suffix('.replay.json').write_text(json.dumps(report, indent=2))
    return report['success']


if __name__ == '__main__':
    os._exit(0 if main() else 1)
