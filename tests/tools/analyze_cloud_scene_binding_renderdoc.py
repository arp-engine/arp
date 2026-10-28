"""Reuse the ordinary replay and check density against the loaded scene owner."""
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback

import renderdoc as rd
sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
import analyze_ordinary_mixed_cloud_renderdoc as ordinary


def main():
    ordinary.main()
    path = Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'])
    expected_path = os.environ.get('ARP_CLOUD_SCENE_ASSET_BYTES')
    expected = Path(expected_path).read_bytes() if expected_path else None
    assert expected is None or (expected and len(expected) % 16 == 0)
    cap = rd.OpenCaptureFile()
    assert ordinary.succeeded(cap.OpenFile(str(path), '', None))
    options = rd.ReplayOptions()
    options.optimisation = rd.ReplayOptimisationLevel.NoOptimisation
    options.apiValidation = True
    status, controller = cap.OpenCapture(options, None)
    assert ordinary.succeeded(status)
    try:
        actions = list(ordinary.flatten(controller.GetRootActions()))
        dispatches = [a for a in actions
                      if a.flags & rd.ActionFlags.Dispatch]
        assert len(dispatches) == 2
        output_hashes = []
        for action, index in zip(dispatches, (1, 4)):
            controller.SetFrameEvent(action.eventId, True)
            pipeline = controller.GetPipelineState()
            reflection = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
            if expected is not None:
                density = ordinary.binding(pipeline, rd.ShaderStage.Compute, reflection, index)
                assert density.byteSize == len(expected)
                actual = bytes(controller.GetBufferData(density.resource, density.byteOffset, len(expected)))
                assert actual == expected
            output = ordinary.binding(pipeline, rd.ShaderStage.Compute, reflection, 3)
            data = bytes(controller.GetBufferData(output.resource, output.byteOffset, output.byteSize))
            output_hashes.append(hashlib.sha256(data).hexdigest())
        draw = [a for a in actions if a.flags & rd.ActionFlags.Drawcall][-1]
        controller.SetFrameEvent(draw.eventId, True)
        target = ordinary.descriptor_resource(controller.GetPipelineState().GetOutputTargets()[0])
        hdr = ordinary.texture_bytes(controller, target)
        report = json.loads(path.with_suffix('.json').read_text())
        report['hdrSha256'] = hashlib.sha256(hdr).hexdigest()
        report['dispatchOutputSha256'] = output_hashes
        report['sceneDensityConsumerVerified'] = expected is not None
        if expected is not None:
            report['sceneAssetBytes'] = len(expected)
            report['sceneAssetSha256'] = hashlib.sha256(expected).hexdigest()
        analyzer = Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools/analyze_cloud_scene_binding_renderdoc.py'
        report['selectedInputSha256']['tests/tools/analyze_cloud_scene_binding_renderdoc.py'] = hashlib.sha256(analyzer.read_bytes()).hexdigest()
        for name in ('src/renderer/volumetric_cloud/cloud_scene_binding.h',
                     'src/renderer/volumetric_cloud/cloud_scene_binding.cpp',
                     'tests/fixtures/cloud/cloud_scene_fixture.h'):
            report['selectedInputSha256'][name] = hashlib.sha256((Path(os.environ['ARP_REPO_ROOT']) / name).read_bytes()).hexdigest()
        path.with_suffix('.json').write_text(json.dumps(report, indent=2))
    finally:
        controller.Shutdown()
        cap.Shutdown()


if __name__ == '__main__':
    try:
        main()
        os._exit(0)
    except Exception:
        Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'] + '.scene-error.txt').write_text(traceback.format_exc())
        os._exit(1)
