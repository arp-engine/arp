"""Replay real Dawn/D3D12 cloud work and compare bound payloads to browser readback."""
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0,str(Path(os.environ['ARP_REPO_ROOT'])/'tests/tools'))
from analyze_ui_text_renderdoc import flatten,descriptor_resource,texture_bytes,write_bmp
from renderdoc_cloud_common import display,succeeded


def main():
    path=Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'])
    expected=Path(os.environ['ARP_CLOUD_BROWSER_READBACK'])
    asset=Path(os.environ['ARP_CLOUD_ASSET_BYTES']).read_bytes()
    cap=rd.OpenCaptureFile();assert succeeded(cap.OpenFile(str(path),'',None))
    status,c=cap.OpenCapture(rd.ReplayOptions(),None);assert succeeded(status)
    report=dict(capture=str(path),productionReady=False,dispatches=[],composites=[])
    try:
        assert c.GetAPIProperties().pipelineType==rd.GraphicsAPI.D3D12
        report['nativeApi']='D3D12 via Chrome Dawn WebGPU'
        actions=list(flatten(c.GetRootActions()));textures={t.resourceId:t for t in c.GetTextures()}
        dispatches=[a for a in actions if a.flags&rd.ActionFlags.Dispatch]
        assert len(dispatches)==2
        raw_id=resolved_id=hdr_id=None
        for i,a in enumerate(dispatches):
            c.SetFrameEvent(a.eventId,True);p=c.GetPipelineState()
            constants=p.GetConstantBlocks(rd.ShaderStage.Compute);assert len(constants)==1
            d=constants[0].descriptor
            # Dawn's native CBV is rounded to 256-byte alignment. Cloud ABI is 368 bytes.
            assert d.byteSize>=368
            data=bytes(c.GetBufferData(d.resource,d.byteOffset,368))
            assert struct.unpack_from('<4I',data,272)==(32,24,32,24)
            assert struct.unpack_from('<2I',data,288)==(256,32)
            assert struct.unpack_from('<f',data,348)[0]==0  # History flag.
            assert struct.unpack_from('<f',data,364)[0]==0  # Perspective capture.
            writes=p.GetReadWriteResources(rd.ShaderStage.Compute);assert len(writes)==1
            d=writes[0].descriptor;actual=bytes(c.GetBufferData(d.resource,d.byteOffset,24576))
            assert actual==(expected/('raw.bin' if i==0 else 'resolved.bin')).read_bytes()
            samples=list(struct.iter_unpack('<8f',actual))
            assert all(all(math.isfinite(x) for x in sample) and min(sample[:3])>=0 and 0<=sample[3]<=1 for sample in samples)
            if i==0:
                raw_id=d.resource
                read=p.GetReadOnlyResources(rd.ShaderStage.Compute)
                density=[b.descriptor for b in read if b.descriptor.byteSize==len(asset)]
                assert len(density)==1
                d2=density[0];assert bytes(c.GetBufferData(d2.resource,d2.byteOffset,len(asset)))==asset
                report['assetSha256']=hashlib.sha256(asset).hexdigest()
                assert any(b.descriptor.resource in textures and textures[b.descriptor.resource].format.compCount==1 for b in read)
            else:
                resolved_id=d.resource
                assert sum(b.descriptor.resource==raw_id for b in p.GetReadOnlyResources(rd.ShaderStage.Compute))==2
            report['dispatches'].append(dict(event=a.eventId,groups=[a.dispatchDimension.x,a.dispatchDimension.y,a.dispatchDimension.z] if hasattr(a.dispatchDimension,'x') else list(a.dispatchDimension),samples=len(samples),browserBytesExact=True))
        for a in actions:
            if not a.flags&rd.ActionFlags.Drawcall:continue
            c.SetFrameEvent(a.eventId,True);p=c.GetPipelineState()
            inputs=p.GetReadOnlyResources(rd.ShaderStage.Fragment)
            if any(b.descriptor.resource==resolved_id for b in inputs):
                targets=p.GetOutputTargets();assert len(targets)==1
                hdr_id=descriptor_resource(targets[0]);t=textures[hdr_id]
                assert (t.width,t.height)==(32,24)
                actual=texture_bytes(c,hdr_id)
                assert actual==(expected/'hdr.bin').read_bytes()
                values=list(struct.iter_unpack('<4e',actual));assert all(all(math.isfinite(x) and x>=0 for x in value) for value in values)
                image=str(path.with_suffix('.hdr.bmp'))
                write_bmp(image,32,24,[(display(v[0]),display(v[1]),display(v[2]),255) for v in values])
                report['composites'].append(dict(event=a.eventId,width=32,height=24,hdrBytes=len(actual),browserBytesExact=True,image=image))
            elif hdr_id is not None and any(b.descriptor.resource==hdr_id for b in inputs):
                report['presentConsumesComposite']=True
        assert len(report['composites'])==1 and report.get('presentConsumesComposite')
        c.SetFrameEvent(actions[-1].eventId,True)
        report['debugMessages']=[dict(severity=str(m.severity),message=m.description) for m in c.GetDebugMessages()]
        assert not report['debugMessages'],report['debugMessages']
        report['success']=True
    finally:c.Shutdown();cap.Shutdown()
    path.with_suffix('.visual.json').write_text(json.dumps(report,indent=2))

try:
    main();os._exit(0)
except:
    Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE']+'.visual.error.txt').write_text(traceback.format_exc());os._exit(1)
