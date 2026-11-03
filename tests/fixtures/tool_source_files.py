"""Original Unity tool source bindings from its existing package metadata."""
import hashlib
from pathlib import Path, PurePosixPath
import yaml


class ToolSourceFiles:
    entry = 'unity_import'
    absent = ('io', 'yaml', 'asset', 'arp_import_host')

    def __init__(self, root):
        root = Path(root).resolve(strict=True) / 'tools/unity_import'
        package = yaml.safe_load((root / 'aot.package.yaml').read_bytes())
        assert package['type'] == 'arp.script.aot-package/v1'
        self.paths = {}
        for module in package['modules']:
            name, source = module['id'], module['source']
            assert source == name + '.arp' and name not in self.paths
            assert not PurePosixPath(source).is_absolute() and '/' not in source and '\\' not in source
            self.paths[name] = (root / source).resolve(strict=True)
        assert self.entry in self.paths and not set(self.absent).intersection(self.paths)

    def routes(self, prefix):
        result = {prefix + name + '.arp': path.read_bytes() for name, path in self.paths.items()}
        result.update({prefix + name + '.arp': None for name in self.absent})
        return result

    def identity(self, output):
        lines = ['#pragma once', '#include <array>',
                 'namespace arp::tests::tool_source_identity {',
                 'struct File { const char* name; const char* sha256; };',
                 'inline constexpr std::array<File,' + str(len(self.paths)) + '> files{{']
        for name, path in self.paths.items():
            lines.append('    {"' + name + '","sha256:' + hashlib.sha256(path.read_bytes()).hexdigest() + '"},')
        lines += ['}};', '} // namespace arp::tests::tool_source_identity', '']
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text('\n'.join(lines), encoding='utf-8')
