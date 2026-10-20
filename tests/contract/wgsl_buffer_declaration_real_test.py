"""Real Slang target declarations, physical maps and explicit array rejection."""
import argparse
import hashlib
import os
import pathlib
import subprocess
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests' / 'fixtures'))
from spirv_layout import SpirvLayoutFacts
from buffer_declaration_type_layout_crosscheck import BufferTypeLayoutCrossCheck


class RealDeclarationFixture:
    TYPES = {'UniformBuffer': 1, 'ReadOnlyStorageBuffer': 2, 'StorageBuffer': 3}

    def __init__(self, executable):
        self.executable = executable
        self.type_checker = BufferTypeLayoutCrossCheck(os.environ['ARP_BUFFER_TYPE_LAYOUT_VERIFIER']) if os.environ.get('ARP_BUFFER_TYPE_LAYOUT_VERIFIER') else None
        self.accepted = 0
        self.rejected = 0
        self.unavailable = 0

    def publication(self, path):
        publication = yaml.safe_load(path.read_text(encoding='utf-8'))
        assert publication['type'] == 'SingleEntryShaderArtifact' and publication['contractVersion'] in (1, 2, 3, 4)
        files = publication['files']
        reflection_path = path.parent / files['reflection']['path']
        for role, value in files.items():
            if value.get('status') == 'DiagnosticFailure':
                assert role == 'wgsl' and value['diagnosticId'] and value['contract']
                continue
            file = path.parent / value['path']
            assert hashlib.sha256(file.read_bytes()).hexdigest() == value['sha256'], file
        reflection = yaml.safe_load(reflection_path.read_text(encoding='utf-8'))
        if files['wgsl'].get('status') == 'DiagnosticFailure':
            self.unavailable += 1
            return None
        wgsl = path.parent / files['wgsl']['path']
        expected = {}
        arrays = False
        for binding in reflection['webGpuBindingSpace']:
            if not binding['active'] or binding['physicalType'] not in self.TYPES:
                continue
            key = (binding['group'], binding['webGpuBinding'])
            assert key not in expected
            expected[key] = self.TYPES[binding['physicalType']]
            arrays |= binding['capacity'] > 1
        if reflection['pushConstantWebGpuBinding'] is not None:
            push = reflection['pushConstantWebGpuBinding']
            key = (push['group'], push['binding'])
            assert key not in expected
            expected[key] = 1
        result = subprocess.run([self.executable, str(wgsl)], text=True, capture_output=True)
        if arrays:
            assert result.returncode == 1 and result.stdout == '' and 'SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED' in result.stderr, result
            if self.type_checker:
                self.type_checker.invoke(wgsl, '--wgsl', expect_failure=True)
            self.rejected += 1
            return None
        assert result.returncode == 0 and result.stderr == '', (path, result.stderr)
        facts = yaml.safe_load(result.stdout)
        assert facts['type'] == 'WgslBufferDeclarationMinimums' and facts['status'] == 'DeclarationOnly' and facts['contractVersion'] == 1
        actual = {(f['group'], f['binding']): f['addressSpace'] for f in facts['bindings']}
        assert actual == expected and len(actual) == len(facts['bindings']), (path, actual, expected)
        for f in facts['bindings']:
            assert f['minimumBindingBytes'] > 0 and f['alignmentBytes'] > 0
            assert f['hasRuntimeArray'] == bool(f['runtimeArrayStrideBytes'])
            assert f['runtimeArrayOffsetBytes'] == 0 or f['hasRuntimeArray']
        if self.type_checker:
            graph = self.type_checker.invoke(wgsl, '--wgsl')
            roots = {(r['set'], r['binding']): r for r in graph['roots']}
            assert set(roots) == set(actual) and len(roots) == len(graph['roots'])
            for f in facts['bindings']:
                root = roots[(f['group'], f['binding'])]
                node = graph['nodes'][root['type']]
                assert root['descriptorCount'] == 1 and root['descriptorType'] is None
                assert root['storage'] == {1: 1, 2: 3, 3: 4}[f['addressSpace']]
                assert node['targetSizeKnown'] and node['targetSizeBytes'] == f['minimumBindingBytes']
                assert node['alignmentKnown'] and node['alignmentBytes'] == f['alignmentBytes']
            for index, node in enumerate(graph['nodes']):
                assert node['alignmentKnown'] and node['alignmentBytes'] > 0 and node['targetSizeKnown'] and node['targetSizeBytes'] > 0
                assert node['occupiedExtentBytes'] <= node['targetSizeBytes']
                assert node['child'] is None or 0 <= node['child'] < index
                for member in graph['members'][node['memberBegin']:node['memberBegin'] + node['memberCount']]:
                    assert 0 <= member['type'] < index and member['alignmentKnown'] and member['alignmentBytes'] > 0
        self.accepted += 1
        return facts

    def probe(self, path):
        facts = self.publication(path)
        by_binding = {f['binding']: f for f in facts['bindings']}
        assert [by_binding[b]['minimumBindingBytes'] for b in range(3)] == [4, 48, 4]
        binary = SpirvLayoutFacts(path.parent / 'buffer_layout_probe.spv').buffer_layouts()
        assert binary[(0, 0)]['staticOccupiedExtentBytes'] == 4
        assert binary[(0, 1)]['runtimeElementOccupiedExtentBytes'] == 40
        assert binary[(0, 1)]['runtimeArrayStrideBytes'] == by_binding[1]['runtimeArrayStrideBytes'] == 48
        # Occupied40 and target minimum48 are independent assertions; matching
        # a stride is not general cross-target layout/length authority.
        assert by_binding[1]['minimumBindingBytes'] != binary[(0, 1)]['runtimeElementOccupiedExtentBytes']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True)
    parser.add_argument('--fixtures', type=pathlib.Path, required=True)
    parser.add_argument('--probe', type=pathlib.Path, required=True)
    args = parser.parse_args()
    fixture = RealDeclarationFixture(args.executable)
    publications = sorted(args.fixtures.glob('*/*.entry.yaml'))
    assert publications
    for publication in publications:
        fixture.publication(publication)
    fixture.probe(args.probe)
    print(f'RealSlangWgslDeclarationMinimum/1: accepted={fixture.accepted}, descriptor-array-diagnostic={fixture.rejected}, unavailable-target={fixture.unavailable}; typeGraphs={bool(fixture.type_checker)}; hashes, physical maps, scalar occupied4/min4 and struct occupied40/min48 PASS; no original authority')


if __name__ == '__main__':
    main()
