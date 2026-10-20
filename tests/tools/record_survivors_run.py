"""Record a one-frame-per-second GPU timeline, replay and encode it offline.
Desktop capture is deliberately unused: GDI cannot see Vulkan in this session.
"""
import argparse,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--ffmpeg',type=Path,required=True);p.add_argument('--replay-only',action='store_true')
a=p.parse_args();a.build=a.build.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
if not a.replay_only:
    with (a.output/'run.log').open('w',encoding='utf-8') as log:
        run=subprocess.Popen(['C:/Program Files/RenderDoc/renderdoccmd.exe','capture','-w',str(a.build/'arp_survivors.exe'),'--autoplay','--hidden','--mute','--start-delay','1','--result-hold','2','--capture-series',str(a.output/'frame'),'--save-dir',str(a.output/'save')],stdout=log,stderr=subprocess.STDOUT)
        start=time.monotonic();last=0
        while run.poll() is None:
            elapsed=int(time.monotonic()-start)
            if elapsed//30>last:last=elapsed//30;print('Native GPU recording: %ds'%elapsed,flush=True)
            if elapsed>660:run.terminate();raise RuntimeError('native run timeout')
            time.sleep(1)
        if run.returncode:raise RuntimeError('native run failed')
if not (a.output/'series.yaml').is_file():
    env=dict(os.environ,ARP_SURVIVORS_SERIES=str(a.output),ARP_REPO_ROOT=str(root))
    subprocess.Popen(['C:/Program Files/RenderDoc/qrenderdoc.exe','--python',str(root/'tests/tools/replay_survivors_series.py')],env=env)
    start=time.monotonic();last=0
    while not (a.output/'series.yaml').is_file():
        if (a.output/'series-error.log').is_file():raise RuntimeError((a.output/'series-error.log').read_text())
        elapsed=int(time.monotonic()-start)
        if elapsed//30>last:
            last=elapsed//30;progress=a.output/'replay-progress.log';print(progress.read_text().splitlines()[-1] if progress.is_file() else 'Waiting for replay',flush=True)
        if elapsed>900:raise RuntimeError('GPU replay timeout')
        time.sleep(1)
video=a.output/'full-run-gpu-1fps.mp4'
with (a.output/'encoding.log').open('w',encoding='utf-8') as log:
    subprocess.run([str(a.ffmpeg.resolve()),'-y','-framerate','1','-i',str(a.output/'image-%05d.png'),'-c:v','libx264','-preset','veryfast','-crf','18','-pix_fmt','yuv420p',str(video)],stdout=log,stderr=subprocess.STDOUT,check=True)
print('Verified GPU timeline encoded: '+str(video),flush=True)
