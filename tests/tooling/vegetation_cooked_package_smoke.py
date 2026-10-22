"""Cook and consume a closed terrain/catalog/material package in separate processes."""

import argparse
from pathlib import Path
import subprocess
import shutil
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True)
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--terrain-tool")
    parser.add_argument("--authoring", action="store_true")
    parser.add_argument("--spatial-weights", action="store_true")
    parser.add_argument("--catalog-authoring", action="store_true")
    parser.add_argument("--reader")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="vegetation-cooked-", dir=args.workspace) as directory:
        output = Path(directory) / "terrain.arpkg"
        if args.terrain_tool:
            prepared = Path(directory) / "prepared"
            preparation_mode = "--prepare-authoring" if args.authoring else "--prepare-closure"
            if args.spatial_weights:
                preparation_mode = "--prepare-spatial-authoring"
            if args.catalog_authoring:
                preparation_mode = "--prepare-catalog-authoring"
            subprocess.run([args.tool, preparation_mode, str(prepared)], check=True, timeout=25)
            if args.catalog_authoring:
                catalog = prepared / "catalog.arpkg"
                catalog_command = [args.terrain_tool, "cook-catalog",
                                   str(prepared / "catalog-authoring.yaml"), str(catalog)]
                subprocess.run(catalog_command, check=True, timeout=25)
                subprocess.run([args.tool, "--load-catalog-authoring", str(catalog)],
                               check=True, timeout=25)
                if subprocess.run(catalog_command, timeout=25).returncode == 0:
                    raise RuntimeError("Catalog publication overwrote an existing target")
            command = [args.terrain_tool, "build" if args.authoring else "package",
                       str(prepared / "authoring.yaml") if args.authoring else str(prepared), str(output)]
            subprocess.run(command, check=True, timeout=25)
            # Existing targets cannot be replaced, even by an otherwise valid source.
            if subprocess.run(command, timeout=25).returncode == 0:
                raise RuntimeError("Closed package publication overwrote an existing target")
            shutil.rmtree(prepared)
            if (output / "files/unlisted.cpu-fixture").exists():
                raise RuntimeError("Publication copied a file outside the declared closure")
        else:
            subprocess.run([args.tool, "--cook", str(output)], check=True, timeout=25)
        # There is no GLB, authoring catalog or PCG process in the consumer environment.
        if list(output.rglob("*.glb")):
            raise RuntimeError("Cooked package unexpectedly depends on GLB authoring")
        relocated = output.with_name("relocated.arpkg")
        output.rename(relocated)
        mode = "--load-closure" if args.terrain_tool else "--load"
        if args.authoring:
            mode = "--load-authoring"
        if args.spatial_weights:
            mode = "--load-spatial-authoring"
        subprocess.run([args.tool, mode, str(relocated)], check=True, timeout=25)
        if args.reader:
            subprocess.run([args.reader, str(relocated)], check=True, timeout=30)
    print("Fresh-process cooked terrain/catalog/material load/query/unload/reload passed")


if __name__ == "__main__":
    main()
