"""Keep physics independent of scene and application domains."""

import importlib.util
from pathlib import Path
import re
import sys


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    source_root = root / "src"
    allowed_domains = {"core", "reflection", "physics"}
    include = re.compile(r'^\s*#\s*(?:include|import)\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)
    pending = list((source_root / "physics").rglob("*.h"))
    pending.extend((source_root / "physics").rglob("*.cpp"))
    visited = set()
    errors = []
    while pending:
        path = pending.pop().resolve()
        if path in visited:
            continue
        visited.add(path)
        for header in include.findall(path.read_text(encoding="utf-8-sig")):
            candidates = (path.parent / header, source_root / header)
            dependency = next((p.resolve() for p in candidates if p.is_file()), None)
            if dependency is None or not dependency.is_relative_to(source_root):
                continue
            relative = dependency.relative_to(source_root)
            if relative.parts[0] not in allowed_domains:
                errors.append(f"{path.relative_to(root)} includes upper-layer {relative}")
            else:
                pending.append(dependency)

    expected = {
        "arp_physics": {"arp_core", "arp_reflection", "arp_jolt_headers"},
        "arp_physics2d": {"arp_core", "arp_reflection", "box2d"},
    }
    cmake = "\n".join((root / p).read_text(encoding="utf-8-sig") for p in
                      ("CMakeLists.txt", "cmake/ArpSurvivors.cmake"))
    for target, allowed in expected.items():
        declarations = re.findall(rf"target_link_libraries\(\s*{target}\s+([^)]*)\)", cmake)
        dependencies = set(" ".join(declarations).split()) - {"PUBLIC", "PRIVATE", "INTERFACE"}
        if not declarations or dependencies != allowed:
            errors.append(f"{target} dependencies: expected {sorted(allowed)}, got {sorted(dependencies)}")

    spec = importlib.util.spec_from_file_location("arp_build", root / "build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    generated = set(build._auto_discover_modules()["arp_physics"].deps)
    if generated != expected["arp_physics"]:
        errors.append(f"build.py arp_physics dependencies: {sorted(generated)}")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Physics dependency contract passed ({len(visited)} transitive source/header files; both targets).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
