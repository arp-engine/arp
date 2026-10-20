"""Verify original bitmap GUI native vertices, sources and every draw's pixels."""
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
SOURCE = json.loads((OUTPUT / 'gui-inputs.json').read_text())
CORNERS = ((0, 0), (1, 0), (1, 1), (0, 0), (1, 1), (0, 1))


def analyze(path):
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(str(path), '', None).code != rd.ResultCode.Succeeded:
        raise RuntimeError('GUI-CAPTURE-OPEN')
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded or not controller:
        capture.Shutdown()
        raise RuntimeError('GUI-CAPTURE-REPLAY')
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
            if [(a.name, a.format.compCount) for a in attributes] != [('TEXCOORD', 2)]:
                continue
            ordinal = len(group)
            attribute = attributes[0]
            if (attribute.format.compType != rd.CompType.Float
                    or attribute.format.compByteWidth != 4 or attribute.perInstance):
                raise RuntimeError('GUI-CAPTURE-ATTRIBUTE-FORMAT')
            stream = pipe.GetVBuffers()[attribute.vertexBuffer]
            for index, expected in enumerate(CORNERS):
                offset = stream.byteOffset + attribute.byteOffset + (index + action.vertexOffset) * stream.byteStride
                actual = struct.unpack('<2f', bytes(controller.GetBufferData(stream.resourceId, offset, 8)))
                if actual != expected:
                    raise RuntimeError('GUI-CAPTURE-ORIGINAL-CORNERS')
            sampled = {descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)}
            sampled.discard(rd.ResourceId.Null())
            if len(sampled) != 1:
                raise RuntimeError('GUI-CAPTURE-SOURCE-BINDING')
            source_id = next(iter(sampled))
            source = textures[source_id]
            actual_source = rgba8_pixels(texture_bytes(controller, source_id), source.format.Name())
            if ordinal < 3:
                expected_source = SOURCE['glyph' if ordinal == 2 else 'image']
                if ((source.width, source.height) != tuple(expected_source['extent'])
                        or [list(p) for p in actual_source] != expected_source['rgba']):
                    raise RuntimeError('GUI-CAPTURE-ORIGINAL-SOURCE-PIXELS')
            else:
                if (source.width, source.height) != (64, 64) or any(
                        any(abs(p[c] - (26, 51, 77, 128)[c]) > 1 for c in range(4)) for p in actual_source):
                    raise RuntimeError('GUI-CAPTURE-OFFSCREEN-PIXELS')
            targets = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
            targets = [rid for rid in targets if rid in textures]
            if len(targets) != 1 or targets[0] == source_id:
                raise RuntimeError('GUI-CAPTURE-TARGET')
            target = textures[targets[0]]
            w, h = target.width, target.height
            native_state = WebGlProductNativeState.capture(controller, pipe, 'gui', ordinal, [w,h])
            left, top, width, height = ((0,0,1,1), (0,0,.5,.5), (.25,.25,.5,.5), (.5,.5,.5,.5))[ordinal]
            post_vs = WebGlProductNativeState.post_vs(controller,
                [[(left+x*width)*2-1, (top+y*height)*2-1, 0, 1] for x,y in CORNERS])
            if (w, h) not in ((64, 64), (48, 32)):
                raise RuntimeError('GUI-CAPTURE-EXTENT')
            pixels = rgba8_pixels(texture_bytes(controller, targets[0]), target.format.Name())
            if len(pixels) != w * h:
                raise RuntimeError('GUI-CAPTURE-OUTPUT-SIZE')
            for y in range(h):
                # Original WebGlShaderCoordinates/1 and raw native readback,
                # independently fixed by the browser oracle and source upload.
                top = h - 1 - y
                for x in range(w):
                    expected = [26, 26, 26, 255]
                    if ordinal >= 1 and 4 <= x < w // 2 and 4 <= top < h // 2:
                        tx, ty = int((x + .5) / (w / 2) * 8), int((top + .5) / (h / 2) * 8)
                        expected[:3] = SOURCE['image']['rgba'][ty * 16 + tx][:3]
                    if ordinal >= 2 and w // 4 <= x < w * 3 // 4 and h // 4 <= top < h * 3 // 4:
                        tx = int((x - w / 4 + .5) / (w / 2) * 6)
                        ty = int((top - h / 4 + .5) / (h / 2) * 8)
                        if SOURCE['glyph']['rgba'][ty * 8 + tx][3]:
                            expected = [255, 255, 255, 255]
                    if ordinal >= 3 and x >= w // 2 and top >= h // 2:
                        texel = actual_source[0]
                        expected[:3] = [math.floor(texel[c] + expected[c] * (1 - texel[3] / 255) + .5)
                                        for c in range(3)]
                    actual = pixels[y * w + x]
                    if any(abs(actual[c] - expected[c]) > 1 for c in range(4)):
                        raise RuntimeError('GUI-CAPTURE-OUTPUT-PIXEL: ' + repr((ordinal, x, y, actual, expected)))
            image = path.with_name(path.stem + '-gui-' + str(action.eventId) + '.bmp')
            write_bmp(str(image), w, h, [p for y in range(h - 1, -1, -1) for p in pixels[y * w:(y + 1) * w]])
            draw = dict(event=action.eventId, extent=[w, h], command=ordinal, source=str(source_id),
                nativeState=native_state, outputTarget=str(targets[0]), outputFormat=target.format.Name(),
                depthTarget=str(descriptor_resource(pipe.GetDepthTarget())),
                outputSha256=hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest(),
                verifiedPostVsVertices=len(post_vs), postVsPositions=post_vs,
                        image=str(image), verifiedVertices=6, verifiedSourcePixels=len(actual_source),
                        verifiedOutputPixels=len(pixels))
            group.append(draw)
            report['draws'].append(draw)
            if ordinal == 3:
                if (group[0]['source'] != group[1]['source']
                        or len({d['source'] for d in group}) != 3):
                    raise RuntimeError('GUI-CAPTURE-ORIGINAL-OWNERS')
                group = []
        if group:
            raise RuntimeError('GUI-CAPTURE-INCOMPLETE-COMMANDS')
        errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        if errors:
            raise RuntimeError('GUI-CAPTURE-REPLAY-ERRORS: ' + repr(errors))
        report['replayErrors'] = errors
        return report
    finally:
        controller.Shutdown()
        capture.Shutdown()


result = dict(success=False)
try:
    runtime = json.loads((OUTPUT / 'runtime.json').read_text())
    if (not runtime.get('success') or runtime.get('frames') != 6
            or runtime.get('type') != 'GuiOrderedProductSmoke'
            or runtime.get('gpuCompletionClaimed') is not False):
        raise RuntimeError('GUI-CAPTURE-RUNTIME')
    frames = [analyze(path) for path in sorted(OUTPUT.glob('program_frame*.rdc'))]
    result['frames'] = frames
    draws = [draw for frame in frames for draw in frame['draws']]
    if {tuple(d['extent']) for d in draws} != {(64, 64), (48, 32)}:
        raise RuntimeError('GUI-CAPTURE-RESIZE-COVERAGE')
    result.update(success=True, verifiedDraws=len(draws), image=draws[-1]['image'],
                  gpuCompletionClaimed=False, productionReady=False)
except Exception:
    result['error'] = traceback.format_exc()
(OUTPUT / 'replay.json').write_text(json.dumps(result, indent=2))
os._exit(0 if result['success'] else 1)
