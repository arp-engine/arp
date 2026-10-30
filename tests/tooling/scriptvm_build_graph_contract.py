#!/usr/bin/env python3
"""Validate that generated ScriptVM build edges match the split AOT contract."""

from __future__ import annotations

import sys
import re
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"scriptvm build graph contract failed: {message}")


def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: scriptvm_build_graph_contract.py <source-root>")
    root = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(root))

    import build  # pylint: disable=import-outside-toplevel

    modules = build._auto_discover_modules()
    if "arp_scriptvm_aot" in modules:
        fail("legacy aggregate appears in auto-discovered modules")
    tooling = modules.get("arp_scriptvm_aot_tooling")
    if tooling is None:
        fail("arp_scriptvm_aot_tooling is not auto-discovered")
    expected_sources = {
        "src/scriptvm/aot/aot_codegen_v5.cpp",
        "src/scriptvm/aot/aot_package.cpp",
    }
    if set(tooling.srcs) != expected_sources:
        fail(f"unexpected tooling sources: {tooling.srcs}")
    ir = modules.get("arp_scriptvm_ir")
    frontend = modules.get("arp_scriptvm_frontend")
    interpreter = modules.get("arp_scriptvm_interpreter")
    if ir is None or set(ir.srcs) != {"src/scriptvm/ir/typed_ir.cpp"}:
        fail("Typed IR is not isolated in arp_scriptvm_ir")
    if frontend is None or any("compiler." in source for source in frontend.srcs):
        fail("frontend target is missing or still owns the retired compiler")
    if interpreter is None or "src/scriptvm/interpreter/bytecode.cpp" not in interpreter.srcs:
        fail("Interpreter target does not own bytecode")

    plugin_build = modules.get("arp_scriptvm_plugin_build")
    if plugin_build is None or set(plugin_build.srcs) != {
            "src/scriptvm/plugin/plugin_host_text_reader.cpp",
            "src/scriptvm/plugin/script_source_snapshot.cpp",
            "src/scriptvm/plugin/compiler_toolchain_layout.cpp",
            "src/scriptvm/plugin/immutable_build_input_snapshot.cpp"}:
        fail("plugin build support is not owned by scriptvm/plugin")
    if "arp_scriptvm_plugin_build" not in modules["arp_scriptvm_runtime"].deps:
        fail("plugin runtime is missing its build-support dependency")
    registry_header = (root / "src/scriptvm/runtime/module_registry.h").read_text(encoding="utf-8")
    registry_source = (root / "src/scriptvm/runtime/module_registry.cpp").read_text(encoding="utf-8")
    for name in ("ScriptPluginRegistrar", "ScriptPluginServiceRegistry", "StaticPluginCatalog", "ScriptPluginRuntimeScope"):
        if re.search(r"class\s+(?:ARP_API\s+)?" + name + r"\b", registry_header):
            fail(f"plugin type is still defined in the mixed registry: {name}")
        if name + "::" in registry_source:
            fail(f"plugin implementation is still defined in the mixed registry: {name}")
    if (root / "src/tooling").exists():
        fail("retired generic tooling source directory remains")

    tests = build._discover_test_modules()
    forbidden_targets = {
        "arp_scriptvm_test_runtime",
        "arp_scriptvm_aot_tests",
        "arp_scriptvm_hybrid_tests",
        "arp_scriptvm_component_benchmark",
        "arp_scriptvm_scene_behavior_dispatcher_tests",
    }
    for name, module in tests.items():
        if forbidden_targets.intersection(module.deps):
            fail(f"ABI-v2 build graph carries retired dependency: {name}")
        if name in forbidden_targets:
            fail(f"retired target leaked into auto discovery: {name}")

    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    for token in ("arp_scriptvm_aot_tests", "arp_scriptvm_hybrid_tests",
                  "arp_scriptvm_component_benchmark"):
        if token in cmake:
            fail(f"retired ScriptVM token remains in CMakeLists.txt: {token}")

    generator = (root / "scripts" / "cmake_gen.py").read_text(encoding="utf-8")
    if '"arp_scriptvm_aot"' in generator:
        fail("cmake generator still names the removed aggregate")
    libraries = (root / "cmake" / "ScriptAotLibraries.cmake").read_text(
        encoding="utf-8")
    if "add_library(arp_scriptvm_aot INTERFACE" in libraries:
        fail("ScriptAotLibraries still defines the removed aggregate")
    if "add_library(arp_scriptvm_aot_tooling" not in libraries:
        fail("ScriptAotLibraries does not define tooling target")
    if "add_library(arp_scriptvm_editor_runtime" not in libraries:
        fail("ScriptAotLibraries does not define the explicit editor runtime")
    if "src/scriptvm/editor/editor_hot_update.cpp" not in libraries:
        fail("editor runtime does not own editor_hot_update.cpp")
    if "arp_scriptvm_compile" in libraries:
        fail("retired compile target remains in ScriptAotLibraries")
    direct_start = libraries.find(
        "add_library(arp_scriptvm_direct_native_runtime")
    direct_end = libraries.find("\nadd_library(", direct_start + 1)
    if direct_start == -1 or direct_end == -1:
        fail("DirectNative runtime target cannot be located")
    direct_runtime = libraries[direct_start:direct_end]
    for token in ("src/scriptvm/aot/aot_package.cpp",
                  "src/scriptvm/aot/aot_codegen_v5.cpp",
                  "src/scriptvm/ir/typed_ir.cpp",
                  "src/scriptvm/compile/", "src/scriptvm/interpreter/"):
        if token in direct_runtime:
            fail(f"DirectNative runtime carries non-runtime source: {token}")
    if ("target_link_libraries(arp_scriptvm_direct_native_runtime PUBLIC\n"
            "    arp_core arp_reflection)" not in direct_runtime):
        fail("DirectNative runtime has an unexpected link closure")
    host_start = libraries.find("add_library(arp_scriptvm_host_plugin_runtime")
    host_end = libraries.find("\nadd_library(", host_start + 1)
    if host_start == -1 or host_end == -1:
        fail("host plugin runtime target cannot be located")
    host_sources = libraries[host_start:host_end]
    for token in ("src/scriptvm/aot/aot_package.cpp",
                  "src/scriptvm/compile/", "src/scriptvm/interpreter/"):
        if token in host_sources:
            fail(f"AOT-only host plugin runtime carries tooling source: {token}")
    for cmake_name in ("ScriptAotLibraries.cmake", "ScriptAotTargets.cmake",
                       "ScriptAotTestRegistrations.cmake"):
        cmake_text = (root / "cmake" / cmake_name).read_text(encoding="utf-8")
        for token in ("src/scriptvm/aot/aot.cpp", "src/scriptvm/aot/hybrid.cpp"):
            if token in cmake_text:
                fail(f"retired ScriptVM token remains in {cmake_name}: {token}")
    targets = (root / "cmake" / "ScriptAotTargets.cmake").read_text(
        encoding="utf-8")
    registrations = (root / "cmake" / "ScriptAotTestRegistrations.cmake").read_text(
        encoding="utf-8")
    for token in ("arp_scriptvm_legacy_runtime", "arp_scriptvm_hybrid",
                  "ARP_SCRIPTVM_LEGACY_MIGRATION_ONLY=1",
                  "scriptvm/aot/legacy/"):
        if token in libraries or token in targets or token in registrations:
            fail(f"retired Legacy build token remains: {token}")
    legacy_dir = root / "src" / "scriptvm" / "aot" / "legacy"
    if legacy_dir.exists():
        remaining = sorted(str(path.relative_to(root))
                           for path in legacy_dir.rglob("*"))
        fail(f"retired Legacy source directory remains: {remaining}")
    for token in ("arp_scriptvm_legacy_runtime", "arp_scriptvm_hybrid"):
        if token in (root / "CMakeLists.txt").read_text(encoding="utf-8"):
            fail(f"legacy target leaked into the root build graph: {token}")
    direct_header = (root / "src" / "scriptvm" / "aot" / "aot_v5.h").read_text(
        encoding="utf-8")
    if '"scriptvm/interpreter/suspended_frame.h"' in direct_header:
        fail("v5 public API still depends on the Interpreter directory")
    if '"scriptvm/runtime/suspended_frame.h"' not in direct_header:
        fail("v5 public API must use the runtime suspended-frame contract")
    retirement = root / "docs" / "scriptvm" / "aot_legacy_retirement_matrix.md"
    if retirement.exists():
        retirement_text = retirement.read_text(encoding="utf-8")
        if "legacy/continuation_orchestration.cpp" in retirement_text:
            fail("retirement matrix still names a deleted legacy owner")
        if "full legacy-retirement gate: contract complete" not in retirement_text.lower():
            fail("retirement matrix must record the closed full-retirement gate")
    for token in ("AotModuleDescriptorV1", "HybridExecutionDispatcher",
                  "LegacyGeneratorBridge", "legacyGeneratorBridge"):
        for path in (root / "src" / "scriptvm").rglob("*"):
            if path.is_file() and path.suffix in {".h", ".cpp"}:
                if token in path.read_text(encoding="utf-8"):
                    fail(f"retired ScriptVM token remains in {path}: {token}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
