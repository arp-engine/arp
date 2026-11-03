"""Real offline generator: original Windows bytes, engine HTTP, failure protection."""
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
from survivors_content_files import ContentFiles


class CodegenCliSmoke:
    def __init__(self, args):
        self.args = args
        self.baseline = yaml.safe_load((ROOT / 'tests/fixtures/survivors_codegen/original-bytes.yaml')
                                       .read_text(encoding='utf-8'))
        self.cases = []

    def invoke(self, name, source, output, *options, executable=None, code=0):
        result = subprocess.run([str(executable or self.args.executable), str(source),
                                 str(output), *options], capture_output=True, timeout=60)
        self.record(name, result, code)
        return result

    def record(self, name, result, code):
        self.cases.append(dict(case=name, returncode=result.returncode,
                               stdout=result.stdout.decode('utf-8') if isinstance(result.stdout, bytes)
                               else result.stdout,
                               stderr=result.stderr.decode('utf-8') if isinstance(result.stderr, bytes)
                               else result.stderr))
        assert result.returncode == code, (name, result)
        if code == 0:
            assert not result.stderr, (name, result)
        else:
            assert result.stderr, (name, result)
            assert 'Generated ArpScript' not in self.cases[-1]['stdout'], (name, result)

    def original_outputs(self, directory):
        for name, frozen in self.baseline['outputs'].items():
            raw = (directory / name).read_bytes()
            assert len(raw) == frozen['sizeBytes'], name
            assert hashlib.sha256(raw).hexdigest() == frozen['sha256'], name

    @staticmethod
    def snapshot(directory):
        return {p.relative_to(directory).as_posix(): p.read_bytes()
                for p in directory.rglob('*') if p.is_file()}

    def execute(self, root):
        assets = self.args.assets
        for name, sha in self.baseline['inputsSha256'].items():
            assert hashlib.sha256((assets / name).read_bytes()).hexdigest() == sha, name
        output = root / 'output'
        first = self.invoke('native-create', assets, output)
        assert first.stdout == self.baseline['stdout'].replace('\n', '\r\n').encode('utf-8')
        self.original_outputs(output)
        self.invoke('native-replace', assets, output)
        self.original_outputs(output)
        before = self.snapshot(output)
        checked = self.invoke('check', assets, output, '--check')
        assert checked.stdout == b'PASS canonical component module compilation\r\n'
        for options in [('--unknown',), ('--source-base-url', ''),
                        ('--source-base-url', '', '--source-base-url', 'http://127.0.0.1:1/'),
                        ('--max-output-bytes', '0'), ('--max-output-bytes', 'bad'),
                        ('--max-output-bytes', str(2**64 - 1)),
                        ('--max-output-bytes', '0', '--max-output-bytes', '16777216')]:
            self.invoke('options-' + str(len(self.cases)), assets, output, *options, code=2)
            assert self.snapshot(output) == before
        self.invoke('output-budget', assets, output, '--max-output-bytes', '1', code=1)
        assert self.snapshot(output) == before
        uncreated = root / 'uncreated'
        self.invoke('output-budget-no-directory', assets, uncreated,
                    '--max-output-bytes', '1', code=1)
        assert not uncreated.exists()
        assert self.snapshot(output) == before
        occupied = root / 'occupied-output'
        occupied.write_bytes(b'existing file')
        self.invoke('output-directory-conflict', assets, occupied, code=1)
        assert occupied.read_bytes() == b'existing file'
        assert self.snapshot(output) == before
        source = root / '中文 资源 & $()'
        shutil.copytree(assets, source)
        unicode_exe = root / '生成器 中文.exe'
        shutil.copyfile(self.args.executable, unicode_exe)
        self.invoke('unicode-paths', source, root / '中文 输出', executable=unicode_exe)
        self.original_outputs(root / '中文 输出')
        scripts = yaml.safe_load((source / 'script-package.yaml').read_text(encoding='utf-8'))['sources']
        targets = [('utf8', scripts[0], b'\xff'), ('nul', scripts[0], b'\x00'),
                   ('wave', 'hit.wav', b'bad'), ('missing', scripts[0], None),
                   ('metadata-version', 'game.yaml', b'type: arp.survivors-config\ncontractVersion: 99\n')]
        for name, path, content in targets:
            target = source / path
            original = target.read_bytes()
            if content is None:
                target.unlink()
            else:
                target.write_bytes(content)
            changed_source = self.snapshot(source)
            self.invoke('native-' + name, source, output, code=1)
            assert self.snapshot(output) == before and self.snapshot(source) == changed_source
            target.write_bytes(original)
        blocked = output / 'survivors.manifest.yaml'
        blocked.write_bytes(b'x' * (16 * 1024 * 1024 + 1))
        self.invoke('third-baseline-budget', assets, output, code=1)
        assert (output / 'script_aot_v5.gen.h').read_bytes() == before['script_aot_v5.gen.h']
        assert (output / 'survivors.gen.cpp').read_bytes() == before['survivors.gen.cpp']
        assert blocked.read_bytes() == b'x' * (16 * 1024 * 1024 + 1)
        blocked.write_bytes(before['survivors.manifest.yaml'])
        self.http(root, assets, output, before, scripts)

    def http(self, root, assets, output, before, scripts):
        all_names = ['game.yaml', 'script-package.yaml', *scripts, *ContentFiles(assets).names]
        for mode in ('success', 'content-version', 'wave', 'missing-game'):
            remote = root / ('http-' + mode)
            shutil.copytree(assets, remote)
            expected = {'/' + name: 1 for name in all_names}
            if mode == 'content-version':
                node = yaml.safe_load((remote / 'content.yaml').read_text(encoding='utf-8'))
                node['contractVersion'] = 99
                (remote / 'content.yaml').write_text(yaml.safe_dump(node), encoding='utf-8')
                expected = {'/' + name: 1 for name in all_names[:len(scripts) + 3]}
            if mode == 'wave':
                (remote / 'hit.wav').write_bytes(b'bad')
            if mode == 'missing-game':
                (remote / 'game.yaml').unlink()
                expected = {'/game.yaml': 1}

            def arguments(url):
                # Success has no local asset tree; failures have valid local
                # files to prove an explicit remote error cannot fall back.
                local = root / 'no-local-inputs' if mode == 'success' else assets
                return [str(local), str(output), '--source-base-url', url]

            def verify(result):
                self.record('http-' + mode, result, 0 if mode == 'success' else 1)
                assert self.snapshot(output) == before
                if mode == 'success':
                    self.original_outputs(output)

            assert run(self.args.executable, remote, expected, arguments=arguments, verify=verify) == 0

    def run(self):
        self.args.output.mkdir(parents=True, exist_ok=True)
        complete = False
        try:
            with tempfile.TemporaryDirectory(prefix='arp-survivors-codegen-') as directory:
                self.execute(Path(directory))
            complete = True
        finally:
            report = dict(type='SurvivorsCodegenCliVerification', contractVersion=1,
                          cases=self.cases, complete=complete, originalOutputsUnchanged=complete,
                          readSourceUnchanged=complete, httpNoLocalFallback=complete,
                          productionReady=False)
            (self.args.output / 'report.yaml').write_text(
                yaml.safe_dump(report, allow_unicode=True, sort_keys=False), encoding='utf-8')
        print('PASS', len(self.cases), 'real CLI calls; three original files, native/HTTP, failures')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    CodegenCliSmoke(parser.parse_args()).run()
