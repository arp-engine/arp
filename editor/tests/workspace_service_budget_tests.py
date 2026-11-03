"""Standalone host budget admission and actual reads beyond the default 2 MiB."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import tempfile
import urllib.error
import urllib.request


class ServiceProcess:
    def __init__(self, executable, root, environment, options):
        self.executable, self.root = executable, root
        self.environment, self.options = environment, options

    def __enter__(self):
        self.process = subprocess.Popen([str(self.executable), '--root', str(self.root),
            '--id', 'budgetCLI', '--port', '0', '--duration-ms', '4000', *self.options],
            cwd=self.root, env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        line = self.process.stdout.readline().strip()
        match = re.fullmatch(r'ARP workspace: (http://127\.0\.0\.1:[0-9]+/workspace/v2) '
            r'id=budgetCLI session=([a-z0-9-]+)', line)
        if not match:
            self.process.kill()
            _, error = self.process.communicate(timeout=5)
            raise AssertionError(line + error)
        self.url, self.session = match.groups()
        return self

    def call(self, operation, **fields):
        payload = dict(type='arp.workspace.request', contractVersion=2,
            workspace='budgetCLI', operation=operation, **fields)
        if operation != 'info':
            payload['session'] = self.session
        request = urllib.request.Request(self.url, json.dumps(payload).encode(),
            {'Content-Type': 'application/json',
             'Authorization': 'Bearer ' + self.environment['ARP_WORKSPACE_READ_TOKEN']})
        with urllib.request.urlopen(request, timeout=10) as response:
            data = response.read(8 * 1024 * 1024 + 1)
            assert len(data) <= 8 * 1024 * 1024
            return json.loads(data)

    def __exit__(self, kind, value, traceback):
        try:
            _, error = self.process.communicate(timeout=10)
            assert self.process.returncode == 0, error
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.communicate(timeout=5)


def contracts(executable, root, environment):
    byte_flags = ('--max-file-bytes', '--max-request-bytes', '--max-response-bytes',
        '--preparation-bytes', '--max-query-bytes', '--max-directory-name-bytes')
    count_flags = ('--max-directory-entries', '--max-receipts')
    cases = [(flag, value) for flag in (*byte_flags, *count_flags)
             for value in ('0', '-1', 'not-a-number', '18446744073709551616')]
    cases += [(flag, '9223372036854775808') for flag in byte_flags]
    cases += [(flag, '4294967296') for flag in count_flags]
    for flag, value in cases:
        result = subprocess.run([str(executable), '--root', str(root), '--id', 'budgetCLI',
            '--port', '0', flag, value], cwd=root, env=environment, capture_output=True,
            text=True, timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        assert result.returncode == 2 and 'WORKSPACE-CLI-BUDGET' in result.stderr, (
            flag, value, result.stdout, result.stderr)
    # A valid scalar does not make inconsistent wire/body budgets admissible.
    # The existing protocol validator must reject them without widening defaults.
    for options in (['--max-file-bytes', '4194304'], ['--max-request-bytes', '1024'],
                    ['--max-response-bytes', '1024']):
        result = subprocess.run([str(executable), '--root', str(root), '--id', 'budgetCLI',
            '--port', '0', *options], cwd=root, env=environment, capture_output=True,
            text=True, timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        assert result.returncode == 2 and 'WORKSPACE-' in result.stderr, result
    print('PASS explicit service budget admission:', len(cases), 'invalid inputs')


def smoke(executable, root, environment):
    original = bytes(range(256)) * (3 * 1024 * 1024 // 256) + b'bounded-real-file'
    (root / 'large.bin').write_bytes(original)
    limit = 4 * 1024 * 1024
    version = dict(kind='content-sha256',
        digest=base64.b64encode(hashlib.sha256(original).digest()).decode('ascii'))
    request = dict(path='large.bin', maxBytes=limit, offset=0, size=0,
        version=version, maxVersionBytes=limit)
    with ServiceProcess(executable, root, environment, []) as server:
        assert server.call('info')['maxFileBytes'] == 2 * 1024 * 1024
        try:
            server.call('read', **request)
            raise AssertionError('Default budget accepted an oversized request')
        except urllib.error.HTTPError as error:
            assert error.code == 400
    options = ['--max-file-bytes', str(limit), '--max-request-bytes', str(6 * 1024 * 1024),
        '--max-response-bytes', str(6 * 1024 * 1024), '--preparation-bytes', str(64 * 1024 * 1024),
        '--max-query-bytes', str(128 * 1024), '--max-directory-entries', '64',
        '--max-directory-name-bytes', str(64 * 1024), '--max-receipts', '8']
    with ServiceProcess(executable, root, environment, options) as server:
        info = server.call('info')
        assert (info['maxFileBytes'], info['maxRequestBytes'], info['maxResponseBytes'],
            info['maxDirectoryEntries'], info['maxReceipts']) == (limit, 6 * 1024 * 1024,
            6 * 1024 * 1024, 64, 8)
        result = server.call('read', **request)
        assert result['ioStatus'] == 'success' and result['version'] == version, result
        assert base64.b64decode(result['data'], validate=True) == original
        assert (root / 'large.bin').read_bytes() == original
    print('PASS actual engine HTTP/FileService read:', len(original),
        'bytes; default budget rejects; explicit budget succeeds; both hosts exit normally')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    environment = dict(os.environ, ARP_WORKSPACE_READ_TOKEN=secrets.token_urlsafe(32))
    with tempfile.TemporaryDirectory(prefix='arp-workspace-budget-') as directory:
        root = Path(directory).resolve(strict=True)
        if args.smoke:
            smoke(executable, root, environment)
        else:
            contracts(executable, root, environment)


if __name__ == '__main__':
    main()
