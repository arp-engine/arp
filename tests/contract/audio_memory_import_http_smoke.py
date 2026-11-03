"""Exact four raw WAV reads through the engine HTTP client."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'fixtures'))
from source_file_http_smoke import run

raise SystemExit(run(sys.argv[1], sys.argv[2],
                     {'/' + name: 1 for name in ('hit.wav', 'upgrade.wav', 'menu.wav', 'music.wav')}))
