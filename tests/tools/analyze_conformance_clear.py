"""Independent RenderDoc replay of the frozen common YAML clear workload."""
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback
import renderdoc as rd

root = Path(os.environ['ARP_REPO_ROOT'])
sys.path.insert(0, str(root / 'tests/tools'))
if os.environ.get('ARP_HOST_PYTHON_SITE'):
    sys.path.append(os.environ['ARP_HOST_PYTHON_SITE'])
import yaml
from analyze_ui_text_renderdoc import flatten, texture_bytes, rgba8_pixels, write_bmp
from renderdoc_texture_buffer_copy import RenderDocTextureBufferCopy

output = Path(os.environ['ARP_CONFORMANCE_CAPTURE'])
report = {'success': False, 'readinessPromoted': False, 'gpuCompletionClaimed': False}
try:
    source = (output / 'conformance-workload.yaml').read_bytes()
    fixture = yaml.safe_load(source)
    if (set(fixture) != {'type', 'contractVersion', 'id', 'frames'}
            or fixture['type'] != 'ConformanceClearWorkload' or fixture['contractVersion'] != 1):
        raise RuntimeError('CONFORMANCE-CAPTURE-FIXTURE-SCHEMA')
    expected = {(f['extent']['width'], f['extent']['height'], tuple(f['rgba8'])) for f in fixture['frames']}
    seen = set()
    frames = []
    owned_copies = []
    excluded = []
    report.update(frames=frames, ownedReadbackCopies=owned_copies, excludedObservations=excluded)
    captures = sorted(output.glob('*.rdc'))
    if not captures:
        raise RuntimeError('CONFORMANCE-CAPTURE-MISSING')
    for capture in captures:
        owner = rd.OpenCaptureFile()
        status = owner.OpenFile(str(capture), '', None)
        if status.code != rd.ResultCode.Succeeded:
            raise RuntimeError('CONFORMANCE-CAPTURE-OPEN: ' + str(status))
        status, controller = owner.OpenCapture(rd.ReplayOptions(), None)
        if status.code != rd.ResultCode.Succeeded:
            owner.Shutdown()
            raise RuntimeError('CONFORMANCE-CAPTURE-REPLAY: ' + str(status))
        try:
            api = controller.GetAPIProperties().pipelineType
            if api not in (rd.GraphicsAPI.Vulkan, rd.GraphicsAPI.D3D11, rd.GraphicsAPI.D3D12):
                raise RuntimeError('CONFORMANCE-CAPTURE-API-UNSUPPORTED: ' + str(api))
            textures = {t.resourceId: t for t in controller.GetTextures()}
            buffers = {b.resourceId: b for b in controller.GetBuffers()}
            clear_events = {}
            for action in flatten(controller.GetRootActions()):
                # Vulkan attachment clears appear as BeginPass, then Present.
                # ANGLE also clears fresh surfaces for browser security. Select
                # the real test observation by copy provenance, before reading
                # pixel values: color target -> same-sized staging (NoFlags).
                if api in (rd.GraphicsAPI.Vulkan, rd.GraphicsAPI.D3D12):
                    if action.flags & rd.ActionFlags.ClearColor:
                        clear_events[action.copyDestination] = action.eventId
                    if (action.flags & rd.ActionFlags.Copy
                            and action.copySource in textures and action.copyDestination in buffers):
                        source_texture = textures[action.copySource]
                        extent = (source_texture.width, source_texture.height)
                        if extent in {(w, h) for w, h, _ in expected}:
                            controller.SetFrameEvent(action.eventId, True)
                            layout = None
                            if api == rd.GraphicsAPI.D3D12:
                                if action.copySource not in clear_events:
                                    raise RuntimeError('CONFORMANCE-CAPTURE-CLEAR-PROVENANCE')
                                raw, layout = RenderDocTextureBufferCopy.d3d12_rgba8(
                                    controller, action, source_texture, buffers[action.copyDestination])
                            else:
                                raw = controller.GetBufferData(action.copyDestination, 0,
                                    extent[0] * extent[1] * 4)
                            copied = rgba8_pixels(raw, source_texture.format.Name())
                            original = rgba8_pixels(texture_bytes(controller, action.copySource),
                                source_texture.format.Name())
                            if copied != original:
                                raise RuntimeError('CONFORMANCE-CAPTURE-OWNED-BUFFER-COPY')
                            matches = [rgba for w, h, rgba in expected if (w, h) == extent
                                and len(copied) == w * h and all(tuple(p) == rgba for p in copied)]
                            if len(matches) != 1:
                                raise RuntimeError('CONFORMANCE-CAPTURE-OWNED-BUFFER-PIXELS')
                            owned_copies.append(dict(capture=str(capture), event=action.eventId,
                                sourceImage=str(action.copySource), destinationBuffer=str(action.copyDestination),
                                extent=list(extent), rgba=list(matches[0]), verifiedPixels=len(copied),
                                nativeCopyLayout=layout,
                                outputSha256='sha256:' + hashlib.sha256(
                                    bytes(c for p in copied for c in p)).hexdigest()))
                    if not action.flags & rd.ActionFlags.Present:
                        continue
                    rid = action.copyDestination
                else:
                    if action.flags & rd.ActionFlags.ClearColor:
                        clear_events[action.copyDestination] = action.eventId
                    if not action.flags & rd.ActionFlags.Copy:
                        continue
                    rid = action.copySource
                    destination = textures.get(action.copyDestination)
                    origin = textures.get(rid)
                    if (not destination or not origin
                            or destination.creationFlags != rd.TextureCategory.NoFlags
                            or not origin.creationFlags & rd.TextureCategory.ColorTarget
                            or (destination.width, destination.height) != (origin.width, origin.height)):
                        continue
                    if rid not in clear_events:
                        # Capture boundaries may split a clear from a later
                        # staging copy. Initial contents are not command proof.
                        # Reject this observation as evidence before reading
                        # pixels; complete authored coverage is still required.
                        excluded.append(dict(capture=str(capture), event=action.eventId,
                            diagnostic='CONFORMANCE-CAPTURE-CLEAR-PROVENANCE',
                            evidenceAccepted=False))
                        continue
                if rid not in textures:
                    continue
                texture = textures[rid]
                if (texture.width, texture.height) not in {(w, h) for w, h, _ in expected}:
                    continue
                controller.SetFrameEvent(action.eventId, True)
                fmt = texture.format.Name()
                if 'SRGB' in fmt.upper():
                    raise RuntimeError('CONFORMANCE-CAPTURE-COLOR-SPACE')
                pixels = rgba8_pixels(texture_bytes(controller, rid), fmt)
                if api not in (rd.GraphicsAPI.Vulkan, rd.GraphicsAPI.D3D12):
                    copied = rgba8_pixels(texture_bytes(controller, action.copyDestination), destination.format.Name())
                    if copied != pixels:
                        raise RuntimeError('CONFORMANCE-CAPTURE-OBSERVATION-COPY')
                matches = [rgba for w, h, rgba in expected if (w, h) == (texture.width, texture.height)
                    and len(pixels) == w * h and all(tuple(p) == rgba for p in pixels)]
                if len(matches) != 1:
                    raise RuntimeError('CONFORMANCE-CAPTURE-PIXELS: ' + repr((capture.name, action.eventId, fmt, pixels[:2])))
                rgba = matches[0]
                seen.add((texture.width, texture.height, rgba))
                image = output / (capture.stem + '-event' + str(action.eventId) + '.bmp')
                write_bmp(str(image), texture.width, texture.height, pixels)
                frames.append(dict(capture=str(capture), event=action.eventId, api=str(api),
                    sourceImage=str(rid),
                    clearEvent=clear_events.get(rid), observation='present' if api in (rd.GraphicsAPI.Vulkan, rd.GraphicsAPI.D3D12) else 'color-target-to-staging-copy',
                    extent=[texture.width, texture.height], rgba=list(rgba), verifiedPixels=len(pixels),
                    outputSha256='sha256:' + hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest(),
                    format=fmt, image=str(image)))
            errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
            if errors:
                raise RuntimeError('CONFORMANCE-CAPTURE-REPLAY-ERRORS: ' + repr(errors))
        finally:
            controller.Shutdown()
            owner.Shutdown()
    if seen != expected:
        raise RuntimeError('CONFORMANCE-CAPTURE-COVERAGE: ' + repr((seen, expected)))
    if os.environ.get('ARP_CONFORMANCE_REQUIRE_OWNED_COPY') == '1':
        observed = {(c['extent'][0], c['extent'][1], tuple(c['rgba'])) for c in owned_copies}
        if observed != expected:
            raise RuntimeError('CONFORMANCE-CAPTURE-OWNED-COPY-COVERAGE')
        for copy in owned_copies:
            if not any(frame['capture'] == copy['capture']
                    and frame['sourceImage'] == copy['sourceImage']
                    and frame['outputSha256'] == copy['outputSha256'] for frame in frames):
                raise RuntimeError('CONFORMANCE-CAPTURE-OWNED-COPY-PRESENT-IDENTITY')
    report.update(success=True, workloadSha256='sha256:' + hashlib.sha256(source).hexdigest(),
        captures=len(captures), frames=frames, uniqueFrames=len(seen), replayErrors=[],
        excludedObservations=excluded)
except Exception:
    report['error'] = traceback.format_exc()
(output / 'replay.json').write_text(json.dumps(report, indent=2))
os._exit(0 if report['success'] else 1)
