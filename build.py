#!/usr/bin/env python3
"""
arp-engine 统一构建脚本 (v2)

自动发现源文件，根据不同平台/后端/功能需求生成 CMakeLists.txt 并构建。

用法:
    python build.py configure [--platform=win] [--backend=vulkan] [--config=debug]
    python build.py build [--target=...]
    python build.py test [--filter=...]
    python build.py list-modules
    python build.py list-targets

平台:  win (默认), mac, linux, android, ios, web
后端:  vulkan (win/linux/android 默认), metal (mac/ios 默认), webgpu (web 默认)
配置:  debug (默认), release；package 尚无已登记构建契约
"""

import argparse
import json
import os
import subprocess
import sys
import glob as glob_mod
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# CMake 工程文件生成逻辑（CmakeWriter / generate_cmake_presets / topo_sort / exists）
# 已拆分到 scripts/cmake_gen.py，本文件单向 import。
sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))
from cmake_gen import (
    BuildBackendSelection,
    BuildBackendSelectionError,
    BuildPresetSelection,
    CMakePresetDocument,
    CMakePresetDocumentError,
    CmakeWriter,
    generate_cmake_presets,
    plan_cmake_presets,
    topo_sort,
    exists as _exists,
)
from cmake_build_cache import CMakeBuildCacheError, CMakeBuildIntent, CMakeCacheDocument


# ============================================================
# 项目根目录
# ============================================================
ROOT = Path(__file__).resolve().parent


# ============================================================
# 模块定义（基于实际文件系统自动发现）
# ============================================================

class ModuleDef:
    """描述一个可编译模块（静态库/共享库/可执行文件）。"""
    def __init__(self, name: str, type_: str = "library",
                 srcs: List[str] = None, deps: List[str] = None,
                 include_dirs: List[str] = None,
                 compile_defs: Dict[str, str] = None,
                 link_flags: List[str] = None,
                 platform_srcs: Dict[str, List[str]] = None,
                 platform_deps: Dict[str, List[str]] = None,
                 platform_compile_defs: Dict[str, Dict[str, str]] = None,
                 platform_link_flags: Dict[str, List[str]] = None,
                 apple_frameworks: List[str] = None,
                 apple_mm_srcs: List[str] = None,
                 bundle_identifier: Optional[str] = None,
                 info_plist: Optional[str] = None,
                 resources: List[str] = None,
                 condition: Optional[str] = None,
                 output_name: Optional[str] = None,
                 static_on_web: bool = False):
        self.name = name
        self.type = type_  # "library", "shared", "executable"
        self.srcs = srcs or []
        self.deps = deps or []
        self.include_dirs = include_dirs or []
        self.compile_defs = compile_defs or {}
        self.link_flags = link_flags or []
        # 平台特定
        self.platform_srcs = platform_srcs or {}
        self.platform_deps = platform_deps or {}
        self.platform_compile_defs = platform_compile_defs or {}
        self.platform_link_flags = platform_link_flags or {}
        # Apple 专用
        self.apple_frameworks = apple_frameworks or []
        self.apple_mm_srcs = apple_mm_srcs or []
        self.bundle_identifier = bundle_identifier
        self.info_plist = info_plist
        self.resources = resources or []
        # 条件编译
        self.condition = condition
        self.output_name = output_name
        self.static_on_web = static_on_web


# ============================================================
# 自动发现源文件（只包含实际存在的文件）
# ============================================================

def _find_srcs(directory: str, patterns: List[str] = None) -> List[str]:
    """在目录下查找所有 .cpp 文件，只返回相对路径。"""
    if patterns is None:
        patterns = ["*.cpp", "*.cc", "*.c"]
    d = ROOT / directory
    if not d.exists():
        return []
    result = []
    for pat in patterns:
        for f in d.rglob(pat):
            rel = str(f.relative_to(ROOT)).replace("\\", "/")
            result.append(rel)
    return sorted(result)


def _find_mm_srcs(directory: str) -> List[str]:
    """查找 Objective-C++ 文件。"""
    d = ROOT / directory
    if not d.exists():
        return []
    result = []
    for f in d.rglob("*.mm"):
        result.append(str(f.relative_to(ROOT)).replace("\\", "/"))
    return sorted(result)


def _auto_discover_modules() -> Dict[str, ModuleDef]:
    """
    基于文件系统自动发现所有模块。
    返回模块名 -> ModuleDef 的字典。
    """
    modules: Dict[str, ModuleDef] = {}

    # ============================================================
    # 核心库
    # ============================================================
    core_srcs = _find_srcs("src/core")
    if core_srcs:
        modules["arp_core"] = ModuleDef(
            "arp_core", "library",
            srcs=core_srcs,
            include_dirs=["src"],
            deps=["arp_mimalloc"],
        )

    plugin_build_srcs = [
        "src/scriptvm/plugin/compiler_toolchain_layout.cpp",
        "src/scriptvm/plugin/immutable_build_input_snapshot.cpp",
    ]
    if all(_exists(path) for path in plugin_build_srcs):
        modules["arp_scriptvm_plugin_build"] = ModuleDef(
            "arp_scriptvm_plugin_build", "library", srcs=plugin_build_srcs,
            include_dirs=["src"], deps=["arp_core"])

    # ============================================================
    # 反射
    # ============================================================
    ref_srcs = _find_srcs("src/reflection")
    if ref_srcs:
        modules["arp_reflection"] = ModuleDef(
            "arp_reflection", "library",
            srcs=ref_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_fkyaml_headers"],
        )

    # ============================================================
    # 脚本 VM (ArpScript)
    # ============================================================
    scriptvm_runtime_srcs = _find_srcs("src/scriptvm/runtime") + [
        "src/scriptvm/plugin/cpp_plugin_importer.cpp",
        "src/scriptvm/plugin/plugin_services.cpp",
        "src/scriptvm/plugin/plugin_registrar.cpp",
        "src/scriptvm/plugin/static_plugin_catalog.cpp",
        "src/scriptvm/plugin/plugin_runtime_scope.cpp",
    ]
    if scriptvm_runtime_srcs:
        modules["arp_scriptvm_runtime"] = ModuleDef(
            "arp_scriptvm_runtime", "library",
            srcs=scriptvm_runtime_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_reflection", "arp_scriptvm_plugin_build"],
        )
    scriptvm_ir_srcs = _find_srcs("src/scriptvm/ir")
    if scriptvm_ir_srcs:
        modules["arp_scriptvm_ir"] = ModuleDef(
            "arp_scriptvm_ir", "library",
            srcs=scriptvm_ir_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_reflection"],
        )
    scriptvm_frontend_srcs = _find_srcs("src/scriptvm/compile")
    if scriptvm_frontend_srcs:
        modules["arp_scriptvm_frontend"] = ModuleDef(
            "arp_scriptvm_frontend", "library",
            srcs=scriptvm_frontend_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_reflection", "arp_scriptvm_ir"],
        )
    scriptvm_interpreter_srcs = _find_srcs("src/scriptvm/interpreter")
    if scriptvm_interpreter_srcs:
        modules["arp_scriptvm_interpreter"] = ModuleDef(
            "arp_scriptvm_interpreter", "library",
            srcs=scriptvm_interpreter_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_reflection", "arp_scriptvm_runtime",
                  "arp_scriptvm_ir", "arp_scriptvm_frontend"],
        )
    # AOT is intentionally split into a production DirectNative runtime and
    # tooling. Only ABI-v2 sources are discovered by the engine build graph.
    scriptvm_aot_tooling_srcs = [
        path for path in (
            "src/scriptvm/aot/aot_codegen_v5.cpp",
            "src/scriptvm/aot/aot_package.cpp",
        ) if _exists(path)
    ]
    if scriptvm_aot_tooling_srcs:
        modules["arp_scriptvm_aot_tooling"] = ModuleDef(
            "arp_scriptvm_aot_tooling", "library",
            srcs=scriptvm_aot_tooling_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_reflection", "arp_scriptvm_runtime",
                  "arp_scriptvm_ir", "arp_scriptvm_frontend"],
        )

    # ============================================================
    # Shader 编译器
    # ============================================================
    shader_srcs = _find_srcs("src/shader")
    if shader_srcs:
        modules["arp_shader"] = ModuleDef(
            "arp_shader", "library",
            srcs=shader_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_fkyaml_headers"],
        )

    # ============================================================
    # 动画
    # ============================================================
    anim_srcs = _find_srcs("src/animation")
    if anim_srcs:
        modules["arp_animation"] = ModuleDef(
            "arp_animation", "library",
            srcs=anim_srcs,
            include_dirs=["src"],
            deps=["arp_core"],
        )

    # ============================================================
    # 资源导入/Assets
    # ============================================================
    assets_srcs = _find_srcs("src/assets")
    # bc7enc (offline BC7 block compressor, MIT/PD) is vendored under
    # 3rd/bc7enc. When present, compile bc7enc.c into arp_assets so the
    # KTX2/BC7 writer (ktx2_bc7.cpp) can encode blocks. The decoder path
    # has zero bc7enc dependency, so absence only disables encoding.
    bc7enc_srcs = ["3rd/bc7enc/bc7enc.c"] if _exists("3rd/bc7enc/bc7enc.c") else []
    bc7enc_inc = ["3rd/bc7enc"] if _exists("3rd/bc7enc/bc7enc.h") else []
    if assets_srcs:
        modules["arp_assets"] = ModuleDef(
            "arp_assets", "library",
            srcs=assets_srcs + bc7enc_srcs,
            include_dirs=["src", "3rd/stb", "3rd/fkYAML/include"] + bc7enc_inc,
            deps=["arp_animation", "arp_asset_import_deps"],
        )

    pcg_srcs = _find_srcs("src/pcg")
    if pcg_srcs:
        modules["arp_pcg"] = ModuleDef(
            "arp_pcg", "library", srcs=pcg_srcs,
            include_dirs=["src"], deps=["arp_core"])

    # ============================================================
    # Unity 导入工具
    # ============================================================
    unity_srcs = _find_srcs("tools/unity_import")
    if unity_srcs:
        modules["arp_unity_import"] = ModuleDef(
            "arp_unity_import", "library",
            srcs=unity_srcs,
            include_dirs=["src", "tools/unity_import", "3rd/fkYAML/include"],
            deps=["arp_assets"],
        )

    # ============================================================
    # 资源管理
    # ============================================================
    resource_srcs = _find_srcs("src/resource")
    if resource_srcs:
        modules["arp_resource"] = ModuleDef(
            "arp_resource", "library",
            srcs=resource_srcs,
            include_dirs=["src", "tools/unity_import"],
            deps=["arp_reflection", "arp_assets", "arp_unity_import",
                  "arp_fkyaml_headers", "arp_audio"],
        )

    # ============================================================
    # 网络
    # ============================================================
    net_srcs = _find_srcs("src/network")
    if net_srcs:
        modules["arp_network"] = ModuleDef(
            "arp_network", "library",
            srcs=net_srcs,
            include_dirs=["src"],
            deps=["arp_core"],
            platform_deps={"win": ["ws2_32"]},
        )

    # ============================================================
    # 场景
    # ============================================================
    scene_srcs = _find_srcs("src/scene")
    if scene_srcs:
        modules["arp_scene"] = ModuleDef(
            "arp_scene", "library",
            srcs=scene_srcs,
            include_dirs=["src"],
            deps=["arp_core"],
        )

    # ============================================================
    # Physics 3D (L2 contract + Jolt backend; 2D has its own source target)
    # ============================================================
    physics_srcs = _find_srcs("src/physics/3d")
    if physics_srcs:
        modules["arp_physics"] = ModuleDef(
            "arp_physics", "library",
            srcs=physics_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_reflection", "arp_jolt_headers"],
        )

    # ============================================================
    # 3D / 2D / Input
    # ============================================================
    _3d_srcs = _find_srcs("src/3d")
    if _3d_srcs:
        modules["arp_3d"] = ModuleDef(
            "arp_3d", "library",
            srcs=_3d_srcs,
            include_dirs=["src"],
            deps=["arp_scene", "arp_animation", "arp_physics"],
        )

    _2d_srcs = _find_srcs("src/2d")
    if _2d_srcs:
        modules["arp_2d"] = ModuleDef(
            "arp_2d", "library",
            srcs=_2d_srcs,
            include_dirs=["src"],
            deps=["arp_scene"],
        )

    input_srcs = _find_srcs("src/input")
    if input_srcs:
        modules["arp_input"] = ModuleDef(
            "arp_input", "library",
            srcs=input_srcs,
            include_dirs=["src"],
            deps=["arp_scene"],
        )

    # ============================================================
    # Audio (miniaudio device/decoder backend plus scene-facing runtime)
    # ============================================================
    audio_srcs = _find_srcs("src/audio")
    if audio_srcs:
        modules["arp_audio"] = ModuleDef(
            "arp_audio", "library",
            srcs=audio_srcs,
            include_dirs=["src", "3rd/miniaudio"],
            deps=["arp_core", "arp_scene"],
        )

    # ============================================================
    # UI
    # ============================================================
    ui_font_srcs = _find_srcs("src/ui", ["ui_font.cpp", "ui_font_face_cache.cpp"])
    if ui_font_srcs:
        modules["arp_ui_font"] = ModuleDef(
            "arp_ui_font", "library",
            srcs=ui_font_srcs,
            include_dirs=["src"],
            deps=["arp_core", "arp_freetype"],
        )

    ui_srcs = _find_srcs("src/ui")
    # 排除 ui_font.cpp 和 ui_font_face_cache.cpp (已在上面的 module)
    ui_runtime_srcs = [s for s in ui_srcs
                       if "ui_font.cpp" not in s
                       and "ui_font_face_cache.cpp" not in s]
    if ui_runtime_srcs:
        modules["arp_ui_runtime"] = ModuleDef(
            "arp_ui_runtime", "library",
            srcs=ui_runtime_srcs,
            include_dirs=["src"],
            deps=["arp_input", "arp_scene", "arp_resource", "arp_ui_font",
                  "arp_assets", "arp_fkyaml_headers"],
        )

    # ============================================================
    # GUI (ImGui)
    # ============================================================
    gui_srcs = _find_srcs("src/gui")
    if gui_srcs:
        modules["arp_gui"] = ModuleDef(
            "arp_gui", "library",
            srcs=gui_srcs,
            deps=["arp_core", "arp_ui_font", "arp_ui_runtime"],
        )

    # ============================================================
    # RenderGraph
    # ============================================================
    rg_plan_srcs = _find_srcs("src/renderer/render_graph", ["rhi_frame_plan.cpp"])
    if rg_plan_srcs:
        modules["arp_render_graph_rhi_plan"] = ModuleDef(
            "arp_render_graph_rhi_plan", "library",
            srcs=rg_plan_srcs,
            deps=["arp_core"],
        )

    rg_srcs_full = _find_srcs("src/renderer/render_graph")
    rg_srcs = [s for s in rg_srcs_full if "rhi_frame_plan.cpp" not in s]
    if rg_srcs:
        modules["arp_render_graph"] = ModuleDef(
            "arp_render_graph", "library",
            srcs=rg_srcs,
            deps=["arp_core", "arp_render_graph_rhi_plan"],
        )

    # ============================================================
    # Platform (shared contracts plus explicitly routed OS implementations)
    # ============================================================
    plat_core = (_find_srcs("src/platform/common")
                 + _find_srcs("src/platform/android")
                 + _find_srcs("src/platform/ios"))

    # Shared contracts above remain host-testable; OS windows and GPU execution
    # sources are selected only by their registered target-platform route.
    win_window = "src/platform/windows/window_win32.cpp"
    linux_window = "src/platform/linux/window_xcb.cpp"
    mac_window = "src/platform/macos/window_cocoa.mm"
    web_window = "src/platform/web/window_web.cpp"
    vulkan_rs = "src/platform/native/render_system_vulkan.cpp"
    webgpu_rs = "src/platform/web/render_system_webgpu.cpp"
    metal_rs = "src/platform/native/render_system_metal.cpp"
    native_shared_rs = "src/platform/native/render_system_native_shared.cpp"

    modules["arp_platform"] = ModuleDef(
        "arp_platform", "library",
        srcs=plat_core,
        include_dirs=["src"],
        deps=["arp_core", "arp_input", "arp_gui", "arp_renderer", "arp_audio",
              "arp_rhi_semantics", "arp_renderer_deployment"],
        platform_srcs={
            "web": [web_window, native_shared_rs, webgpu_rs],
            "win": [win_window],
            "linux": [linux_window],
            "mac": [mac_window],
            "vulkan_native": [vulkan_rs],
            "apple": [native_shared_rs, metal_rs],
        },
    )

    # ============================================================
    # Renderer
    # ============================================================
    rr_contracts = _find_srcs("src/renderer", ["rhi_draw_plan_bridge.cpp"])
    if rr_contracts:
        modules["arp_renderer_rhi_contracts"] = ModuleDef(
            "arp_renderer_rhi_contracts", "library",
            srcs=rr_contracts,
            deps=["arp_render_graph_rhi_plan"],
        )

    rr_srcs_all = _find_srcs("src/renderer")
    rr_srcs = [s for s in rr_srcs_all
               if "render_graph/" not in s
               and "deployment/" not in s
               and "rhi_draw_plan_bridge.cpp" not in s]
    if rr_srcs:
        modules["arp_renderer"] = ModuleDef(
            "arp_renderer", "library",
            srcs=rr_srcs,
            deps=["arp_render_graph", "arp_renderer_rhi_contracts",
                  "arp_3d", "arp_2d", "arp_ui_runtime", "arp_assets"],
        )

    # ============================================================
    # Renderer Deployment
    # ============================================================
    depl_srcs = _find_srcs("src/renderer/deployment")
    if depl_srcs:
        modules["arp_renderer_deployment"] = ModuleDef(
            "arp_renderer_deployment", "library",
            srcs=depl_srcs,
            include_dirs=["src"],
            deps=["arp_rhi_semantics"],
        )

    # ============================================================
    # RHI Core + Backends
    # ============================================================
    rhi_base = _find_srcs("src/rhi")
    # 排除 backends/ (vulkan/metal/webgpu 各独立模块) 和 semantics/ 目录。
    # 但保留 null backend —— 它是 RHI 核心的一部分，所有测试都需要。
    rhi_core = [s for s in rhi_base
                if "semantics/" not in s
                and ("backends/" not in s or "backends/null/" in s)]
    if rhi_core:
        modules["arp_rhi"] = ModuleDef(
            "arp_rhi", "library",
            srcs=rhi_core,
            include_dirs=["src"],
            deps=["arp_core"],
        )

    # RHI Semantics
    sem_srcs_all = _find_srcs("src/rhi/semantics")
    # WebGPU bridge/telemetry files live here — they are in the semantics namespace.
    webgpu_bridge = [s for s in _find_srcs("src/rhi/backends/webgpu")
                     if "bridge" in s or "telemetry" in s]
    sem_srcs = sem_srcs_all + webgpu_bridge
    if sem_srcs:
        modules["arp_rhi_semantics"] = ModuleDef(
            "arp_rhi_semantics", "library",
            srcs=sem_srcs,
            include_dirs=["src"],
            deps=["arp_rhi"],
        )

    # RHI Vulkan backend
    vk_srcs = _find_srcs("src/rhi/backends/vulkan")
    if vk_srcs:
        modules["arp_rhi_vulkan"] = ModuleDef(
            "arp_rhi_vulkan", "library",
            srcs=vk_srcs,
            include_dirs=["src"],
            deps=["arp_rhi_semantics", "arp_shader"],
        )

    # RHI Metal backend
    mt_srcs_all = _find_srcs("src/rhi/backends/metal")
    mt_mm = _find_mm_srcs("src/rhi/backends/metal")
    # Source selection follows the CMake target platform, not the host that
    # generated CMakeLists.txt. Apple builds consume Objective-C++ payloads;
    # non-Apple builds consume deterministic diagnostic stubs.
    mt_non_apple = [
        source for source in mt_srcs_all
        if "stub" in source or source.endswith("metal_staging_upload_pool.cpp")
    ]
    mt_common = [
        source for source in mt_srcs_all
        if source not in mt_non_apple
    ]
    if mt_common:
        modules["arp_rhi_metal"] = ModuleDef(
            "arp_rhi_metal", "library",
            srcs=mt_common,
            include_dirs=["src"],
            deps=["arp_rhi_semantics"],
            platform_srcs={
                "apple": mt_mm,
                "non_apple": mt_non_apple,
            },
            apple_frameworks=["Metal", "QuartzCore"],
        )

    # CMake's Xcode generator owns project-file generation. Keeping the app
    # target in the same graph as the backend prevents a checked-in pbxproj
    # from drifting away from the authoritative module and shader contracts.
    ios_app_sources = [
        "src/platform/ios/app/main.mm",
        "src/platform/ios/app/ios_app_delegate.mm",
        "src/platform/ios/app/ios_metal_view.mm",
        "src/platform/ios/ios_runtime_contract.cpp",
    ]
    if all(_exists(source) for source in ios_app_sources):
        modules["arp_ios_smoke_app"] = ModuleDef(
            "arp_ios_smoke_app", "bundle",
            srcs=ios_app_sources,
            deps=["arp_core", "arp_rhi_metal"],
            include_dirs=["src"],
            apple_frameworks=["UIKit", "Metal", "QuartzCore"],
            bundle_identifier="com.animerenderpipeline.metal-smoke",
            info_plist="src/platform/ios/app/Info.plist",
            resources=["src/platform/ios/app/Resources/asset_manifest.yaml"],
            condition="BUILD_PLATFORM == 'ios'",
        )

    # RHI WebGPU backend
    wgpu_srcs_all = _find_srcs("src/rhi/backends/webgpu")
    # 排除已经在 semantics 里的 bridge 文件
    wgpu_srcs = [s for s in wgpu_srcs_all
                 if "bridge" not in s and "telemetry" not in s]
    if wgpu_srcs:
        modules["arp_rhi_webgpu"] = ModuleDef(
            "arp_rhi_webgpu", "library",
            srcs=wgpu_srcs,
            include_dirs=["src"],
            # WebGPU's generated WGSL binding-space artifact is parsed by the
            # backend-private lowering implementation.  This dependency is
            # intentionally below the RHI abstraction: arp_shader depends only
            # on core/fkYAML and never depends on any RHI backend.
            deps=["arp_rhi_semantics", "arp_shader"],
        )

    # ============================================================
    # Tools
    # ============================================================
    # NOTE: arp_ai_asset_generator and arp_cluster_previewer were removed as
    # stale (fixed-pipeline GL previewer + Meshy AI generator no longer used;
    # their source dirs under tools/ were deleted). Do not re-add without
    # restoring tool sources and their (now-missing) link deps.
    tools = {
        "arp_reflect_codegen": "tools/reflect_codegen/main.cpp",
        "arp_asset_importer": "tools/asset_importer/main.cpp",
        "arp_platform_capabilities": "tools/platform_capabilities/main.cpp",
    }
    tool_deps = {}
    for tname, tmain in tools.items():
        if _exists(tmain):
            tool_sources = [tmain]
            tool_includes = [".", "src"]
            if tname == "arp_asset_importer":
                tool_sources.append("tools/asset_importer/script_runner.cpp")
                tool_includes.extend(["tools/unity_import", "3rd/fkYAML/include"])
            modules[tname] = ModuleDef(
                tname, "executable",
                srcs=tool_sources,
                include_dirs=tool_includes,
                deps=tool_deps.get(tname, []),
            )

    # ============================================================
    # Engine DLL (shared library)
    # ============================================================
    engine_dll = "src/engine/engine_dll.cpp"
    if _exists(engine_dll):
        modules["arp_engine"] = ModuleDef(
            "arp_engine", "shared",
            srcs=[engine_dll],
            include_dirs=[".", "src", "3rd/stb"],
            output_name="arp_engine",
            static_on_web=True,
        )

    # ============================================================
    # Test executables
    # ============================================================
    test_modules = _discover_test_modules()
    modules.update(test_modules)

    return modules


def _discover_test_modules() -> Dict[str, ModuleDef]:
    """基于文件存在性注册测试可执行模块。

    每个测试是一个独立的 executable，依赖其所需的引擎库。
    只注册源文件实际存在的测试——不存在则静默跳过。
    """
    test_modules: Dict[str, ModuleDef] = {}

    # (测试源文件相对路径, CMake target 名, 依赖库列表,
    #  [可选] link_flags, extra_includes, build condition, extra_sources)
    scriptvm_core_deps = [
        "arp_scriptvm_runtime",
        "arp_scriptvm_ir",
        "arp_scriptvm_frontend",
        "arp_scriptvm_interpreter",
        "arp_scriptvm_aot_tooling",
    ]
    test_configs = [
        ("tests/unit/cpu_allocator_lifetime_tests.cpp",
         "arp_cpu_allocator_lifetime_tests",
         ["arp_core"]),
        ("tests/integration/terrain_heightfield_tests.cpp",
         "arp_terrain_heightfield_tests",
         ["arp_pcg"]),
        ("tests/integration/physics_l2_contract_tests.cpp",
         "arp_physics_l2_contract_tests",
         ["arp_physics"],
         ["-static-libstdc++", "-static-libgcc"]),
        ("tests/integration/physics_jolt_backend_tests.cpp",
         "arp_physics_jolt_backend_tests",
         ["arp_physics"],
         ["-static-libstdc++", "-static-libgcc"]),
        ("tests/integration/collider_component_contract_tests.cpp",
         "arp_collider_component_contract_tests",
         ["arp_3d", "arp_physics"],
         ["-static-libstdc++", "-static-libgcc"]),
        # Phase 2 (render-thread double-buffer plan): engine-layer SPSC frame
        # double buffer + dedicated render thread contract tests. Both depend
        # only on arp_core (the templates are header-only; the tests use `int`
        # as T so they do not pull in WoCC types). Guards the state-machine
        # invariants, back-pressure, clean shutdown, frame-completion fence,
        # and the consumer stop contract that the WoCC wiring relies on.
        ("tests/contract/spsc_frame_double_buffer_contract_tests.cpp",
         "arp_spsc_frame_double_buffer_contract_tests",
         ["arp_core"]),
        ("tests/contract/render_thread_contract_tests.cpp",
         "arp_render_thread_contract_tests",
         ["arp_core"]),
        ("tests/contract/rhi_command_contract_tests.cpp",
         "arp_rhi_command_contract_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        # Cross-backend fallback decisions are a pure L2/backend-semantics
        # contract. Keep this executable in the normal discovery list so the
        # capability matrix cannot drift without a registered test target.
        ("tests/contract/rhi_fallback_contract_tests.cpp",
         "arp_rhi_fallback_contract_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        # Descriptor access-mode contract, including the logical
        # ReadOnlyStorageBuffer type consumed by WebGPU as
        # WGPUBufferBindingType_ReadOnlyStorage. Keep this as a normal host
        # contract test so all RHI backends share the same logical vocabulary.
        ("tests/contract/rhi_descriptor_contract_tests.cpp",
         "arp_rhi_descriptor_contract_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        ("tests/integration/rhi_device_validation_tests.cpp",
         "arp_rhi_device_validation_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        # Dynamic viewport/scissor L2 command contract test (Phase 1.5a).
        # Covers L2 validation, pipeline-state lowering parity across all
        # four backends, runtime command lowering parity, and the [VS-Exxx]
        # stable diagnostic codes. Mirrors the rhi_depth_bias_clamp_contract_tests
        # shape: no live device required, asserts the lowering decision surface.
        ("tests/contract/rhi_dynamic_viewport_scissor_contract_tests.cpp",
         "arp_rhi_dynamic_viewport_scissor_contract_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        # DDGI Phase 2.2: Rg16Float storage texture per-backend capability
        # contract test. Mirrors the depth_bias_clamp 4-facet shape (static
        # defaults / fail-closed / pass-through / negative) plus the r32float
        # texture-format shape (Null createTexture + size + usage). No live
        # device required; asserts the lowering decision surface and Null
        # texture creation.
        ("tests/contract/rhi_texture_format_rg16f_contract_tests.cpp",
         "arp_rhi_texture_format_rg16f_contract_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        # BC7 block-compressed texture format contract test. Verifies the
        # enum + block-aware helpers (isBlockCompressed/bytesPerBlock/
        # blockExtent/bytesForTextureMip), the mip-chain placement estimate,
        # and Null device createTexture(BC7, Sampled|CopyDst, mips) for both
        # UNORM and SRGB variants. No live Vulkan device required; mirrors the
        # rg16f texture-format test shape. Added for offline-baked BC7 KTX2
        # textures (replacing runtime nearest-point downscale + CPU mipgen).
        ("tests/contract/rhi_texture_format_bc7_contract_tests.cpp",
         "arp_rhi_texture_format_bc7_contract_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        ("tests/contract/rhi_pipeline_contract_tests.cpp",
         "arp_rhi_pipeline_contract_tests",
         ["arp_rhi", "arp_rhi_semantics"]),
        # Keep the backend contract separate from WoCC browser integration.
        # This source validates ARP's public RHI/backend boundary only; the
        # browser canvas smoke and production readbacks are owned by WoCC.
        # This contract does not require a live browser device; it exercises
        # the backend-owned descriptor/lowering boundary directly.
        ("tests/contract/rhi_webgpu_input_attachment_contract_tests.cpp",
         "arp_rhi_webgpu_input_attachment_contract_tests",
         ["arp_rhi", "arp_rhi_semantics", "arp_rhi_webgpu"],
         [], [], "BUILD_PLATFORM != 'web'"),
        # This executable exercises the Emscripten WebGPU runtime. It is
        # intentionally generated only for the web build and is run by the
        # browser runner, never by desktop CTest.
        ("tests/contract/rhi_webgpu_device_contract_tests.cpp",
         "arp_rhi_webgpu_device_contract_tests",
         ["arp_rhi", "arp_rhi_semantics", "arp_rhi_webgpu"],
         [], [], "BUILD_PLATFORM == 'web'",
         ["src/platform/common/platform_lifecycle_state.cpp"]),
        # These two contracts inspect the backend source tree and are host
        # tests. Keeping them out of the Emscripten executable set avoids
        # treating Node's incomplete filesystem/runtime as browser evidence.
        ("tests/contract/rhi_webgpu_memory_contract_tests.cpp",
         "arp_rhi_webgpu_memory_contract_tests",
         ["arp_core"],
         [], [], "BUILD_PLATFORM != 'web'"),
        ("tests/integration/rhi_webgpu_staging_upload_pool_tests.cpp",
         "arp_rhi_webgpu_staging_upload_pool_tests",
         ["arp_core", "arp_rhi", "arp_rhi_semantics", "arp_rhi_webgpu"],
         [], [], "BUILD_PLATFORM != 'web'",
         ["src/platform/common/platform_lifecycle_state.cpp"]),
        ("tests/contract/rhi_vulkan_device_contract_tests.cpp",
         "arp_rhi_vulkan_device_contract_tests",
         ["arp_rhi", "arp_rhi_semantics", "arp_rhi_vulkan"],
         [], [], "BUILD_PLATFORM != 'web'"),
        ("tests/contract/rhi_metal_device_contract_tests.cpp",
         "arp_rhi_metal_device_contract_tests",
         ["arp_rhi", "arp_rhi_semantics", "arp_rhi_metal"],
         [], [], "BUILD_PLATFORM != 'web'"),
        ("tests/contract/rhi_metal_runtime_hash_contract_tests.cpp",
         "arp_rhi_metal_runtime_hash_contract_tests",
         ["arp_rhi", "arp_rhi_semantics", "arp_rhi_metal"],
         [], [], "BUILD_PLATFORM != 'web'"),
        ("tests/contract/ios_platform_contract_tests.cpp",
         "arp_ios_platform_contract_tests",
         ["arp_platform", "arp_renderer"],
         [], [], "BUILD_PLATFORM != 'web'"),
        ("tests/contract/macos_platform_contract_tests.cpp",
         "arp_macos_platform_contract_tests",
         ["arp_platform"],
         [], [], "BUILD_PLATFORM == 'mac'"),
        ("tests/smoke/macos_metal_surface_smoke.cpp",
         "arp_macos_metal_surface_smoke",
         ["arp_platform"],
         [], [], "BUILD_PLATFORM == 'mac'"),
        # Shader layout / reflection contract tests. Depends on arp_shader (for
        # ShaderReflectionCache fkYAML loader + ShaderCompiler) and
        # arp_spirv_reflect (to cross-check generated bindings against the real
        # SPIR-V payload). Reads compiled .spv / .reflection.yaml from the
        # shaders/ output directory produced by arp_compile_shaders, and
        # #includes the generated wocc_sky.shader_bindings.h header.
        ("tests/contract/shader_tests.cpp",
         "arp_shader_tests",
         ["arp_shader", "arp_spirv_reflect"],
         [],
         ["${CMAKE_BINARY_DIR}/shaders"]),
        # Animation blending + IK tests (Phase 0-4 of the blend/IK plan).
        ("tests/unit/core_math_tests.cpp",
         "arp_core_math_tests",
         ["arp_core"]),
        ("tests/unit/poisson_scatter_tests.cpp",
         "arp_poisson_scatter_tests",
         ["arp_core"]),
        # DDGI Chebyshev visibility weight unit test (Phase 2.1).
        # Mirrors the shader contract from ddgi.slang:439-459 (chebyshevWeight).
        # Pure scalar math, no engine dependency; re-declares the formula
        # locally because Slang shaders cannot share a C++ header.
        ("tests/unit/ddgi_chebyshev_tests.cpp",
         "arp_ddgi_chebyshev_tests",
         []),
        ("tests/integration/animation_pose_blend_tests.cpp",
         "arp_animation_pose_blend_tests",
         ["arp_animation"]),
        ("tests/integration/blend_tree_tests.cpp",
         "arp_blend_tree_tests",
         ["arp_animation"]),
        ("tests/integration/ik_solver_tests.cpp",
         "arp_ik_solver_tests",
         ["arp_animation"]),
        ("tests/integration/foot_ik_tests.cpp",
         "arp_foot_ik_tests",
         ["arp_animation"]),
        ("tests/integration/skinned_mesh_player_foot_ik_tests.cpp",
         "arp_skinned_mesh_player_foot_ik_tests",
         ["arp_animation"]),
        # Render graph topology HTML dump contract tests. Validates the
        # self-contained HTML export (SVG dependency graph + diagnostic tables):
        # minimal graph dump, RAW/WAR/WAW edge derivation, culled pass display,
        # attachment-write edges, env-var auto-dump, invalid path failure,
        # HTML escaping, and offline self-containment. Uses Null backend (no
        # live GPU required, CI-safe).
        ("tests/integration/render_graph_topology_dump_tests.cpp",
         "arp_render_graph_topology_dump_tests",
         ["arp_render_graph", "arp_rhi", "arp_rhi_semantics"],
         ["-static-libstdc++", "-static-libgcc"]),
        ("tests/unit/audio_system_tests.cpp", "arp_audio_system_tests", ["arp_audio"]),
        ("tests/integration/audio_clip_resource_tests.cpp", "arp_audio_clip_resource_tests", ["arp_audio", "arp_resource"]),
        ("tests/integration/audio_component_tests.cpp", "arp_audio_component_tests", ["arp_audio"]),
        ("tests/integration/audio_importer_tests.cpp", "arp_audio_importer_tests", ["arp_audio"]),
        ("tests/integration/audio_cue_lifecycle_tests.cpp", "arp_audio_cue_lifecycle_tests", ["arp_audio", "arp_platform"]),
        ("tests/contract/audio_package_contract_tests.cpp", "arp_audio_package_tests", ["arp_resource"]),
        ("tests/integration/audio_resource_lifecycle_tests.cpp", "arp_audio_resource_lifecycle_tests", ["arp_resource", "arp_audio"]),
        # Explicit device smoke. Its name intentionally omits `_tests` so
        # environments without an output device do not run it through CTest.
        ("tests/smoke/audio_backend_smoke.cpp", "arp_audio_backend_smoke", ["arp_audio"]),
        # ArpScript VM unit tests (Phase 1: hand-authored bytecode).
        ("tests/unit/scriptvm_vm_tests.cpp",
         "arp_scriptvm_vm_tests",
         scriptvm_core_deps),
        # ArpScript VM smoke (headless, no window).
        ("tests/smoke/scriptvm_vm_smoke.cpp",
         "arp_scriptvm_vm_smoke",
         scriptvm_core_deps),
        # ArpScript Lexer unit tests (Phase 2).
        ("tests/unit/scriptvm_lexer_tests.cpp",
         "arp_scriptvm_lexer_tests",
         scriptvm_core_deps),
        # ArpScript Lexer smoke (Phase 2).
        ("tests/smoke/scriptvm_lexer_smoke.cpp",
         "arp_scriptvm_lexer_smoke",
         scriptvm_core_deps),
        # ArpScript Parser unit tests (Phase 3).
        ("tests/unit/scriptvm_parser_tests.cpp",
         "arp_scriptvm_parser_tests",
         scriptvm_core_deps),
        # ArpScript Parser smoke (Phase 3).
        ("tests/smoke/scriptvm_parser_smoke.cpp",
         "arp_scriptvm_parser_smoke",
         scriptvm_core_deps),
        # ArpScript Sema unit tests (Phase 4).
        ("tests/unit/scriptvm_sema_tests.cpp",
         "arp_scriptvm_sema_tests",
         scriptvm_core_deps),
        # ArpScript Sema smoke (Phase 4).
        ("tests/smoke/scriptvm_sema_smoke.cpp",
         "arp_scriptvm_sema_smoke",
         scriptvm_core_deps),
        # ArpScript Compiler unit tests (Phase 5).
        ("tests/unit/scriptvm_interpreter_pipeline_tests.cpp",
         "arp_scriptvm_interpreter_pipeline_tests",
         scriptvm_core_deps),
        ("tests/smoke/scriptvm_interpreter_pipeline_full_smoke.cpp",
         "arp_scriptvm_interpreter_pipeline_full_smoke",
         scriptvm_core_deps),
        ("tests/contract/scriptvm_e2e_contract_tests.cpp",
         "arp_scriptvm_e2e_contract_tests",
         scriptvm_core_deps),
        ("tests/smoke/scriptvm_e2e_smoke.cpp",
         "arp_scriptvm_e2e_smoke",
         scriptvm_core_deps),
        ("tests/unit/scriptvm_fastpath_tests.cpp",
         "arp_scriptvm_fastpath_tests",
         scriptvm_core_deps),
        ("tests/unit/scriptvm_module_tests.cpp",
         "arp_scriptvm_module_tests",
         scriptvm_core_deps),
        ("tests/unit/scriptvm_typed_ir_tests.cpp",
         "arp_scriptvm_typed_ir_tests",
         scriptvm_core_deps),
        ("tests/unit/scriptvm_semantic_runtime_tests.cpp",
         "arp_scriptvm_semantic_runtime_tests",
         scriptvm_core_deps),
    ]

    bench_configs = [
        ("tests/bench/scriptvm_fib_benchmark.cpp",
         "arp_scriptvm_fib_benchmark",
         scriptvm_core_deps,
         ["-static-libstdc++", "-static-libgcc"]),
        ("tests/bench/scriptvm_stress_benchmark.cpp",
         "arp_scriptvm_stress_benchmark",
         scriptvm_core_deps,
         ["-static-libstdc++", "-static-libgcc"]),
    ]

    for config in test_configs:
        src_rel = config[0]
        target_name = config[1]
        deps = config[2]
        link_flags = config[3] if len(config) > 3 else []
        extra_includes = config[4] if len(config) > 4 else []
        condition = config[5] if len(config) > 5 else None
        extra_sources = config[6] if len(config) > 6 else []
        if not _exists(src_rel):
            continue
        include_dirs = [".", "src"] + extra_includes
        test_modules[target_name] = ModuleDef(
            target_name, "executable",
            srcs=[src_rel.replace("\\", "/")] + extra_sources,
            include_dirs=include_dirs,
            deps=deps,
            link_flags=link_flags,
            condition=condition,
        )

    # Benchmark targets (built with the engine, not run by ctest automatically)
    for config in bench_configs:
        src_rel = config[0]
        target_name = config[1]
        deps = config[2]
        link_flags = config[3] if len(config) > 3 else []
        extra_includes = config[4] if len(config) > 4 else []
        if not _exists(src_rel):
            continue
        include_dirs = [".", "src"] + extra_includes
        test_modules[target_name] = ModuleDef(
            target_name, "executable",
            srcs=[src_rel.replace("\\", "/")],
            include_dirs=include_dirs,
            deps=deps,
            link_flags=link_flags,
        )

    return test_modules


# ============================================================
# 全局构建状态
# ============================================================
BUILD_PLATFORM = "win"
BUILD_BACKEND = "vulkan"


def _find_vulkan_sdk() -> str:
    configured = os.environ.get("VULKAN_SDK", "").replace("\\", "/")
    if configured and os.path.exists(f"{configured}/Include/vulkan/vulkan.h"):
        return configured

    sdk_root = Path("C:/VulkanSDK")
    if not sdk_root.is_dir():
        return ""

    def version_key(path: Path) -> Tuple[int, ...]:
        try:
            return tuple(int(part) for part in path.name.split("."))
        except ValueError:
            return ()

    for candidate in sorted(sdk_root.iterdir(), key=version_key, reverse=True):
        normalized = str(candidate).replace("\\", "/")
        if os.path.exists(f"{normalized}/Include/vulkan/vulkan.h"):
            return normalized
    return ""


# ============================================================
# 命令行接口
# ============================================================

def _find_mingw_compilers() -> Tuple[str, str, str, str, str]:
    """在 PATH 和常见位置查找 MinGW 编译器。"""
    gcc = "gcc"
    gxx = "g++"
    ninja = "ninja"
    vk_sdk = ""
    slangc = ""

    mingw_dirs = [
        "C:/mingw64",
        "C:/msys64/mingw64",
        "C:/msys64/ucrt64",
    ]
    for d in mingw_dirs:
        gcc_candidate = f"{d}/bin/gcc.exe"
        if os.path.exists(gcc_candidate):
            gcc = gcc_candidate
            gxx = f"{d}/bin/g++.exe"
            ninja_candidate = f"{d}/bin/ninja.exe"
            if os.path.exists(ninja_candidate):
                ninja = ninja_candidate
            break

    # Vulkan SDK
    vk_candidates = [_find_vulkan_sdk()]
    for d in vk_candidates:
        if d and os.path.exists(f"{d}/Include/vulkan/vulkan.h"):
            vk_sdk = d
            slangc_candidate = f"{d}/Bin/slangc.exe"
            if os.path.exists(slangc_candidate):
                slangc = slangc_candidate
            break

    return gcc, gxx, ninja, vk_sdk, slangc


def _find_mac_compilers() -> Tuple[str, str, str, str, str]:
    return "clang", "clang++", "ninja", "", os.environ.get("SLANGC_EXECUTABLE", "")


def _find_emscripten_compilers() -> Tuple[str, str, str, str, str]:
    """Locate the Emscripten compiler used by the WebGPU browser preset."""
    emsdk_root = os.environ.get("EMSDK", "C:/emsdk").replace("\\", "/")
    emscripten_dir = f"{emsdk_root}/upstream/emscripten"
    emcc = f"{emscripten_dir}/emcc.exe"
    emxx = f"{emscripten_dir}/em++.exe"
    if not os.path.exists(emcc) or not os.path.exists(emxx):
        raise RuntimeError(
            "Web build requires Emscripten emcc/em++ under "
            f"{emscripten_dir}")

    # Ninja is host tooling and is independent of the cross compiler.
    ninja = "ninja"
    for candidate in ("C:/mingw64/bin/ninja.exe", "C:/msys64/mingw64/bin/ninja.exe"):
        if os.path.exists(candidate):
            ninja = candidate
            break

    vk_sdk = ""
    slangc = ""
    for d in (_find_vulkan_sdk(),):
        if d and os.path.exists(f"{d}/Include/vulkan/vulkan.h"):
            vk_sdk = d
            candidate = f"{d}/Bin/slangc.exe"
            if os.path.exists(candidate):
                slangc = candidate
            break
    return emcc, emxx, ninja, vk_sdk, slangc


def cmd_configure(args):
    """生成 CMakeLists.txt 并运行 cmake configure。"""
    global BUILD_PLATFORM, BUILD_BACKEND

    selected_backend = BuildBackendSelection.resolve(args.platform, args.backend)
    preset_name = BuildPresetSelection.resolve(args.platform, selected_backend, args.config)
    CMakePresetDocument.load(ROOT / "CMakePresets.json")
    selected_directory = ROOT / 'build' / preset_name
    existing_cache = CMakeCacheDocument.load(selected_directory / 'CMakeCache.txt', allow_missing=True)
    if existing_cache is not None:
        existing_cache.require_locations(ROOT, selected_directory)
    BUILD_PLATFORM = args.platform
    BUILD_BACKEND = selected_backend

    # 自动检测编译器
    if args.platform == "web":
        gcc, gxx, ninja, vk_sdk, slangc = _find_emscripten_compilers()
    elif sys.platform == "win32":
        gcc, gxx, ninja, vk_sdk, slangc = _find_mingw_compilers()
    elif sys.platform == "darwin":
        gcc, gxx, ninja, vk_sdk, slangc = _find_mac_compilers()
    else:
        gcc, gxx, ninja, vk_sdk, slangc = "gcc", "g++", "ninja", "", ""

    build_dir_name = f"build/{preset_name}"
    build_dir = ROOT / build_dir_name
    planned, _ = plan_cmake_presets(args.config, BUILD_PLATFORM, BUILD_BACKEND,
        gcc, gxx, ninja, vk_sdk, slangc)
    selected = next(p for p in planned['configurePresets'] if p['name'] == preset_name)
    CMakeBuildIntent(ROOT, selected, BUILD_BACKEND,
        BuildPresetSelection.CONFIGURATIONS[args.config]).validate_existing(allow_missing=True)

    print(f"\n=== arp-engine Build Configuration ===")
    print(f"  Platform: {BUILD_PLATFORM}")
    print(f"  Backend:  {BUILD_BACKEND}")
    print(f"  Config:   {args.config}")
    print(f"  GCC:      {gcc}")
    print(f"  G++:      {gxx}")
    print(f"  Ninja:    {ninja}")
    if vk_sdk:
        print(f"  VulkanSDK:{vk_sdk}")
    if slangc:
        print(f"  slangc:   {slangc}")
    print()

    # 1. 自动发现模块
    print("Auto-discovering modules...")
    modules = _auto_discover_modules()

    # 2. 过滤：只选实际需要的模块
    selected = set()
    for name, m in modules.items():
        # 排除条件不满足的模块
        if m.condition is not None and not eval(m.condition):
            continue
        # A browser build has exactly one executable RHI backend. Do not let
        # desktop Vulkan/Metal sources leak into the Emscripten target: that
        # would make platform selection depend on incidental source presence
        # and can pull native loader/framework contracts into a wasm link.
        if BUILD_PLATFORM == "web" and name in {"arp_rhi_vulkan", "arp_rhi_metal"}:
            continue
        selected.add(name)

    print(f"  Found {len(modules)} modules, {len(selected)} selected")
    for m in sorted(selected):
        print(f"    - {m}")

    # 3. 生成 CMakeLists.txt
    print("\nGenerating CMakeLists.txt...")
    modules = {name: module for name, module in modules.items()
               if name in selected}
    writer = CmakeWriter(modules, str(build_dir), args.config,
                         BUILD_PLATFORM, BUILD_BACKEND,
                         gcc, gxx, ninja, vk_sdk, slangc)
    if args.regenerate_cmake:
        writer.generate(str(ROOT / "CMakeLists.txt"))
    else:
        print("  Preserving versioned CMakeLists.txt"
              " (use --regenerate-cmake explicitly)")

    # 4. 生成 CMakePresets.json
    print("\nGenerating CMakePresets.json...")
    generate_cmake_presets(str(ROOT / "CMakePresets.json"), args.config,
                           BUILD_PLATFORM, BUILD_BACKEND,
                           gcc, gxx, ninja, vk_sdk, slangc)

    # 5. 运行 cmake configure
    print(f"\nRunning cmake configure (preset: {preset_name})...")
    build_dir.mkdir(parents=True, exist_ok=True)

    cmd = ["cmake", "--preset", preset_name, "-S", str(ROOT)]
    result = subprocess.run(cmd, cwd=str(ROOT))
    if result.returncode != 0:
        print("ERROR: cmake configure failed!")
        sys.exit(1)
    print("  OK: cmake configure succeeded")
    print(f"\nNext: python build.py build --platform={BUILD_PLATFORM} --backend={BUILD_BACKEND} --config={args.config}")


def _configured_build_intent(args, preset_name, category):
    document = CMakePresetDocument.load(ROOT / 'CMakePresets.json')
    operation = document.preset(category, preset_name)
    if not operation or 'inherits' in operation or operation.get('configurePreset') != preset_name:
        raise CMakeBuildCacheError('RHI-BUILD-CACHE-INTENT: operation must explicitly reference its selected configure preset')
    backend = BuildBackendSelection.resolve(
        getattr(args, 'platform', BUILD_PLATFORM), getattr(args, 'backend', BUILD_BACKEND))
    intent = CMakeBuildIntent(ROOT, document.preset('configurePresets', preset_name), backend,
        BuildPresetSelection.CONFIGURATIONS[args.config])
    intent.validate_existing()
    return intent


def cmd_build(args):
    """运行 cmake build。"""
    preset_name = BuildPresetSelection.resolve(
        getattr(args, 'platform', BUILD_PLATFORM), getattr(args, 'backend', BUILD_BACKEND), args.config)
    intent = _configured_build_intent(args, preset_name, 'buildPresets')
    cmd = ["cmake", "--build", "--preset", preset_name, "--config", intent.configuration]
    if args.target:
        cmd.extend(["--target", args.target])
    if args.parallel:
        cmd.extend(["--parallel", str(args.parallel)])

    print(f"\nBuilding (preset: {preset_name})...")
    result = subprocess.run(cmd, cwd=str(ROOT))
    if result.returncode != 0:
        print("ERROR: build failed!")
        sys.exit(1)
    print("  OK: build succeeded")


def cmd_test(args):
    """运行 cmake test。"""
    preset_name = BuildPresetSelection.resolve(
        getattr(args, 'platform', BUILD_PLATFORM), getattr(args, 'backend', BUILD_BACKEND), args.config)
    intent = _configured_build_intent(args, preset_name, 'testPresets')
    cmd = ["ctest", "--preset", preset_name, "-C", intent.configuration, "--output-on-failure"]
    if args.filter:
        cmd.extend(["-R", args.filter])

    print(f"\nRunning tests (preset: {preset_name})...")
    result = subprocess.run(cmd, cwd=str(ROOT))
    if result.returncode != 0:
        print("Some tests failed (check output above)")
        sys.exit(1)
    print("  OK: all tests passed")


def cmd_list_modules(args):
    """列出所有可用模块。"""
    print("Auto-discovering modules...\n")
    modules = _auto_discover_modules()
    for name in sorted(modules):
        m = modules[name]
        src_count = len(m.srcs) + len(m.apple_mm_srcs)
        print(f"  {m.type:12s} {name:45s} ({src_count:3d} srcs) -> {m.deps}")


def cmd_list_targets(args):
    """列出所有可执行 target。"""
    modules = _auto_discover_modules()
    print("Executable targets:")
    for name in sorted(modules):
        m = modules[name]
        if m.type in {"executable", "bundle"}:
            print(f"  {name}")


def main():
    parser = argparse.ArgumentParser(
        description="arp-engine unified build system (v2)")
    sub = parser.add_subparsers(dest="command")

    # configure
    p_conf = sub.add_parser("configure", help="Configure the versioned CMake project")
    p_conf.add_argument("--platform", default="win",
                        help="win/mac/linux/android/ios/web")
    p_conf.add_argument("--backend", default="auto",
                        help="vulkan/metal/webgpu (auto = platform default)")
    p_conf.add_argument("--config", default="debug",
                        choices=["debug", "release", "package"])
    p_conf.add_argument("--regenerate-cmake", action="store_true",
                        help="explicitly replace the versioned CMakeLists.txt")

    # build
    p_build = sub.add_parser("build", help="Build")
    p_build.add_argument("--platform", default="win", help="win/mac/linux/android/ios/web")
    p_build.add_argument("--backend", default="auto", help="explicit product or auto = platform default")
    p_build.add_argument("--config", default="debug",
                         choices=["debug", "release", "package"])
    p_build.add_argument("--target", default=None)
    p_build.add_argument("--parallel", type=int, default=None)

    # test
    p_test = sub.add_parser("test", help="Run tests")
    p_test.add_argument("--platform", default="win", help="win/mac/linux/android/ios/web")
    p_test.add_argument("--backend", default="auto", help="explicit product or auto = platform default")
    p_test.add_argument("--config", default="debug",
                        choices=["debug", "release", "package"])
    p_test.add_argument("--filter", default=None)

    # list-modules
    sub.add_parser("list-modules", help="List all modules")
    sub.add_parser("list-targets", help="List all executable targets")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    # Dispatch
    handlers = {
        "configure": cmd_configure,
        "build": cmd_build,
        "test": cmd_test,
        "list-modules": cmd_list_modules,
        "list-targets": cmd_list_targets,
    }
    try:
        handlers[args.command](args)
    except (BuildBackendSelectionError, CMakePresetDocumentError, CMakeBuildCacheError) as error:
        parser.exit(2, f'{error}\n')


if __name__ == "__main__":
    main()
