"""Real HTTP project resource acquisition with closed scene provenance."""
import functools
import http.server
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed.append(self.path)
        super().do_GET()


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='arp-project-source-') as folder:
        subprocess.run([executable, '--write-fixtures', folder], check=True, timeout=30)
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run([executable, '--http', f'http://127.0.0.1:{server.server_port}/'],
                           check=True, timeout=150)
            files = sorted('/' + path.relative_to(folder).as_posix()
                           for path in Path(folder).rglob('*') if path.is_file())
            assert len(server.observed) == len(files) * 40, server.observed
            for cycle in range(40):
                batch = server.observed[cycle * len(files):(cycle + 1) * len(files)]
                assert batch[0] == '/workspace/project.arp.yaml', batch
                assert sorted(batch) == files, batch
            print(f'Project resource sources: {len(server.observed)} real GETs; declarative planning; no rereads')
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
