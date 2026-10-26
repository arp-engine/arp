#!/usr/bin/env python3
"""
arp-engine unified build orchestrator.

Engine/editor distribution and game targets have separate build behaviors.
Game targets depend on the engine library (transitively via CMake).

Usage:
  python build.py engine                    # Build engine DLL + standard editor
  python build.py engine --no-editor        # Build only engine DLL + deps
  python build.py engine --with-shaders     # Engine + shader artifacts
  python build.py editor                    # Build standard ARP Editor
  python build.py shaders                   # Build shader artifacts only
  python build.py game all                  # Build all game targets
  python build.py game pika                 # Build a specific game
  python build.py configure                 # CMake configure step
  python build.py test                      # Build and run tests
  python build.py all --with-games          # Full rebuild
"""

import argparse
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmake_build_cache import CMakeBuildCacheError, CMakePresetBuildAdmission
from cmake_gen import BuildPresetSelection
from cmake_presets_document import CMakePresetDocumentError

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PRESET = "win-vulkan-debug"


def _preset_admission(preset, category, config=None, cwd=None):
    return CMakePresetBuildAdmission(cwd if cwd is not None else ROOT,
        preset, category, config, registered_backend_for=BuildPresetSelection.registered_backend)


def _config_for_preset(preset, explicit_config=None, category='configurePresets', cwd=None):
    """Select declared configuration; execution still requires cache admission."""
    return _preset_admission(preset, category, explicit_config, cwd).configuration

# ── target registry ────────────────────────────────────────────────────────

ENGINE_TARGET = "arp_engine"
EDITOR_TARGET = "arp_editor"
EDITOR_CLI_TARGET = "arp_editor_cli"
SHADER_TARGET = "arp_compile_shaders"

# Game executables (all in games/), each links arp_engine transitively.
GAME_TARGETS = {
    "survivors":          "arp_survivors",
    "pika":               "arp_pika",
    "wocc":               "arp_world_of_claudecraft",
    "project-lane":       "arp_project_lane",
    "material-editor":    "arp_material_editor",
    "character-viewer":   "arp_character_viewer",
    "stylized-showcase":  "arp_stylized_showcase",
    "production-demo":    "arp_production_demo",
    "splash-boot":        "arp_splash_boot",
    "clear-sample":       "arp_clear_sample",
}

# Key test targets for smoke validation
SMOKE_TESTS = [
    "arp_rhi_null_hidden_frame_smoke_tests",
    "arp_render_graph_resource_management_smoke_tests",
    "arp_scriptvm_aot_v5_generated_smoke",
    "arp_scriptvm_aot_v5_cross_backend_smoke",
]
WEB_SMOKE_TESTS = [
    "arp_scriptvm_aot_v5_generated_smoke",
    "arp_scriptvm_aot_v5_cross_backend_smoke",
]

# CTest-only checks may still consume generated build artifacts. Keep those
# prerequisites explicit so `test --target` remains target-scoped without
# accepting stale catalog/reflection output as valid evidence.
CTEST_BUILD_DEPENDENCIES = {
    "arp_script_component_authoring_smoke": ["arp_script_component_authoring_tests"],
    "arp_script_component_identity_admission_tests": ["arp_script_component_authoring_tests"],
    "arp_script_component_identity_smoke": ["arp_script_component_identity_tests"],
    "arp_editor_scene_history_budget_tests": ["arp_editor_document_transaction_tests"],
    "arp_editor_scene_history_budget_smoke": ["arp_editor_document_transaction_tests"],
    "arp_editor_physics_authoring_history_tests": ["arp_editor_document_transaction_tests"],
    "arp_editor_physics_authoring_history_smoke": ["arp_editor_document_transaction_tests"],
    "arp_physics2d_authoring_smoke": ["arp_physics2d_authoring_tests"],
    "arp_scene_identity_restoration_smoke": ["arp_scene_identity_restoration_tests"],
    "arp_scene_reparent_safety_smoke": ["arp_scene_reparent_safety_tests"],
    "arp_editor_scene_identity_history_tests": ["arp_editor_document_transaction_tests"],
    "arp_editor_scene_identity_history_smoke": ["arp_editor_document_transaction_tests"],
    "arp_editor_reparent_safety_tests": ["arp_editor_document_transaction_tests"],
    "arp_editor_reparent_safety_smoke": ["arp_editor_document_transaction_tests"],
    "arp_editor_reflection_metadata_smoke": ["arp_editor_scene_file_safety_tests"],
    "arp_material_resource_fingerprint_smoke": ["arp_material_resource_fingerprint_tests"],
    "arp_editor_material_reload_tests": ["arp_editor_reference_assignment_tests"],
    "arp_editor_project_build_boundary_tests": [],
    "arp_editor_project_migration_tests": ["arp_editor_project_migrate"],
    "arp_editor_scene_migration_tests": ["arp_editor_scene_migrate"],
    "arp_editor_scene_migration_smoke": ["arp_editor_scene_migrate"],
    "arp_editor_scene_identity_contract_tests": ["arp_editor_project_workflow_tests"],
    "arp_editor_project_settings_smoke": ["arp_editor_project_workflow_tests"],
    "arp_editor_scene_identity_smoke": ["arp_editor_project_workflow_tests"],
    "arp_editor_scene_export_smoke": ["arp_editor_scene_export_tests"],
    "arp_editor_scene_export_ui_contract_tests": ["arp_editor_project_workflow_tests"],
    "arp_editor_scene_export_ui_smoke": ["arp_editor_project_workflow_tests"],
    "arp_editor_play_launch_smoke": ["arp_editor_play_launch_tests"],
    "arp_editor_document_transaction_smoke": ["arp_editor_document_transaction_tests"],
    "arp_editor_object_command_safety_tests": ["arp_editor_document_transaction_tests"],
    "arp_editor_object_command_safety_smoke": ["arp_editor_document_transaction_tests"],
    "arp_editor_script_file_safety_smoke": ["arp_editor_script_file_safety_tests"],
    "arp_editor_script_mutation_safety_smoke": ["arp_editor_script_mutation_safety_tests"],
    "arp_editor_script_command_safety_smoke": ["arp_editor_script_command_safety_tests"],
    "arp_editor_script_input_safety_smoke": ["arp_editor_script_input_safety_tests"],
    "arp_editor_script_modal_safety_smoke": ["arp_editor_script_modal_safety_tests"],
    "arp_editor_modifier_state_smoke": ["arp_editor_modifier_state_tests"],
    "arp_editor_scene_keyboard_receipt_smoke": ["arp_editor_scene_keyboard_receipt_tests"],
    "arp_editor_scene_modal_safety_smoke": ["arp_editor_scene_modal_safety_tests"],
    "arp_editor_exit_safety_smoke": ["arp_editor_exit_safety_tests"],
    "arp_editor_script_close_smoke": ["arp_editor_script_close_tests"],
    "arp_editor_scene_file_safety_smoke": ["arp_editor_scene_file_safety_tests"],
    "arp_editor_project_input_snapshot_smoke": ["arp_editor_project_input_snapshot_tests"],
    "arp_editor_resource_locations_smoke": ["arp_editor_resource_locations_tests"],
    "arp_editor_recovery_codec_smoke": ["arp_editor_recovery_codec_tests"],
    "arp_editor_recovery_session_smoke": ["arp_editor_recovery_session_tests"],
    "arp_editor_recovery_preparation_smoke": ["arp_editor_recovery_preparation_tests"],
    "arp_editor_script_recovery_identity_smoke": ["arp_editor_script_recovery_identity_tests"],
    "arp_editor_recovery_admission_smoke": ["arp_editor_recovery_admission_tests"],
    "arp_editor_play_launch_cli_tests": [EDITOR_TARGET],
    "arp_editor_scene_publication_preparation_smoke": ["arp_editor_scene_publication_preparation_tests"],
    "arp_editor_scene_replacement_preparation_smoke": ["arp_editor_scene_replacement_preparation_tests"],
    "arp_editor_scene_interaction_handoff_smoke": ["arp_editor_scene_interaction_handoff_tests"],
    "arp_editor_scene_service_binding_smoke": ["arp_editor_scene_service_binding_tests"],
    "arp_editor_live_service_session_smoke": ["arp_editor_live_service_session_tests"],
    "arp_editor_selection_focus_smoke": ["arp_editor_selection_focus_tests"],
    "arp_editor_command_receipt_smoke": ["arp_editor_command_receipt_tests"],
    "arp_editor_transform_gesture_smoke": ["arp_editor_transform_gesture_tests"],
    "arp_editor_single_gesture_safety_tests": ["arp_editor_transform_gesture_tests"],
    "arp_editor_single_gesture_safety_smoke": ["arp_editor_transform_gesture_tests"],
    "arp_editor_inspector_normal_hidden_smoke": ["arp_editor"],
    "arp_editor_inspector_failed_hidden_smoke": ["arp_editor"],
    "arp_editor_inspector_retry_hidden_smoke": ["arp_editor"],
    "arp_editor_inspector_safety_smoke": ["arp_editor_inspector_safety_tests"],
    "arp_editor_inspector_resource_reference_smoke": ["arp_editor_inspector_resource_reference_tests"],
    "arp_editor_inspector_picker_smoke": ["arp_editor_inspector_picker_tests"],
    "arp_editor_inspector_selection_smoke": ["arp_editor_inspector_selection_tests"],
    "arp_editor_reference_focus_smoke": ["arp_editor_reference_focus_tests"],
    "arp_editor_reference_assignment_smoke": ["arp_editor_reference_assignment_tests"],
    "arp_editor_reference_assignment_command_smoke": ["arp_editor_reference_assignment_command_tests"],
    "arp_editor_reference_drag_smoke": ["arp_editor_reference_drag_tests"],
    "arp_editor_object_view_state_smoke": ["arp_editor_object_view_state_tests"],
    "arp_editor_object_view_command_smoke": ["arp_editor_object_view_command_tests"],
    "arp_editor_object_view_ui_smoke": ["arp_editor_object_view_ui_tests"],
    "arp_editor_inspector_label_layout_smoke": ["arp_editor_inspector_label_layout_tests"],
    **{f"arp_editor_inspector_{scenario}_hidden_smoke": [EDITOR_TARGET]
       for scenario in ("label_nested", "label_locked")},
    "arp_editor_resource_navigation_smoke": ["arp_editor_resource_navigation_tests"],
    "arp_editor_inspector_selection_ui_smoke": ["arp_editor_inspector_selection_ui_tests"],
    "arp_editor_inspector_reference_smoke": ["arp_editor_inspector_reference_tests"],
    **{f"arp_editor_inspector_{scenario}_hidden_smoke": [EDITOR_TARGET]
       for scenario in ("view_visible", "view_hidden", "view_shown", "view_locked", "view_unlocked", "view_ui_hidden", "view_ui_shown")},
    **{f"arp_editor_inspector_{scenario}_hidden_smoke": [EDITOR_TARGET]
       for scenario in ("drag_hover", "drag_assigned", "drag_rejected", "drag_cancelled", "nav_valid", "nav_current", "nav_missing", "nav_unmapped", "nav_failed", "nav_retry", "focus_valid", "focus_current", "focus_missing", "focus_failed", "focus_retry", "ref_unassigned", "ref_missing", "ref_type", "ref_repaired", "res_unassigned", "res_missing", "res_type", "res_not_ready", "res_capacity", "res_repaired", "res_stale", "picker_first", "picker_last", "picker_repaired", "picker_invalid", "picker_objects", "picker_object_repaired", "selection_same", "selection_mixed", "selection_incompatible", "selection_ambiguous", "selection_failed", "selection_retry", "selection_single")},
    "arp_editor_transform_transaction_smoke": ["arp_editor_transform_transaction_tests"],
    "arp_editor_transform_selection_gesture_smoke": ["arp_editor_transform_selection_gesture_tests"],
    "arp_editor_transform_settings_panel_smoke": ["arp_editor_transform_settings_panel_tests"],
    "arp_editor_transform_settings_smoke": ["arp_editor_transform_settings_tests"],
    "arp_editor_transform_batch_smoke": ["arp_editor_transform_batch_tests"],
    "arp_ui_mutation_safety_smoke": ["arp_ui_mutation_safety_tests"],
    **{f"arp_editor_transform_gesture_{scenario}_hidden_smoke": [EDITOR_TARGET]
       for scenario in ("before", "move", "scale", "rotate", "undo", "redo", "world_axes", "local_axes",
                        "local_move", "local_scale", "world_scale_rejected", "local_rotate", "world_rotate", "local_undo", "local_redo", "snap_before", "snap_move", "snap_negative",
                        "snap_scale", "snap_rotate", "snap_undo", "snap_redo", "batch_move", "batch_undo", "batch_redo",
                        "group_move", "group_rotate", "group_scale", "group_individual", "group_undo", "group_redo",
                        "settings_open", "settings_draft", "settings_invalid", "settings_failed", "settings_applied", "settings_cancel",
                        "input_tool", "input_text", "input_modal", "input_recovery")},
    "arp_editor_selection_focus_cli_smoke": [EDITOR_TARGET, EDITOR_CLI_TARGET],
    "arp_editor_transform_pivot_cli_tests": [EDITOR_CLI_TARGET],
    "arp_editor_transform_settings_cli_tests": [EDITOR_CLI_TARGET],
    "arp_editor_project_manifest_fixture_smoke": ["arp_editor_project_manifest_tests"],
    "arp_rhi_vulkan_resource_identity_tests": ["arp_rhi_vulkan_native_resource_tests"],
    "arp_rhi_vulkan_descriptor_liveness_tests": ["arp_rhi_vulkan_native_resource_tests"],
    "arp_rhi_vulkan_wsi_failure_retention_tests": [
        "arp_render_graph_resource_management_vulkan_hidden_smoke"
    ],
    "arp_rhi_vulkan_wsi_maintenance_failure_retention_tests": [
        "arp_render_graph_resource_management_vulkan_hidden_smoke"
    ],
    "arp_editor_hidden_smoke": [EDITOR_TARGET],
    "arp_editor_after_hidden_smoke": [EDITOR_TARGET],
    "arp_editor_script_hidden_smoke": [EDITOR_TARGET],
    "arp_editor_cli_help_smoke": [EDITOR_CLI_TARGET],
    "arp_script_plugin_codegen_contract": [
        "arp_script_plugin_core_static",
        "arp_script_plugin_scene_static",
        "arp_script_plugin_scene3d_static",
        "arp_script_plugin_resources_static",
        "arp_script_plugin_network_static",
    ],
    "arp_scriptvm_stress_web_performance_corpus": [
        "arp_scriptvm_stress_benchmark",
    ],
    "arp_scriptvm_python_benchmark_smoke": [
        "arp_scriptvm_stress_benchmark",
    ],
    "arp_scriptvm_stress_differential": [
        "arp_scriptvm_stress_benchmark",
    ],
    "arp_scriptvm_stress_release_performance_corpus": [
        "arp_scriptvm_stress_benchmark",
    ],
    "arp_scriptvm_aot_v5_randomized_differential": [
        "arp_scriptvm_aot_v5_randomized_differential_tests",
    ],
    "arp_scriptvm_aot_v5_endurance_smoke": [
        "arp_scriptvm_aot_v5_randomized_differential_tests",
    ],
    "arp_scriptvm_benchmark_frontend_tests": [
        "arp_script_aotc",
    ],
    "arp_scriptvm_aot_v5_release_audit": [
        "arp_scriptvm_aot_v5_generated_smoke",
    ],
    "arp_scriptvm_aot_v5_browser_smoke": [
        "arp_scriptvm_aot_v5_generated_smoke",
    ],
}


def _editor_targets(preset, config=None):
    """Return distribution targets available on the selected editor platform."""
    targets = [EDITOR_TARGET]
    if _preset_admission(preset, 'buildPresets', config).intent.backend != 'webgpu':
        targets.append(EDITOR_CLI_TARGET)
    return targets


# ── helpers ────────────────────────────────────────────────────────────────


def _cmake(args, cwd=None, check=True, configuration=None):
    """Run a CMake command and return the process."""
    cwd = ROOT if cwd is None else cwd
    args = list(args)
    if args.count('--preset') != 1 or args.index('--preset') + 1 == len(args):
        raise CMakeBuildCacheError('RHI-BUILD-CACHE-INTENT: one explicit preset is required')
    preset = args[args.index('--preset') + 1]
    is_build = '--build' in args
    if args.count('--config') > 1:
        raise CMakeBuildCacheError('RHI-BUILD-CACHE-CONFIG: duplicate configuration is unsupported')
    if '--config' in args:
        index = args.index('--config') + 1
        if index == len(args):
            raise CMakeBuildCacheError('RHI-BUILD-CACHE-CONFIG: missing configuration value')
        configuration = args[index]
    admission = _preset_admission(preset, 'buildPresets' if is_build else 'configurePresets',
        configuration, cwd)
    admission.validate_existing(allow_missing=not is_build)
    if is_build and '--config' not in args:
        args += ['--config', admission.configuration]
    cmd = ["cmake"] + args
    label = " ".join(cmd)
    print(f"[build] {label}")
    result = subprocess.run(cmd, cwd=cwd)
    if check and result.returncode != 0:
        sys.exit(result.returncode)
    return result


def _ctest(preset, test_regex=None, cwd=None, config=None):
    """Run CTest for the configured preset."""
    cwd = ROOT if cwd is None else cwd
    admission = _preset_admission(preset, 'testPresets', config, cwd)
    admission.validate_existing()
    cmd = ["ctest", "--preset", preset, "-C", admission.configuration, "--output-on-failure"]
    if test_regex:
        cmd += ["-R", test_regex, "--no-tests=error"]
    label = " ".join(cmd)
    print(f"[test] {label}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(result.returncode)


def _ctest_regex_for_target(preset, target, cwd=None, config=None):
    """Resolve a CMake target to its registered CTest name(s)."""
    cwd = ROOT if cwd is None else cwd
    admission = _preset_admission(preset, 'testPresets', config, cwd)
    admission.validate_existing()
    query = subprocess.run(
        ["ctest", "--preset", preset, "-C", admission.configuration, "--show-only=json-v1"],
        cwd=cwd, capture_output=True, text=True)
    if query.returncode != 0:
        print(query.stderr, file=sys.stderr)
        sys.exit(query.returncode)
    document = json.loads(query.stdout)
    matches = []
    for test in document.get("tests", []):
        command = test.get("command", [])
        command_stems = {
            os.path.splitext(os.path.basename(token))[0]
            for token in command
            if token and not token.startswith("-")
        }
        test_name = test.get("name", "")
        if (test_name == target
                or test_name == target + "_wasm"
                or target in command_stems):
            matches.append(test["name"])
    if not matches:
        print(f"ERROR: target '{target}' has no registered CTest", file=sys.stderr)
        sys.exit(2)
    return "^(" + "|".join(re.escape(name) for name in matches) + ")$"


def _cmake_target_exists(preset, target, config, cwd=None):
    """Return whether a named build target exists for the configured preset."""
    cwd = ROOT if cwd is None else cwd
    admission = _preset_admission(preset, 'buildPresets', config, cwd)
    admission.validate_existing()
    query = subprocess.run(
        ["cmake", "--build", "--preset", preset,
         "--target", "help", "--config", admission.configuration],
        cwd=cwd, capture_output=True, text=True)
    if query.returncode != 0:
        print(query.stderr, file=sys.stderr)
        sys.exit(query.returncode)
    for line in query.stdout.splitlines():
        # Ninja and Make generators both print target names at the beginning
        # of a help line; avoid treating descriptions as target identifiers.
        candidate = line.strip().split()[0] if line.strip() else ""
        candidate = candidate.rstrip(":")
        if candidate == target:
            return True
    return False


# ── commands ───────────────────────────────────────────────────────────────


def cmd_configure(args):
    """Run CMake configure via preset."""
    _cmake(["--preset", args.preset], configuration=args.config)


def cmd_webgl1_probe(args):
    """Build the isolated test-only WebGL1 fixture with explicit host tools."""
    from webgl1_probe_build import WebGl1ProbeBuilder
    builder = WebGl1ProbeBuilder(ROOT, args.output_dir, {
        "slangc": args.slangc, "spirvCross": args.spirv_cross,
        "spirvReflect": args.spirv_reflect, "emxx": args.emxx,
    })
    manifest = builder.build()
    print(f"[webgl1-probe] ProbeOnly: {manifest}")
    print("[webgl1-probe] No production RHI backend capability is granted")


def cmd_webgl1_runtime(args):
    """Build the root runtime component with explicit Prototype admission."""
    from webgl1_runtime_build import WebGl1RuntimeBuilder
    result = WebGl1RuntimeBuilder(ROOT,args.output_dir,args.program_directory,
        args.cmake,args.ninja,args.toolchain,args.browser,args.build_type).build()
    print(f"[webgl1-runtime] Prototype component: {result}")


def cmd_webgl1_visual(args):
    """Capture and replay immutable products; keep tool evidence out of RHI admission."""
    from webgl_visual_validation import WebGlVisualValidation
    result = WebGlVisualValidation(ROOT, args.manifest, args.output_dir, args.qrenderdoc,
        browser=args.browser, bundle=args.bundle, replay_source=args.replay_source,
        profiles=args.profiles).run()
    print(f"[webgl1-visual] Tool native validation: {result}")


def cmd_webgl1_performance(args):
    from webgl_product_performance import WebGlProductPerformance
    result = WebGlProductPerformance(ROOT, args.manifest, args.plan, args.browser, args.output_dir).run()
    print(f"[webgl1-performance] CPU observations: {result}")


def cmd_engine(args):
    """Build the engine distribution, including its standard editor."""
    targets = [ENGINE_TARGET] if args.no_editor else [ENGINE_TARGET] + _editor_targets(args.preset, args.config)
    _cmake(["--build", "--preset", args.preset, "--target"] + targets + ["--config", args.config])
    if args.with_shaders:
        _cmake(["--build", "--preset", args.preset, "--target", SHADER_TARGET, "--config", args.config])
    print(f"[engine] build complete (preset={args.preset})")


def cmd_shaders(args):
    """Build shader artifacts only."""
    _cmake(["--build", "--preset", args.preset, "--target", SHADER_TARGET, "--config", args.config])
    print(f"[shaders] build complete (preset={args.preset})")


def cmd_editor(args):
    """Build ARP Editor with its engine modules and shared Slang artifacts."""
    _cmake(["--build", "--preset", args.preset, "--target"]
           + _editor_targets(args.preset, args.config) + ["--config", args.config])
    print(f"[editor] build complete (preset={args.preset})")


def cmd_game(args):
    """Build one or all game targets.

    CMake handles engine dependency transitively: building a game target
    will auto-build arp_engine and its modules if they are out of date.
    """
    if args.name == "all":
        targets = list(GAME_TARGETS.values())
    elif args.name in GAME_TARGETS:
        targets = [GAME_TARGETS[args.name]]
    else:
        print(f"ERROR: unknown game '{args.name}'. Available: {list(GAME_TARGETS.keys()) + ['all']}",
              file=sys.stderr)
        sys.exit(1)

    if args.with_shaders:
        _cmake(["--build", "--preset", args.preset, "--target", SHADER_TARGET, "--config", args.config])

    _cmake(["--build", "--preset", args.preset] + ["--target"] + targets + ["--config", args.config])
    print(f"[game] build complete: {', '.join(targets)} (preset={args.preset})")


def cmd_test(args):
    """Build key test targets and run CTest."""
    if args.target:
        regex = _ctest_regex_for_target(args.preset, args.target, config=args.config)
        if _cmake_target_exists(args.preset, args.target, args.config):
            _cmake(["--build", "--preset", args.preset,
                    "--target", args.target, "--config", args.config])
        else:
            prerequisites = CTEST_BUILD_DEPENDENCIES.get(args.target, [])
            if prerequisites:
                _cmake(["--build", "--preset", args.preset,
                        "--target"] + prerequisites
                       + ["--config", args.config])
            else:
                print(f"[test] '{args.target}' is CTest-only; no build prerequisites")
        _ctest(args.preset, regex, config=args.config)
    else:
        product = _preset_admission(args.preset, 'testPresets', args.config).intent.backend
        smoke_tests = WEB_SMOKE_TESTS if product == 'webgpu' else SMOKE_TESTS
        _cmake(["--build", "--preset", args.preset, "--target", ENGINE_TARGET, "--config", args.config])
        _cmake(["--build", "--preset", args.preset, "--target"] + smoke_tests + ["--config", args.config])
        smoke_names = [name for base in smoke_tests for name in
                       (base, base + "_wasm")]
        smoke_regex = "^(" + "|".join(
            re.escape(name) for name in smoke_names) + ")$"
        _ctest(args.preset, smoke_regex, config=args.config)


def cmd_all(args):
    """Full build: engine + standard editor + shaders [+ games]."""
    cmd_engine(args)
    _cmake(["--build", "--preset", args.preset, "--target", SHADER_TARGET, "--config", args.config])
    if args.with_games:
        _cmake(["--build", "--preset", args.preset]
               + ["--target"] + list(GAME_TARGETS.values())
               + ["--config", args.config])
    print(f"[all] build complete (preset={args.preset})")


# ── main ───────────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(
        description="arp-engine unified build orchestrator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build.py engine
  python build.py engine --with-shaders
  python build.py editor
  python build.py game pika
  python build.py game all
  python build.py shaders
  python build.py test
  python build.py configure --preset win-vulkan-release
        """.strip(),
    )
    parser.add_argument("--preset", default=DEFAULT_PRESET, help=f"CMake preset (default: {DEFAULT_PRESET})")
    parser.add_argument(
        "--config", default=None,
        help="Build configuration (default: declared preset configuration; cache must match)",
    )
    sub = parser.add_subparsers(dest="command", help="Build sub-command")

    # engine
    eng = sub.add_parser("engine", help="Build engine and standard ARP Editor")
    eng.add_argument("--with-shaders", action="store_true", help="Also build shader artifacts")
    eng.add_argument("--no-editor", action="store_true", help="Build only the engine (also use with ARP_BUILD_EDITOR=OFF)")

    # editor
    sub.add_parser("editor", help="Build the standard ARP Editor")

    # shaders
    sub.add_parser("shaders", help="Build shader artifacts only")

    # game
    gm = sub.add_parser("game", help="Build game targets")
    gm.add_argument("name", nargs="?", default="all",
                    help="Game name or 'all' (choices: %s)" % ", ".join(sorted(GAME_TARGETS.keys())))
    gm.add_argument("--with-shaders", action="store_true", help="Also build shader artifacts")

    # configure
    sub.add_parser("configure", help="Run CMake configure once")

    # A new test-only entry keeps the four existing Win/Web presets unchanged.
    probe = sub.add_parser("webgl1-probe", help="Build an isolated WebGL1-only feasibility fixture")
    probe.add_argument("--output-dir", default=os.path.join(ROOT, "build", "web-webgl1-probe"))
    probe.add_argument("--slangc", required=True, help="Explicit host Slang executable")
    probe.add_argument("--spirv-cross", required=True, help="Explicit host SPIRV-Cross executable")
    probe.add_argument("--spirv-reflect", required=True, help="Explicit host SPIRV-Reflect executable")
    probe.add_argument("--emxx", required=True, help="Explicit Emscripten C++ compiler")

    runtime = sub.add_parser("webgl1-runtime", help="Build root WebGL1 runtime component Prototype")
    runtime.add_argument("--output-dir", default=os.path.join(ROOT,"build","web-webgl1-runtime"))
    runtime.add_argument("--program-directory", required=True, help="Original host-generated Slang program artifacts")
    runtime.add_argument("--cmake", required=True)
    runtime.add_argument("--ninja", required=True)
    runtime.add_argument("--toolchain", required=True, help="Actual Emscripten toolchain file")
    runtime.add_argument("--browser", help="Run the real browser validation workload")
    runtime.add_argument("--build-type", choices=('Debug','Release'), default='Debug')

    visual = sub.add_parser("webgl1-visual", help="Freeze, capture, replay and audit WebGL1 product scenes")
    visual.add_argument("--manifest", required=True, help="Successful immutable root build manifest")
    visual.add_argument("--output-dir", default=os.path.join(ROOT, 'build', 'webgl1-visual'))
    visual.add_argument("--qrenderdoc", required=True)
    visual.add_argument("--browser", help="Dedicated browser for fresh native captures")
    visual.add_argument("--bundle", help="Exact four-file frozen bundle; defaults to original build")
    visual.add_argument("--replay-source", help="Existing matching capture cohort; never overwritten")
    visual.add_argument("--profiles", nargs='+', choices=('scene2d','gui','basic-forward'))

    performance = sub.add_parser("webgl1-performance", help="Measure frozen Release WebGL1 product frames")
    performance.add_argument("--manifest", required=True)
    performance.add_argument("--browser", required=True)
    performance.add_argument("--plan", default=os.path.join(ROOT, 'tests', 'fixtures', 'webgl1_product_performance.yaml'))
    performance.add_argument("--output-dir", default=os.path.join(ROOT, 'build', 'webgl1-performance'))

    # test
    tst = sub.add_parser("test", help="Build and run tests")
    tst.add_argument("--target", help="Specific test target (default: smoke suite)")

    # all
    al = sub.add_parser("all", help="Full build (engine + standard editor + shaders)")
    al.add_argument("--with-games", action="store_true", help="Also build all game targets")
    al.add_argument("--no-editor", action="store_true", help="Build without ARP Editor")
    al.set_defaults(with_shaders=False)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    dispatch = {
        "engine": cmd_engine,
        "editor": cmd_editor,
        "shaders": cmd_shaders,
        "game": cmd_game,
        "configure": cmd_configure,
        "webgl1-probe": cmd_webgl1_probe,
        "webgl1-runtime": cmd_webgl1_runtime,
        "webgl1-visual": cmd_webgl1_visual,
        "webgl1-performance": cmd_webgl1_performance,
        "test": cmd_test,
        "all": cmd_all,
    }
    try:
        if args.command not in ('webgl1-probe','webgl1-runtime','webgl1-visual','webgl1-performance'):
            category = ('configurePresets' if args.command == 'configure' else
                        'testPresets' if args.command == 'test' else 'buildPresets')
            args.config = _config_for_preset(args.preset, args.config, category)
        dispatch[args.command](args)
    except (CMakeBuildCacheError, CMakePresetDocumentError) as error:
        parser.exit(2, f'{error}\n')


if __name__ == "__main__":
    main()
