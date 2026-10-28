"""Independent bytes oracle for the public ordinary multi-mip upload fixture."""
from pathlib import Path
import hashlib
import json
import os
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import flatten, write_bmp
from renderdoc_cloud_common import succeeded


def main():
    path = Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'])
    capture = rd.OpenCaptureFile()
    assert succeeded(capture.OpenFile(str(path), '', None))
    options = rd.ReplayOptions()
    options.optimisation = rd.ReplayOptimisationLevel.NoOptimisation
    options.apiValidation = True
    status, controller = capture.OpenCapture(options, None)
    assert succeeded(status) and controller
    report = dict(capture=str(path), captureSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  scope='Public ordinary upload destination bytes only; no PBR sampling or cloud scene acceptance', textures=[])
    try:
        actions = flatten(controller.GetRootActions())
        structured = controller.GetStructuredFile()
        copies = [a for a in actions if 'CopyBufferToImage' in a.GetName(structured)]
        destinations = {}
        for action in copies:
            destinations.setdefault(action.copyDestination, []).append(action.eventId)
        assert len(destinations) == 3, [(str(k),v) for k,v in destinations.items()]
        textures = {t.resourceId:t for t in controller.GetTextures()}
        seen = set()
        for resource, events in destinations.items():
            t = textures[resource]
            assert t.width == t.height == 2 and t.mips == 2
            shape = 2 if t.depth == 2 else 1 if t.arraysize == 2 else 0
            assert shape not in seen and len(events) == 2
            seen.add(shape)
            controller.SetFrameEvent(events[-1], True)
            total = 20 if shape == 0 else 40 if shape == 1 else 36
            expected = bytes(range(1 + shape * 40, 1 + shape * 40 + total))
            actual = b''
            for mip in range(2):
                for layer in range(t.arraysize):
                    actual += bytes(controller.GetTextureData(resource, rd.Subresource(mip, layer, 0)))
            assert actual == expected, dict(shape=shape, actual=actual.hex(), expected=expected.hex())
            base = bytes(controller.GetTextureData(resource, rd.Subresource(0, 0, 0)))
            image = path.with_name(path.stem + '-shape-' + str(shape) + '.bmp')
            pixels = [tuple(base[i:i+3]) + (255,) for i in range(0,16,4)]
            write_bmp(str(image),2,2,pixels)
            report['textures'].append(dict(shape=shape, resource=str(resource), events=events,
                checkedBytes=len(actual), contentSha256=hashlib.sha256(actual).hexdigest(), image=str(image)))
        assert seen == {0,1,2}
        controller.SetFrameEvent(max(a.eventId for a in actions), True)
        report['debugMessages'] = [str(m.description) for m in controller.GetDebugMessages()]
        assert not report['debugMessages'], report['debugMessages']
        report['success'] = True
        path.with_suffix('.json').write_text(json.dumps(report,indent=2))
    finally:
        controller.Shutdown()
        capture.Shutdown()


if __name__ == '__main__':
    try:
        main()
        os._exit(0)
    except Exception:
        Path(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'] + '.error.txt').write_text(traceback.format_exc())
        os._exit(1)
