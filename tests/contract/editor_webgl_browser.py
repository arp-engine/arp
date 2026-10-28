"""Run the actual pthread/WebGL editor and its file/command workflow."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from browser_contract_runner import BrowserContractRunner

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--directory',required=True)
    parser.add_argument('--browser',required=True)
    parser.add_argument('--output',required=True)
    parser.add_argument('--negative',choices=('derivatives','shader','visibility','loss'))
    parser.add_argument('--cycles',choices=('20',))
    args=parser.parse_args()
    entry='arp_editor.html?negative='+args.negative if args.negative else 'arp_editor.html?smoke=1'
    if args.cycles:entry+='&cycles='+args.cycles
    result=BrowserContractRunner(args.browser,args.directory,entry,args.output,500 if args.cycles else 80 if args.negative else 200,
        cross_origin_isolated=True).run()
    if not result.get('success') or result.get('errors') or result.get('gpuCompletionClaimed') is not False:
        raise RuntimeError('EDITOR-WEB-SMOKE: '+repr(result))
    print('PASS: real editor WebGL UI, commands, script/scene save, YAML export/import and resize')

if __name__=='__main__':main()
