"""Give native ownership tests private copies they can remove after sealing."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import hashlib
import yaml

parser = argparse.ArgumentParser()
parser.add_argument("--executable", required=True)
parser.add_argument("--publication", action="append", required=True)
args = parser.parse_args()
for broken in (False, True):
    with tempfile.TemporaryDirectory(prefix="arp-ordinary-provider-") as temporary:
        copies = []
        for index, name in enumerate(args.publication):
            source = pathlib.Path(name)
            folder = pathlib.Path(temporary) / str(index)
            shutil.copytree(source.parent, folder)
            copies.append(str(folder / source.name))
        if broken:
            path = pathlib.Path(copies[1])
            node = yaml.safe_load(path.read_text())
            role = node["files"]["metal"]
            target = path.parent / role["path"]
            target.write_text("#include <metal_stdlib>\nusing namespace metal;\nundefined_type fail_compile;\n")
            role["sha256"] = hashlib.sha256(target.read_bytes()).hexdigest()
            path.write_text(yaml.safe_dump(node, sort_keys=False))
        command = [args.executable, *copies]
        if broken:
            command.append("RHI-METAL-PREPARED-GRAPHICS-PIPELINE-CREATION-FAILED")
        result = subprocess.run(command)
        if result.returncode:
            raise SystemExit(result.returncode)
