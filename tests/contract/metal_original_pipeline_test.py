"""Original in-memory Metal validation after removing only isolated copied artifacts."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True)
    parser.add_argument('--sampled', type=Path, required=True)
    parser.add_argument('--storage', type=Path, required=True)
    parser.add_argument('--compute', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='arp-metal-original-pipeline-') as temp:
        root = Path(temp).resolve()
        sampled = root / 'sampled'
        storage = root / 'storage'
        shutil.copytree(args.sampled.parent, sampled)
        shutil.copytree(args.storage.parent, storage)
        compute = storage / args.compute.relative_to(args.storage.parent)
        process = subprocess.Popen([args.executable, '--deleted', str(sampled / args.sampled.name),
            str(storage / args.storage.name), str(compute)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            ready = process.stdout.readline().strip()
            assert ready == 'METAL_ORIGINAL_INPUTS_RETAINED', (ready, process.stderr.read())
            for target in (sampled, storage):
                assert target.resolve().parent == root
                shutil.rmtree(target)
            stdout, stderr = process.communicate('\n', timeout=90)
            assert process.returncode == 0, (stdout, stderr)
            assert 'deleted=1 native=0 sharedOutput=0' in stdout
            print(stdout, end='')
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()


if __name__ == '__main__':
    main()
