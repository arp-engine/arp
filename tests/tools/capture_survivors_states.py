"""Capture and replay named native states; validate pixels and instance payloads."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--states',nargs='+',help='Only capture these named states (default: all baseline states).')
mode=p.add_mutually_exclusive_group()
mode.add_argument('--script-interpreter',action='store_true',help='Explicitly execute the current ArpScript source.')
mode.add_argument('--script-aot',action='store_true',help='Explicitly execute the source-matched DirectNative package.')
p.add_argument('--timeout',type=int,default=150,help='Bounded timeout per capture in seconds.')
p.add_argument('--assets',type=Path,help='Explicit asset and script source directory.')
a=p.parse_args();build=a.build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
scenarios=[('opening',['--capture-frame','10','--frames','20']),('physics',['--capture-state','physics','--frames','30']),('combat',['--capture-state','combat','--frames','3650']),('upgrade',['--scenario','upgrades','--capture-state','upgrade','--frames','120']),('death',['--scenario','death','--capture-state','death']),('pause',['--scenario','pause','--capture-state','pause','--frames','120']),('resize',['--resize-check','--integer-scale','--capture-frame','45','--frames','90'])]
if a.states:
    scenarios+=[('evolved',['--scenario','evolution','--capture-state','evolved','--frames','1200']),('evolved-upgrade',['--scenario','evolution','--capture-state','evolved-upgrade','--frames','1200']),('victory',['--capture-state','victory'])]
    if set(a.states)-{name for name,_ in scenarios}:p.error('Unknown capture state')
    scenarios=[(name,flags) for name,flags in scenarios if name in a.states]
for name,flags in scenarios:
    prefix=out/name;env=dict(os.environ,ARP_VULKAN_VALIDATION='1')
    with (out/(name+'.log')).open('w',encoding='utf-8') as log:
        execution=['--script-aot'] if a.script_aot else ['--script-interpreter'] if a.script_interpreter else []
        if a.assets:execution+=['--assets',str(a.assets.resolve())]
        subprocess.run(['C:/Program Files/RenderDoc/renderdoccmd.exe','capture','-w',str(build/'arp_survivors.exe'),'--hidden','--autoplay','--mute','--fixed-step','--capture-path',str(prefix),'--save-dir',str(out/(name+'-save'))]+execution+flags,stdout=log,stderr=subprocess.STDOUT,env=env,check=True,timeout=a.timeout)
    capture=out/(name+'_capture.rdc')
    if not capture.is_file():raise RuntimeError('No capture: '+name)
    env=dict(os.environ,ARP_RENDERDOC_ANALYZE_CAPTURE=str(capture),ARP_REPO_ROOT=str(root))
    subprocess.run(['C:/Program Files/RenderDoc/qrenderdoc.exe','--python',str(root/'tests/tools/analyze_survivors_renderdoc.py')],env=env,check=True,timeout=60)
    if not capture.with_suffix('.yaml').is_file():raise RuntimeError('No validated replay: '+name)
    print('Validated '+name,flush=True)
