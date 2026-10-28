"""Real WebGPU Cloud Reference/1 readback against the existing CPU oracle."""
import argparse
import base64
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from browser_contract_runner import BrowserContractRunner


def main():
    parser=argparse.ArgumentParser()
    for key in ('directory','browser','output','oracle','asset'):
        parser.add_argument('--'+key,required=True)
    parser.add_argument('--cases',nargs='+',default=['clear','sparse','thick','below','inside','above','horizon','backlit','occlusion','orthographic'])
    parser.add_argument('--ordinary',action='store_true')
    args=parser.parse_args()
    records=[]
    for name in args.cases:
        output=Path(args.output)/name
        result=BrowserContractRunner(args.browser,Path(args.directory),'arp_volumetric_cloud.html?case='+name+('&ordinary=1' if args.ordinary else '&capture=1'),str(output),90).run()
        if args.ordinary:
            assert result['success'] is False and len(result['nativeErrors'])==1,result
            assert 'RHI-WEBGPU-COMMAND-RECORDING-USAGE-UNPROVEN' in result['nativeErrors'][0],result
            continue
        assert result['success'] is True and not result['nativeErrors'],result
        assert any('frames=4 history=0' in m for m in result['messages']),result
        probe=result['readback'];assert (probe['width'],probe['height'],probe['rowPitch'])==(32,24,256)
        for key in ('constants','raw','resolved','hdr'):
            (output/(key+'.bin')).write_bytes(base64.b64decode(probe[key]))
        (output/'hdr.png').write_bytes(base64.b64decode(result['image']['png'].split(',')[1]))
        oracle=subprocess.run([args.oracle,str(output),args.asset,name],capture_output=True,text=True,timeout=180)
        (output/'oracle.log').write_text(oracle.stdout+oracle.stderr)
        assert oracle.returncode==0,(name,oracle.stdout,oracle.stderr)
        metric=json.loads(oracle.stdout);metric['case']=name
        (output/'oracle.json').write_text(json.dumps(metric,indent=2));records.append(metric)
        print(name,metric,flush=True)
    Path(args.output).mkdir(parents=True,exist_ok=True)
    (Path(args.output)/'summary.json').write_text(json.dumps(dict(success=True,records=records,ordinaryRefusal=args.ordinary,productionReady=False),indent=2))


if __name__=='__main__':main()
