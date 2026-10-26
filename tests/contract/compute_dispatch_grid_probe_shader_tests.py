"""Selected dispatch probe binary ABI; no native capability inference."""
import argparse
from pathlib import Path
import sys

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from spirv_layout import SpirvLayoutFacts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    directory = parser.parse_args().directory
    stem = directory / 'compute_dispatch_grid_probe'
    facts = SpirvLayoutFacts(stem.with_suffix('.spv'))
    layouts = facts.buffer_layouts()
    assert set(layouts) == {(0, 0), (0, 1)}, layouts
    assert layouts[(0, 0)]['staticOccupiedExtentBytes'] == 16, layouts
    assert layouts[(0, 1)]['runtimeArrayStrideBytes'] == 4, layouts
    assert not facts.push_constant_layouts()
    # OpExecutionMode LocalSize is independent of the YAML publication.
    assert any(op == 16 and args[1:] == (17, 8, 8, 1)
               for _, op, args in facts.instructions)
    reflection = yaml.safe_load(stem.with_suffix('.reflection.yaml').read_text(encoding='utf-8'))
    entry, = reflection['entryPoints']
    assert (entry['computeWorkgroupSizeX'], entry['computeWorkgroupSizeY'],
            entry['computeWorkgroupSizeZ']) == (8, 8, 1)
    assert {(b['set'], b['binding'], b['type']) for b in reflection['bindings']} == {
        (0, 0, 'UniformBuffer'), (0, 1, 'StorageBuffer')}
    for target in ('metal', 'webGpu'):
        assert reflection[target + 'BindingSpaceStatus'] == 'Available'
        assert len(reflection[target + 'BindingSpace']) == 2
    for suffix in ('.metal', '.wgsl', '.computeMain.entry.yaml'):
        assert Path(str(stem) + suffix).stat().st_size > 0
    print('DISPATCH_GRID_PROBE_ABI_PASS uniform16/storage4/local8x8x1/three targets')


if __name__ == '__main__':
    main()
