"""qrenderdoc --python exporter for the real fog example and Wocc captures."""
import json
import os
from pathlib import Path
import traceback

import renderdoc as rd

folder = Path(os.environ['ARP_FOG_AUDIT_OUTPUT'])
folder.mkdir(parents=True, exist_ok=True)


def progress(message):
    with (folder / 'progress.txt').open('a', encoding='utf-8') as stream:
        stream.write(str(message) + '\n')


def main():
    progress('opening capture')
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ['ARP_FOG_AUDIT_CAPTURE'], '', None)
    assert status.OK(), str(status)
    status, ctl = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status.OK(), str(status)
    report = {'capture': os.environ['ARP_FOG_AUDIT_CAPTURE'], 'dispatches': [], 'textures': {}}

    def flatten(actions):
        for action in actions:
            yield action
            yield from flatten(action.children)

    def resources(state, ref, stage, write=False):
        refs = ref.readWriteResources if write else ref.readOnlyResources
        bindings = state.GetReadWriteResources(stage) if write else state.GetReadOnlyResources(stage)
        return {refs[b.access.index].name: b.descriptor.resource for b in bindings}

    def save_texture(name, resource):
        desc = next(t for t in ctl.GetTextures() if t.resourceId == resource)
        data = bytes(ctl.GetTextureData(resource, rd.Subresource(0, 0, 0)))
        (folder / (name + '.bin')).write_bytes(data)
        report['textures'][name] = {'width': desc.width, 'height': desc.height,
                                   'components': desc.format.compCount,
                                   'componentBytes': desc.format.compByteWidth,
                                   'format': str(desc.format), 'bytes': len(data)}

    try:
        actions = list(flatten(ctl.GetRootActions()))
        # Geometry draws do not consume fog outputs. Avoid replaying the entire
        # forest once per mesh; inspect all compute and the post-processing tail.
        dispatches = [a for a in actions if a.flags & rd.ActionFlags.Dispatch]
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
        candidates = sorted(dispatches + draws[-45:], key=lambda a: a.eventId)
        progress('opened; inspecting ' + str(len(candidates)) + ' candidate events')
        last_draw = None
        found_composite = False
        for action in candidates:
            if not action.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch):
                continue
            ctl.SetFrameEvent(action.eventId, True)
            state = ctl.GetPipelineState()
            compute = bool(action.flags & rd.ActionFlags.Dispatch)
            stage = rd.ShaderStage.Compute if compute else rd.ShaderStage.Fragment
            ref = state.GetShaderReflection(stage)
            if not ref:
                continue
            ro, rw = resources(state, ref, stage), resources(state, ref, stage, True)
            if compute:
                if not any(n.startswith('Fog') for n in rw):
                    continue
                progress('fog dispatch ' + str(action.eventId))
                report['dispatches'].append({'event': action.eventId, 'reads': list(ro), 'writes': list(rw)})
                for block in state.GetConstantBlocks(stage):
                    if ref.constantBlocks[block.access.index].byteSize == 784:
                        d = block.descriptor
                        (folder / 'uniform.bin').write_bytes(bytes(ctl.GetBufferData(d.resource, d.byteOffset, 784)))
                for name, resource in rw.items():
                    (folder / (name + '.bin')).write_bytes(bytes(ctl.GetBufferData(resource, 0, 0)))
                if 'FogShadow' in ro:
                    save_texture('FogShadow', ro['FogShadow'])
            else:
                last_draw = action.eventId
                if 'FogSceneColor' in ro:
                    found_composite = True
                    save_texture('FogSceneColor', ro['FogSceneColor'])
                    save_texture('FogDepth', ro['FogDepth'])
                    save_texture('FogComposite', state.GetOutputTargets()[0].resource)
        assert last_draw is not None and found_composite and len(report['dispatches']) == 4
        ctl.SetFrameEvent(last_draw, True)
        save = rd.TextureSave()
        save.resourceId = ctl.GetPipelineState().GetOutputTargets()[0].resource
        save.destType = rd.FileType.PNG
        assert ctl.SaveTexture(save, str(folder / 'final.png'))
        report['errors'] = [m.description for m in ctl.GetDebugMessages() if 'Error' in str(m.severity)]
        assert not report['errors'], report['errors']
        (folder / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    finally:
        ctl.Shutdown()
        capture.Shutdown()


try:
    main()
except Exception:
    (folder / 'error.txt').write_text(traceback.format_exc(), encoding='utf-8')
    os._exit(1)
os._exit(0)
