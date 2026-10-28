"""Capture-local native block inventory; no whole-device physical peak claim."""
import json
import os
from pathlib import Path
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT'])/'tests/tools'))
from renderdoc_cloud_common import fields, number, succeeded


def analyze(path):
    cap = rd.OpenCaptureFile()
    assert succeeded(cap.OpenFile(str(path), '', None))
    options = rd.ReplayOptions()
    options.optimisation = rd.ReplayOptimisationLevel.NoOptimisation
    options.apiValidation = True
    status, controller = cap.OpenCapture(options, None)
    assert succeeded(status)
    try:
        names = {int(str(r.resourceId).split('::')[-1]): r.name for r in controller.GetResources()}
        allocations, buffers, bindings = {}, {}, {}
        for chunk in controller.GetStructuredFile().chunks:
            if chunk.name not in ('vkAllocateMemory', 'vkCreateBuffer', 'vkBindBufferMemory'):
                continue
            data = fields(chunk)
            if chunk.name == 'vkAllocateMemory':
                info = fields(data['AllocateInfo'])
                identity = number(data['Memory'])
                assert identity not in allocations
                allocations[identity] = {'captureResourceId': identity,
                    'backingBytes': number(info['allocationSize']),
                    'memoryTypeIndex': number(info['memoryTypeIndex']), 'buffers': []}
            elif chunk.name == 'vkCreateBuffer':
                buffers[number(data['Buffer'])] = number(fields(data['CreateInfo'])['size'])
            else:
                bindings[number(data['buffer'])] = (number(data['memory']), number(data['memoryOffset']))
        for identity, (memory, offset) in bindings.items():
            allocation = allocations[memory]
            size = buffers[identity]
            assert offset + size <= allocation['backingBytes']
            allocation['buffers'].append({'captureResourceId': identity,
                'name': names.get(identity), 'offsetBytes': offset, 'logicalBufferBytes': size})
        assert allocations and not controller.GetDebugMessages()
        result = {'success': True, 'capture': str(path), 'blocks': list(allocations.values()),
            'capturedBlockSubtotalBytes': sum(a['backingBytes'] for a in allocations.values()),
            'scope': 'Only native memory allocations retained in this capture. Buffers omit alignment and image occupancy. May omit live unused blocks; not full allocator backing, residency or physical peak.',
            'physicalPeakValidated': False}
        path.with_suffix('.backing.json').write_text(json.dumps(result, indent=2))
    finally:
        controller.Shutdown()
        cap.Shutdown()


if __name__ == '__main__':
    path = Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'])
    try:
        analyze(path)
        os._exit(0)
    except Exception:
        path.with_suffix('.backing.error.txt').write_text(traceback.format_exc())
        os._exit(1)
