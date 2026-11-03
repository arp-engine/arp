"""Real host-native reflection generation with frozen metadata and source identity."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from source_file_http_smoke import run


class ReflectionCliSmoke:
    def __init__(self, args):
        self.args = args
        self.baseline = yaml.safe_load((ROOT / 'tests/fixtures/reflection_io/original-bytes.yaml')
                                       .read_text(encoding='utf-8'))
        self.results = []

    def arguments(self, case, output, *extra):
        values = ['--module', case['module'], '--out-json', str(output / 'schema.json'),
                  '--out-cpp', str(output / 'schema.cpp')]
        for header in case['headers']:
            values += ['--header', header]
        return [*values, *extra]

    def record(self, name, result, code):
        self.results.append(dict(case=name, returncode=result.returncode,
                                 stdout=result.stdout, stderr=result.stderr))
        assert result.returncode == code, (name, result)
        if code == 0:
            assert not result.stderr, (name, result)
        else:
            assert result.stderr, (name, result)

    def invoke(self, name, case, output, *extra, code=0, cwd=ROOT, executable=None):
        result = subprocess.run([str(executable or self.args.executable),
                                 *self.arguments(case, output, *extra)],
                                cwd=cwd, capture_output=True, text=True, encoding='utf-8', timeout=60)
        self.record(name, result, code)
        return result

    @staticmethod
    def snapshot(output):
        return {p.name: p.read_bytes() for p in output.iterdir() if p.is_file()}

    @staticmethod
    def verify(case, output):
        for filename, sha in case['outputs'].items():
            assert hashlib.sha256((output / filename).read_bytes()).hexdigest() == sha, filename

    def execute(self, root):
        for case in self.baseline['cases']:
            for path, sha in case['sourceSha256'].items():
                assert hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == sha, path
            output = root / ('native-' + case['name'])
            result = self.invoke('native-create-' + case['name'], case, output)
            assert result.stdout == bytes.fromhex(case['stdoutHex']).decode('utf-8')
            self.verify(case, output)
            self.invoke('native-replace-' + case['name'], case, output)
            self.verify(case, output)
        case = self.baseline['cases'][0]
        output = root / ('native-' + case['name'])
        before = self.snapshot(output)
        relative = case['headers'][0]
        staged = root / 'staged'
        source = staged / relative
        source.parent.mkdir(parents=True)
        original = (ROOT / relative).read_bytes()
        source.write_bytes(original.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        self.invoke('crlf-original-canonical-source', case, output, cwd=staged)
        self.verify(case, output)
        for name, data in [('utf8', b'\xff'), ('nul', b'x\0y'), ('ctrlz', b'// end\x1a')]:
            source.write_bytes(data)
            result = self.invoke(name, case, output, cwd=staged, code=1)
            expected = {'utf8': 'TEXT-INVALID-UTF8', 'nul': 'TEXT-NUL-REJECTED',
                        'ctrlz': 'REFLECT-IO-TEXT-EOF-UNSUPPORTED'}[name]
            assert expected in result.stderr, result
            assert source.read_bytes() == data and self.snapshot(output) == before
        source.unlink()
        self.invoke('missing-header', case, output, cwd=staged, code=1)
        assert self.snapshot(output) == before
        for option in [('--max-header-bytes', '1'), ('--max-input-bytes', '1'),
                       ('--max-output-bytes', '1'), ('--source-base-url', ''),
                       ('--source-base-url', '', '--source-base-url', 'http://127.0.0.1:1/'),
                       ('--max-output-bytes', '0', '--max-output-bytes', '16777216'),
                       ('--max-headers', '0'), ('--max-headers', '129'), ('--unknown',)]:
            self.invoke('options-' + str(len(self.results)), case, output, *option, code=1)
            assert self.snapshot(output) == before
        self.invoke('header-count-budget', self.baseline['cases'][2], output,
                    '--max-headers', '1', code=1)
        assert self.snapshot(output) == before
        uncreated = root / 'no-output'
        self.invoke('output-budget-no-directory', case, uncreated,
                    '--max-output-bytes', '1', code=1)
        assert not uncreated.exists()
        same = root / 'same'
        result = subprocess.run([str(self.args.executable), '--module', 'io_alias',
                                 '--header', case['headers'][0], '--out-json', str(same),
                                 '--out-cpp', str(same)], cwd=ROOT, capture_output=True,
                                text=True, encoding='utf-8', timeout=60)
        self.record('duplicate-target', result, 1)
        assert 'TOOL-OUTPUT-DUPLICATE' in result.stderr and not same.exists()
        (output / 'schema.cpp').write_bytes(b'x' * (16 * 1024 * 1024 + 1))
        self.invoke('second-baseline-budget', case, output, code=1)
        assert (output / 'schema.json').read_bytes() == before['schema.json']
        assert (output / 'schema.cpp').stat().st_size == 16 * 1024 * 1024 + 1
        (output / 'schema.cpp').write_bytes(before['schema.cpp'])
        unicode_exe = root / '反射 生成器.exe'
        shutil.copyfile(self.args.executable, unicode_exe)
        unicode_source = root / '中文 头文件.h'
        unicode_source.write_bytes(original)
        unicode_case = dict(case, headers=[str(unicode_source)])
        self.invoke('unicode-executable-header-output', unicode_case, root / '中文 产物',
                    executable=unicode_exe)
        self.http(root)

    def http(self, root):
        no_sources = root / 'no-local-sources'
        no_sources.mkdir()
        remote = root / 'http'
        for case in self.baseline['cases']:
            for header in case['headers']:
                target = remote / header
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / header, target)
        for case in self.baseline['cases']:
            output = root / ('http-' + case['name'])
            def verify(result):
                self.record('http-' + case['name'], result, 0)
                self.verify(case, output)
            assert run(self.args.executable, remote, {'/' + h: 1 for h in case['headers']},
                       cwd=no_sources,
                       arguments=lambda url: self.arguments(case, output, '--source-base-url', url),
                       verify=verify) == 0
        case = self.baseline['cases'][0]
        output = root / ('native-' + case['name'])
        before = self.snapshot(output)
        for mode in ('missing', 'escape'):
            header = case['headers'][0]
            if mode == 'missing':
                (remote / header).unlink()
            selected = case if mode == 'missing' else dict(case, headers=['../escape.h'])
            def verify(result):
                self.record('http-' + mode, result, 1)
                assert self.snapshot(output) == before
            assert run(self.args.executable, remote,
                       {'/' + header: 1} if mode == 'missing' else {}, cwd=ROOT,
                       arguments=lambda url: self.arguments(selected, output, '--source-base-url', url),
                       verify=verify) == 0

    def run(self):
        self.args.output.mkdir(parents=True, exist_ok=True)
        complete = False
        try:
            with tempfile.TemporaryDirectory(prefix='arp-reflection-io-') as folder:
                self.execute(Path(folder))
            complete = True
        finally:
            report = dict(type='ReflectionCodegenCliVerification', contractVersion=1,
                          complete=complete, cases=self.results, originalCases=3, originalFiles=6,
                          httpWithoutLocalSources=complete, productionReady=False)
            (self.args.output / 'report.yaml').write_text(
                yaml.safe_dump(report, allow_unicode=True, sort_keys=False), encoding='utf-8')
        print('PASS', len(self.results), 'reflection CLI calls, original six files and explicit HTTP')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    ReflectionCliSmoke(parser.parse_args()).run()
