"""MinGW tool link custody: original snapshot owner and one engine allocator."""
import argparse
from pathlib import Path
import re
import subprocess


def run(args):
    symbols = subprocess.check_output([str(args.nm), '-C', str(args.executable)],
        text=True, encoding='utf-8', errors='replace', timeout=60)
    assert 'arp::scriptvm::ScriptSourceSnapshot::add(' in symbols
    assert re.search(r'\bI __imp__ZN3arp4core24defaultCpuMemoryResourceEv\b', symbols)
    for prohibited in ('MimallocCpuAllocationBackend', 'FallbackCpuAllocationBackend',
                       'CoreCpuAllocationLedger', 'mi_malloc', 'mi_heap_malloc'):
        assert prohibited not in symbols, prohibited
    assembly = subprocess.check_output([str(args.objdump), '-d', '-C', str(args.executable)],
        text=True, encoding='utf-8', errors='replace', timeout=60)
    provider = re.search(r'<arp::core::defaultCpuMemoryResource\(\)>:\n([^\n]+)', assembly)
    assert provider and '__imp__ZN3arp4core24defaultCpuMemoryResourceEv' in provider[1], provider
    print('PASS tool snapshot archive with original engine CPU-memory import thunk, no second allocator implementation')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('executable', 'nm', 'objdump'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
