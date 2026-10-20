"""RenderDoc oracle for the original C++ prepared-command WebGL workload."""
import json
import os
from pathlib import Path
import sys
import traceback
import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp


def analyze(path, catalog=False, multipass=False, canvas=False, repeated=False):
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(str(path), '', None).code != rd.ResultCode.Succeeded:
        raise RuntimeError('WEBGL-PREPARED-CAPTURE-OPEN')
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded or not controller:
        raise RuntimeError('WEBGL-PREPARED-CAPTURE-REPLAY')
    report = dict(capture=str(path), nativeApi=str(controller.GetAPIProperties().pipelineType), draws=[], candidates=[], surfaceClears=[])
    try:
        textures = {t.resourceId: t for t in controller.GetTextures()}
        for action in flatten(controller.GetRootActions()):
            if action.flags & rd.ActionFlags.Clear:
                controller.SetFrameEvent(action.eventId, True)
                target = action.copyDestination
                if target in textures:
                    texture = textures[target]
                    extent = (texture.width, texture.height)
                    if extent in ((48, 32), (64, 64)):
                        pixels = rgba8_pixels(texture_bytes(controller, target), texture.format.Name())
                        if len(pixels) == extent[0] * extent[1] and all(tuple(p) == (0,0,255,255) for p in pixels):
                            report['surfaceClears'].append(dict(event=action.eventId, target=str(target), extent=extent, verifiedPixels=len(pixels), rgba=(0,0,255,255)))
                            image = path.with_name(path.stem + '-surface-' + str(extent[0]) + 'x' + str(extent[1]) + '.bmp')
                            write_bmp(str(image), *extent, pixels)
                            report['surfaceClears'][-1]['image'] = str(image)
            if not action.flags & rd.ActionFlags.Drawcall or action.numIndices != 3:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            streams = pipe.GetVBuffers()
            targets = [descriptor_resource(t) for t in pipe.GetOutputTargets()]
            report['candidates'].append(dict(event=action.eventId, flags=str(action.flags),
                streams=[int(v.byteStride) for v in streams],
                outputs=[dict(id=str(t), width=textures[t].width, height=textures[t].height) for t in targets if t in textures]))
            attributes = [a for a in pipe.GetVertexInputs() if a.used]
            report['candidates'][-1]['attributes'] = [dict(name=a.name, buffer=a.vertexBuffer, offset=a.byteOffset, components=a.format.compCount) for a in attributes]
            if [a.format.compCount for a in attributes] != [3, 2, 4]:
                continue
            targets = [t for t in targets if t in textures and textures[t].width == textures[t].height and textures[t].width in (32,64)]
            if len(targets) != 1:
                continue
            if textures[targets[0]].width == 32:
                if not multipass:
                    raise RuntimeError('WEBGL-MULTIPASS-RUNTIME-IDENTITY')
                target = targets[0]
                pixels = rgba8_pixels(texture_bytes(controller, target), textures[target].format.Name())
                if len(pixels) != 1024 or any(tuple(p) != (255,0,255,255) for p in pixels):
                    raise RuntimeError('WEBGL-MULTIPASS-OFFSCREEN-PIXELS: ' + repr((str(path),action.eventId)))
                if bool(pipe.GetColorBlends()[0].enabled):
                    raise RuntimeError('WEBGL-MULTIPASS-FROZEN-PIPELINE')
                report['draws'].append(dict(event=action.eventId,indexed=False,verifiedPixels=1024,offscreenSampling=True))
                image = path.with_name(path.stem + '-offscreen.bmp')
                write_bmp(str(image),32,32,pixels)
                report['offscreenImage'] = str(image)
                continue
            target = targets[0]
            pixels = rgba8_pixels(texture_bytes(controller, target), textures[target].format.Name())
            canvas_color = tuple(pixels[0]) if pixels else None
            allowed_canvas = ((255,0,255,255), (0,255,255,255)) if repeated else ((255,0,255,255),)
            if canvas and len(pixels)==4096 and canvas_color in allowed_canvas and all(tuple(p)==canvas_color for p in pixels):
                if bool(pipe.GetColorBlends()[0].enabled):
                    raise RuntimeError('WEBGL-CANVAS-FROZEN-PIPELINE')
                report['draws'].append(dict(event=action.eventId,indexed=False,verifiedPixels=4096,canvas=True,rgba=canvas_color))
                suffix = '-canvas-cyan.bmp' if canvas_color == (0,255,255,255) else '-canvas.bmp'
                image=path.with_name(path.stem+suffix)
                write_bmp(str(image),64,64,pixels)
                report['canvasCyanImage' if canvas_color == (0,255,255,255) else 'canvasImage']=str(image)
                continue
            indexed = bool(action.flags & rd.ActionFlags.Indexed)
            expected_right = (0, 0, 255, 255) if indexed else (0, 255, 0, 255)
            for y in range(64):
                for x in range(64):
                    expected = (255, 0, 0, 255) if x < 32 else expected_right
                    if tuple(pixels[y * 64 + x]) != expected:
                        raise RuntimeError('WEBGL-PREPARED-DRAW-PIXEL: ' + repr((str(path), action.eventId, x, y, pixels[y * 64 + x], expected)))
            if bool(pipe.GetColorBlends()[0].enabled) != (catalog and not indexed):
                raise RuntimeError('WEBGL-PREPARED-FROZEN-PIPELINE-BLEND')
            report['draws'].append(dict(event=action.eventId, indexed=indexed, verifiedPixels=4096,
                                        redPixels=2048, greenPixels=0 if indexed else 2048))
            if not indexed:
                image = path.with_suffix('.bmp')
                rows = [pixels[i:i + 64] for i in range(0, len(pixels), 64)]
                write_bmp(str(image), 64, 64, [pixel for row in reversed(rows) for pixel in row])
                report['image'] = str(image)
        errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        if errors:
            raise RuntimeError('WEBGL-PREPARED-REPLAY-ERROR: ' + repr(errors))
        report['replayErrors'] = errors
        return report
    finally:
        controller.Shutdown()
        capture.Shutdown()


output = Path(os.environ['ARP_WEBGL_PROGRAM_CAPTURE'])
result = dict(success=False)
try:
    runtime = json.loads((output / 'runtime.json').read_text())
    if not runtime.get('success') or runtime.get('preparedDraws') != 2:
        raise RuntimeError('WEBGL-PREPARED-RUNTIME-FAILED')
    captures = sorted(output.glob('program_frame*.rdc'))
    if not captures:
        raise RuntimeError('WEBGL-PREPARED-CAPTURE-MISSING')
    frames = [analyze(path, runtime.get('pipelineCatalog') is True, runtime.get('multiPass') is True, runtime.get('canvasLease') is True, runtime.get('repeatedFrames', 0) >= 2) for path in captures]
    result["frames"] = frames
    draws = [draw for frame in frames for draw in frame['draws']]
    clears = [clear for frame in frames for clear in frame['surfaceClears']]
    if runtime.get('surfaceLifecycle') is True and {tuple(c['extent']) for c in clears} != {(48,32), (64,64)}:
        raise RuntimeError('WEBGL-SURFACE-FIRST-FRAME-CLEAR-COVERAGE: ' + repr(clears))
    if not any(d['indexed'] for d in draws) or not any(not d['indexed'] for d in draws):
        raise RuntimeError('WEBGL-PREPARED-INDEXED-NONINDEXED-COVERAGE')
    if runtime.get('multiPass') is True and not any(d.get('offscreenSampling') for d in draws):
        raise RuntimeError('WEBGL-MULTIPASS-CAPTURE-COVERAGE')
    if runtime.get('canvasLease') is True and not any(d.get('canvas') for d in draws):
        raise RuntimeError('WEBGL-CANVAS-CAPTURE-COVERAGE')
    if runtime.get('repeatedFrames', 0) >= 2:
        colors = {tuple(d['rgba']) for d in draws if d.get('canvas')}
        if colors != {(255,0,255,255), (0,255,255,255)}:
            raise RuntimeError('WEBGL-CANVAS-REUSE-UPDATED-OUTCOMES: ' + repr(colors))
    result.update(success=True, frames=frames, verifiedDraws=len(draws), verifiedSurfaceClears=clears,
                  image=next(frame['image'] for frame in reversed(frames) if 'image' in frame),
                  scope='PreparedRenderCommands, CanvasReuse and SurfaceLifecycle Prototype; exact catalog/Device/multipass/reuse/lifecycle evidence gated by runtime flags; target platform and production remain open',
                  productionBackendValidated=False, gpuCompletionClaimed=False)
except Exception:
    result['error'] = traceback.format_exc()
(output / 'replay.json').write_text(json.dumps(result, indent=2))
os._exit(0 if result['success'] else 1)
