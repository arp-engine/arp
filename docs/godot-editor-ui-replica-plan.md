> Superseded as an application by [ARP Editor](../editor/README.md). The implementation now lives at `editor/` and builds as `arp_editor`; entries below retain historical validation names and capture paths.

# Godot editor UI replica (Arp UI)

Scope: standalone, interactive Arp UI reference editor shell in `examples/ui/godot_editor`.
The supplied image defines a 1920 x 1080 design canvas. All editor chrome is authored
as `ui.document/v1` controls. The center viewport is an explicitly identified static
reference crop, not a live scene or a reconstructed Godot project.

## Stages

1. Audit and design contract — complete. Reuse YAML runtime loader, inherited
   semantic control hooks, ordered InputFrame, DrawPlanner, unified glyph/image
   atlas, GuiRenderer and FrameGraph. Preserve unrelated changes in both repos.
2. Author UI and implement interactions — complete. 370 Arp UI controls, Scene selection/search,
   real project-root filesystem navigation/search, workspace and dock tabs, property editing/toggles,
   output dock and menu feedback. Native host with scaled layout/input and resize.
3. Filesystem and Script workspace — complete. The filesystem enumerates the configured project root,
   double-click opens `.arp` files, Script view supports UTF-8 text editing and Ctrl+S save, and
   Python-style lexical token coloring distinguishes keywords, strings, comments, numbers, functions and types.
   LF/CRLF is normalized while editing and restored when saving; project-root traversal is rejected.
4. Validate — complete. The focused interaction test and default, after-interaction, and script-view
   hidden native smoke tests pass, alongside ordered event routing, runtime tree and renderer integration.
   Final RenderDoc replay at `build/godot-editor-evidence/validated/analysis.txt`
   has zero debug errors and confirms visible selection, Inspector name, intensity,
   checkbox and Update Mode changes. AI inspected both extracted screenshots.

Delivered: `build/ui-control-events-debug/arp_ui_godot_editor.exe`,
`examples/ui/godot_editor/editor.arpui`, atlas, source, generator, validation script
and README. Status: Prototype Complete; Windows/Vulkan Backend Validated.

An independent `acceptsPointerInput` widget flag was added after the first capture
showed that disabling decorative controls removed their rendering. It keeps
labels/images visible while permitting pointer input to reach underlying controls;
the existing default and YAML schema are preserved. Both interaction and rendering
are asserted by the editor tests, with the existing tree/event/renderer checks passing.

Interactive-launch failure reproduced on October 12, 2026: revealing Output or
Signals passed embedded newlines to Arp UI's single-line text-layout cache, which
returned `UI text layout cache supports single-line text only` and terminated the
host. Replaced multiline labels with separate one-line controls, added load-time
validation for authored strings, exercised Signals/Groups/Output in interaction and
native Vulkan tests, and confirmed all three editor tests pass.

## Boundaries

No RHI or shader contracts change. Host expresses existing GUI draw intents only.
The Script workspace is a basic editor with Python-style lexical highlighting, editing, and saving;
it does not implement language-server services such as completion, diagnostics, refactoring or undo.
The example does not implement game execution, asset import, scene persistence or ReflectionProbe rendering.
Windows/Vulkan is the native validation target; no claim of Metal/WebGPU runtime
validation or Production Ready is made.


## Visual comparison follow-up (October 12, 2026)

A full-canvas comparison against the original 1920 x 1080 screenshot found that
the center preview and panel geometry already aligned closely. The strongest
remaining differences were the hand-drawn toolbar/tree icons, missing workspace
icons, placeholder glyphs in scene tabs, normal-weight workspace labels, and
inspector slider/reset details. Updated the atlas generator to extract matching
workspace, toolbar, tree, folder, scene-tab, run/debug and reset icons from the
reference, added the five workspace icons and separate semibold font token, and
added slider handles plus the selected Output tab treatment.

Validation after the pass: 370 runtime controls, 1283 UI draws, all three
`arp_ui_godot_editor` CTests passing, and a clean RenderDoc replay in
`build/godot-editor-evidence/full-fidelity-final_capture.rdc`. The center viewport
remains a static screenshot crop; full behavior and live Godot rendering are not
part of this replica.

A second typography comparison on October 12, 2026 found that the reference uses a
stronger hierarchy across menu labels, dock/scene tabs, selected object names, and
Inspector section headings. Applied the Semibold face recursively through nested
controls (including Inspector content); property labels and values remain regular.
Final verification: `ctest --test-dir D:/arp/build/ui-control-events-debug -R
'^arp_ui_godot_editor' --output-on-failure` passed 3/3; hidden Vulkan smoke drew
370 controls / 1283 UI commands; RenderDoc replay is saved as
`build/godot-editor-evidence/final-ui-20261012_capture.rdc` with its reviewed frame
at `build/godot-editor-evidence/final-ui-20261012-renderdoc.bmp`.

Tree icon alignment follow-up (October 12, 2026): the tree had reused one generic
blue icon for both MultiplayerSpawner and WorldEnvironment, and the latter's
visible alpha bounds sat right of the red node icon column. Extracted the original
multicolor MultiplayerSpawner glyph, adjusted the WorldEnvironment glyph anchor,
and checked the resulting tree in RenderDoc. The 3 editor CTests passed; final
capture: `build/godot-editor-evidence/tree-icon-alignment-20261012_capture.rdc`.

Checkbox atlas correction (October 12, 2026): after adding the MultiplayerSpawner
sprite, icon slots shifted by one but the checked-state overlay still sampled the
old slot (the save icon). Updated the overlay source rect to the check glyph and
added an interaction-test assertion tying the draw command to that atlas cell.
The UI test, hidden smoke, after-interaction smoke, and RenderDoc replay passed;
see `build/godot-editor-evidence/checkbox-atlas-fix-20261012_capture.rdc`.

Rounded selected-state pass (October 13, 2026): generated a transparent 24 x 24
rounded atlas patch and expanded selected scene/file rows plus active workspace,
dock, and scene tabs into nine tinted image slices. This keeps corner radii stable
while backgrounds stretch. Added a test asserting the selected scene row emits all
nine image patches. Validation: 3/3 Godot editor CTests; 370 controls and 1323 UI
draws in the default hidden Vulkan smoke; RenderDoc capture and thumbnail at
`build/godot-editor-evidence/rounded-selection-20261013_capture.rdc` and
`build/godot-editor-evidence/rounded-selection-thumb.png`.

Hover/selection consistency follow-up (October 13, 2026): hover backgrounds now
use the same rounded atlas nine-slice as selected backgrounds. Hovering a scene-tree
row uses the same fill color as a selected row; other transient buttons retain their
lighter hover feedback. A regression test verifies the hovered row produces nine
patches with the same tint as the selected row. All 3 editor CTests passed; the
RenderDoc hover-state image is `build/godot-editor-evidence/hover-selected-match-20261013.png`.

Atlas standardization follow-up (October 13, 2026): normalized extracted sprites to
trimmed alpha bounds, aspect-preserved centered 24 x 24 regions on a 32 x 32 cell
grid with four transparent gutter pixels. The generator now rejects noncanonical,
overlapping, off-center, or gutter-contaminated regions and emits the runtime region
header. Rounded row backgrounds now use independent destination and atlas source
coordinates for all nine slices, preventing the right edge from sampling outside its
atlas patch. The generator enforces these layout checks; validation also includes 3/3 editor CTests and a
RenderDoc hover smoke passed (370 controls, 1331 draws). See
`build/godot-editor-evidence/atlas-standardized-20261013_capture.rdc` and
`build/godot-editor-evidence/atlas-standardized-20261013.png`.


Interactive hover launch regression (October 13, 2026): the `--hover` preview
routed its synthetic pointer event with window ID 0 before creating the native window.
The first real pointer event then arrived with the native window ID and terminated the
runtime as `UI-INPUT-WINDOW-MISMATCH`. The preview event now routes only after window
creation and uses that window's ID. Validation: 3/3 editor CTests, `--smoke --hover`
(370 controls), and a visible `--hover` launch with pointer movement; the process
remained responsive. Status remains Prototype Complete; Windows/Vulkan Backend
Validated.


## Real filesystem and script editing (October 13, 2026)

The Filesystem dock now enumerates the configured project root (`--project-root`, defaulting to the process working directory), supports directory navigation and filtering, and opens `.arp` files on double-click. The Script workspace provides a multiline editing buffer, cursor movement, Enter/Tab, Backspace/Delete, Ctrl+S persistence, and Python-style lexical coloring for ArpScript tokens. UTF-8 files load with line endings normalized for editing and save with their original LF/CRLF convention. File access is restricted to the selected project root.

Validation: `arp_ui_godot_editor_tests`, default hidden smoke, ordered-interaction hidden smoke, and script-view hidden smoke all pass. Script-view smoke covers startup with an opened `.arp` file; filesystem navigation, editing, token colors and saving are asserted in the interaction test. The editor-specific glyph atlas page is 2048 x 2048 so the added code editor text fits without exhausting the atlas.
