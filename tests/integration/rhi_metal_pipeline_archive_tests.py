import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='arp-metal-archive-') as folder:
    cache = Path(folder) / 'pipeline.bin'
    env = dict(os.environ, ARP_METAL_PIPELINE_CACHE_PATH=str(cache), MTL_DEBUG_LAYER='1')
    for mode in ('cold', 'warm', 'corrupt'):
        if mode == 'corrupt':
            data = bytearray(cache.read_bytes())
            data[-1] ^= 0x80
            cache.write_bytes(data)
        result = subprocess.run([args.executable, mode], env=env, capture_output=True, text=True, timeout=120)
        print(result.stdout, result.stderr, flush=True)
        assert result.returncode == 0
        if 'ArchiveApiUnavailable' in result.stderr:
            assert not cache.exists()
            break
        assert 'status=NativeBinaryArchive' in result.stderr and 'save=Saved' in result.stderr
        assert cache.exists() and 72 < cache.stat().st_size <= 64 * 1024 * 1024
        expected = {'cold': 'EmptyOrMissing', 'warm': 'CompatibleLoaded', 'corrupt': 'IncompatibleRejected'}
        assert 'input=' + expected[mode] in result.stderr
