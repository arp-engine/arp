#!/usr/bin/env python3
"""Fail if the removed script object facade leaks back into ARP sources."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: direct_object_boundary_contract.py <arp-root>", file=sys.stderr)
        return 2

    root = Path(sys.argv[1]).resolve()
    self_path = Path(__file__).resolve()
    forbidden = [
        "Script" + "ObjectIdentity",
        "Script" + "SceneWorldService",
        "Script" + "SceneWorldAdapter",
        "Script" + "SceneHostAdapter",
        "Script" + "Scene3DService",
        "SceneScript" + "Scene3DServiceAdapter",
        "Script" + "ObjectReference",
        "Component" + "Handle",
        "Host" + "Module",
        "Host" + "ModuleBuilder",
        "register" + "HostModule",
        "AotRegistrationHostV1",
    ]
    source_roots = [root / "src", root / "modules", root / "tests", root / "cmake"]
    source_files = []
    for source_root in source_roots:
        if source_root.exists():
            source_files.extend(
                path for path in source_root.rglob("*")
                if path.is_file() and path != self_path
            )
    source_files.append(root / "CMakeLists.txt")

    violations = []
    for path in source_files:
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for token in forbidden:
            if token in text:
                violations.append(f"{path}: {token}")

    removed_paths = [
        root / "src/scene/script_scene_world_service.h",
        root / "src/scene/script_scene_world_adapter.h",
        root / "src/scene/script_scene_world_adapter.cpp",
        root / "src/scene/script_scene_host_adapter.h",
        root / "src/scene/script_scene_host_adapter.cpp",
        root / "src/3d/script_scene3d_service.h",
        root / "src/3d/script_scene3d_service.cpp",
    ]
    violations.extend(
        f"removed source still exists: {path}"
        for path in removed_paths if path.exists()
    )

    if violations:
        print("direct-object boundary contract: FAIL")
        print("\n".join(violations))
        return 1

    print("direct-object boundary contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
