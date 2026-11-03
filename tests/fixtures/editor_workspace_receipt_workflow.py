"""Product Shell/Interpreter CLI receipt lifecycle over deliberately faulty delivery."""
import base64
import hashlib
import time


class EditorWorkspaceReceiptWorkflow:
    def __init__(self, cli, state, project, proxy, sample, repeated_command):
        self.cli, self.state, self.project = cli, state, project
        self.proxy, self.sample = proxy, sample
        self.repeated_command = repeated_command
        self.observations = []

    def finish(self, accepted, expected='succeeded'):
        task = accepted['projectIoTask']['taskId']
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            value = self.state()
            if value['projectIoTask']['status'] != 'pending':
                assert value['projectIoTask']['taskId'] == task
                assert value['projectIoTask']['status'] == expected, value
                return value
            time.sleep(.05)
        raise AssertionError('receipt source operation deadline')

    def observe(self, saved, operation, expected_state):
        receipt = saved['scriptSaveReceipt']['receipt']
        accepted = self.cli('source.receipt.' + operation, 'workspace', str(receipt['scope']),
            str(receipt['operationId']), receipt['sessionVersion']['digest'],
            receipt['requestVersion']['digest'])
        task = accepted['sourceReceiptTask']['taskId']
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            current = self.state()
            observed = current['sourceReceiptTask']
            if observed['status'] != 'pending':
                assert observed['taskId'] == task and observed['submitted']
                assert observed['result']['state'] == expected_state, observed
                assert observed['status'] == ('failed' if expected_state == 'unavailable' else 'succeeded'), observed
                assert current['scriptSaveReceipt'] == saved['scriptSaveReceipt']
                assert current['scriptDirty'] == saved['scriptDirty']
                self.observations.append(dict(operation=operation, receipt=receipt,
                    result=observed['result'], originalSaveUnchanged=True))
                return observed['result']
            time.sleep(.05)
        raise AssertionError('explicit product receipt observation deadline')

    def save(self, path, text, expected='succeeded'):
        opened = self.finish(self.cli('script.open', path))
        document = str(opened['activeDocumentId'])
        self.cli('script.replace', document, text)
        saved = self.finish(self.cli('script.save', document), expected)
        actual = (self.project / path).read_bytes()
        assert actual == text.replace('\n', '\r\n').encode()
        return document, saved, actual

    def cycle(self, index):
        started = time.monotonic()
        # Two alternating equal-size bodies keep the stable sample comparable.
        suffix = 'A' if index % 2 == 0 else 'B'
        document, known, actual = self.save('receipt_retained.arp', '# retained ' + suffix + '\n')
        receipt = known['scriptSaveReceipt']
        assert receipt['commitState'] == 'committed' and receipt['published'] and not known['scriptDirty']
        assert receipt['receiptMaintenance']['releaseAttempted']
        assert bytes(receipt['versionDigest']) == hashlib.sha256(actual).digest()
        result = self.observe(known, 'query', 'terminal')
        assert result['outcome']['commitState'] == 'committed'
        assert base64.b64decode(result['outcome']['version']['digest'], validate=True) == hashlib.sha256(actual).digest()
        with self.proxy.lock:
            self.proxy.block_forget_suffixes.clear()
        self.observe(known, 'release', 'released')
        self.observe(known, 'query', 'unavailable')
        self.cli('script.close', document)

        with self.proxy.lock:
            self.proxy.block_forget_suffixes.add('/receipt_retained.arp')

        document, unknown, actual = self.save('receipt_unknown.arp', '# unknown ' + suffix + '\n', 'failed')
        receipt = unknown['scriptSaveReceipt']
        assert receipt['commitState'] == 'outcome_unknown' and not receipt['published']
        assert unknown['scriptDirty'] and unknown['scriptText'] == '# unknown ' + suffix + '\n'
        assert receipt['receiptMaintenance']['queryAttempted']
        assert not receipt['receiptMaintenance']['releaseAttempted']
        with self.proxy.lock:
            self.proxy.bad_receipts.discard(receipt['receipt']['operationId'])
        result = self.observe(unknown, 'query', 'terminal')
        assert result['outcome']['commitState'] == 'committed'
        assert result['outcome']['version']['kind'] == 'content-sha256'
        assert base64.b64decode(result['outcome']['version']['digest'], validate=True) == hashlib.sha256(actual).digest()
        self.observe(unknown, 'release', 'released')
        self.observe(unknown, 'query', 'unavailable')
        # Explicit reread establishes a fresh baseline; observing a receipt alone
        # must not rewrite the original Unknown save or clear the user's draft.
        refreshed = self.finish(self.cli('script.reload', document, 'discard'))
        assert not refreshed['scriptDirty'] and refreshed['scriptText'] == '# unknown ' + suffix + '\n'
        assert refreshed['scriptSaveReceipt']['baselineRefreshed']
        assert refreshed['scriptSaveReceipt']['commitState'] == 'outcome_unknown'
        self.cli('script.close', document)
        print(f'PASS receipt lifecycle cycle {index}: {time.monotonic() - started:.2f}s', flush=True)

    def run(self, cycles):
        assert cycles in (1, 20)
        self.cycle(0)
        self.cli = self.repeated_command
        # Prewarm both actual A/B texts before comparing repeats. The UI keeps
        # content-keyed text layout runs; first exposure is retained cache data.
        self.cycle(1)
        baseline = self.sample()
        samples = []
        for index in range(2, cycles + 2):
            self.cycle(index)
            value = self.sample()
            samples.append(value)
            assert value == baseline, (baseline, value)
        with self.proxy.lock:
            counts = dict(self.proxy.counts)
            logical = len(self.proxy.replace_wires)
        assert logical == 2 * (cycles + 2)
        assert counts['dropped'] >= cycles + 2 and counts['corrupted'] == cycles + 2
        assert counts['blockedForget'] >= cycles + 2
        return dict(success=True, cycles=cycles, warmupCycles=2, baseline=baseline,
            samples=samples, observations=self.observations, proxyCounts=counts,
            firstCycleTransport='original Interpreter CLI', repeatedTransport='original typed REST bridge',
            logicalSaves=logical, wholeProcessLeakClaim=False, stressTest=False,
            originalSaveReceiptNeverRewritten=True, unknownBaselineOnlyFromExplicitReload=True)
