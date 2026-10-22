"""Inspect world-instance bytes in upload/publication/consumption captures.

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
        generations = int(os.environ.get('ARP_RENDERDOC_INSTANCE_GENERATIONS', '1'))
        consuming = os.environ.get('ARP_RENDERDOC_INSTANCE_CONSUMPTION', '0') == '1'
        assert generations in (1, 2)
        assert not consuming or generations == 1
        assert len(copies) == 4 * generations + (3 if consuming else 0), 'unexpected bounded native copy chunks'
        uploads = copies[:4 * generations]
        consumption = copies[4 * generations:]
        destinations = list(dict.fromkeys(a.copyDestination for a in uploads))
        assert len(destinations) == 3 * generations, 'unexpected tile/prototype destinations'
        sources = {a.copyDestination: a.copySource for a in uploads}
        final_copy_events = {a.copyDestination: a.eventId for a in uploads}
        buffers = {b.resourceId: b for b in controller.GetBuffers()}
        expected_positions = {(103., -4., -205.), (-17., 8., 31.),
                              (-11., 9., 36.), (111., 7., -198.)}
        base_positions = set(expected_positions)
        expected_by_generation = [base_positions]
        if generations == 2:
            shifted = {(x + 13., y, z) for x, y, z in base_positions}
            expected_positions |= shifted
            expected_by_generation.append(shifted)
        positions = set()
        positions_by_generation = [set() for _ in range(generations)]
        report = {'capture': path, 'nativeCopyChunks': len(copies),
                  'destinations': [], 'draws': 0,
                  'status': 'upload-only' if generations == 1 else 'instance-publication-only',
                  'generations': generations}
        upload_bytes = {}
        for destination_index, destination in enumerate(destinations):
            # Staging buffers may be retired/reused between generations. Inspect
            # each pair at its own last copy event, while both resources exist.
            controller.SetFrameEvent(final_copy_events[destination], True)
            size = buffers[destination].length
            raw = bytes(controller.GetBufferData(destination, 0, size))
            original = bytes(controller.GetBufferData(sources[destination], 0, size))
            assert raw == original and size in (64, 128), (
                f'native destination differs from upload: destination={destination} '
                f'event={final_copy_events[destination]} expected={size} '
                f'destinationBytes={len(raw)} sourceBytes={len(original)}')
            records = list(struct.iter_unpack('<16f', raw))
            upload_bytes[destination] = raw
            for record in records:
                assert all(math.isfinite(v) for v in record)
                positions.add(tuple(record[:3]))
                positions_by_generation[destination_index // 3].add(tuple(record[:3]))
                assert record[3] == 2. and record[10] == 3.
                assert abs(record[4] - .73) < 1e-5
                assert abs(record[8] + .31) < 1e-5
                assert abs(record[9] - .17) < 1e-5
                assert record[7] == 0. and record[11:] == (0., 0., 0., 0., 0.)
            report['destinations'].append({'id': str(destination), 'bytes': size,
                                           'copyEvent': final_copy_events[destination],
                                           'records': [list(r) for r in records]})
        assert positions_by_generation == expected_by_generation, 'per-generation source mismatch'
        assert positions == expected_positions, 'world translation applied twice or records missing'
        assert sorted(len(d['records']) for d in report['destinations']) == sorted([1, 1, 2] * generations)
        if consuming:
            assert {a.copySource for a in consumption} == set(destinations), 'consumed another generation'
            assert len({a.copyDestination for a in consumption}) == 3
            assert not {a.copyDestination for a in consumption} & set(destinations)
            report['status'] = 'instance-consuming-transfer-only'
            report['consumption'] = []
            for action in consumption:
                controller.SetFrameEvent(action.eventId, True)
                size = buffers[action.copySource].length
                assert buffers[action.copyDestination].length == size
                original = bytes(controller.GetBufferData(action.copySource, 0, size))
                readback = bytes(controller.GetBufferData(action.copyDestination, 0, size))
                assert original == readback == upload_bytes[action.copySource], 'consuming transfer bytes mismatch'
                report['consumption'].append({'event': action.eventId,
                    'source': str(action.copySource), 'destination': str(action.copyDestination),
                    'bytes': size, 'exactUploadBytes': True})
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
