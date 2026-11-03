"""Extend the original Queen geometry replay with the declared native GUI atlas."""
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
import analyze_queen_animated_source_renderdoc as geometry
from analyze_ui_text_renderdoc import (
    descriptor_resource, flatten, rgba8_pixels, texture_bytes, write_bmp,
)


def main():
    path = Path(os.environ['ARP_QUEEN_SOURCE_CAPTURE'])
    if not geometry.main():
        return False
    report = json.loads(path.with_suffix('.replay.json').read_text())
    cap = rd.OpenCaptureFile()
    controller = None
    try:
        assert cap.OpenFile(str(path), '', None).code == rd.ResultCode.Succeeded
        status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        assert status.code == rd.ResultCode.Succeeded
        textures = {t.resourceId: t for t in controller.GetTextures()}
        # Native debug labels are optional. Identify the actual sampled texture
        # through shader reflection and used-descriptor access, never dimensions.
        atlas_id = None
        draws = []
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            fragment = pipe.GetShaderReflection(rd.ShaderStage.Fragment)
            if not fragment or 'GuiAtlasTexture' not in {r.name for r in fragment.readOnlyResources}:
                continue
            declaration, = [r for r in fragment.readOnlyResources
                            if r.name == 'GuiAtlasTexture']
            assert (declaration.fixedBindSetOrSpace, declaration.fixedBindNumber) == (1, 0)
            sampled, = [descriptor_resource(d.descriptor)
                        for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)
                        if fragment.readOnlyResources[d.access.index].name == 'GuiAtlasTexture']
            assert sampled in textures, (action.eventId, sampled)
            if atlas_id is None:
                atlas_id = sampled
            assert sampled == atlas_id, (action.eventId, sampled, atlas_id)
            draws.append(dict(event=action.eventId, vertices=action.numIndices,
                              atlas=str(atlas_id)))
        assert draws, 'No actual GUI shader consumed the declared atlas'
        controller.SetFrameEvent(draws[-1]['event'], True)
        atlas = textures[atlas_id]
        data = texture_bytes(controller, atlas_id)
        pixels = rgba8_pixels(data, atlas.format.Name())
        assert len(pixels) == atlas.width * atlas.height
        levels = {a for _, _, _, a in pixels}
        assert 0 in levels and 255 in levels and any(0 < a < 255 for a in levels), levels
        coverage = path.with_suffix('.atlas.bmp')
        write_bmp(str(coverage), atlas.width, atlas.height, [(a, a, a, 255) for _, _, _, a in pixels])
        report['nativeHud'] = dict(binding='GuiAtlasTexture/set1/binding0', guiDraws=draws,
            atlasSha256=hashlib.sha256(data).hexdigest(),
            atlasExtent=[atlas.width, atlas.height], atlasFormat=atlas.format.Name(),
            coverageLevels=len(levels), coverageImage=str(coverage), realGlyphCoverage=True)
    except Exception:
        report.update(success=False, hudError=traceback.format_exc())
    finally:
        if controller:
            controller.Shutdown()
        cap.Shutdown()
        path.with_suffix('.replay.json').write_text(json.dumps(report, indent=2))
    return report['success']


if __name__ == '__main__':
    os._exit(0 if main() else 1)
