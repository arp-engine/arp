# ARP Editor promotion

Promote the existing native editor to the engine's standard editor at `editor/`.
Preserve all current project browsing, script editing, game preview, icon and
YAML UI behavior, including the working tree's uncommitted editor changes.

1. [x] Define migration and build contract: root `editor/`, `arp_editor` target,
   default desktop/Web build inclusion, explicit engine-only opt-out, and no
   dependency on a UI smoke target. Reuse interaction tests and hidden smokes.
2. [x] Move implementation/assets, update namespace and branding, extract editor
   CMake integration, migrate reference metadata to YAML, and update build entry
   points and documentation. Run interaction tests and default hidden smoke.
3. [x] Rebuild with MinGW, run ordered-interaction/script/icon validation, and
   capture/replay native Windows/Vulkan frames using RenderDoc. Record evidence.

Readiness: migration **Contract Complete**; functionality **Prototype Complete**;
Windows/Vulkan **Backend Validated** and Windows **Platform Validated** for the
tested interaction, script, icon and hidden-frame paths. **Production Ready** is
not claimed. Focused CTest 6/6 and MinGW Debug build passed in
`build/ui-control-events-debug`.
This promotion does not change L2/lowering/shader
contracts. Existing render intents continue through the engine's platform,
GuiRenderer and FrameGraph. Null remains validator-only; Vulkan native validation
is required here; Metal and WebGPU retain their existing paths and require their
own native platform evidence. No new capability or implicit fallback is added.

Functional status remains Prototype Complete; promotion does not imply scene
persistence, general asset editing, a live scene renderer or Production Ready.

## Validation evidence (2026-10-14)

- Executable: `build/ui-control-events-debug/arp_editor.exe`.
- Tests: `build/arp-editor-evidence/promotion/tests.txt` (6/6 CTests, including
  four build-orchestrator unit cases; interaction test asserts ARP branding).
- Engine-only configuration: `build/arp-editor-engine-only-contract`, explicitly
  configured with `ARP_BUILD_EDITOR=OFF`; Ninja inventory contains no editor
  targets. Result: `build/arp-editor-evidence/promotion/engine-only-check.txt`.
- Final captures: `build/arp-editor-evidence/promotion-final/before_capture.rdc`
  and `after_capture.rdc`; replay extracts `.bmp`/`.png` screenshots. AI inspected
  both screenshots, including the final ARP Editor footer.
- `promotion-final/analysis.txt`: 1440x810, 1296/1360 draws, zero replay high/error
  diagnostics. Changed pixels: selection 4573, Inspector name 42, intensity 251,
  Interior checkbox 48, Update Mode 2388.
- Reference metadata preserves atlas/crop values and round-trips through the
  existing authoring emitter as YAML. Old production JSON metadata is removed.
- Standard `win-vulkan-debug` configure reaches generation but Ninja fails with
  `failed recompaction: Permission denied` in that existing build tree. The
  independently configured UI build above completes and supplies the verified
  executable. No unrelated build processes or files were removed to bypass it.

Metal and WebGPU retain their migrated native/Web build paths; runtime/platform
validation for them is not claimed by the Windows captures. Null is not promoted
to a native editor renderer. Mobile authoring is explicitly unsupported.
