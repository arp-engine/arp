#!/usr/bin/env python3
"""
arp-engine CMake 工程文件生成器。

从 build.py 拆出，负责把模块描述降级为 CMakeLists.txt 与 CMakePresets.json。
不依赖 build.py，被 build.py 单向 import。

职责边界：
  - CmakeWriter          : ModuleDef 集合 -> CMakeLists.txt
  - generate_cmake_presets: 编译器/平台配置 -> CMakePresets.json
  - topo_sort            : 模块依赖拓扑排序（CmakeWriter 内部用）
  - exists               : 相对项目根的文件存在检查（CmakeWriter + build.py 共用）
"""

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Set


# ============================================================
# 项目根目录（scripts/ 的上一级）
# ============================================================
ROOT = Path(__file__).resolve().parent.parent

# Keep CMake output names and source dependencies identical to the shader
# compiler's authoritative manifest.
sys.path.insert(0, str(ROOT / "scripts"))
from compile_shaders import SHADER_SOURCES, SHADER_SHARED_SOURCES  # noqa: E402
from shader_bindings_codegen import PILOT_STRUCT_NAMES  # noqa: E402
from cmake_presets_document import CMakePresetDocument, CMakePresetDocumentError  # noqa: E402


class BuildBackendSelectionError(ValueError):
    """Rejected build-product intent; never a GPU capability result."""


class BuildBackendSelection:
    """BuildBackendSelection/1, before discovery, file writes or configure."""

    VERSION = 1
    DEFAULTS = {'win': 'vulkan', 'linux': 'vulkan', 'android': 'vulkan',
                'mac': 'metal', 'ios': 'metal', 'web': 'webgpu'}

    @classmethod
    def resolve(cls, platform, requested):
        if platform not in cls.DEFAULTS:
            raise BuildBackendSelectionError('RHI-BUILD-PLATFORM-UNKNOWN: unsupported build platform')
        if requested == 'auto':
            return cls.DEFAULTS[platform]
        if requested not in ('vulkan', 'metal', 'webgpu', 'webgl1'):
            raise BuildBackendSelectionError('RHI-BUILD-BACKEND-UNKNOWN: unsupported build backend')
        if platform != 'web' and requested in ('webgpu', 'webgl1'):
            raise BuildBackendSelectionError('RHI-WEB-BUILD-PLATFORM: Web RHI product requires web platform')
        if platform == 'web' and requested == 'webgl1':
            raise BuildBackendSelectionError('RHI-WEBGL1-BUILD-UNAVAILABLE: production WebGL1 target is not implemented; webgl1-probe is ProbeOnly')
        if requested != cls.DEFAULTS[platform]:
            raise BuildBackendSelectionError('RHI-BUILD-BACKEND-PLATFORM: backend does not match the registered platform product')
        return requested


class BuildPresetSelection:
    """BuildPresetSelection/1: product/config identity, never GPU admission."""

    VERSION = 1
    STEMS = {'win': 'win-vulkan', 'web': 'web-webgpu', 'mac': 'mac-metal',
             'ios': 'ios-metal', 'linux': 'linux-vulkan', 'android': 'android-arm64-vulkan'}
    CONFIGURATIONS = {'debug': 'Debug', 'release': 'Release'}

    @classmethod
    def registered_backend(cls, name):
        """Exact registered build identities; not a native capability query."""
        for platform in cls.STEMS:
            for configuration in cls.CONFIGURATIONS:
                if name == cls.resolve(platform, 'auto', configuration):
                    return BuildBackendSelection.DEFAULTS[platform]
        legacy = {'macos-metal': 'metal', 'ios-metal': 'metal',
                  'linux-vulkan': 'vulkan', 'android-arm64-vulkan': 'vulkan'}
        return legacy.get(name)

    @classmethod
    def resolve(cls, platform, backend, config):
        BuildBackendSelection.resolve(platform, backend)
        if config == 'package':
            raise BuildBackendSelectionError('RHI-BUILD-CONFIG-UNAVAILABLE: package configuration has no registered build contract')
        if config not in cls.CONFIGURATIONS:
            raise BuildBackendSelectionError('RHI-BUILD-CONFIG-UNKNOWN: unsupported build configuration')
        return f'{cls.STEMS[platform]}-{config}'


# ScriptVM browser tests exercise native string marshalling through Emscripten.
# A resizable Wasm memory exposes resizable ArrayBuffer views, which browser
# TextDecoder implementations reject. Keep these targets on the fixed-memory
# contract used by browser smoke tests; the target-local flags intentionally
# override any cache-level linker flags supplied by a caller.
SCRIPT_VM_WASM_TEST_TARGETS = frozenset({
    "arp_scriptvm_typed_ir_tests",
    "arp_scriptvm_semantic_runtime_tests",
    "arp_scriptvm_aot_v5_scene_behavior_smoke",
})


# ============================================================
# 共享工具
# ============================================================

def exists(path: str) -> bool:
    """检查相对项目根的路径是否存在。"""
    return (ROOT / path).exists()


def topo_sort(modules: Dict[str, Any], selected: Set[str]) -> List[str]:
    """对选中的模块进行拓扑排序（Kahn's algorithm）。"""
    # `selected` is intentionally a set at call sites.  Keep every tie-break
    # in ModuleDef declaration order so regenerating CMake is reproducible.
    selected_names = [name for name in modules if name in selected]
    order = {name: index for index, name in enumerate(selected_names)}
    in_degree: Dict[str, int] = {}
    adj: Dict[str, List[str]] = {}

    for m in selected_names:
        in_degree[m] = 0
        adj[m] = []

    for m in selected_names:
        for dep in modules[m].deps:
            if dep in selected:
                adj[dep].append(m)
                in_degree[m] = in_degree.get(m, 0) + 1

    queue = [m for m in selected_names if in_degree[m] == 0]
    result: List[str] = []
    while queue:
        m = queue.pop(0)
        result.append(m)
        for n in sorted(adj.get(m, []), key=lambda name: order[name]):
            in_degree[n] -= 1
            if in_degree[n] == 0:
                queue.append(n)
        queue.sort(key=lambda name: order[name])

    if len(result) != len(selected):
        # Cycle detected - fall back to original order
        return selected_names
    return result


# ============================================================
# CMakeLists.txt 生成器
# ============================================================

class CmakeWriter:
    """将模块描述写入 CMakeLists.txt。

    platform/backend 作为构造参数显式传入，不再依赖全局变量，
    使生成逻辑可独立测试与复用。
    """

    def __init__(self, modules: Dict[str, Any], build_dir: str,
                 config: str, platform: str, backend: str,
                 compiler_gcc: str, compiler_gxx: str,
                 ninja: str, vulkan_sdk: str, slangc: str):
        self.modules = modules
        self.build_dir = build_dir
        self.config = config
        self.platform = platform
        self.backend = BuildBackendSelection.resolve(platform, backend)
        BuildPresetSelection.resolve(platform, self.backend, config)
        self.compiler_gcc = compiler_gcc
        self.compiler_gxx = compiler_gxx
        self.ninja = ninja
        self.vulkan_sdk = vulkan_sdk
        self.slangc = slangc
        self.lines: List[str] = []

    def _w(self, text: str = ""):
        self.lines.append(text)

    def _emit_header(self):
        self._w("# Auto-generated by build.py — DO NOT EDIT MANUALLY")
        self._w(f"# Platform: {self.platform}  Backend: {self.backend}  Config: {self.config}")
        self._w()
        self._w("cmake_minimum_required(VERSION 3.24)")
        self._w()

        # RC compiler for MinGW
        self._w("if(CMAKE_HOST_WIN32 AND EXISTS \"C:/mingw64/bin/windres.exe\")")
        self._w("    set(ENV{RC} \"C:/mingw64/bin/windres.exe\")")
        self._w("    set(CMAKE_RC_COMPILER \"C:/mingw64/bin/windres.exe\" CACHE FILEPATH \"\" FORCE)")
        self._w("endif()")
        self._w()
        self._w("project(AnimeRenderPipeline LANGUAGES C CXX)")
        self._w()
        self._w("include(cmake/ArpWebRhiSelection.cmake)")
        self._w('set(ARP_WEB_RHI "" CACHE STRING "Web RHI product: empty preserves platform default; none, webgpu or webgl1")')
        self._w('set_property(CACHE ARP_WEB_RHI PROPERTY STRINGS "" none webgpu webgl1)')
        self._w('option(ARP_WEBGL1_PROTOTYPE_PRODUCT "Build explicit WebGL1 runtime component Prototype" OFF)')
        self._w('if(ARP_WEBGL1_PROTOTYPE_PRODUCT)')
        self._w('    include(cmake/ArpWebGlProduct.cmake)')
        self._w('    return()')
        self._w('endif()')
        self._w('arp_select_web_rhi("${CMAKE_SYSTEM_NAME}" "${ARP_WEB_RHI}" ARP_SELECTED_WEB_RHI)')
        self._w()
        self._w('set(ARP_IOS_BUNDLE_IDENTIFIER "com.animerenderpipeline.metal-smoke" CACHE STRING')
        self._w('    "Bundle identifier used by the generated iOS application target")')
        self._w()

    def _emit_platform_specific(self):
        # The engine is frequently consumed through add_subdirectory() by a
        # project configured for a different final platform.  Select flags from
        # CMake's target platform, not from the host platform that generated
        # this file.
        self._w("if(CMAKE_SYSTEM_NAME STREQUAL \"Emscripten\")")
        self._w("    add_compile_options(-matomics -mbulk-memory -fexceptions)")
        self._w("    add_link_options(-fexceptions -sDISABLE_EXCEPTION_CATCHING=0")
        self._w("        \"--js-library=${CMAKE_CURRENT_SOURCE_DIR}/src/scriptvm/runtime/wasm_random_get.js\")")
        self._w("endif()")
        self._w()
        self._w("if(APPLE)")
        self._w("    enable_language(OBJCXX)")
        self._w("endif()")
        self._w()

    @staticmethod
    def _cmake_platform_condition(platform: str) -> str:
        """Return the CMake condition for a ModuleDef platform key."""
        conditions = {
            "web": 'CMAKE_SYSTEM_NAME STREQUAL "Emscripten"',
            "native": 'NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten"',
            "win": 'WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten"',
            "mac": 'APPLE AND NOT IOS',
            "ios": 'IOS',
            "apple": 'APPLE',
            "non_apple": 'NOT APPLE',
            "vulkan_native": 'NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND ARP_HAS_SDL3_DEVEL',
            "linux": 'UNIX AND NOT APPLE AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten"',
            "android": 'ANDROID',
        }
        return conditions.get(platform, "")

    @classmethod
    def _cmake_module_platform_condition(cls, module_name: str, platform: str) -> str:
        # The platform's Web sources/dependencies implement the selected
        # product. OS identity alone must not select a second RHI route.
        if module_name == "arp_platform" and platform == "web":
            return 'ARP_SELECTED_WEB_RHI STREQUAL "webgpu"'
        if module_name == "arp_platform" and platform == "vulkan_native":
            return 'NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten"'
        return cls._cmake_platform_condition(platform)

    def _emit_std_flags(self):
        self._w("set(CMAKE_CXX_STANDARD 17)")
        self._w("set(CMAKE_CXX_STANDARD_REQUIRED ON)")
        self._w("set(CMAKE_CXX_EXTENSIONS OFF)")
        self._w("set(CMAKE_POSITION_INDEPENDENT_CODE ON)")
        self._w()
        self._w("find_package(Python3 REQUIRED COMPONENTS Interpreter)")
        self._w()
        self._w("include(cmake/ThirdParty.cmake)")
        self._w("include(cmake/ReflectionCodegen.cmake)")
        self._w()

    def _emit_engine_dll(self):
        if "arp_engine" not in self.modules:
            return
        self._w("if(CMAKE_SYSTEM_NAME STREQUAL \"Emscripten\")")
        self._w("    add_library(arp_engine STATIC src/engine/engine_dll.cpp)")
        self._w("else()")
        self._w("    add_library(arp_engine SHARED src/engine/engine_dll.cpp)")
        self._w("endif()")
        self._w("target_include_directories(arp_engine PUBLIC . src 3rd/stb)")
        self._w("target_compile_definitions(arp_engine PRIVATE ARP_BUILDING_DLL=1)")
        self._w("set_target_properties(arp_engine PROPERTIES"
                " OUTPUT_NAME arp_engine PREFIX \"\" DEBUG_POSTFIX \"\")")
        self._w("if(MINGW)")
        self._w("    target_link_options(arp_engine PRIVATE \"-Wl,--exclude-libs,libassimpd.a\")")
        self._w("    target_link_options(arp_engine PRIVATE \"-Wl,--exclude-libs,libJoltd.a\")")
        self._w("elseif(WIN32)")
        self._w("    set_target_properties(arp_engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)")
        self._w("endif()")
        self._w()

    def _emit_library(self, m: Any):
        if not m.srcs:
            return  # skip empty modules
        self._w(f"add_library({m.name}")
        for s in m.srcs:
            if exists(s):
                self._w(f"    {s}")
        self._w(")")

        # Include dirs
        inc_dirs = " ".join(m.include_dirs)
        if inc_dirs:
            self._w(f"target_include_directories({m.name} PUBLIC {inc_dirs})")

        # Dependencies (include ALL deps - external targets from ThirdParty.cmake are valid)
        if m.deps:
            deps_str = " ".join(m.deps)
            self._w(f"target_link_libraries({m.name} PUBLIC {deps_str})")

        # Compile definitions
        for scope, defs in m.compile_defs.items():
            if defs:
                def_str = " ".join(f"{k}={v}" for k, v in defs.items())
                self._w(f"target_compile_definitions({m.name} {scope} {def_str})")

        if m.link_flags:
            self._w(f"target_link_options({m.name} PRIVATE {' '.join(m.link_flags)})")
        for platform, flags in m.platform_link_flags.items():
            condition = self._cmake_platform_condition(platform)
            if flags and condition:
                self._w(f"if({condition})")
                self._w(f"    target_link_options({m.name} PRIVATE {' '.join(flags)})")
                self._w("endif()")
        if m.name == "arp_rhi_webgpu":
            self._w('if(ARP_SELECTED_WEB_RHI STREQUAL "webgpu")')
            self._w("    target_compile_options(arp_rhi_webgpu PRIVATE --use-port=emdawnwebgpu)")
            # The emdawn port provides the C API symbols consumed by the
            # backend's native payloads. Propagate that final-link contract
            # to every executable that links this static backend, including
            # contract tests and browser fixtures.
            self._w("    # Original device-loss registrations use supported val ownership.")
            self._w("    target_link_options(arp_rhi_webgpu INTERFACE --use-port=emdawnwebgpu -lembind)")
            self._w("endif()")

        # Platform-specific deps are evaluated by the final CMake target, not
        # by the platform used to generate this file.
        for platform, deps in m.platform_deps.items():
            condition = self._cmake_module_platform_condition(m.name, platform)
            if deps and condition:
                self._w(f"if({condition})")
                self._w(f"    target_link_libraries({m.name} PUBLIC {' '.join(deps)})")
                self._w("endif()")

        # Platform-specific sources likewise follow the final target platform.
        for platform, sources in m.platform_srcs.items():
            condition = self._cmake_module_platform_condition(m.name, platform)
            existing_sources = [s for s in sources if exists(s)]
            if existing_sources and condition:
                self._w(f"if({condition})")
                self._w(f"    target_sources({m.name} PRIVATE {' '.join(existing_sources)})")
                self._w("endif()")

        if m.apple_frameworks:
            frameworks = " ".join(
                f'"-framework {framework}"' for framework in m.apple_frameworks)
            self._w("if(APPLE)")
            self._w(f"    target_link_libraries({m.name} PRIVATE {frameworks})")
            self._w("endif()")

        self._w()

    def _emit_executable(self, m: Any):
        bundle_arg = " MACOSX_BUNDLE" if m.type == "bundle" else ""
        self._w(f"add_executable({m.name}{bundle_arg}")
        for s in m.srcs:
            if exists(s):
                self._w(f"    {s}")
        self._w(")")

        inc_dirs = " ".join(m.include_dirs)
        if inc_dirs:
            self._w(f"target_include_directories({m.name} PRIVATE {inc_dirs})")

        if m.deps:
            deps_str = " ".join(m.deps)
            self._w(f"target_link_libraries({m.name} PRIVATE {deps_str})")

        if m.apple_frameworks:
            frameworks = " ".join(
                f'"-framework {framework}"' for framework in m.apple_frameworks)
            self._w("if(APPLE)")
            self._w(f"    target_link_libraries({m.name} PRIVATE {frameworks})")
            self._w("endif()")

        if m.type == "bundle":
            self._w("if(IOS)")
            if m.info_plist:
                self._w(f"    set_target_properties({m.name} PROPERTIES")
                self._w(f'        MACOSX_BUNDLE_INFO_PLIST "${{CMAKE_CURRENT_SOURCE_DIR}}/{m.info_plist}"')
                self._w("        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES")
                self._w("        XCODE_GENERATE_SCHEME TRUE")
                self._w("        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY \"1,2\"")
                self._w("        XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO")
                if m.bundle_identifier:
                    self._w('        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${ARP_IOS_BUNDLE_IDENTIFIER}"')
                self._w("        XCODE_ATTRIBUTE_SKIP_INSTALL NO")
                self._w("    )")
            for resource in m.resources:
                if exists(resource):
                    self._w(
                        f'    target_sources({m.name} PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}/{resource}")')
                    self._w(
                        f'    set_source_files_properties("${{CMAKE_CURRENT_SOURCE_DIR}}/{resource}" '
                        "PROPERTIES MACOSX_PACKAGE_LOCATION Resources)")
            self._w("endif()")

        for scope, defs in m.compile_defs.items():
            if defs:
                def_str = " ".join(f"{k}={v}" for k, v in defs.items())
                self._w(f"target_compile_definitions({m.name} {scope} {def_str})")

        # Source-inspection contracts must read the checkout they were built
        # from. Do not expose a host filesystem path to Emscripten: browser
        # targets use the virtual filesystem and explicitly preload only the
        # runtime fixtures they need.
        self._w('if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")')
        self._w(f'    target_compile_definitions({m.name} PRIVATE ARP_SOURCE_DIR="${{CMAKE_CURRENT_SOURCE_DIR}}")')
        self._w("endif()")

        if m.name in {"arp_scriptvm_cpp_plugin_tests", "arp_scriptvm_cpp_plugin_smoke"}:
            self._w('if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")')
            self._w(f"    add_dependencies({m.name} arp_engine arp_reflect_codegen)")
            self._w(f"    target_compile_definitions({m.name} PRIVATE")
            self._w('        ARP_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"')
            self._w('        ARP_TEST_REFLECTION_CODEGEN="$<TARGET_FILE:arp_reflect_codegen>"')
            self._w('        ARP_TEST_HOST_LIBRARY="$<TARGET_FILE:arp_engine>")')
            self._w("endif()")

        if m.name == "arp_rhi_webgpu_device_contract_tests":
            # The runtime contract loads this reflection artifact through
            # ShaderReflectionCache. A browser executable cannot access the
            # host checkout, so mount the fixture at the relative path used
            # by makeWebGpuShaderPackage().
            self._w('if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")')
            self._w(
                f'    target_link_options({m.name} PRIVATE '
                '"--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/'
                'webgpu_rhi.reflection.yaml@/webgpu_rhi.reflection.yaml")')
            self._w(
                f'    target_link_options({m.name} PRIVATE '
                '"--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/'
                'webgpu_rhi.wgsl@/webgpu_rhi.wgsl")')
            self._w(
                f'    target_link_options({m.name} PRIVATE '
                '"--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/'
                'webgpu_device_bootstrap_pre.js")')
            self._w("endif()")

        if m.link_flags:
            self._w(f"target_link_options({m.name} PRIVATE {' '.join(m.link_flags)})")
        if self.platform in m.platform_link_flags and m.platform_link_flags[self.platform]:
            self._w(f"target_link_options({m.name} PRIVATE {' '.join(m.platform_link_flags[self.platform])})")
        if m.name in SCRIPT_VM_WASM_TEST_TARGETS:
            self._w('if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")')
            self._w(
                f'    target_link_options({m.name} PRIVATE '
                '"-sALLOW_MEMORY_GROWTH=0" "-sINITIAL_MEMORY=536870912")')
            self._w("endif()")
        self._w()

    def _emit_engine_modules(self):
        """生成所有引擎模块（按拓扑排序）。"""
        # 收集所有 library 模块
        lib_modules = [m for m in self.modules.values()
                       if m.type == "library"]
        lib_names = [m.name for m in lib_modules]
        sorted_names = topo_sort(self.modules, set(lib_names))

        scriptvm_packages = {
            "arp_scriptvm_runtime",
            "arp_scriptvm_ir",
            "arp_scriptvm_frontend",
            "arp_scriptvm_interpreter",
            "arp_scriptvm_direct_native_runtime",
            "arp_scriptvm_aot_tooling",
            "arp_scriptvm_host_plugin_runtime",
            "arp_scriptvm_semantic_runtime_core_aot",
            "arp_scriptvm_semantic_runtime_aot_only",
        }
        scriptvm_packages_emitted = False
        for name in sorted_names:
            if name == "arp_engine":
                continue
            if name in scriptvm_packages:
                if not scriptvm_packages_emitted:
                    self._w("include(cmake/ScriptAotLibraries.cmake)")
                    self._w()
                    scriptvm_packages_emitted = True
                continue
            m = self.modules[name]
            self._emit_library(m)

    def _emit_reflect_codegen(self):
        """生成 reflect_codegen 工具 target（必须在 reflection codegen 之前）。"""
        if "arp_reflect_codegen" not in self.modules:
            return
        m = self.modules["arp_reflect_codegen"]
        self._w(f"add_executable({m.name}")
        for s in m.srcs:
            if exists(s):
                self._w(f"    {s}")
        self._w(")")
        # Static link to avoid MinGW runtime DLL dependency (0xc0000139 crash)
        self._w("if(MINGW)")
        self._w("    target_link_options(arp_reflect_codegen PRIVATE -static)")
        self._w("endif()")
        self._w()

    def _emit_reflection_codegen(self):
        """生成 reflection codegen 自定义命令。"""
        if "arp_resource" not in self.modules:
            return

        reflections = []
        # core_math
        if exists("src/core/math/math.h"):
            reflections.append(("core_math", "src/core/math/math.h"))
        # core_curve
        if exists("src/core/math/curve.h"):
            reflections.append(("core_curve", "src/core/math/curve.h"))
        # prefab_asset_resources
        if exists("src/resource/prefab_asset_resources.h"):
            reflections.append(("prefab_asset_resources", "src/resource/prefab_asset_resources.h"))
        # particle_asset_resources
        if exists("src/resource/particle_asset_resources.h"):
            reflections.append(("particle_asset_resources", "src/resource/particle_asset_resources.h"))

        if not reflections:
            return

        self._w("# Reflection codegen")
        for module, header in reflections:
            self._w(f"set(ARP_{module.upper()}_REFLECTION_HEADER")
            self._w(f"    \"${{CMAKE_CURRENT_SOURCE_DIR}}/{header}\")")
            self._w("arp_reflection_generate(")
            self._w("    TARGET arp_resource")
            self._w(f"    MODULE {module}")
            self._w(f"    OUT_JSON \"${{CMAKE_CURRENT_BINARY_DIR}}/generated/reflection/arp_resource/{module}.reflection.json\"")
            self._w(f"    OUT_CPP \"${{CMAKE_CURRENT_BINARY_DIR}}/generated/reflection/arp_resource/{module}.reflection.gen.cpp\"")
            self._w(f"    HEADERS \"${{ARP_{module.upper()}_REFLECTION_HEADER}}\"")
            self._w(")")

        self._w()

    def _emit_targets(self):
        """生成所有 executable（跳过已单独处理的反映代码生成）。"""
        exe_modules = [m for m in self.modules.values()
                       if m.type in {"executable", "bundle"}
                       and m.name != "arp_reflect_codegen"]
        for m in exe_modules:
            self._emit_executable(m)

    def _emit_engine_assembly(self):
        """将模块链接到 arp_engine DLL。"""
        if "arp_engine" not in self.modules:
            return

        engine_targets = []
        for m in self.modules.values():
            if m.type == "library" and m.name != "arp_engine":
                # Filter to only actual engine modules
                if any(m.name.startswith(p) for p in [
                    "arp_core", "arp_reflection", "arp_shader", "arp_animation",
                    "arp_terrain_scene_runtime",
                    "arp_assets", "arp_unity_import", "arp_resource",
                    "arp_scene", "arp_physics", "arp_3d", "arp_2d", "arp_input", "arp_audio",
                    "arp_ui_font", "arp_ui_runtime", "arp_gui",
                    "arp_render_graph", "arp_render_graph_rhi_plan",
                    "arp_renderer", "arp_renderer_rhi_contracts",
                    "arp_renderer_deployment", "arp_platform",
                    "arp_rhi", "arp_rhi_semantics",
                    "arp_rhi_vulkan", "arp_rhi_metal", "arp_rhi_webgpu",
                    "arp_network", "arp_pcg",
                ]):
                    engine_targets.append(m.name)

        self._w("if(ARP_SCRIPT_AOT_ONLY_RELEASE)")
        self._w("    set(ARP_SCRIPT_RUNTIME_TARGETS")
        self._w("        arp_scriptvm_host_plugin_runtime")
        self._w("        arp_scriptvm_semantic_runtime_core_aot)")
        self._w("else()")
        self._w("    set(ARP_SCRIPT_RUNTIME_TARGETS")
        self._w("        arp_scriptvm_runtime")
        self._w("        arp_scriptvm_ir")
        self._w("        arp_scriptvm_frontend")
        self._w("        arp_scriptvm_interpreter")
        self._w("        arp_scriptvm_aot_tooling)")
        self._w("endif()")
        self._w("set(ARP_ENGINE_MODULE_TARGETS")
        for t in engine_targets:
            self._w(f"    {t}")
        self._w("    ${ARP_SCRIPT_RUNTIME_TARGETS}")
        self._w(")")

        self._w("foreach(moduleTarget IN LISTS ARP_ENGINE_MODULE_TARGETS)")
        self._w("    target_compile_definitions(${moduleTarget}")
        self._w("        PRIVATE ARP_BUILDING_DLL=1")
        self._w("        INTERFACE ARP_STATIC_LINK=1")
        self._w("    )")
        self._w("endforeach()")
        self._w()

        self._w("if(MINGW)")
        self._w("    target_link_libraries(arp_engine PRIVATE")
        self._w("        \"-Wl,--whole-archive\"")
        self._w("        ${ARP_ENGINE_MODULE_TARGETS}")
        self._w("        \"-Wl,--no-whole-archive\"")
        self._w("    )")
        self._w("else()")
        self._w("    target_link_libraries(arp_engine PRIVATE ${ARP_ENGINE_MODULE_TARGETS})")
        self._w("endif()")
        self._w("include(cmake/ArpEnginePluginAbi.cmake)")
        self._w()

    def _emit_sdl3_link(self):
        self._w("if(ARP_HAS_SDL3_DEVEL AND NOT CMAKE_SYSTEM_NAME STREQUAL \"Emscripten\")")
        self._w("    target_link_libraries(arp_engine PUBLIC arp_sdl3)")
        self._w("    if(TARGET arp_platform)")
        self._w("        target_link_libraries(arp_platform PUBLIC arp_sdl3)")
        self._w("    endif()")
        self._w("else()")
        self._w("    target_compile_definitions(arp_engine PUBLIC ARP_NO_SDL3=1)")
        self._w("    if(TARGET arp_platform)")
        self._w("        target_compile_definitions(arp_platform PUBLIC ARP_NO_SDL3=1)")
        self._w("    endif()")
        self._w("endif()")
        self._w("if(ARP_HAS_VULKAN_HEADERS AND NOT CMAKE_SYSTEM_NAME STREQUAL \"Emscripten\")")
        self._w("    target_link_libraries(arp_engine PUBLIC arp_vulkan_headers)")
        self._w("endif()")
        self._w()

    def _emit_rhi_linkage(self):
        """连接 RHI backend 到 platform。"""
        self._w('if(ARP_SELECTED_WEB_RHI STREQUAL "webgpu")')
        self._w("    target_link_libraries(arp_platform PUBLIC arp_rhi_webgpu)")
        self._w("elseif(APPLE)")
        self._w("    target_link_libraries(arp_platform PUBLIC arp_rhi_metal)")
        self._w('elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")')
        self._w("    target_link_libraries(arp_platform PUBLIC arp_rhi_vulkan)")
        self._w("endif()")
        self._w()
        # Vulkan loader library
        self._w("if(TARGET arp_rhi_vulkan AND NOT APPLE)")
        self._w("    if(ARP_HAS_VULKAN_HEADERS AND NOT CMAKE_SYSTEM_NAME STREQUAL \"Emscripten\")")
        self._w("        target_link_libraries(arp_rhi_vulkan PUBLIC")
        self._w("            \"${ARP_VULKAN_SDK}/Lib/vulkan-1.lib\")")
        self._w("    else()")
        self._w("        target_compile_definitions(arp_rhi_vulkan PUBLIC ARP_NO_VULKAN_HEADERS=1)")
        self._w("    endif()")
        self._w()
        # VMA
        self._w("    if(TARGET arp_vma_headers)")
        self._w("        target_link_libraries(arp_rhi_vulkan PRIVATE arp_vma_headers)")
        self._w("    else()")
        self._w("        target_compile_definitions(arp_rhi_vulkan PRIVATE ARP_RHI_NO_VMA=1)")
        self._w("    endif()")
        self._w("endif()")
        self._w()

    def _emit_shader_compilation(self):
        """生成 shader 编译 target。"""
        # 扫描实际存在的 shader 源文件
        # Note: wocc-specific shaders (sky/post/ddgi/depth_prepass/pbr_scene/...)
        # live under shaders/wocc/ in the engine repo after the engine/wocc
        # split. They are kept here as test fixtures for arp_shader_tests.
        shader_files: Dict[str, str] = {}
        for _module_name, prefix, source_rel, _entries in SHADER_SOURCES:
            source_path = ROOT / "shaders" / source_rel
            if not source_path.exists():
                raise FileNotFoundError(
                    f"shader manifest source is missing: {source_path}"
                )
            if prefix in shader_files:
                raise ValueError(
                    f"shader manifest contains duplicate output prefix: {prefix}"
                )
            shader_files[prefix] = f"shaders/{source_rel}"

        if not shader_files:
            return

        # Only pilot shaders (those with a codegen struct name registered in
        # shader_bindings_codegen.PILOT_STRUCT_NAMES) actually emit a
        # .shader_bindings.h file. Declaring it in OUTPUT/DEPENDS for other
        # shaders would make CMake expect a file that compile_shaders.py never
        # writes, breaking incremental rebuilds. Keep this set in sync with
        # scripts/shader_bindings_codegen.py:PILOT_STRUCT_NAMES.
        codegen_pilot_prefixes = set(PILOT_STRUCT_NAMES)

        def _shader_output_exts(out_name):
            exts = [".spv", ".metal", ".wgsl", ".reflection.yaml"]
            if out_name in codegen_pilot_prefixes:
                exts.append(".shader_bindings.h")
            return exts

        self._w("if(SLANGC_EXECUTABLE)")
        self._w("    add_custom_command(")
        self._w("        OUTPUT")
        for out_name in sorted(shader_files):
            for ext in _shader_output_exts(out_name):
                self._w(f"            \"${{CMAKE_BINARY_DIR}}/shaders/{out_name}{ext}\"")
        # NOTE: use CMAKE_CURRENT_SOURCE_DIR (not CMAKE_SOURCE_DIR) so the
        # engine tree resolves correctly when consumed as a subproject (e.g.
        # via add_subdirectory by the wocc repo). CMAKE_SOURCE_DIR would point
        # at the consumer's root, breaking script/shader path resolution.
        self._w("        COMMAND \"${Python3_EXECUTABLE}\""
                " \"${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py\"")
        self._w("            --slangc \"${SLANGC_EXECUTABLE}\"")
        self._w("            --vulkan-sdk \"${ARP_VULKAN_SDK}\"")
        self._w("            --out-dir \"${CMAKE_BINARY_DIR}/shaders\"")
        self._w("        DEPENDS")
        self._w("            \"${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py\"")
        self._w("            \"${CMAKE_CURRENT_SOURCE_DIR}/scripts/shader_bindings_codegen.py\"")
        for out_name in sorted(shader_files):
            self._w(f"            \"${{CMAKE_CURRENT_SOURCE_DIR}}/{shader_files[out_name]}\"")
        for source_rel in SHADER_SHARED_SOURCES:
            self._w(f"            \"${{CMAKE_CURRENT_SOURCE_DIR}}/shaders/{source_rel}\"")
        self._w("        VERBATIM")
        self._w("    )")
        self._w("    add_custom_target(arp_compile_shaders DEPENDS")
        for out_name in sorted(shader_files):
            for ext in _shader_output_exts(out_name):
                self._w(f"        \"${{CMAKE_BINARY_DIR}}/shaders/{out_name}{ext}\"")
        self._w("    )")
        self._w()

        # 链接 shader 依赖到相关 target
        shader_targets = []
        for tname in ["arp_shader_tests", "arp_shader"]:
            if tname in self.modules:
                shader_targets.append(tname)

        if shader_targets:
            for tname in shader_targets:
                self._w(f"    if(TARGET {tname})")
                self._w(f"        add_dependencies({tname} arp_compile_shaders)")
                self._w("    endif()")

        self._w("endif()  # SLANGC_EXECUTABLE")
        if "arp_ios_smoke_app" in self.modules:
            self._w("if(TARGET arp_ios_smoke_app AND TARGET arp_compile_shaders)")
            self._w("    add_dependencies(arp_ios_smoke_app arp_compile_shaders)")
            self._w("    add_custom_command(TARGET arp_ios_smoke_app POST_BUILD")
            self._w("        COMMAND \"${CMAKE_COMMAND}\" -E copy_directory")
            self._w("            \"${CMAKE_BINARY_DIR}/shaders\"")
            self._w("            \"$<TARGET_BUNDLE_DIR:arp_ios_smoke_app>/shaders\"")
            self._w("        VERBATIM")
            self._w("    )")
            self._w("elseif(TARGET arp_ios_smoke_app)")
            self._w("    message(FATAL_ERROR \"arp_ios_smoke_app requires an explicit SLANGC_EXECUTABLE\")")
            self._w("endif()")
        self._w()

    def _emit_sdl3_postbuild(self):
        """SDL3.dll post-build copy。"""
        targets = ["arp_engine"]
        if not targets:
            return
        self._w("if(ARP_HAS_SDL3_DEVEL AND NOT APPLE AND EXISTS \"${ARP_SDL3_DLL}\")")
        self._w("    set(_sdl3_targets")
        for t in targets:
            self._w(f"        {t}")
        self._w("    )")
        self._w("    foreach(target ${_sdl3_targets})")
        self._w("        if(TARGET ${target})")
        self._w("            add_custom_command(TARGET ${target} POST_BUILD")
        self._w("                COMMAND \"${CMAKE_COMMAND}\" -E copy_if_different")
        self._w("                    \"${ARP_SDL3_DLL}\"")
        self._w("                    \"$<TARGET_FILE_DIR:${target}>/SDL3.dll\"")
        self._w("                VERBATIM")
        self._w("            )")
        self._w("        endif()")
        self._w("    endforeach()")
        self._w("endif()")
        self._w()

    def _emit_enable_testing(self):
        """生成 enable_testing() + add_test() 注册。"""
        test_exes = [m for m in self.modules.values()
                     if m.type == "executable" and "_tests" in m.name]
        if not test_exes:
            return

        self._w('if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")')
        self._w("    get_property(_arp_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)")
        self._w("    foreach(_arp_target IN LISTS _arp_directory_targets)")
        self._w('        if(_arp_target MATCHES "(_tests|_smoke)$")')
        self._w("            target_compile_options(${_arp_target} PRIVATE -UNDEBUG)")
        self._w("        endif()")
        self._w("    endforeach()")
        self._w("endif()")
        self._w()

        # Emscripten executables are browser artifacts. The ScriptVM unit
        # artifacts are explicitly safe to run under Node because they do not
        # require window/WebGPU; other browser targets remain covered by the
        # dedicated CDP runner instead of desktop CTest.
        self._w('if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")')
        self._w("    enable_testing()")
        for m in sorted(test_exes, key=lambda m: m.name):
            self._w(f"    add_test(NAME {m.name} COMMAND {m.name})")
        if "arp_scriptvm_stress_benchmark" in self.modules:
            self._w("    add_test(NAME arp_scriptvm_stress_differential")
            self._w("        COMMAND \"${Python3_EXECUTABLE}\"")
            self._w("            \"${CMAKE_CURRENT_SOURCE_DIR}/tests/bench/scriptvm_stress_benchmark.py\"")
            self._w("            \"$<TARGET_FILE:arp_scriptvm_stress_benchmark>\" --quick)")
        self._w("else()")
        self._w("    find_program(ARP_NODE_EXECUTABLE node)")
        self._w("    if(ARP_NODE_EXECUTABLE)")
        self._w("        enable_testing()")
        wasm_tests = [
            "arp_scriptvm_typed_ir_tests",
            "arp_scriptvm_semantic_runtime_tests",
            "arp_scriptvm_aot_v5_scene_behavior_smoke",
        ]
        for name in wasm_tests:
            if name in self.modules:
                self._w(f"        add_test(NAME {name}_wasm")
                self._w(f"            COMMAND \"${{ARP_NODE_EXECUTABLE}}\" \"$<TARGET_FILE:{name}>\")")
        self._w("    else()")
        self._w("        message(WARNING \"Node.js was not found; Emscripten ScriptVM CTest entries are unavailable\")")
        self._w("    endif()")
        self._w("endif()")
        self._w()

    def _emit_android_package_contract(self):
        if not exists("scripts/verify_scriptvm_android_package.py"):
            return
        required = {"arp_android_platform_contract_tests"}
        if not required.issubset(self.modules):
            return
        self._w("if(ANDROID)")
        self._w('    get_filename_component(ARP_ANDROID_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)')
        self._w('    find_program(ARP_ANDROID_READELF NAMES llvm-readelf llvm-readelf.exe')
        self._w('        HINTS "${ARP_ANDROID_LLVM_BIN}" REQUIRED)')
        self._w("    add_custom_target(arp_scriptvm_android_package_contract")
        self._w('        COMMAND "${Python3_EXECUTABLE}"')
        self._w('            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_scriptvm_android_package.py"')
        self._w('            --artifact-dir "${CMAKE_BINARY_DIR}"')
        self._w('            --readelf "${ARP_ANDROID_READELF}"')
        for target in sorted(required):
            self._w(f'            --artifact "{target}"')
        self._w('            --output "${CMAKE_BINARY_DIR}/scriptvm-android-package-contract.json"')
        self._w("        DEPENDS")
        for target in sorted(required):
            self._w(f"            {target}")
        self._w("    )")
        self._w("endif()")
        self._w()

    def generate(self, output_path: str):
        self._emit_header()
        self._emit_platform_specific()
        self._emit_std_flags()
        self._emit_engine_dll()
        self._emit_engine_modules()
        self._emit_reflect_codegen()       # tool binary must exist before codegen calls
        self._emit_reflection_codegen()
        self._emit_targets()
        self._w("include(cmake/ScriptAotTargets.cmake)")
        self._w()
        self._emit_engine_assembly()
        self._emit_sdl3_link()
        self._emit_rhi_linkage()
        self._emit_shader_compilation()
        self._emit_sdl3_postbuild()
        self._emit_enable_testing()
        self._w("include(cmake/ScriptAotTestRegistrations.cmake)")
        self._w()
        self._emit_android_package_contract()
        self._w("include(cmake/ArpEditor.cmake)")
        self._w("include(cmake/ArpHttpServer.cmake)")
        self._w("include(cmake/ArpVolumetricFog.cmake)")
        self._w("include(cmake/ArpVolumetricCloud.cmake)")
        self._w("include(cmake/ArpOriginalTextureUpload.cmake)")
        self._w("include(cmake/ArpVulkanCommandRetention.cmake)")
        self._w("include(cmake/ArpQueryPassTiming.cmake)")
        self._w("include(cmake/ArpMetalDispatchProbe.cmake)")
        self._w("include(cmake/ArpOriginalSubmissionContracts.cmake)")
        self._w()

        content = "\n".join(self.lines) + "\n"
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"  Generated: {output_path} ({len(self.lines)} lines)")


# ============================================================
# CMakePresets.json 生成器
# ============================================================

def _generate_single_cmake_presets(output_path: str, config: str,
                                   platform: str, backend: str,
                                   compiler_gcc: str, compiler_gxx: str,
                                   ninja: str, vulkan_sdk: str, slangc: str):
    """生成 CMakePresets.json。

    platform/backend 作为显式参数传入，不再依赖全局变量。
    """
    backend = BuildBackendSelection.resolve(platform, backend)
    preset_name = BuildPresetSelection.resolve(platform, backend, config)
    document = CMakePresetDocument.load(output_path)
    generator = "Ninja"
    binary_dir = f"build/{preset_name}"

    cache_vars = {
        "CMAKE_BUILD_TYPE": config.capitalize(),
        "CMAKE_C_COMPILER": compiler_gcc,
        "CMAKE_CXX_COMPILER": compiler_gxx,
        "CMAKE_MAKE_PROGRAM": ninja,
    }
    toolchain_file = None
    if vulkan_sdk:
        cache_vars["VULKAN_SDK"] = vulkan_sdk
    if slangc:
        cache_vars["SLANGC_EXECUTABLE"] = slangc

    # Platform-specific overrides
    if platform == "android":
        cache_vars["CMAKE_SYSTEM_NAME"] = "Android"
        cache_vars["CMAKE_ANDROID_ARCH_ABI"] = "arm64-v8a"
        cache_vars["CMAKE_ANDROID_STL_TYPE"] = "c++_static"
        if "ANDROID_NDK" in os.environ:
            toolchain_file = (
                "$env{ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    elif platform == "mac":
        cache_vars["CMAKE_SYSTEM_NAME"] = "Darwin"
        cache_vars["CMAKE_OSX_ARCHITECTURES"] = "arm64;x86_64"
    elif platform == "ios":
        generator = "Xcode"
        cache_vars.pop("CMAKE_MAKE_PROGRAM", None)
        cache_vars["CMAKE_SYSTEM_NAME"] = "iOS"
        cache_vars["CMAKE_OSX_ARCHITECTURES"] = "arm64"
        cache_vars["CMAKE_OSX_SYSROOT"] = "iphoneos"
    elif platform == "web":
        emscripten_root = os.environ.get("EMSDK", "C:/emsdk").replace("\\", "/")
        emscripten_bin = f"{emscripten_root}/upstream/emscripten"
        toolchain_file = (
            f"{emscripten_bin}/cmake/Modules/Platform/Emscripten.cmake")
        cache_vars["CMAKE_C_COMPILER"] = f"{emscripten_bin}/emcc.exe"
        cache_vars["CMAKE_CXX_COMPILER"] = f"{emscripten_bin}/em++.exe"

    presets = {
        "version": 6,
        "configurePresets": [{
            "name": preset_name,
            "displayName": f"{platform.title()} {backend.upper()} {config.title()}",
            "generator": generator,
            "binaryDir": f"${{sourceDir}}/{binary_dir}",
            "cacheVariables": cache_vars,
        }],
        "buildPresets": [{
            "name": preset_name,
            "configurePreset": preset_name,
        }],
        "testPresets": [{
            "name": preset_name,
            "configurePreset": preset_name,
            "output": {"outputOnFailure": True},
        }],
    }
    if toolchain_file:
        presets["configurePresets"][0]["toolchainFile"] = toolchain_file
    if platform == "ios":
        for category in ("buildPresets", "testPresets"):
            presets[category][0]["configuration"] = BuildPresetSelection.CONFIGURATIONS[config]

    document.write_managed(presets)
    print(f"  Generated: {output_path}")


def generate_cmake_presets(output_path: str, config: str,
                           platform: str, backend: str,
                           compiler_gcc: str, compiler_gxx: str,
                           ninja: str, vulkan_sdk: str, slangc: str):
    """Generate stable platform presets without dropping the matrix.

    The selected configure command may refresh compiler discovery, but it must
    never remove another supported AOT validation preset.
    """
    backend = BuildBackendSelection.resolve(platform, backend)
    BuildPresetSelection.resolve(platform, backend, config)
    document = CMakePresetDocument.load(output_path)
    presets, managed_names = plan_cmake_presets(config, platform, backend,
        compiler_gcc, compiler_gxx, ninja, vulkan_sdk, slangc)
    document.write_managed(presets, managed_names)
    print(f"  Generated: {output_path}")


def plan_cmake_presets(config: str, platform: str, backend: str,
                       compiler_gcc: str, compiler_gxx: str,
                       ninja: str, vulkan_sdk: str, slangc: str):
    """Plan the same registered matrix without publishing files."""
    backend = BuildBackendSelection.resolve(platform, backend)
    BuildPresetSelection.resolve(platform, backend, config)

    mingw_root = os.environ.get("ARP_MINGW_ROOT", "C:/mingw64").replace("\\", "/")
    win_gcc = compiler_gcc if platform == "win" else f"{mingw_root}/bin/gcc.exe"
    win_gxx = compiler_gxx if platform == "win" else f"{mingw_root}/bin/g++.exe"
    host_ninja = ninja if ninja else f"{mingw_root}/bin/ninja.exe"
    sdk = vulkan_sdk or os.environ.get("VULKAN_SDK", "")
    if not sdk:
        sdk_root = Path("C:/VulkanSDK")
        candidates = []
        if sdk_root.is_dir():
            for candidate in sdk_root.iterdir():
                try:
                    version = tuple(int(part) for part in candidate.name.split("."))
                except ValueError:
                    continue
                if (candidate / "Include/vulkan/vulkan.h").is_file():
                    candidates.append((version, candidate))
        if candidates:
            sdk = str(max(candidates, key=lambda item: item[0])[1])
    sdk = sdk.replace("\\", "/")
    host_slangc = slangc or (f"{sdk}/Bin/slangc.exe" if sdk else "slangc")

    emsdk_root = os.environ.get("EMSDK", "C:/emsdk").replace("\\", "/")
    emscripten_bin = f"{emsdk_root}/upstream/emscripten"
    web_gcc = compiler_gcc if platform == "web" else f"{emscripten_bin}/emcc.exe"
    web_gxx = compiler_gxx if platform == "web" else f"{emscripten_bin}/em++.exe"
    web_toolchain = f"{emscripten_bin}/cmake/Modules/Platform/Emscripten.cmake"
    emsdk_python = os.environ.get(
        "EMSDK_PYTHON", f"{emsdk_root}/python/3.13.3_64bit/python.exe"
    ).replace("\\", "/")
    emsdk_python_dir = str(Path(emsdk_python).parent).replace("\\", "/")

    managed_names = {
        f"{target_platform}-{target_backend}-{target_config}"
        for target_platform, target_backend in (("win", "vulkan"), ("web", "webgpu"))
        for target_config in ("debug", "release")
    }
    # These names are part of the documented platform matrix and are also
    # consumed by the platform contract tests. Keep them in the generated
    # file even when configure is invoked for another platform.
    managed_names.update({
        "macos-metal",
        "ios-metal",
        "linux-vulkan",
        "android-arm64-vulkan",
        "mac-metal-debug",
        "mac-metal-release",
    })
    managed_names.update(BuildPresetSelection.resolve(target, 'auto', variant)
                         for target in BuildPresetSelection.STEMS
                         for variant in BuildPresetSelection.CONFIGURATIONS)
    configure_presets = []
    build_presets = []
    test_presets = []

    for target_platform, target_backend in (("win", "vulkan"), ("web", "webgpu")):
        for target_config in ("debug", "release"):
            name = f"{target_platform}-{target_backend}-{target_config}"
            cache_vars = {
                "CMAKE_BUILD_TYPE": target_config.capitalize(),
                "CMAKE_C_COMPILER": win_gcc if target_platform == "win" else web_gcc,
                "CMAKE_CXX_COMPILER": win_gxx if target_platform == "win" else web_gxx,
                "CMAKE_MAKE_PROGRAM": host_ninja,
            }
            if target_platform == "win":
                if sdk:
                    cache_vars["ARP_VULKAN_SDK"] = sdk
                cache_vars["SLANGC_EXECUTABLE"] = host_slangc
            else:
                cache_vars["Python3_EXECUTABLE"] = emsdk_python
                cache_vars["ARP_SCRIPT_AOTC_HOST"] = (
                    "${sourceDir}/build/win-vulkan-release/arp_script_aotc.exe"
                )

            configure = {
                "name": name,
                "displayName": (
                    f"{target_platform.title()} {target_backend.upper()} "
                    f"{target_config.title()}"),
                "generator": "Ninja",
                "binaryDir": f"${{sourceDir}}/build/{name}",
                "cacheVariables": cache_vars,
            }
            if target_platform == "web":
                configure["toolchainFile"] = web_toolchain
                configure["environment"] = {
                    "EMSDK": emsdk_root,
                    "EMSDK_PYTHON": emsdk_python,
                    "EM_BINARYEN_ROOT": f"{emsdk_root}/upstream",
                    "EM_LLVM_ROOT": f"{emsdk_root}/upstream/bin",
                    "PATH": (
                        f"{emsdk_python_dir};{emsdk_root}/upstream/bin;"
                        "$penv{PATH}"
                    ),
                }
            configure_presets.append(configure)
            build_preset = {"name": name, "configurePreset": name}
            test_preset = {
                "name": name,
                "configurePreset": name,
                "output": {"outputOnFailure": True},
            }
            if target_platform == "web":
                preset_environment = {
                    "EMSDK": emsdk_root,
                    "EMSDK_PYTHON": emsdk_python,
                    "EM_BINARYEN_ROOT": f"{emsdk_root}/upstream",
                    "EM_LLVM_ROOT": f"{emsdk_root}/upstream/bin",
                    "PATH": (
                        f"{emsdk_python_dir};{emsdk_root}/upstream/bin;"
                        "$penv{PATH}"
                    ),
                }
                build_preset["environment"] = preset_environment
                test_preset["environment"] = preset_environment
            build_presets.append(build_preset)
            test_presets.append(test_preset)

    def append_matrix_preset(name, display_name, generator, binary_dir,
                             cache_vars, toolchain_file=None):
        configure = {
            "name": name,
            "displayName": display_name,
            "generator": generator,
            "binaryDir": f"${{sourceDir}}/{binary_dir}",
            "cacheVariables": cache_vars,
        }
        if toolchain_file:
            configure["toolchainFile"] = toolchain_file
        configure_presets.append(configure)
        build_presets.append({"name": name, "configurePreset": name})
        test_presets.append({
            "name": name,
            "configurePreset": name,
            "output": {"outputOnFailure": True},
        })

    # Apple desktop is the native Metal build used by the macOS validation
    # pipeline. The short debug/release aliases are used by build.py, while
    # macos-metal is the stable matrix name documented for direct use.
    mac_cache = {
        "CMAKE_BUILD_TYPE": config.capitalize(),
        "CMAKE_C_COMPILER": compiler_gcc if platform == "mac" else "clang",
        "CMAKE_CXX_COMPILER": compiler_gxx if platform == "mac" else "clang++",
        "CMAKE_OSX_ARCHITECTURES": "arm64",
    }
    append_matrix_preset(
        "macos-metal", "MacOS METAL", "Ninja", "build/macos-metal",
        dict(mac_cache, CMAKE_BUILD_TYPE="Debug"))
    append_matrix_preset(
        "mac-metal-debug", "Mac METAL Debug", "Ninja",
        "build/mac-metal-debug", dict(mac_cache, CMAKE_BUILD_TYPE="Debug"))
    append_matrix_preset(
        "mac-metal-release", "Mac METAL Release", "Ninja",
        "build/mac-metal-release", dict(mac_cache, CMAKE_BUILD_TYPE="Release"))

    # Keep the remaining documented matrix entries discoverable. They are
    # intentionally explicit about their target platform and require the
    # corresponding SDK/toolchain when selected.
    append_matrix_preset(
        "ios-metal", "IOS METAL", "Xcode", "build/ios-metal",
        {
            "CMAKE_BUILD_TYPE": "Debug",
            "CMAKE_C_COMPILER": "clang",
            "CMAKE_CXX_COMPILER": "clang++",
            "CMAKE_SYSTEM_NAME": "iOS",
            "CMAKE_OSX_SYSROOT": "iphoneos",
            "CMAKE_OSX_ARCHITECTURES": "arm64",
        })
    append_matrix_preset(
        "linux-vulkan", "Linux VULKAN", "Ninja", "build/linux-vulkan",
        {
            "CMAKE_BUILD_TYPE": "Debug",
            "CMAKE_C_COMPILER": "gcc",
            "CMAKE_CXX_COMPILER": "g++",
        })
    append_matrix_preset(
        "android-arm64-vulkan", "Android VULKAN", "Ninja",
        "build/android-arm64-vulkan",
        {
            "CMAKE_BUILD_TYPE": "Debug",
            "CMAKE_C_COMPILER": "clang",
            "CMAKE_CXX_COMPILER": "clang++",
            "CMAKE_SYSTEM_NAME": "Android",
            "CMAKE_ANDROID_ARCH_ABI": "arm64-v8a",
            "CMAKE_ANDROID_STL_TYPE": "c++_static",
        })

    # Preserve the stable matrix aliases and add isolated configuration
    # products, using the exact same identity selected by the root CLI.
    for target in ('ios', 'linux', 'android'):
        base_name = BuildPresetSelection.STEMS[target]
        base = next(preset for preset in configure_presets if preset['name'] == base_name)
        for variant, configuration in BuildPresetSelection.CONFIGURATIONS.items():
            name = BuildPresetSelection.resolve(target, 'auto', variant)
            append_matrix_preset(name, base['displayName'] + ' ' + configuration,
                base['generator'], 'build/' + name,
                dict(base['cacheVariables'], CMAKE_BUILD_TYPE=configuration))
            if base['generator'] == 'Xcode':
                build_presets[-1]['configuration'] = configuration
                test_presets[-1]['configuration'] = configuration

    presets = {
        "version": CMakePresetDocument.SCHEMA_VERSION,
        "configurePresets": configure_presets,
        "buildPresets": build_presets,
        "testPresets": test_presets,
    }
    return presets, managed_names
