"""Real browser program replay, immutable draw state and image evidence."""
import json
import os
import re
import struct
import sys
import traceback
import renderdoc as rd

sys.path.insert(0,os.path.join(os.environ['ARP_REPO_ROOT'],'tests','tools'))
from analyze_ui_text_renderdoc import flatten,descriptor_resource,texture_bytes,rgba8_pixels,write_bmp
sys.path.insert(0,os.path.join(os.environ['ARP_REPO_ROOT'],'tests','tooling'))
from webgl_shader_program_replay_contract import validate_capture_trace

def analyze(path,runtime):
    cap=rd.OpenCaptureFile()
    if cap.OpenFile(path,'',None).code!=rd.ResultCode.Succeeded:raise RuntimeError('WEBGL1-CAPTURE-OPEN')
    status,controller=cap.OpenCapture(rd.ReplayOptions(),None)
    if status.code!=rd.ResultCode.Succeeded or not controller:raise RuntimeError('WEBGL1-CAPTURE-REPLAY')
    report={'capture':path,'nativeApi':str(controller.GetAPIProperties().pipelineType),'draws':[]}
    try:
        textures={t.resourceId:t for t in controller.GetTextures()}
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall:continue
            controller.SetFrameEvent(action.eventId,True);pipe=controller.GetPipelineState()
            streams=pipe.GetVBuffers()
            if not streams or streams[0].byteStride!=36 or action.numIndices!=3:continue
            outputs=[descriptor_resource(t) for t in pipe.GetOutputTargets()]
            outputs=[r for r in outputs if r in textures and textures[r].width==64 and textures[r].height==64]
            if len(outputs)!=1:continue
            pixels=rgba8_pixels(texture_bytes(controller,outputs[0]),textures[outputs[0]].format.Name())
            red=sum(1 for p in pixels if p[:3]==(32,0,0));green=sum(1 for p in pixels if p[:3]==(0,48,0))
            blend=pipe.GetColorBlends()[0]
            item={'event':action.eventId,'redPixels':red,'greenPixels':green,'blendEnabled':blend.enabled,
                'samples':[list(pixels[y*64+x][:3]) for x,y in ((24,27),(46,27),(24,36))]}
            report['draws'].append(item)
            if red and green:
                image=os.path.splitext(path)[0]+'.bmp'
                write_bmp(image,64,64,[pixel for row in reversed([pixels[i:i+64] for i in range(0,len(pixels),64)]) for pixel in row])
                report['image']=image
        draws=report['draws']
        errors=[m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        if errors:raise RuntimeError('WEBGL1-CAPTURE-DEBUG: '+repr(errors))
        report.update(success=True,replayErrors=errors,productionBackendValidated=False,
            programIdentity=runtime['programIdentity'],rasterRows='nativeRowEqualsGlReadPixelsY')
        return report
    finally:controller.Shutdown();cap.Shutdown()

output=os.environ['ARP_WEBGL_PROGRAM_CAPTURE']
result={'success':False,'attempts':[]}
try:
    with open(os.path.join(output,'runtime.json')) as stream:runtime=json.load(stream)
    if not runtime['success']:raise RuntimeError(runtime.get('diagnostic'))
    captures=sorted((name for name in os.listdir(output) if name.endswith('.rdc')),
                    key=lambda name:int(re.fullmatch(r'program_frame(\d+)\.rdc',name).group(1)))
    if not captures:raise RuntimeError('WEBGL1-CAPTURE-MISSING')
    indices=[int(re.fullmatch(r'program_frame(\d+)\.rdc',name).group(1)) for name in captures]
    if any(b!=a+1 for a,b in zip(indices,indices[1:])):raise RuntimeError('WEBGL1-CAPTURE-FRAME-GAP')
    frames=[analyze(os.path.join(output,name),runtime) for name in captures]
    draws=[draw for frame in frames for draw in frame['draws']]
    trace=validate_capture_trace(draws,runtime['samples'])
    result=dict(success=True,capturedFrames=frames,trace=trace,
                image=next(frame['image'] for frame in reversed(frames) if 'image' in frame),
                programIdentity=runtime['programIdentity'],productionBackendValidated=False)
except Exception:result['error']=traceback.format_exc()
with open(os.path.join(output,'replay.json'),'w') as stream:json.dump(result,stream,indent=2)
os._exit(0 if result.get('success') else 1)
