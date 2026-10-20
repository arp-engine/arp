"""RenderDoc verification of original Scene2D vertices, atlas identity and pixels."""
import json
import hashlib
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp
from webgl_product_native_state import WebGlProductNativeState

OUTPUT = Path(os.environ['ARP_WEBGL_PROGRAM_CAPTURE'])
SOURCE = json.loads((OUTPUT / 'scene2d-inputs.json').read_text())
CORNERS = ((0, 0), (1, 0), (1, 1), (0, 0), (1, 1), (0, 1))


def analyze(path):
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(str(path), '', None).code != rd.ResultCode.Succeeded:
        raise RuntimeError('SCENE2D-CAPTURE-OPEN')
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded or not controller:
        capture.Shutdown()
        raise RuntimeError('SCENE2D-CAPTURE-REPLAY')
    report = dict(capture=str(path), nativeApi=str(controller.GetAPIProperties().pipelineType), draws=[])
    try:
        textures = {t.resourceId: t for t in controller.GetTextures()}
        group = []
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall or action.numIndices != 6:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            attributes = [a for a in pipe.GetVertexInputs() if a.used]
            if [(a.name, a.format.compCount) for a in attributes] != [
                    ('TEXCOORD0', 2), ('TEXCOORD1', 2), ('TEXCOORD2', 4)]:
                continue
            ordinal = len(group)
            if ordinal > 2:
                raise RuntimeError('SCENE2D-CAPTURE-BATCH-COUNT')
            streams = pipe.GetVBuffers()
            vertices = []
            for index in range(6):
                components = []
                for attribute in attributes:
                    if (attribute.format.compType != rd.CompType.Float
                            or attribute.format.compByteWidth != 4 or attribute.perInstance):
                        raise RuntimeError('SCENE2D-CAPTURE-ATTRIBUTE-FORMAT')
                    stream = streams[attribute.vertexBuffer]
                    offset = stream.byteOffset + attribute.byteOffset + (index + action.vertexOffset) * stream.byteStride
                    data = bytes(controller.GetBufferData(stream.resourceId, offset, attribute.format.compCount * 4))
                    components.extend(struct.unpack('<' + str(attribute.format.compCount) + 'f', data))
                vertices.append(components)
            flip = ordinal == 2 and vertices[0][3] == 0
            for vertex, (cx, cy) in zip(vertices, CORNERS):
                expected = [-1 + ordinal * .5 + cx, -.5 + cy,
                    (0.5 if ordinal == 1 else 0) + cx * .5,
                    cy if flip else 1 - cy, 1, 1, 1, .5 if ordinal == 2 else 1]
                if vertex != expected:
                    raise RuntimeError('SCENE2D-CAPTURE-ORIGINAL-VERTEX: ' + repr((ordinal, vertex, expected)))
            sampled = {descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)}
            sampled.discard(rd.ResourceId.Null())
            if len(sampled) != 1:
                raise RuntimeError('SCENE2D-CAPTURE-ATLAS-BINDING')
            atlas = next(iter(sampled))
            source = textures[atlas]
            actual_source = rgba8_pixels(texture_bytes(controller, atlas), source.format.Name())
            if ((source.width, source.height) != tuple(SOURCE['extent'])
                    or [list(p) for p in actual_source] != SOURCE['rgba']):
                raise RuntimeError('SCENE2D-CAPTURE-ATLAS-PIXELS')
            targets = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
            targets = [rid for rid in targets if rid in textures]
            if len(targets) != 1 or targets[0] == atlas:
                raise RuntimeError('SCENE2D-CAPTURE-TARGET')
            target = textures[targets[0]]
            w, h = target.width, target.height
            native_state = WebGlProductNativeState.capture(controller, pipe, 'scene2d', ordinal, [w,h])
            post_vs = WebGlProductNativeState.post_vs(controller,
                [[-1+ordinal*.5+x, -.5+y, 0, 1] for x,y in CORNERS])
            if (w, h) not in ((64, 64), (48, 32)):
                raise RuntimeError('SCENE2D-CAPTURE-EXTENT')
            pixels = rgba8_pixels(texture_bytes(controller, targets[0]), target.format.Name())
            if len(pixels) != w * h:
                raise RuntimeError('SCENE2D-CAPTURE-OUTPUT-SIZE')
            for y in range(h):
                for x in range(w):
                    expected = [0, 0, 0, 255]
                    if 4 <= x < w - 4 and h // 4 <= y < h * 3 // 4:
                        for sprite in range(ordinal + 1):
                            left = sprite * w // 4
                            if not left <= x < left + w // 2:
                                continue
                            u = (x - left + .5) / (w / 2)
                            # Original canvas readback rows follow the native
                            # WebGlShaderCoordinates/1 contract; atlas rows are
                            # independently verified against the CPU upload.
                            logical_y = h - 1 - y
                            v = 1 - (logical_y - h / 4 + .5) / (h / 2)
                            if sprite == 2 and flip:
                                v = 1 - v
                            texel = SOURCE['rgba'][int(v * 8) * 16 + (8 if sprite == 1 else 0) + int(u * 8)]
                            alpha = texel[3] / 255 * (.5 if sprite == 2 else 1)
                            expected[:3] = [math.floor(texel[c] * alpha + expected[c] * (1 - alpha) + .5) for c in range(3)]
                    actual = pixels[y * w + x]
                    if any(abs(actual[c] - expected[c]) > 1 for c in range(4)):
                        raise RuntimeError('SCENE2D-CAPTURE-OUTPUT-PIXEL: ' + repr((ordinal, x, y, actual, expected)))
            image = path.with_name(path.stem + '-scene2d-' + str(action.eventId) + '.bmp')
            top_left_pixels = [pixel for y in range(h - 1, -1, -1) for pixel in pixels[y * w:(y + 1) * w]]
            write_bmp(str(image), w, h, top_left_pixels)
            draw = dict(event=action.eventId, extent=[w, h], batch=ordinal, atlas=str(atlas),
                nativeState=native_state, outputTarget=str(targets[0]), outputFormat=target.format.Name(),
                depthTarget=str(descriptor_resource(pipe.GetDepthTarget())),
                outputSha256=hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest(),
                verifiedPostVsVertices=len(post_vs), postVsPositions=post_vs,
                flipY=flip, image=str(image), verifiedVertices=6,
                verifiedAtlasPixels=len(actual_source), verifiedOutputPixels=len(pixels))
            group.append(draw)
            report['draws'].append(draw)
            if ordinal == 2:
                if group[0]['atlas'] != group[2]['atlas'] or group[0]['atlas'] == group[1]['atlas']:
                    raise RuntimeError('SCENE2D-CAPTURE-ABA-ORIGINAL-OWNERS')
                group = []
        if group:
            raise RuntimeError('SCENE2D-CAPTURE-INCOMPLETE-BATCHES')
        errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        if errors:
            raise RuntimeError('SCENE2D-CAPTURE-REPLAY-ERRORS: ' + repr(errors))
        report['replayErrors'] = errors
        return report
    finally:
        controller.Shutdown()
        capture.Shutdown()


result = dict(success=False)
try:
    runtime = json.loads((OUTPUT / 'runtime.json').read_text())
    if (not runtime.get('success') or runtime.get('frames') != 6
            or runtime.get('type') != 'Scene2DOrderedProductSmoke'
            or runtime.get('gpuCompletionClaimed') is not False):
        raise RuntimeError('SCENE2D-CAPTURE-RUNTIME')
    frames = [analyze(path) for path in sorted(OUTPUT.glob('program_frame*.rdc'))]
    result['frames'] = frames
    draws = [draw for frame in frames for draw in frame['draws']]
    if ({tuple(d['extent']) for d in draws} != {(64,64), (48,32)}
            or {d['flipY'] for d in draws if d['batch'] == 2} != {False, True}):
        raise RuntimeError('SCENE2D-CAPTURE-RESIZE-FLIP-COVERAGE')
    result.update(success=True, verifiedDraws=len(draws), image=draws[-1]['image'],
        gpuCompletionClaimed=False, productionReady=False)
except Exception:
    result['error'] = traceback.format_exc()
(OUTPUT / 'replay.json').write_text(json.dumps(result, indent=2))
os._exit(0 if result['success'] else 1)
