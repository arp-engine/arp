"""Verify the fixed P0/P3 no-history reference in an actual RenderDoc capture."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
import analyze_ordinary_mixed_cloud_renderdoc as ordinary


def main():
    ordinary.main()
    path = Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'])
    oracle = json.loads(Path(os.environ['ARP_CLOUD_ORACLE_PATH']).read_text())
    expected_asset = Path(os.environ['ARP_CLOUD_SCENE_ASSET_BYTES']).read_bytes()
    spec = json.loads(os.environ['ARP_CLOUD_REFERENCE_SPEC'])
    assert spec['temporal'] is False and oracle['historyUsed'] is False
    assert (oracle['width'], oracle['height']) == (spec['width'], spec['height'])
    assert (oracle['sampleWidth'], oracle['sampleHeight']) == (spec['width'], spec['height'])
    assert (oracle['viewSteps'], oracle['lightSteps']) == (spec['viewSteps'], spec['lightSteps'])
    assert oracle['tolerance'] == spec['absoluteTolerance']
    cap = rd.OpenCaptureFile()
    assert ordinary.succeeded(cap.OpenFile(str(path), '', None))
    options = rd.ReplayOptions()
    options.optimisation = rd.ReplayOptimisationLevel.NoOptimisation
    options.apiValidation = True
    status, controller = cap.OpenCapture(options, None)
    assert ordinary.succeeded(status)
    try:
        dispatches = [a for a in ordinary.flatten(controller.GetRootActions())
                      if a.flags & rd.ActionFlags.Dispatch]
        outputs = []
        for index, action in enumerate(dispatches):
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            reflection = pipe.GetShaderReflection(rd.ShaderStage.Compute)
            blocks = [b.descriptor for b in pipe.GetConstantBlocks(rd.ShaderStage.Compute)
                      if reflection.constantBlocks[b.access.index].fixedBindNumber == 0]
            assert len(blocks) == 1 and blocks[0].byteSize == 368
            block = blocks[0]
            data = bytes(controller.GetBufferData(block.resource, block.byteOffset, 368))
            assert struct.unpack_from('<4I', data, 272) == (spec['width'], spec['height'])*2
            assert struct.unpack_from('<2I', data, 288) == (spec['viewSteps'], spec['lightSteps'])
            assert struct.unpack_from('<f', data, 320)[0] == 0.0  # temporal weight
            assert struct.unpack_from('<f', data, 348)[0] == 0.0  # previous valid
            assert struct.unpack_from('<f', data, 364)[0] == float(oracle['orthographic'])
            if not index:
                density = ordinary.binding(pipe, rd.ShaderStage.Compute, reflection, 1)
                assert density.byteSize == len(expected_asset)
                assert bytes(controller.GetBufferData(density.resource, density.byteOffset,
                                                      density.byteSize)) == expected_asset
            output = ordinary.binding(pipe, rd.ShaderStage.Compute, reflection, 3)
            outputs.append(bytes(controller.GetBufferData(output.resource, output.byteOffset,
                                                          output.byteSize)))
        assert len(outputs) == 2 and outputs[0] == outputs[1], 'no-history resolve must copy raw'
        report = json.loads(path.with_suffix('.json').read_text())
        report['referenceBaseline'] = dict(contractVersion=1, case=spec['case'],
            fullResolution=True, historyEnabled=False, viewSteps=spec['viewSteps'],
            lightSteps=spec['lightSteps'], loadedAssetBytes=len(expected_asset),
            loadedAssetSha256=hashlib.sha256(expected_asset).hexdigest(),
            rawEqualsResolved=True, coverage=oracle['coverage'], extinction=oracle['extinction'],
            camera=oracle['camera'])
        report['selectedInputSha256']['tests/fixtures/cloud/cloud_reference_cases.h'] = hashlib.sha256(
            (Path(os.environ['ARP_REPO_ROOT'])/'tests/fixtures/cloud/cloud_reference_cases.h').read_bytes()).hexdigest()
        path.with_suffix('.json').write_text(json.dumps(report, indent=2))
    finally:
        controller.Shutdown()
        cap.Shutdown()


if __name__ == '__main__':
    try:
        main()
        os._exit(0)
    except Exception:
        Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'] + '.reference-error.txt').write_text(traceback.format_exc())
        os._exit(1)
