#!/usr/bin/env python3
"""Enforce the v2 plugin/code-generation boundary for every ARP module."""

from __future__ import annotations

import re
import json
import hashlib
import sys
from pathlib import Path


MODULE_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
FORBIDDEN = (
    "Host" + "ModuleBuilder",
    "register" + "Host" + "Module(",
    "registration::class_",
    "bindType<",
    "bindClass<",
    "bindObject(",
    "exportValue(",
    "exportFunction(",
    "exportCallable(",
)
GENERATED_RE = re.compile(
    r"reflection::generated::register_([A-Za-z0-9_]+)_reflection\s*\(")
SERVICE_EXPORT_RE = re.compile(
    r"exportService(?:Function|Callable)\s*\(\s*[\"']([^\"']+)[\"']")
FORMAL_RUNTIME_MODULES = {
    "arp_core", "arp_scene", "arp_scene3d", "arp_resources", "arp_network",
}


def parse_manifest(path: Path) -> tuple[dict[str, str], list[str], list[str]]:
    values: dict[str, str] = {}
    headers: list[str] = []
    service_exports: list[str] = []
    in_headers = False
    in_service_exports = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line == "reflectionHeaders:":
            in_headers = True
            in_service_exports = False
            continue
        if line == "serviceExports:":
            in_service_exports = True
            in_headers = False
            continue
        if in_headers and line.startswith("-"):
            headers.append(line[1:].strip())
            continue
        if in_service_exports and line.startswith("-"):
            service_exports.append(line[1:].strip().strip('"'))
            continue
        if in_headers and re.match(r"^[A-Za-z][A-Za-z0-9_]*:", line):
            in_headers = False
        if in_service_exports and re.match(r"^[A-Za-z][A-Za-z0-9_]*:", line):
            in_service_exports = False
        if not in_headers and ":" in line:
            key, value = line.split(":", 1)
            values[key.strip()] = value.strip().strip('"')
    return values, headers, service_exports


def resolve_header(root: Path, manifest: Path, value: str) -> Path:
    if value.startswith("src/"):
        return root / value
    return manifest.parent / value


def parse_required_services(path: Path) -> list[tuple[str, str]]:
    services: list[tuple[str, str]] = []
    pending_id: str | None = None
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        match_id = re.fullmatch(r"-[ \t]*id:[ \t]*([^#]+)", line)
        if match_id:
            pending_id = match_id.group(1).strip().strip('"')
            continue
        match_version = re.fullmatch(r"version:[ \t]*([0-9]+)", line)
        if pending_id is not None and match_version:
            services.append((pending_id, match_version.group(1)))
            pending_id = None
    return services


def check_module(root: Path, manifest: Path) -> list[str]:
    errors: list[str] = []
    values, headers, declared_service_exports = parse_manifest(manifest)
    prefix = str(manifest.relative_to(root))
    if values.get("type") != "arp.script.cpp-plugin":
        errors.append(f"{prefix}: type must be arp.script.cpp-plugin")
    if values.get("contractVersion") != "2":
        errors.append(f"{prefix}: contractVersion must be 2")
    if not values.get("exportContractHash"):
        errors.append(f"{prefix}: exportContractHash must be declared")
    module_version = values.get("moduleVersion", "")
    if not re.fullmatch(r"[1-9][0-9]*", module_version):
        errors.append(f"{prefix}: moduleVersion must be a positive integer")
    module = values.get("module", "")
    if not MODULE_RE.fullmatch(module):
        errors.append(f"{prefix}: module must be a flat identifier: {module!r}")
    source_name = values.get("source", "")
    source = manifest.parent / source_name
    if not source_name or not source.is_file():
        errors.append(f"{prefix}: source does not exist: {source_name!r}")
        return errors
    if not headers:
        errors.append(f"{prefix}: reflectionHeaders must be non-empty")
    for header in headers:
        if not resolve_header(root, manifest, header).is_file():
            errors.append(f"{prefix}: reflection header does not exist: {header!r}")

    # The entry source must reach generated registration. Some domain plugins
    # keep their service adapter in the implementation paired with a
    # reflection header (for example src/resource/resource_module.cpp), so
    # include those paired files in the same manual-export audit. Do not scan
    # the whole manifest directory: the flat compatibility manifests in
    # modules/ would otherwise see exports belonging to every other plugin.
    source_files = {source}
    for header in headers:
        header_path = resolve_header(root, manifest, header)
        sibling = header_path.with_suffix(".cpp")
        if sibling.is_file():
            source_files.add(sibling)
    source_files = sorted(source_files)
    generated_modules: set[str] = set()
    for cpp in source_files:
        generated_modules.update(
            match.group(1)
            for match in GENERATED_RE.finditer(cpp.read_text(encoding="utf-8")))
    # A compatibility manifest may keep its import/module name while sharing
    # the canonical generated reflection unit (for example `network` uses the
    # `arp_network` domain implementation). The manifest's reflectionModule
    # is the codegen symbol contract in that case.
    expected_generated_module = values.get("reflectionModule") or module
    if expected_generated_module not in generated_modules:
        errors.append(
            f"{prefix}: entry/adapter does not call generated registration "
            f"register_{expected_generated_module}_reflection()")

    for cpp in source_files:
        text = cpp.read_text(encoding="utf-8")
        for token in FORBIDDEN:
            if token in text:
                errors.append(f"{cpp.relative_to(root)}: forbidden manual export token {token!r}")

    # Service callables are the only runtime adapter exception to the
    # codegen-only rule. Their names still belong to the manifest contract:
    # an adapter cannot add an undeclared script symbol, and a declaration
    # cannot silently disappear from the implementation.
    actual_service_exports: list[str] = []
    for cpp in source_files:
        actual_service_exports.extend(
            SERVICE_EXPORT_RE.findall(cpp.read_text(encoding="utf-8")))
    actual_set = set(actual_service_exports)
    declared_set = set(declared_service_exports)
    if module == "arp_scene3d" and (actual_set or declared_set):
        errors.append(
            f"{prefix}: arp_scene3d must expose concrete objects, not serviceExports")
    if len(declared_service_exports) != len(declared_set):
        errors.append(f"{prefix}: serviceExports contains duplicate names")
    for name in sorted(actual_set - declared_set):
        errors.append(
            f"{prefix}: service export {name!r} is not declared in serviceExports")
    for name in sorted(declared_set - actual_set):
        errors.append(
            f"{prefix}: serviceExports declares {name!r} but no adapter exports it")
    return errors


def check_generated_artifacts(
    root: Path, manifests: list[Path], build_dir: Path) -> list[str]:
    """Verify every manifest reaches its own codegen artifacts."""
    errors: list[str] = []
    seen_modules: set[str] = set()
    for manifest in manifests:
        values, manifest_headers, _ = parse_manifest(manifest)
        module = values.get("module", "")
        if not module or module in seen_modules:
            continue
        seen_modules.add(module)
        reflection_dir = build_dir / "generated" / "reflection" / module
        reflection_json = reflection_dir / f"{module}.reflection.json"
        reflection_cpp = reflection_dir / f"{module}.reflection.gen.cpp"
        # Importer-only configurations may not have a build artifact. In a
        # configured build, however, silently omitting generated output would
        # violate the codegen-only export rule.
        if not reflection_json.is_file() or not reflection_cpp.is_file():
            if reflection_dir.exists():
                errors.append(
                    f"{manifest.relative_to(root)}: missing generated reflection "
                    f"artifacts for module {module!r}")
            continue
        try:
            data = json.loads(reflection_json.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{reflection_json}: cannot read generated reflection: {exc}")
            continue
        if data.get("module") != module:
            errors.append(
                f"{reflection_json}: generated module does not match manifest module {module!r}")
        expected_headers = [
            str(resolve_header(root, manifest, header).resolve()).replace("\\", "/")
            for header in manifest_headers
        ]
        generated_headers = [str(value).replace("\\", "/")
                             for value in data.get("headers", [])]
        if generated_headers != expected_headers:
            errors.append(
                f"{reflection_json}: generated headers do not exactly match manifest "
                f"reflectionHeaders: expected {expected_headers!r}, got {generated_headers!r}")
        if not data.get("headers"):
            errors.append(f"{reflection_json}: generated reflection has no source headers")
        if not any(data.get(key) for key in ("classes", "enums", "functions")):
            errors.append(f"{reflection_json}: generated reflection contains no exports")
        if module in FORMAL_RUNTIME_MODULES:
            for item in data.get("classes", []):
                name = str(item.get("reflectName", ""))
                if item.get("ownership") in (None, "", "unspecified"):
                    errors.append(
                        f"{reflection_json}: {name} must declare Ownership")
                if item.get("lifecycle") in (None, "", "unspecified"):
                    errors.append(
                        f"{reflection_json}: {name} must declare Lifecycle")
        if module == "arp_scene":
            class_names = {
                str(item.get("reflectName", "")) for item in data.get("classes", [])}
            if "SceneDiagnostic" not in class_names:
                errors.append(
                    f"{reflection_json}: generated arp.scene reflection omits SceneDiagnostic")
            else:
                diagnostic = next(
                    item for item in data.get("classes", [])
                    if str(item.get("reflectName", "")) == "SceneDiagnostic")
                if diagnostic.get("ownership") != "Value":
                    errors.append(
                        f"{reflection_json}: SceneDiagnostic must declare Ownership=Value")
                if diagnostic.get("lifecycle") != "Value":
                    errors.append(
                        f"{reflection_json}: SceneDiagnostic must declare Lifecycle=Value")
        if module == "arp_core":
            classes = {
                str(item.get("reflectName", "")): item
                for item in data.get("classes", [])}
            for name in ("Vec2", "Vec3", "Vec4", "Quat", "Mat4", "ScriptResult"):
                value_type = classes.get(name)
                if value_type is None:
                    errors.append(
                        f"{reflection_json}: generated arp_core reflection omits {name}")
                    continue
                if value_type.get("ownership") != "Value":
                    errors.append(
                        f"{reflection_json}: {name} must declare Ownership=Value")
                if value_type.get("lifecycle") != "Value":
                    errors.append(
                        f"{reflection_json}: {name} must declare Lifecycle=Value")
            mat4 = classes.get("Mat4")
            if mat4 is not None and not any(
                str(prop.get("name", "")) == "m"
                for prop in mat4.get("properties", [])
            ):
                errors.append(
                    f"{reflection_json}: Mat4.m must be present in generated properties")
        if module == "arp_scene3d":
            classes = {
                str(item.get("reflectName", "")): item
                for item in data.get("classes", [])}
            engine_components = (
                "MeshComponent", "TerrainComponent", "LightComponent",
                "CameraComponent", "ColliderComponent", "VolumetricCloudComponent")
            value_types = (
                "Bounds3D", "TerrainMetadata", "LightComponentDesc",
                "ColliderComponentDesc", "ColliderShapeDesc", "RigidBodyDesc",
                "CameraAspectPolicy", "ViewportRect", "RenderQueueMask",
                "CameraClearIntent", "CameraComponentDesc", "CameraDiagnostic",
                "ResolvedCamera", "CloudLayerDesc", "CloudDesc")
            for name in engine_components:
                component = classes.get(name)
                if component is None:
                    errors.append(
                        f"{reflection_json}: generated arp_scene3d reflection omits {name}")
                    continue
                if component.get("ownership") != "Engine":
                    errors.append(
                        f"{reflection_json}: {name} must declare Ownership=Engine")
                if component.get("lifecycle") != "Explicit":
                    errors.append(
                        f"{reflection_json}: {name} must declare Lifecycle=Explicit")
                if component.get("defaultConstruct"):
                    errors.append(
                        f"{reflection_json}: {name} must not expose a script constructor")
            for name in value_types:
                value_type = classes.get(name)
                if value_type is None:
                    errors.append(
                        f"{reflection_json}: generated arp_scene3d reflection omits {name}")
                    continue
                if value_type.get("ownership") != "Value":
                    errors.append(
                        f"{reflection_json}: {name} must declare Ownership=Value")
                if value_type.get("lifecycle") != "Value":
                    errors.append(
                        f"{reflection_json}: {name} must declare Lifecycle=Value")
        if module == "arp_network":
            classes = {
                str(item.get("reflectName", "")): item
                for item in data.get("classes", [])}
            engine_objects = (
                "NetworkRuntime", "NetworkOperation", "NetworkConnection",
                "NetworkRequest", "NetworkMessage", "TcpSocket", "UdpSocket",
                "HttpClient", "WebSocketClient")
            for name in engine_objects:
                engine_type = classes.get(name)
                if engine_type is None:
                    errors.append(
                        f"{reflection_json}: generated arp_network reflection omits {name}")
                    continue
                if engine_type.get("ownership") != "Engine":
                    errors.append(
                        f"{reflection_json}: {name} must declare Ownership=Engine")
                if engine_type.get("lifecycle") != "Explicit":
                    errors.append(
                        f"{reflection_json}: {name} must declare Lifecycle=Explicit")
                if engine_type.get("defaultConstruct"):
                    errors.append(
                        f"{reflection_json}: {name} must not expose a script constructor")
            for name in (
                "NetworkOperationState", "NetworkConnectionKind",
                "NetworkRequestMethod"):
                if not any(str(item.get("reflectName", "")) == name
                           for item in data.get("enums", [])):
                    errors.append(
                        f"{reflection_json}: generated arp_network reflection omits {name}")
        if module == "arp_resources":
            classes = {
                str(item.get("reflectName", "")): item
                for item in data.get("classes", [])}
            for name in ("ResourceManager", "ResourceBase", "ResourceOperation"):
                engine_type = classes.get(name)
                if engine_type is None:
                    errors.append(
                        f"{reflection_json}: generated arp_resources reflection omits {name}")
                    continue
                if engine_type.get("ownership") != "Engine":
                    errors.append(
                        f"{reflection_json}: {name} must declare Ownership=Engine")
                if engine_type.get("lifecycle") != "Explicit":
                    errors.append(
                        f"{reflection_json}: {name} must declare Lifecycle=Explicit")
                if engine_type.get("defaultConstruct"):
                    errors.append(
                        f"{reflection_json}: {name} must not expose a script constructor")
            for name in ("ResourceAliasResult", "ResourceLoadResult"):
                value_type = classes.get(name)
                if value_type is None:
                    errors.append(
                        f"{reflection_json}: generated arp_resources reflection omits {name}")
                    continue
                if value_type.get("ownership") != "Value":
                    errors.append(
                        f"{reflection_json}: {name} must declare Ownership=Value")
                if value_type.get("lifecycle") != "Value":
                    errors.append(
                        f"{reflection_json}: {name} must declare Lifecycle=Value")
            if "ArpScriptResourceResult" in classes:
                errors.append(
                    f"{reflection_json}: legacy ArpScriptResourceResult must not be exported")
            if not any(str(item.get("reflectName", "")) == "ResourceOperationState"
                       for item in data.get("enums", [])):
                errors.append(
                    f"{reflection_json}: generated arp_resources reflection omits ResourceOperationState")

        generated_text = reflection_cpp.read_text(encoding="utf-8")
        for item in data.get("classes", []):
            export_name = str(item.get("moduleExport", ""))
            if export_name and f'bindObject("{export_name}"' not in generated_text:
                errors.append(
                    f"{reflection_cpp}: generated module export {export_name!r} is not bound")

        # Static builds must be generated from the same manifest and expose a
        # checked registration result. A void adapter would silently discard
        # duplicate/invalid descriptors and make the static path diverge from
        # the dynamic importer.
        catalog_cpp = build_dir / "arp_script_plugins" / f"{module}.cpp"
        if catalog_cpp.is_file():
            catalog_text = catalog_cpp.read_text(encoding="utf-8")
            expected_symbol = f"arp_add_{module}_static_plugin"
            if not re.search(
                rf"\bbool\s+{re.escape(expected_symbol)}\s*\(",
                catalog_text,
            ):
                errors.append(
                    f"{catalog_cpp}: static catalog adapter must return bool")
            if f'"{module}"' not in catalog_text:
                errors.append(
                    f"{catalog_cpp}: static catalog module does not match manifest")
            export_hash = values.get("exportContractHash", "")
            if export_hash and f'"{export_hash}"' not in catalog_text:
                errors.append(
                    f"{catalog_cpp}: static catalog exportContractHash does not match manifest")
            reflection_hash = "sha256:" + hashlib.sha256(
                reflection_json.read_bytes()).hexdigest()
            if f'"{reflection_hash}"' not in catalog_text:
                errors.append(
                    f"{catalog_cpp}: static catalog reflectionRegistryHash does not match generated reflection")
            for service_id, version in parse_required_services(manifest):
                expected_service = (
                    f'ScriptPluginServiceRequirement service; service.serviceId = '
                    f'arp::core::pmr::String("{service_id}"'
                )
                if expected_service not in catalog_text or f"service.version = {version}u" not in catalog_text:
                    errors.append(
                        f"{catalog_cpp}: static catalog is missing required service {service_id}/v{version}")
        elif build_dir.exists():
            errors.append(
                f"{manifest.relative_to(root)}: missing generated static catalog adapter")
    catalog_dir = build_dir / "arp_script_plugins"
    if catalog_dir.is_dir():
        for catalog_cpp in catalog_dir.glob("*.cpp"):
            module = catalog_cpp.stem
            if "." in module:
                errors.append(
                    f"{catalog_cpp}: generated static catalog has dotted module name")
            elif module not in seen_modules:
                errors.append(
                    f"{catalog_cpp}: generated static catalog is not declared by any manifest")
    return errors


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).parents[2]).resolve()
    manifests = sorted((root / "modules").rglob("*.arpplugin"))
    if not manifests:
        print("no ARP plugin manifests found", file=sys.stderr)
        return 1
    errors: list[str] = []
    for manifest in manifests:
        errors.extend(check_module(root, manifest))
    if len(sys.argv) > 2:
        errors.extend(check_generated_artifacts(
            root, manifests, Path(sys.argv[2]).resolve()))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"script plugin codegen contract passed ({len(manifests)} manifests)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
