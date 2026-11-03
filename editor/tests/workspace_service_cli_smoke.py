"""Exercise the standalone host outside the source tree; no Editor or GPU needed."""
import base64
import json
import hashlib
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile
import urllib.request


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    read_token, write_token = secrets.token_hex(32), secrets.token_hex(32)
    env = dict(os.environ, ARP_WORKSPACE_READ_TOKEN=read_token, ARP_WORKSPACE_WRITE_TOKEN=write_token)
    with tempfile.TemporaryDirectory(prefix="arp-workspace-cli-") as directory:
        temporary = Path(directory).resolve(strict=True)
        assert temporary.parent == Path(tempfile.gettempdir()).resolve(strict=True)
        assert temporary.name.startswith("arp-workspace-cli-")
        root, cwd = temporary / "root", temporary / "cwd"
        root.mkdir()
        cwd.mkdir()
        original = b"\xef\xbb\xbf# initial\r\n"
        (root / "hello.arp").write_bytes(original)
        process = subprocess.Popen([str(executable), "--root", str(root), "--id", "fixtureCLI",
                                    "--write", "--port", "0", "--duration-ms", "3000"],
                                   cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            line = process.stdout.readline().strip()
            match = re.fullmatch(r"ARP workspace: (http://127\.0\.0\.1:[0-9]+/workspace/v2) id=fixtureCLI session=([a-z0-9-]+)", line)
            assert match, line
            url, session = match.groups()

            def call(operation, write=False, **fields):
                payload = {"type": "arp.workspace.request", "contractVersion": 2,
                           "workspace": "fixtureCLI", "operation": operation, **fields}
                if operation != "info":
                    payload["session"] = session
                wire = json.dumps(payload).encode()
                request = urllib.request.Request(url, data=wire,
                                                 headers={"Content-Type": "application/json",
                                                          "Authorization": "Bearer " + (write_token if write else read_token)})
                with urllib.request.urlopen(request, timeout=5) as response:
                    result = json.loads(response.read())
                if operation == "replace":
                    assert result["requestVersion"] == {"kind": "content-sha256",
                        "digest": base64.b64encode(hashlib.sha256(wire).digest()).decode("ascii")}
                return result

            info = call("info")
            assert info["session"] == session and not info["writeAuthorized"]
            metadata = call("stat", path="hello.arp", contentVersion=True, maxVersionBytes=65536, maxResultBytes=131072)
            assert metadata["ioStatus"] == "success"
            changed = b"# saved by workspace service\n"
            receipt = call("replace", write=True, path="hello.arp", id=1,
                           precondition="content-sha256", version=metadata["metadata"]["version"],
                           maxWriteBytes=65536, maxBaselineBytes=65536,
                           bytes=len(changed), data=base64.b64encode(changed).decode("ascii"))
            assert receipt["commit"] == "committed"
            assert call("receipt", id=1, receiptScope=info["receiptScope"], requestVersion=receipt["requestVersion"])["commit"] == "committed"
            assert (root / "hello.arp").read_bytes() == changed
            _, stderr = process.communicate(timeout=10)
            assert process.returncode == 0, stderr
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)
    print("PASS standalone workspace host: external cwd, actual disk SHA replacement, receipt and normal process exit")


if __name__ == "__main__":
    main()
