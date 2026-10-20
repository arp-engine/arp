"""Run inside qrenderdoc; inject a dedicated browser and capture the real shader ABI."""
import json
import os
import time
import traceback
import renderdoc as rd

output=os.environ['ARP_WEBGL_PROGRAM_CAPTURE']
target=None
browser_pid=0
events=[]
try:
    with open(os.path.join(output,'server.json')) as stream: server=json.load(stream)
    options=rd.GetDefaultCaptureOptions();options.hookIntoChildren=True
    query='&recoveryGate=1' if server.get('recoveryGate') else ''
    if server.get('extensionsOnly'): query += '&extensionsOnly=1'
    if server.get('orderedOnly'): query += '&orderedOnly=1'
    if server.get('scene2dOnly'): query += '&scene2dOnly=1'
    if server.get('guiOnly'): query += '&guiOnly=1'
    if server.get('basicForwardOnly'): query += '&basicForwardOnly=1'
    if server.get('conformanceClearOnly'): query += '&conformanceClearOnly=1'
    if server.get('conformanceSampledOnly'): query += '&conformanceSampledOnly=1&conformanceSampledVersion=' + str(server.get('conformanceSampledVersion',1))
    if server.get('conformanceStorageOnly'): query += '&conformanceStorageOnly=1'
    entry=server.get('entry','index.html')
    if entry not in ('index.html','pipeline.html','webgpu_shared_conformance_runner.html'):
        raise RuntimeError('BROWSER-CAPTURE-ENTRY')
    command='--no-sandbox --disable-gpu-sandbox --in-process-gpu --no-first-run --no-default-browser-check --disable-background-networking --disable-renderer-backgrounding --disable-background-timer-throttling --disable-backgrounding-occluded-windows --window-size=256,256 --user-data-dir="{}" --app="http://127.0.0.1:{}/{}?frames=12&captureGate=1{}"'.format(server['profile'],server['port'],entry,query)
    if entry=='webgpu_shared_conformance_runner.html':
        command+=' --enable-dawn-features=enable_renderdoc_process_injection'
    launch=rd.ExecuteAndInject(server['browser'],server['root'],command,[],os.path.join(output,'program'),options,False)
    events.append({'launch':str(launch.result),'ident':launch.ident})
    target=rd.CreateTargetControl('',launch.ident,'ARP WebGL shader program',True)
    if not target:raise RuntimeError('WEBGL1-CAPTURE-TARGET')
    browser_pid=target.GetPID();triggered=False;captures=[];started=time.monotonic()
    capture_frames=server.get('captureFrames',4)
    if capture_frames not in (4,24):raise RuntimeError('WEBGL1-CAPTURE-FRAME-BUDGET')
    while target.Connected() and time.monotonic()-started<45:
        message=target.ReceiveMessage(None)
        if message.type==rd.TargetControlMessageType.NewCapture:
            captures.append(str(message.newCapture.path))
        if not triggered and os.path.isfile(os.path.join(output,'ready')):
            target.TriggerCapture(capture_frames);triggered=True
            with open(os.path.join(output,'armed'),'w') as stream:stream.write('armed')
        if len(captures)>=capture_frames and os.path.isfile(os.path.join(output,'runtime.json')):break
    events.append({'captures':captures,'triggered':triggered})
except Exception:events.append({'error':traceback.format_exc()})
finally:
    if target:target.Shutdown()
    if browser_pid:os.system('taskkill /PID {} /T /F >NUL 2>&1'.format(browser_pid))
    with open(os.path.join(output,'capture-attempt.json'),'w') as stream:json.dump(events,stream,indent=2)
os._exit(0)
