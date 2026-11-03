"""Build-only original source SHA oracle, not embedded script payloads."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'fixtures'))
from tool_source_files import ToolSourceFiles

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = ToolSourceFiles(args.root)
    files.identity(args.output)
    print('Original tool sources:', len(files.paths), 'files,',
          sum(path.stat().st_size for path in files.paths.values()), 'bytes')
