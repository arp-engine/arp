"""Coordinate the same gameplay test with qrenderdoc's two real frame captures."""
import argparse
from pathlib import Path
import time
from browser_game import QueenBrowserTest


class CapturedQueenTest(QueenBrowserTest):
    capture_count=0

    def screenshot(self,name):
        if name in ('running-3d','final-3d-look'):
            self.capture_count+=1
            (self.output/('request-'+str(self.capture_count))).write_text(name)
            deadline=time.monotonic()+30
            while not (self.output/('done-'+str(self.capture_count))).exists():
                if time.monotonic()>deadline:raise TimeoutError('RenderDoc capture '+name)
                time.sleep(.025)
        super().screenshot(name)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    deadline=time.monotonic()+45
    while True:
        try:
            test=CapturedQueenTest(9336,args.output)
            break
        except Exception:
            if time.monotonic()>deadline:raise
            time.sleep(.2)
    try:test.run()
    finally:(Path(args.output)/'game-test-done').write_text('done')
