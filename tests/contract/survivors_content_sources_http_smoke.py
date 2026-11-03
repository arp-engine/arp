"""Actual engine HTTP source closure; typed resources match the old loader."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'fixtures'))
from source_file_http_smoke import run
from survivors_content_files import ContentFiles

fixture = ContentFiles(sys.argv[2])
raise SystemExit(run(sys.argv[1], sys.argv[2], {'/' + name: 1 for name in fixture.names}))
