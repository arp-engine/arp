"""Execute the actual embedded rAF wait with deterministic browser scheduling."""
from pathlib import Path
import shutil
import subprocess
import unittest


class BrowserFrameWaitTests(unittest.TestCase):
    def test_equal_timestamp_regression_and_original_deadline(self):
        source = (Path(__file__).resolve().parents[2] /
                  'tests/integration/webgl_pipeline_smoke.cpp').read_text()
        body = source.split('EM_ASYNC_JS(double, nextBrowserFrame, (), {', 1)[1]
        body = body.split('// clang-format off', 1)[1].split('// clang-format on', 1)[0]
        script = r'''
const assert = require('node:assert/strict');
const AsyncFunction = Object.getPrototypeOf(async function(){}).constructor;
const run = new AsyncFunction('Module','requestAnimationFrame','cancelAnimationFrame',
    'setTimeout','clearTimeout', BODY);
(async () => {
    const module = {}, queue = [], canceled = [];
    let timer, deadlines = 0, clears = 0, serial = 0;
    const wait = () => run(module,
        callback => {queue.push(callback); return ++serial;},
        id => canceled.push(id),
        (callback, duration) => {assert.equal(duration,10000); timer=callback; ++deadlines; return deadlines;},
        id => {++clears;});
    let pending = wait(); queue.shift()(10); assert.equal(await pending,10);
    let settled = false;
    pending = wait(); pending.then(() => {settled=true;});
    queue.shift()(10); await Promise.resolve();
    assert.equal(settled,false); assert.equal(queue.length,1);
    assert.equal(deadlines,2); assert.equal(clears,1);
    queue.shift()(20); assert.equal(await pending,20);
    assert.deepEqual(module.arpRafStamps,[10,20]);
    assert.equal(module.arpRepeatedRafStamps,1);
    pending=wait(); queue.shift()(19);
    await assert.rejects(pending,/RAF-TIMESTAMP-REGRESSION/);
    pending=wait(); queue.shift()(NaN);
    await assert.rejects(pending,/RAF-TIMESTAMP-REGRESSION/);
    pending=wait(); const last=serial; timer();
    await assert.rejects(pending,/RAF-TIMEOUT/);
    assert.deepEqual(canceled,[last]);
    assert.deepEqual(module.arpRafStamps,[10,20]);
})().catch(error => { console.error(error); process.exitCode=1; });
'''
        # stdin avoids shell interpolation of JavaScript or source text.
        import json
        script = script.replace('BODY', json.dumps(body), 1)
        node = shutil.which('node')
        self.assertIsNotNone(node)
        result = subprocess.run([node], input=script, text=True,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
