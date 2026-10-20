"""Replay editor captures, verify interactions and optionally compare baselines.

Run in qrenderdoc --python with ARP_EDITOR_EVIDENCE set to the capture directory.
Optional ARP_EDITOR_BASELINE_BEFORE/AFTER/SCRIPT identify pre-refactor captures.
"""
import os
import sys
sys.path.insert(0, os.environ['ARP_EDITOR_TOOLS'])
import renderdoc as rd
from analyze_ui_text_renderdoc import flatten, action_name, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp


def replay(path):
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(path, '', None) != rd.ResultCode.Succeeded:
        capture.Shutdown()
        raise RuntimeError('Cannot open ' + path)
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        capture.Shutdown()
        raise RuntimeError('Cannot replay ' + str(status))
    try:
        actions = controller.GetDrawcalls() if hasattr(controller, 'GetDrawcalls') else controller.GetRootActions()
        draws = [a for a in flatten(actions) if 'DrawIndexed' in action_name(a, controller)]
        if len(draws) < 400:
            raise RuntimeError('Missing editor glyph/control draws')
        controller.SetFrameEvent(draws[-1].eventId, True)
        pipe = controller.GetPipelineState()
        rid = descriptor_resource(pipe.GetOutputTargets()[0])
        target = next(t for t in controller.GetTextures() if t.resourceId == rid)
        pixels = rgba8_pixels(texture_bytes(controller, rid), target.format.Name())
        if (target.width, target.height) != (1440, 810):
            raise RuntimeError('Unexpected native extent')
        if len(pixels) != target.width * target.height:
            raise RuntimeError('Expected tightly packed RGBA8/BGRA8 backbuffer')
        errors = [m for m in controller.GetDebugMessages() if 'Error' in str(m.severity) or 'High' in str(m.severity)]
        if errors:
            raise RuntimeError('Replay errors: ' + str(errors))
        write_bmp(os.path.splitext(path)[0] + '.bmp', target.width, target.height, pixels)
        return pixels, len(draws)
    finally:
        controller.Shutdown()
        capture.Shutdown()


def main():
    evidence = os.environ['ARP_EDITOR_EVIDENCE']
    files = os.listdir(evidence)
    before_path = next(f for f in files if f.startswith('before') and f.endswith('.rdc'))
    after_path = next(f for f in files if f.startswith('after') and f.endswith('.rdc'))
    before, bd = replay(os.path.join(evidence, before_path))
    after, ad = replay(os.path.join(evidence, after_path))
    lines = ['Windows/Vulkan: Backend Validated', 'extent=1440x810', 'before_draws=%d after_draws=%d' % (bd, ad), 'replay_debug_errors=0']
    # Rectangles are derived from the explicit 1920 x 1080 authoring canvas.
    for name, x, y, w, h in [('selection', 37, 306, 211, 48), ('inspector_name', 1628, 110, 240, 32), ('intensity', 1744, 252, 152, 27), ('interior', 1752, 495, 17, 20), ('update_mode',1744,220,152,29)]:
        x, y, w, h = [int(n * .75) for n in (x, y, w, h)]
        changed = sum(before[row * 1440 + col] != after[row * 1440 + col] for row in range(y, y+h) for col in range(x, x+w))
        if not changed:
            raise RuntimeError('No visual change in ' + name)
        lines.append('%s_changed_pixels=%d' % (name, changed))
    frames = {'before': (before, bd), 'after': (after, ad)}
    script_paths = [f for f in files if f.startswith('script') and f.endswith('.rdc')]
    if len(script_paths) > 1:
        raise RuntimeError('Ambiguous script captures')
    if script_paths:
        frames['script'] = replay(os.path.join(evidence, script_paths[0]))
        lines.append('script_draws=%d' % frames['script'][1])
    for name, (pixels, draw_count) in frames.items():
        baseline = os.environ.get('ARP_EDITOR_BASELINE_' + name.upper())
        if not baseline:
            continue
        old_pixels, old_draw_count = replay(baseline)
        changed = sum(old != new for old, new in zip(old_pixels, pixels))
        lines.append('%s_refactor_changed_pixels=%d baseline_draws=%d' %
                     (name, changed, old_draw_count))
        if changed or draw_count != old_draw_count:
            raise RuntimeError('Refactor output differs for %s: %d pixels, draws %d -> %d' %
                               (name, changed, old_draw_count, draw_count))
    with open(os.path.join(evidence, 'analysis.txt'), 'w') as output:
        output.write('\n'.join(lines) + '\n')
    print('\n'.join(lines))


try:
    main()
except Exception as error:
    with open(os.path.join(os.environ['ARP_EDITOR_EVIDENCE'], 'analysis_error.txt'), 'w') as output:
        output.write(str(error))
    raise
