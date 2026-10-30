"""Acquire the declared .arpui via engine HTTP; no host path enters the child."""
import functools
import http.server
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed.append(self.path)
        super().do_GET()


def main():
    metadata = len(sys.argv) == 3 and sys.argv[2] == "--metadata"
    image = len(sys.argv) == 3 and sys.argv[2] == "--image"
    font = len(sys.argv) == 3 and sys.argv[2] == "--font"
    bundle = len(sys.argv) == 3 and sys.argv[2] == "--bundle"
    header = "ui_metadata_fixture.h" if metadata or image else "ui_document_fixture.h"
    if font or bundle:
        header = "ui_font_fixture.h"
    fixture = (ROOT / "tests/fixtures" / header).read_text(encoding="utf-8")
    entries = [("meta/atlas.arpui", "atlas"), ("theme.arpui", "theme")] if metadata else [("hud.arpui", "yaml")]
    if image:
        entries = [("meta/atlas.arpui", "atlas")]
    if font:
        entries = [("meta/atlas.arpui", "font")]
    if bundle:
        entries = [("meta/atlas.arpui", "font")]
    expected = [f"/{name}" for name, _ in entries]
    with tempfile.TemporaryDirectory(prefix="arp-ui-source-") as folder:
        for name, delimiter in entries:
            yaml = fixture.split(f'R"{delimiter}(', 1)[1].split(f'){delimiter}"', 1)[0]
            target = Path(folder) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(yaml.encode("utf-8"))
        if bundle:
            composition = (ROOT / "tests/fixtures/ui_bundle_fixture.h").read_text(encoding="utf-8")
            for name, delimiter in [("theme.arpui", "bundleTheme"), ("hud.arpui", "bundleDocument")]:
                value = composition.split(f'R"{delimiter}(', 1)[1].split(f'){delimiter}"', 1)[0]
                (Path(folder) / name).write_bytes(value.encode("utf-8"))
                expected.append("/" + name)
        if image or bundle:
            pixels = (ROOT / "tests/fixtures/ui_atlas_pixels.h").read_text(encoding="utf-8")
            declared = pixels.split("uiAtlasPngBytes[] = {", 1)[1].split("};", 1)[0]
            target = Path(folder) / "images/atlas.png"
            target.parent.mkdir(parents=True)
            target.write_bytes(bytes(int(value, 16) for value in re.findall(r"0x[0-9a-f]+", declared)))
            expected.append("/images/atlas.png")
        if font or bundle:
            target = Path(folder) / "fonts/test.ttf"
            target.parent.mkdir(parents=True)
            target.write_bytes((ROOT / "3rd/box2d-3.1.1/samples/data/droid_sans.ttf").read_bytes())
            expected.append("/fonts/test.ttf")
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run([str(Path(sys.argv[1]).resolve()), "--http",
                            f"http://127.0.0.1:{server.server_port}/"], check=True, timeout=45)
            if bundle:
                assert len(server.observed) == len(expected) * 40, server.observed
                for cycle in range(40):
                    assert sorted(server.observed[cycle * len(expected):(cycle + 1) * len(expected)]) == sorted(expected), server.observed
            else:
                assert server.observed == expected * 40, server.observed
            print(f"UI source HTTP smoke: {40 * len(expected)} real reads; one read per resource per cycle")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
