"""Exercise the real Wasm game through browser input and retain visual evidence.

Requires websocket-client; Chrome must expose its dedicated DevTools port.
The receipt is test transport, not game resource authoring.
"""
import argparse
import base64
import json
from pathlib import Path
import sys
import time
import urllib.request
import websocket
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'scripts'))
from webgl1_runtime_build import WebGl1RuntimeBuilder


class QueenBrowserTest:
    def __init__(self, port, output):
        pages = json.load(urllib.request.urlopen(f'http://localhost:{port}/json'))
        page = next(p for p in pages if 'arp_build_a_queen_webgl.html' in p.get('url', ''))
        self.socket = websocket.create_connection(page['webSocketDebuggerUrl'],
            origin=f'http://localhost:{port}', timeout=30)
        self.serial = 0
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.observations = []
        self.bundle=Path(__file__).resolve().parents[3]/'build/queen-webgl1'
        self.artifacts=self.hashes()

    def hashes(self):
        return {name:WebGl1RuntimeBuilder.sha(self.bundle/name) for name in (
            'arp_build_a_queen_webgl.html','arp_build_a_queen_webgl.js',
            'arp_build_a_queen_webgl.wasm','arp_build_a_queen_webgl.data')}

    def call(self, method, params=None):
        self.serial += 1
        self.socket.send(json.dumps(dict(id=self.serial, method=method, params=params or {})))
        while True:
            result = json.loads(self.socket.recv())
            if result.get('id') == self.serial:
                if 'error' in result:
                    raise RuntimeError(result['error'])
                return result.get('result', {})

    def js(self, expression):
        result = self.call('Runtime.evaluate', dict(expression=expression, returnByValue=True))
        if 'exceptionDetails' in result:
            raise RuntimeError(result)
        return result['result'].get('value')

    def state(self):
        value = self.js('({state:Module.queenState,error:Module.queenError})')
        if value.get('error'):
            raise RuntimeError(value['error'])
        return value.get('state')

    def wait(self, predicate, seconds=40):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            state = self.state()
            if state and predicate(state):
                self.observations.append(state)
                return state
            time.sleep(.025)
        raise TimeoutError(self.state())

    def screenshot(self, name):
        result = self.call('Page.captureScreenshot', dict(format='png'))
        (self.output / (name + '.png')).write_bytes(base64.b64decode(result['data']))

    def key(self, key, code):
        self.call('Input.dispatchKeyEvent', dict(type='keyDown', key=key, code=code))
        self.call('Input.dispatchKeyEvent', dict(type='keyUp', key=key, code=code))

    def lane(self, lane, touch=False):
        rect = self.js('(()=>{const r=Module.canvas.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}})()')
        point = dict(x=rect['x'] + rect['width'] * ((lane + .5) / 3),
                     y=rect['y'] + rect['height'] * .6)
        if touch:
            self.call('Input.dispatchTouchEvent', dict(type='touchStart', touchPoints=[point]))
            self.call('Input.dispatchTouchEvent', dict(type='touchEnd', touchPoints=[]))
        else:
            self.call('Input.dispatchMouseEvent', dict(type='mousePressed', button='left', clickCount=1, **point))
            self.call('Input.dispatchMouseEvent', dict(type='mouseReleased', button='left', clickCount=1, **point))
        self.wait(lambda s: s['lane'] == lane)

    def run(self):
        self.call('Page.enable')
        self.call('Page.bringToFront')
        # Explicit test isolation: no WebGPU fallback can satisfy this run.
        self.call('Page.addScriptToEvaluateOnNewDocument', dict(source=
            "Object.defineProperty(navigator,'gpu',{value:undefined,configurable:true});"))
        self.call('Page.reload', dict(ignoreCache=True))
        self.wait(lambda s: s['phase'] == 0 and s['frames'] > 2)
        assert self.js('navigator.gpu===undefined')
        platform = self.js('Module.queenPlatform')
        assert platform == dict(engineRenderSystem=True, backend=4, orderedGraphics=True,
                               surfaceLifecycle=True, gpuCompletionClaimed=False)
        self.screenshot('menu')
        # Keep steering decisions on the browser's RAF clock. Host screenshot
        # round trips must not miss the 0.72-second gaps between real pickups.
        self.js("""(()=>{
            Module.queenCourse={changes:[],beforeContact:null,lastCount:0};
            const steer=()=>{
                const s=Module.queenState,c=Module.queenCourse;
                if(s.phase===1){
                    if(s.distance<10)c.beforeContact={...s};
                    if(s.collected!==c.lastCount){c.changes.push({...s});c.lastCount=s.collected;}
                    const d=s.distance;
                    const lane=d<10?0:d<19?2:d<37?1:d<46?2:d<60.5?0:d<67?2:d<77?1:0;
                    if(s.lane!==lane){
                        const r=Module.canvas.getBoundingClientRect();
                        Module.canvas.dispatchEvent(new MouseEvent('mousedown',{
                            bubbles:true,button:0,buttons:1,
                            clientX:r.x+r.width*(lane+.5)/3,clientY:r.y+r.height*.6}));
                    }
                }
                if(s.phase!==3)requestAnimationFrame(steer);
            };
            requestAnimationFrame(steer);
        })()""")
        self.key('Enter', 'Enter')
        self.wait(lambda s: s['phase'] == 1)
        self.screenshot('running-3d')
        battle = self.wait(lambda s: s['phase'] == 2)
        self.screenshot('runway-battle')
        result = self.wait(lambda s: s['phase'] == 3)
        assert result['collected'] == 5 and result['won'] and result['vertices'] > 30000
        course=self.js('Module.queenCourse')
        assert course['beforeContact']['collected']==0 and course['beforeContact']['resolved']==0
        assert [s['collected'] for s in course['changes']]==[1,2,3,4,5]
        assert [s['equippedMask'] for s in course['changes']]==[1,3,7,15,31]
        assert course['changes'][0]['resolved']==2
        assert [s['outfit'] for s in course['changes']]==[0,0,256,2304,18688]
        self.screenshot('final-3d-look')
        self.key('Enter', 'Enter')
        self.wait(lambda s: s['phase'] == 1 and s['level'] == 2)
        self.key('ArrowLeft','ArrowLeft')
        self.wait(lambda s:s['lane']==0)
        self.lane(2,touch=True)
        window=self.call('Browser.getWindowForTarget')['windowId']
        self.call('Browser.setWindowBounds',dict(windowId=window,bounds=dict(windowState='minimized')))
        time.sleep(.5)
        paused=self.state()
        time.sleep(.35)
        assert self.state()['frames']==paused['frames']
        self.call('Browser.setWindowBounds',dict(windowId=window,bounds=dict(windowState='normal')))
        self.call('Page.bringToFront')
        self.wait(lambda s:s['frames']>paused['frames']+2 and s['distance']>paused['distance'])
        self.key('r', 'KeyR')
        self.wait(lambda s: s['phase'] == 0)
        receipt = dict(type='QueenWebGlGameAcceptance', contractVersion=1, success=True,
            webGpuUnavailable=True, keyboard=True, mouse=True, touch=True,
            fullLevel=True, nextLevel=True, restart=True, pauseResume=True, course=course, observations=self.observations,
            renderDocVerified=False, productionReady=False,artifacts=self.artifacts,
            platformIntegration=platform)
        assert self.hashes()==self.artifacts,'Game bundle changed during acceptance'
        (self.output / 'gameplay.json').write_text(json.dumps(receipt, indent=2), encoding='utf8')
        self.socket.close()
        print(json.dumps(dict(success=True, result=result, output=str(self.output))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=9335)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    QueenBrowserTest(args.port, args.output).run()
