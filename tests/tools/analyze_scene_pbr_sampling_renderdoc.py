"""Independent finite upload-to-PBR sampling replay; no scene quality claim."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd
sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, write_bmp
from analyze_ordinary_mixed_cloud_renderdoc import binding
from renderdoc_cloud_common import succeeded, display


def main():
    path = Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'])
    cap = rd.OpenCaptureFile()
    assert succeeded(cap.OpenFile(str(path), '', None))
    options = rd.ReplayOptions()
    options.optimisation = rd.ReplayOptimisationLevel.NoOptimisation
    options.apiValidation = True
    status, controller = cap.OpenCapture(options, None)
    assert succeeded(status) and controller
    report = dict(scope='Finite five-texture upload publication to ordinary PBR draw; ambient plus emissive oracle, not representative cloud quality',
                  capture=str(path), captureSha256=hashlib.sha256(path.read_bytes()).hexdigest())
    try:
        actions = flatten(controller.GetRootActions())
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
        assert len(draws) == 1 and not [a for a in actions if a.flags & rd.ActionFlags.Dispatch]
        draw = draws[0]
        controller.SetFrameEvent(draw.eventId, True)
        pipeline = controller.GetPipelineState()
        reflection = pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
        assert reflection.entryPoint == 'fragmentMain'
        textures = {t.resourceId:t for t in controller.GetTextures()}
        source_pixels = [(128,64,192,128),(128,128,255,255),(0,128,128,255),(160,160,160,255),(64,96,128,255)]
        report['sampledTextures'] = []
        for i, expected in enumerate(source_pixels):
            descriptor = binding(pipeline, rd.ShaderStage.Fragment, reflection, i+3)
            texture = textures[descriptor.resource]
            assert texture.width == texture.height == texture.mips == texture.arraysize == 1
            data = texture_bytes(controller, descriptor.resource)
            assert data == bytes(expected), (i, data.hex(), expected)
            name = texture.format.Name()
            assert ('SRGB' in name.upper()) == (i in (0,4)), name
            report['sampledTextures'].append(dict(binding=i+3, resource=str(descriptor.resource),format=name,rgba8=list(data)))
        samplers = pipeline.GetSamplers(rd.ShaderStage.Fragment)
        assert len(samplers) == 1
        assert reflection.samplers[samplers[0].access.index].fixedBindNumber == 8
        report['samplerBinding'] = 8
        blocks = pipeline.GetConstantBlocks(rd.ShaderStage.Fragment)
        block_sizes = {reflection.constantBlocks[b.access.index].fixedBindNumber:b.descriptor.byteSize for b in blocks}
        assert block_sizes == {0:128,2:64}, block_sizes
        report['constantBytes'] = block_sizes
        mesh = controller.GetPostVSData(0,0,rd.MeshDataStage.VSOut)
        assert mesh.numIndices == 3
        data = bytes(controller.GetBufferData(mesh.vertexResourceId,mesh.vertexByteOffset,mesh.vertexByteStride*3))
        positions = [struct.unpack_from('<4f',data,i*mesh.vertexByteStride) for i in range(3)]
        assert positions == [(-1.,-1.,.5,1.),(3.,-1.,.5,1.),(-1.,3.,.5,1.)], positions
        target = descriptor_resource(pipeline.GetOutputTargets()[0])
        depth = descriptor_resource(pipeline.GetDepthTarget())
        hdr = texture_bytes(controller,target)
        depth_data = texture_bytes(controller,depth)
        assert (textures[target].width,textures[target].height)==(32,24)
        assert all(v[0] == .5 for v in struct.iter_unpack('<f',depth_data))
        def linear(x):
            s=x/255.
            return s/12.92 if s <= .04045 else ((s+.055)/1.055)**2.4
        metallic = (128/255.)*.5
        expected = [linear(128)*.5*(1-metallic)*.2*(160/255.)+linear(64)*.4,
                    linear(64)*.25*(1-metallic)*.3*(160/255.)+linear(96)*.3,
                    linear(192)*.125*(1-metallic)*.4*(160/255.)+linear(128)*.2,1.]
        values = list(struct.iter_unpack('<4e',hdr))
        error = max(abs(a-e) for value in values for a,e in zip(value,expected))
        assert len(values) == 32*24 and error < .002, (error,values[0],expected)
        image = path.with_name(path.stem+'-hdr.bmp')
        write_bmp(str(image),32,24,[tuple(display(v) for v in p[:3])+(255,) for p in values])
        copies = [a for a in actions if a.flags & rd.ActionFlags.Copy]
        assert len(copies)==1
        controller.SetFrameEvent(copies[0].eventId,True)
        copied = bytes(controller.GetBufferData(copies[0].copyDestination,0,len(hdr)))
        assert copied==hdr
        controller.SetFrameEvent(max(a.eventId for a in actions),True)
        messages = [str(m.description) for m in controller.GetDebugMessages()]
        assert not messages, messages
        report.update(success=True,drawEvent=draw.eventId,copyEvent=copies[0].eventId,
                      checkedHdrTexels=len(values),checkedReadbackBytes=len(copied),
                      expectedLinearHdr=expected,hdrMaxError=error,hdrSha256=hashlib.sha256(hdr).hexdigest(),
                      alpha128SurvivesMask04=True,allDepthHalf=True,debugMessages=messages,image=str(image))
        path.with_suffix('.json').write_text(json.dumps(report,indent=2))
    finally:
        controller.Shutdown()
        cap.Shutdown()


if __name__ == '__main__':
    try:
        main()
        os._exit(0)
    except Exception:
        Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE']+'.error.txt').write_text(traceback.format_exc())
        os._exit(1)
