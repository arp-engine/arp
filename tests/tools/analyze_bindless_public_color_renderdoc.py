"""Independently replay public Native color8 Compute output with qrenderdoc."""
import hashlib
import json
import os
import struct
import traceback
import renderdoc as rd

directory=os.environ['ARP_BINDLESS_COLOR_PACKAGE_DIR']
name=os.environ['ARP_BINDLESS_COLOR_PACKAGE_NAME']
array=name=='CompletedArrayRoot'
output=os.environ['ARP_BINDLESS_CAPTURE_OUTPUT']
report={'scope':'public Native color8 Compute; explicit mip; '+('2DArray' if array else '2D')}
capture=None
controller=None
try:
    capture=rd.OpenCaptureFile()
    status=capture.OpenFile(os.environ['ARP_BINDLESS_CAPTURE_PATH'],'',None)
    if status.code!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
    status,controller=capture.OpenCapture(rd.ReplayOptions(),None)
    if status.code!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
    hashes={}
    for entry,fault in [('cleanMain',0),('faultMain',1)]:
        with open(os.path.join(directory,name+'.native.'+entry+'.spv'),'rb') as source:
            hashes[hashlib.sha256(source.read()).hexdigest()]=fault

    def walk(actions):
        for action in actions:
            yield action
            yield from walk(action.children)

    def expected(index):
        result=[]
        for layer in range(2 if array else 1):
            result.extend([(index+1)*(16+8*layer)/255,2,
                ((40 if index==0 else 128)+(index+1)*8*layer)/255,2,1,
                (index+1)*(32+8*layer)/255])
            if array:result.extend([2,(index+1)*(32+8*layer)/255])
        return result

    def decode(data):
        words=struct.unpack('<'+'I'*(len(data)//4),data)
        floats=struct.unpack('<'+'f'*(len(data)//4),data)
        return [words[i] if i%(8 if array else 6) in ([1,3,6] if array else [1,3]) else floats[i] for i in range(len(words))]

    actions=list(walk(controller.GetRootActions()))
    buffers={b.resourceId:b.length for b in controller.GetBuffers()}
    textures={t.resourceId:t for t in controller.GetTextures()}
    evidence=[]
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:continue
        controller.SetFrameEvent(action.eventId,True)
        pipe=controller.GetPipelineState()
        shader=pipe.GetShaderReflection(rd.ShaderStage.Compute)
        shader_hash=hashlib.sha256(bytes(shader.rawBytes)).hexdigest() if shader else None
        if shader_hash not in hashes:continue
        fault=hashes[shader_hash]
        if list(action.dispatchDimension)!=[1,1,1]:raise RuntimeError('Wrong original dispatch dimensions')
        descriptors=[d.descriptor for d in pipe.GetAllUsedDescriptors() if d.access.stage==rd.ShaderStage.Compute]
        observed=[]
        for resource in set(d.resource for d in descriptors if buffers.get(d.resource)==(64 if array else 24)):
            data=bytes(controller.GetBufferData(resource,0,buffers[resource]))
            values=decode(data)
            matched=[i for i in range(2) if all(abs(a-b)<1e-5 for a,b in zip(values,expected(i)))]
            if len(matched)!=1:raise RuntimeError('Color numerical output differs: '+str(values))
            observed.append({'index':matched[0],'resource':str(resource),'values':values,'sha256':hashlib.sha256(data).hexdigest()})
        if sorted(d['index'] for d in observed)!=[0,1]:raise RuntimeError('Missing original output buffers')
        sampled=[]
        for resource in set(d.resource for d in descriptors if d.resource in textures):
            texture=textures[resource]
            if texture.width!=2 or texture.height!=2 or texture.mips!=2:raise RuntimeError('Wrong sampled texture shape')
            if texture.arraysize!=(2 if array else 1):raise RuntimeError('Wrong sampled layer count')
            sampled.append(str(resource))
        if len(sampled)!=2:raise RuntimeError('Expected two original sampled textures')
        copied=next((a for a in actions if a.eventId>action.eventId and a.flags & rd.ActionFlags.Copy),None)
        if copied is None:raise RuntimeError('Missing original fault copy')
        controller.SetFrameEvent(copied.eventId,True)
        if buffers.get(copied.copyDestination)!=4:raise RuntimeError('Wrong fault destination')
        bits=struct.unpack('<I',bytes(controller.GetBufferData(copied.copyDestination,0,4)))[0]
        if bits!=fault:raise RuntimeError('Wrong shader fault value')
        evidence.append({'eventId':action.eventId,'spirvSha256':shader_hash,'faultBits':bits,
            'faultCopyEventId':copied.eventId,'outputs':observed,'sampledTextures':sampled})
    if len(evidence)!=2 or sorted(e['faultBits'] for e in evidence)!=[0,1]:raise RuntimeError('Unexpected color dispatch count')
    report['dispatches']=evidence
    report['validationMessages']=[m.description for m in controller.GetDebugMessages()]
    if report['validationMessages']:raise RuntimeError('Vulkan validation messages present')
    report['success']=True
except Exception:
    report['success']=False
    report['error']=traceback.format_exc()
finally:
    with open(output,'w') as destination:json.dump(report,destination,indent=2)
    if controller:controller.Shutdown()
    if capture:capture.Shutdown()
    os._exit(0 if report.get('success') else 1)
