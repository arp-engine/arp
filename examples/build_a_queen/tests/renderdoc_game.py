"""qrenderdoc entry: capture/replay the actual Queen browser product via ANGLE.

ARP_QUEEN_EVIDENCE points to a dedicated evidence folder. capture-config.json
and the request/done files are test-tool transport, never runtime resources.
"""
import json
import math
import os
from pathlib import Path
import struct
import sys
import time
import traceback
import renderdoc as rd


def capture(output, config):
    options=rd.GetDefaultCaptureOptions()
    options.hookIntoChildren=False
    command=('--in-process-gpu --no-first-run '
        '--no-default-browser-check --disable-background-networking '
        '--disable-renderer-backgrounding --disable-background-timer-throttling '
        '--disable-backgrounding-occluded-windows --window-size=1320,820 '
        '--remote-debugging-port=9336 --remote-allow-origins=http://localhost:9336 '
        '--user-data-dir="{}" --app="{}"').format(output/'profile',config['url'])
    launch=rd.ExecuteAndInject(config['browser'],config['root'],command,[],str(output/'queen'),options,False)
    if launch.result.code!=rd.ResultCode.Succeeded:
        raise RuntimeError(str(launch.result))
    target=rd.CreateTargetControl('',launch.ident,'Queen WebGL game acceptance',True)
    if not target:
        raise RuntimeError('QUEEN-RENDERDOC-TARGET')
    captures=[]
    pid=target.GetPID()
    try:
        started=time.monotonic()
        requested=0
        while target.Connected() and time.monotonic()-started<120:
            message=target.ReceiveMessage(None)
            if message.type==rd.TargetControlMessageType.NewCapture:
                captures.append(str(message.newCapture.path))
                (output/('done-'+str(len(captures)))).write_text(captures[-1])
            if requested<2 and (output/('request-'+str(requested+1))).exists():
                target.TriggerCapture(1)
                requested+=1
            if len(captures)==2:
                (output/'captures.json').write_text(json.dumps(captures,indent=2))
                # Leave the result alive until the game test has finished.
                while not (output/'game-test-done').exists() and time.monotonic()-started<120:
                    target.ReceiveMessage(None)
                    time.sleep(.02)
                return dict(success=True,captures=captures,requested=requested)
            time.sleep(.01)
        raise RuntimeError('QUEEN-CAPTURE-TIMEOUT: '+repr(captures))
    finally:
        target.Shutdown()
        os.system('taskkill /PID {} /T /F >NUL 2>&1'.format(pid))


def replay(output, config):
    sys.path.insert(0,str(Path(config['root'])/'tests/tools'))
    from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp
    results=[]
    for ordinal,path in enumerate(json.loads((output/'captures.json').read_text())):
        cap=rd.OpenCaptureFile()
        opened=cap.OpenFile(path,'',None)
        if opened.code!=rd.ResultCode.Succeeded:raise RuntimeError(str(opened))
        status,controller=cap.OpenCapture(rd.ReplayOptions(),None)
        cap.Shutdown()
        if status.code!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
        try:
            textures={t.resourceId:t for t in controller.GetTextures()}
            draws=[]
            final=None
            for action in flatten(controller.GetRootActions()):
                if not action.flags & rd.ActionFlags.Drawcall:continue
                controller.SetFrameEvent(action.eventId,True)
                pipe=controller.GetPipelineState()
                targets=[descriptor_resource(t) for t in pipe.GetOutputTargets()]
                targets=[t for t in targets if t in textures]
                if not targets:continue
                texture=textures[targets[0]]
                extent=[texture.width,texture.height]
                if extent not in ([1024,1024],[1280,720],[300,184]):continue
                streams=pipe.GetVBuffers()
                strides=[v.byteStride for v in streams if v.resourceId!=rd.ResourceId.Null()]
                depth=descriptor_resource(pipe.GetDepthTarget())
                draw=dict(event=action.eventId,vertices=action.numIndices,extent=extent,strides=strides,
                    depthTarget=str(depth),hasDepth=depth!=rd.ResourceId.Null(),vertexShader=str(pipe.GetShader(rd.ShaderStage.Vertex)),
                    fragmentShader=str(pipe.GetShader(rd.ShaderStage.Pixel)))
                if action.numIndices>10000 and streams:
                    post=controller.GetPostVSData(0,0,rd.MeshDataStage.VSOut)
                    data=controller.GetBufferData(post.vertexResourceId,post.vertexByteOffset,post.vertexByteStride*min(32,post.numIndices))
                    positions=[struct.unpack_from('<4f',data,i*post.vertexByteStride) for i in range(min(32,post.numIndices))]
                    if not positions or not all(math.isfinite(v) for p in positions for v in p):
                        raise RuntimeError('QUEEN-NONFINITE-POSTVS')
                    draw['postVsFinitePositions']=len(positions)
                draws.append(draw)
                if extent==[1280,720]:final=(action.eventId,targets[0])
            shadow=[d for d in draws if d['extent']==[1024,1024] and d['vertices']>10000]
            world=[d for d in draws if d['extent']==[1280,720] and d['vertices']>10000]
            preview=[d for d in draws if d['extent']==[300,184] and d['vertices']>10000]
            gui=[d for d in draws if d['extent']==[1280,720] and d['vertices']==6 and not d['hasDepth']]
            if not shadow or not world or not preview or len(gui)<30:
                raise RuntimeError('QUEEN-NATIVE-PASS-COVERAGE: '+repr(draws))
            if shadow[0]['vertices']!=world[0]['vertices']:
                raise RuntimeError('QUEEN-SHADOW-GEOMETRY-MISMATCH')
            if any(not d['hasDepth'] for d in shadow+world+preview):
                raise RuntimeError('QUEEN-DEPTH-TARGET-MISSING')
            controller.SetFrameEvent(final[0],True)
            texture=textures[final[1]]
            pixels=rgba8_pixels(texture_bytes(controller,final[1]),texture.format.Name())
            if len(set(pixels[::97]))<30:raise RuntimeError('QUEEN-FINAL-IMAGE-EMPTY')
            # WebGlShaderCoordinates/1 explicitly defines readbackReverseRows.
            # The browser compositor already presents this attachment upright.
            rows=[pixels[y*texture.width:(y+1)*texture.width] for y in range(texture.height)]
            write_bmp(str(output/('replayed-'+str(ordinal)+'.bmp')),texture.width,texture.height,
                [pixel for row in reversed(rows) for pixel in row])
            errors=[str(m.description) for m in controller.GetDebugMessages()
                if m.severity in (rd.MessageSeverity.High,rd.MessageSeverity.Medium)]
            if errors:raise RuntimeError('QUEEN-REPLAY-ERRORS: '+repr(errors))
            results.append(dict(capture=path,nativeApi=str(controller.GetAPIProperties().pipelineType),
                draws=draws,shadowWorldVerticesMatch=True,preview3D=True,frameworkGuiDraws=len(gui),replayErrors=errors))
        finally:controller.Shutdown()
    return dict(success=True,frames=results,productionReady=False,gpuCompletionClaimed=False)


output=Path(os.environ['ARP_QUEEN_EVIDENCE'])
config=json.loads((output/'capture-config.json').read_text())
mode=os.environ.get('ARP_QUEEN_CAPTURE_MODE','capture')
try:
    result=capture(output,config) if mode=='capture' else replay(output,config)
except Exception:
    result=dict(success=False,error=traceback.format_exc())
(output/(mode+'-report.json')).write_text(json.dumps(result,indent=2))
os._exit(0 if result['success'] else 1)
