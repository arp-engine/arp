"""Inspect actual world-instance destination bytes in an upload-only capture.

Run through qrenderdoc --python with ARP_RENDERDOC_ANALYZE_CAPTURE set.
No image or submitted vegetation draw is claimed by this transfer-only probe.
"""
import json
import math
import os
import struct
import traceback

import renderdoc as rd


def flatten(actions):
    for action in actions:
        yield action
        for child in flatten(action.children):
            yield child


def succeeded(result):
    return result == rd.ResultCode.Succeeded


def inspect(path):
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        assert succeeded(capture.OpenFile(path, '', None))
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert succeeded(status) and controller, str(status)
        actions = list(flatten(controller.GetRootActions()))
        copies = [a for a in actions if a.flags & rd.ActionFlags.Copy]
        assert len(copies) == 4, 'expected four bounded native copy chunks'
        destinations = list(dict.fromkeys(a.copyDestination for a in copies))
        assert len(destinations) == 3, 'expected three tile/prototype destinations'
        sources = {a.copyDestination: a.copySource for a in copies}
        controller.SetFrameEvent(copies[-1].eventId, True)
        buffers = {b.resourceId: b for b in controller.GetBuffers()}
        expected_positions = {(103., -4., -205.), (-17., 8., 31.),
                              (-11., 9., 36.), (111., 7., -198.)}
        positions = set()
        report = {'capture': path, 'nativeCopyChunks': len(copies),
                  'destinations': [], 'draws': 0, 'status': 'upload-only'}
        for destination in destinations:
            size = buffers[destination].length
            raw = bytes(controller.GetBufferData(destination, 0, size))
            original = bytes(controller.GetBufferData(sources[destination], 0, size))
            assert raw == original and size in (64, 128), 'native destination differs from upload'
            records = list(struct.iter_unpack('<16f', raw))
            for record in records:
                assert all(math.isfinite(v) for v in record)
                positions.add(tuple(record[:3]))
                assert record[3] == 2. and record[10] == 3.
                assert abs(record[4] - .73) < 1e-5
                assert abs(record[8] + .31) < 1e-5
                assert abs(record[9] - .17) < 1e-5
                assert record[7] == 0. and record[11:] == (0., 0., 0., 0., 0.)
            report['destinations'].append({'id': str(destination), 'bytes': size,
                                           'records': [list(r) for r in records]})
        assert positions == expected_positions, 'world translation applied twice or records missing'
        assert sorted(len(d['records']) for d in report['destinations']) == [1, 1, 2]
        assert not any(a.flags & rd.ActionFlags.Drawcall for a in actions)
        messages = list(controller.GetDebugMessages())
        errors = [m.description for m in messages if str(m.severity) in ('High', 'Error')]
        assert not errors, errors
        report['debugMessages'] = len(messages)
        report['highSeverityMessages'] = errors
        report['worldPositions'] = sorted(list(p) for p in positions)
        with open(path + '.analysis.json', 'w') as output:
            json.dump(report, output, indent=2)
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()


try:
    inspect(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'])
    os._exit(0)
except Exception:
    with open(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'] + '.error.txt', 'w') as output:
        output.write(traceback.format_exc())
    os._exit(1)
