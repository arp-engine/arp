"""Validate runtime domains against procedural-generation dependencies."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


class RuntimeDomainDependencies:
    source_suffixes = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".inl", ".mm"}
    domain_targets = ("arp_lod", "arp_terrain", "arp_vegetation")
    domain_source_roots = ("src/3d/lod/", "src/3d/vegetation/", "src/3d/terrain/")
    integration_targets = ("arp_terrain_scene_runtime",)
    integration_source_roots = ("src/3d/terrain/integration/",)
    integration_dependencies = ("arp_core", "arp_lod", "arp_terrain", "arp_vegetation",
                                "arp_3d", "arp_scene", "arp_animation", "arp_physics",
                                "arp_resource", "arp_renderer")
    runtime_targets = ("arp_core", "arp_lod", "arp_terrain", "arp_vegetation",
                       "arp_terrain_scene_runtime",
                       "arp_resource", "arp_renderer", "arp_runtime_domains_smoke",
                       "arp_terrain_domain_load_smoke")

    def __init__(self, root: Path):
        self.root = root.resolve()

    def source_diagnostics(self) -> list[str]:
        diagnostics = []
        for domain in ("lod", "terrain", "vegetation"):
            if (self.root / "src" / domain).exists():
                diagnostics.append(f"RUNTIME-DOMAIN-LEGACY-DIRECTORY: src/{domain}")
        for path in sorted((self.root / "src").rglob("*")):
            if path.suffix not in self.source_suffixes:
                continue
            relative = path.relative_to(self.root).as_posix()
            text = path.read_text(encoding="utf-8-sig")
            if re.search(r'#\s*include\s*[<"](?:lod|terrain|vegetation)[/\\]', text):
                diagnostics.append(f"RUNTIME-DOMAIN-LEGACY-INCLUDE: {relative}")
            if relative.startswith("src/pcg/"):
                continue
            if re.search(r'#\s*include\s*[<"](?:[^"<>]*[/\\])?pcg[/\\]', text) or re.search(r'\bpcg::', text):
                diagnostics.append(f"RUNTIME-DOMAIN-PCG-DEPENDENCY: {relative}")
            if relative.startswith(self.domain_source_roots):
                integration = relative.startswith(self.integration_source_roots)
                if not integration and (re.search(r'#\s*include\s*[<"](?:[^"<>]*[/\\])?(?:renderer|resource|scene|animation)[/\\]', text) or re.search(
                    r'#\s*include\s*[<"]3d[/\\](?!(?:lod|terrain|vegetation)[/\\])', text
                )):
                    diagnostics.append(f"RUNTIME-DOMAIN-UPPER-LAYER-DEPENDENCY: {relative}")
                if not integration and re.search(
                    r'#\s*include\s*[<"](?:3d[/\\]terrain[/\\]integration|(?:\.\.[/\\])*integration)[/\\]', text
                ):
                    diagnostics.append(f"RUNTIME-DOMAIN-INTEGRATION-DEPENDENCY: {relative}")
                if re.search(r'\bstd::(?:vector|string|unordered_map|map|function|make_unique|make_shared)\b', text):
                    diagnostics.append(f"RUNTIME-DOMAIN-ALLOCATOR-BYPASS: {relative}")
        return diagnostics

    @staticmethod
    def read_targets(path: Path) -> dict[str, tuple[str, str]]:
        result = {}
        for line in path.read_text(encoding="utf-8").splitlines():
            name, sources, links = line.split("|", 2)
            result[name] = (sources, links)
        return result

    @classmethod
    def target_diagnostics(cls, targets: dict[str, tuple[str, str]]) -> list[str]:
        diagnostics = []
        for entry in dict.fromkeys(cls.runtime_targets + cls.integration_dependencies):
            if entry not in targets:
                if entry in cls.runtime_targets:
                    diagnostics.append(f"RUNTIME-DOMAIN-TARGET-MISSING: {entry}")
                continue
            pending = [(entry, [entry])]
            visited = set()
            while pending:
                name, route = pending.pop()
                if name in visited:
                    continue
                visited.add(name)
                sources, links = targets[name]
                if entry in cls.integration_dependencies and (
                    name in cls.integration_targets
                    or "/3d/terrain/integration/" in "/" + sources.replace("\\", "/")
                ):
                    diagnostics.append("RUNTIME-DOMAIN-INTEGRATION-TARGET: " + " -> ".join(route))
                if entry in cls.domain_targets and name in ("arp_3d", "arp_scene", "arp_animation"):
                    diagnostics.append("RUNTIME-DOMAIN-UPPER-LAYER-TARGET: " + " -> ".join(route))
                if (name == "arp_pcg"
                    or re.search(r'(?:^|[/;])pcg/', sources.replace("\\", "/"))
                    or re.search(r'\b(?:lib)?arp_pcgd?\.(?:a|lib)\b', links, re.IGNORECASE)):
                    diagnostics.append("RUNTIME-DOMAIN-PCG-TARGET: " + " -> ".join(route))
                # Object-library references may live in SOURCES instead of links.
                for token in re.findall(r'[A-Za-z_][A-Za-z_0-9]*', links + ";" + sources):
                    if token in targets:
                        pending.append((token, route + [token]))
        return diagnostics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--targets", type=Path, required=True)
    args = parser.parse_args()
    validator = RuntimeDomainDependencies(args.root)
    diagnostics = validator.source_diagnostics()
    diagnostics += validator.target_diagnostics(validator.read_targets(args.targets))
    for diagnostic in diagnostics:
        print(diagnostic)
    if not diagnostics:
        print("Runtime source and transitive target dependencies contain no PCG")
    return int(bool(diagnostics))


if __name__ == "__main__":
    raise SystemExit(main())
