"""Actual browser fog execution, analytic readback and lifecycle controls."""
import argparse
import base64
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    for key in ('directory', 'browser', 'output'):
        parser.add_argument('--' + key, required=True)
    parser.add_argument('--expect-diagnostic')
    parser.add_argument('--lifecycle-only', action='store_true')
    args = parser.parse_args()
    if args.lifecycle_only:
        result = BrowserContractRunner(args.browser, Path(args.directory),
            'prototype_command_batch.html', args.output, 60).run()
        assert result.get('success') is True, result
        # Explicit Null refusal and the post-physical-loss capability query.
        assert result.get('nativeErrors') == ['[RHI error] RHI-PROTOTYPE-BATCH-UNSUPPORTED',
            '[RHI error] RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE'], result
        assert 'PrototypeCommandBatch/2 native capability discard capacity unsupported fence deadline lease oneShot deviceLoss passed' in result.get('messages', []), result
        print('PrototypeCommandBatch/2 native lifecycle passed')
        return
    cases = [('default', '', 34), ('homogeneous', 'homogeneous=1', 0),
        ('no-temporal', 'no-temporal=1', 0), ('no-fog', 'no-fog=1', 0),
        ('inside', 'camera-inside=1', 34), ('resize', 'resize-frame=10', 33),
        ('disable', 'disable-frame=10', 9)]
    for label, query, history in cases:
        if args.expect_diagnostic:
            query += ('&' if query else '') + 'ordinary=1'
        if not args.expect_diagnostic:
            query += ('&' if query else '') + 'capture=1'
        result = BrowserContractRunner(args.browser, Path(args.directory),
            'arp_volumetric_fog.html' + ('?' + query if query else ''),
            str(Path(args.output) / label), 60).run()
        if args.expect_diagnostic:
            errors = result.get('nativeErrors', [])
            assert result.get('success') is False and result.get('diagnostic') == 'FOG-BROWSER-EXIT-1', result
            assert result.get('productionReady') is False and len(errors) == 1, result
            assert ('RG-BARRIER-TRANSITION-FAILED: ' + args.expect_diagnostic + ' resource=') in errors[0], result
            assert not any(m.startswith('VolumetricFog/1 frames=') for m in result.get('messages', [])), result
            print(label, 'DiagnosticFailure:', args.expect_diagnostic)
            continue
        assert result.get('success') is True and result.get('nativeErrors') == [], result
        image = result.get('image', {})
        assert image.get('maximumLuminance', 0) - image.get('minimumLuminance', 0) > 0.05, result
        assert image.get('png', '').startswith('data:image/png;base64,'), result
        (Path(args.output) / label / 'canvas.png').write_bytes(base64.b64decode(image['png'].split(',', 1)[1]))
        assert result.get('diagnostic') == '' and result.get('productionReady') is False, result
        receipts = [re.fullmatch(r'VolumetricFog/1 frames=35 history=(\d+) bytes=(\d+)', m)
            for m in result.get('messages', [])]
        receipts = [r for r in receipts if r]
        assert len(receipts) == 1 and int(receipts[0][1]) == history, result
        if label == 'homogeneous':
            probe = result.get('readback', {})
            assert probe.get('maxAbsoluteError', float('inf')) < 0.0002, result
            print(label, 'GPU analytic maximum error', probe['maxAbsoluteError'])
        else:
            print(label, '35 frames, history', history)


if __name__ == '__main__':
    main()
