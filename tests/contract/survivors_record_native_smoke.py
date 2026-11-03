"""Own a temporary directory for the actual mounted native record lifecycle."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='arp-survivors-record-') as folder:
    raise SystemExit(subprocess.run([str(Path(sys.argv[1]).resolve()), '--native', folder],
                                    timeout=60).returncode)
