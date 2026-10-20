"""qrenderdoc oracle for the real RenderSystem/FrameGraph ordered workload."""
import json
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import (
    flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp)


def analyze(path):
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(str(path), '', None).code != rd.ResultCode.Succeeded:
        raise RuntimeError('ORDERED-CAPTURE-OPEN')
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded or not controller:
        capture.Shutdown()
        raise RuntimeError('ORDERED-CAPTURE-REPLAY')
    report = dict(capture=str(path), nativeApi=str(controller.GetAPIProperties().pipelineType), draws=[])
    try:
        textures = {t.resourceId: t for t in controller.GetTextures()}
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall or action.numIndices != 3:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            streams = pipe.GetVBuffers()
            attributes = [a for a in pipe.GetVertexInputs() if a.used]
            if [(a.name,a.format.compCount) for a in attributes] != [
                    ('TEXCOORD0',3), ('TEXCOORD1',2), ('TEXCOORD2',4)]:
                continue
            expected = (-1.,-1.,.25,.5,.5,1.,1.,1.,1., -1.,3.,.25,.5,.5,1.,1.,1.,1.,
                        3.,-1.,.25,.5,.5,1.,1.,1.,1.)
            # ANGLE can materialize separate native streams. Decode the actual
            # reflected attribute bindings instead of assuming interleaved storage.
            actual = []
            for vertex in range(3):
                for attribute in attributes:
                    if (attribute.format.compType != rd.CompType.Float
                            or attribute.format.compByteWidth != 4 or attribute.perInstance):
                        raise RuntimeError('ORDERED-CAPTURE-ATTRIBUTE-FORMAT')
                    stream = streams[attribute.vertexBuffer]
                    offset = stream.byteOffset + attribute.byteOffset + vertex*stream.byteStride
                    data = bytes(controller.GetBufferData(stream.resourceId, offset, attribute.format.compCount*4))
                    actual.extend(struct.unpack('<' + str(attribute.format.compCount) + 'f', data))
            if tuple(actual) != expected:
                raise RuntimeError('ORDERED-CAPTURE-ORIGINAL-VERTICES')
            targets = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
            targets = [rid for rid in targets if rid in textures]
            if len(targets) != 1:
                raise RuntimeError('ORDERED-CAPTURE-TARGET-COUNT')
            target = textures[targets[0]]
            extent = (target.width, target.height)
            if extent not in ((64,64), (48,32)):
                raise RuntimeError('ORDERED-CAPTURE-TARGET-EXTENT: ' + repr(extent))
            pixels = rgba8_pixels(texture_bytes(controller, targets[0]), target.format.Name())
            color = tuple(pixels[0])
            if color not in ((255,0,255,255), (0,255,255,255)) or len(pixels) != extent[0]*extent[1] or any(tuple(p) != color for p in pixels):
                raise RuntimeError('ORDERED-CAPTURE-OUTPUT-PIXELS: ' + repr(color))
            sampled = {descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)}
            sampled.discard(rd.ResourceId.Null())
            if len(sampled) != 1 or targets[0] in sampled:
                raise RuntimeError('ORDERED-CAPTURE-SAMPLED-OWNER')
            source_id = next(iter(sampled))
            source = textures[source_id]
            source_pixels = rgba8_pixels(texture_bytes(controller, source_id), source.format.Name())
            if (source.width,source.height) != (64,64) or len(source_pixels) != 4096 or any(tuple(p) != color for p in source_pixels):
                raise RuntimeError('ORDERED-CAPTURE-OFFSCREEN-CLEAR-TO-SAMPLED')
            shaders = [pipe.GetShaderReflection(s) for s in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment)]
            if any(s is None for s in shaders):
                raise RuntimeError('ORDERED-CAPTURE-SHADER-PAYLOAD')
            image = path.with_name(path.stem + '-ordered-' + str(action.eventId) + '.bmp')
            write_bmp(str(image), *extent, pixels)
            report['draws'].append(dict(event=action.eventId, extent=extent, color=color,
                sampledSource=str(source_id), output=str(targets[0]), verifiedSourcePixels=4096,
                verifiedOutputPixels=len(pixels), image=str(image),
                attributes=[dict(name=a.name,buffer=a.vertexBuffer,offset=a.byteOffset,
                    stride=streams[a.vertexBuffer].byteStride,components=a.format.compCount) for a in attributes]))
        errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        if errors:
            raise RuntimeError('ORDERED-CAPTURE-REPLAY-ERRORS: ' + repr(errors))
        report['replayErrors'] = errors
        return report
    finally:
        controller.Shutdown()
        capture.Shutdown()


output = Path(os.environ['ARP_WEBGL_PROGRAM_CAPTURE'])
result = dict(success=False)
try:
    runtime = json.loads((output/'runtime.json').read_text())
    if (not runtime.get('success') or runtime.get('frames') != 8
            or runtime.get('type') != 'RenderSystemOrderedGraphicsSmoke'
            or runtime.get('gpuCompletionClaimed') is not False):
        raise RuntimeError('ORDERED-CAPTURE-RUNTIME')
    frames = [analyze(path) for path in sorted(output.glob('program_frame*.rdc'))]
    result['frames'] = frames
    draws = [draw for frame in frames for draw in frame['draws']]
    if ({tuple(d['extent']) for d in draws} != {(64,64),(48,32)}
            or {tuple(d['color']) for d in draws} != {(255,0,255,255),(0,255,255,255)}):
        raise RuntimeError('ORDERED-CAPTURE-RESIZE-COLOR-COVERAGE')
    result.update(success=True, frames=frames, verifiedDraws=len(draws),
        image=draws[-1]['image'], gpuCompletionClaimed=False, productionReady=False,
        scope='Original RenderSystem/FrameGraph WebGL1 Prototype; platform readiness remains open')
except Exception:
    result['error'] = traceback.format_exc()
(output/'replay.json').write_text(json.dumps(result, indent=2))
os._exit(0 if result['success'] else 1)
