"""Exact authored gameplay text corpus for Node/Fetch contract fixtures."""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import shutil

import yaml


class GameplayFiles:
    def __init__(self, directory):
        self.directory = Path(directory).resolve(strict=True)
        manifest = (self.directory / 'script-package.yaml').read_bytes()
        document = yaml.safe_load(manifest)
        assert document['type'] == 'arp.survivors-script-package'
        assert document['contractVersion'] == 1
        sources = document['sources']
        assert isinstance(sources, list) and 0 < len(sources) <= 32
        self.names = ['game.yaml', 'script-package.yaml']
        for name in sources:
            path = PurePosixPath(name)
            assert (name and '\\' not in name and ':' not in name and '\0' not in name
                    and not path.is_absolute() and path.suffix == '.arp'
                    and all(part not in ('.', '..') for part in name.split('/')))
            assert name not in self.names
            self.names.append(name)
        self.files = {name: (self.directory / name).read_bytes() for name in self.names}
        assert all(len(self.files[name]) <= 65536 for name in self.names[:2])
        assert all(len(self.files[name]) <= 2 * 1024 * 1024 for name in sources)
        assert sum(map(len, self.files.values())) <= 2 * 1024 * 1024 + 2 * 65536

    def stage(self, destination):
        destination = Path(destination).resolve()
        assert destination != self.directory and self.directory not in destination.parents
        destination.mkdir(parents=True, exist_ok=True)
        # Only a test-owned payload directory is reused; unexpected files fail
        # rather than silently joining the preloaded fixture.
        existing = {p.relative_to(destination).as_posix()
                    for p in destination.rglob('*') if p.is_file()}
        assert not existing - set(self.names), existing - set(self.names)
        for name in self.names:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.directory / name, target)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--stamp', type=Path, required=True)
    args = parser.parse_args()
    fixture = GameplayFiles(args.source)
    fixture.stage(args.output)
    args.stamp.write_text(yaml.safe_dump(dict(type='arp.survivors-gameplay-fixture',
        contractVersion=1, files={name: hashlib.sha256(data).hexdigest()
                                 for name, data in fixture.files.items()}), sort_keys=False),
        encoding='utf-8')
