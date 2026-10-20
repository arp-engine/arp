# ARP Editor

ARP Editor is the engine's standard authoring application, maintained at the
repository root in `editor/` and built as `arp_editor`. It uses the engine's
Arp UI controls. The menu bar, toolbar, scene tree, filesystem, inspector, masks
and docks are real controls loaded from `resources/editor.arpui` (`ui.document/v1`). The
The 3D workspace edits an editor-owned engine `scene::Scene`, with selectable
GameObjects, Transform editing, and a live transform marker preview. The Game
workspace can host the running WOCC project. Scene persistence, asset-backed
mesh rendering and asset browsing/import remain prototype boundaries. Project
resource packages load from the explicit YAML project manifest described below.

## Code organization

The editor uses a few modules grouped by responsibility:

| Directory | Responsibility |
| --- | --- |
| `app/` | Native application loop, input routing and GPU submission; game process/window lifecycle in `GamePreviewHost` |
| `workspace/` | `EditorShell` coordination, scene documents, project browsing, script documents/undo, platform input and presentation |
| `ui/` | Font/image/atlas setup and generated atlas layout |
| `resources/` | YAML UI/reference authoring, atlas, sample script and executable icon |
| `tools/` | Reference asset generation, RenderDoc replay analysis and Web diagnostics |
| `tests/` | Interaction coverage and shared helper boundary tests |

`main.cpp` only parses command-line options. `EditorShell` owns workspace state;
its implementation is grouped by feature, without separate files for individual
controls or actions. `editor_shell.h` at the editor root forwards the public
include. The preview host hides native process/window handles in a core-allocated
implementation object. Idle preview synchronization skips UI layout resolution
when no game process is active; ordinary draw commands move into the output list.

Path containment and UTF-8 scalar navigation reuse `core::FilePath` and
`core::Utf8Cursor`; list scrollbars share `ui::ScrollbarGeometry`. The GPU intent,
submission order and shader contracts remain those of the existing renderer.

## Desktop platforms

```powershell
python scripts/build.py --preset win-vulkan-debug configure
python scripts/build.py --preset win-vulkan-debug editor
& ./build/win-vulkan-debug/arp_editor.exe
```

The same `editor` build also produces `arp_editor_cli.exe`. The CLI is an
ArpScript-driven REST client: C++ starts the ScriptVM and network runtime, while
`cli/arp_editor_cli.arp` parses commands, creates the JSON envelope, adds the
Bearer header and prints the REST response. It never loads or calls editor
domain objects directly.

Default window: 1440 x 810. The reference design canvas is 1920 x 1080; rendering
and native pointer coordinates scale with the current client size. Windows,
macOS and Linux use their installed sans, monospace and CJK fonts. The executable
locates authoring assets at the build's `ARP_SOURCE_DIR`, so this is a
source-checkout application, not a portable installer.

Desktop and Web configurations include ARP Editor by default through
`ARP_BUILD_EDITOR=ON`, independently of example or smoke executable targets.
`python scripts/build.py engine` and `all` include the standard editor; `editor`
builds it directly. For engine-only consumers, configure `-DARP_BUILD_EDITOR=OFF`
and use `engine --no-editor` or `all --no-editor`. Android/iOS default to editor
disabled; explicitly enabling it fails with `ARP-EDITOR-PLATFORM-UNSUPPORTED`.

To browse a real project and open a script at startup:

```powershell
& ./build/win-vulkan-debug/arp_editor.exe --project-root <external-workspace> --open-script content/scripts/wocc_gameplay_character.arp
```

Without `--project-root`, the current directory is the project root. The Filesystem
dock enumerates that root, and double-clicking an `.arp` file opens it in the Script
workspace. Script edits are kept in the document buffer until `Ctrl+S`; the editor
preserves the file's LF/CRLF newline style and rejects paths outside the project root.

Project resources are loaded only from package paths listed in `project.arp.yaml`:

```yaml
format: ArpEditorProject
version: 1
assetPackages:
  - Assets/Base.arpkg
  - Assets/Characters.arpkg
```

Each path names an `ArpInternalAssetPackage/v1` directory. List packages in dependency
order, with dependencies before packages that reference them. A missing project manifest
opens with an empty resource set and a diagnostic; the editor does not scan directories
for packages. Invalid manifests, duplicate paths, package load failures, or paths that
resolve outside their project/package root are rejected before replacing the active
resource manager.

## Remote control

Remote access is disabled unless `--remote-port` is provided. It binds to
`127.0.0.1` by default and requires `ARP_EDITOR_REMOTE_TOKEN` (32 to 256 bytes).
`--remote-bind-address` accepts a numeric IPv4 address only when used with
`--remote-port`, but the current HTTP listener rejects every address except
exactly `127.0.0.1`. REST
commands are available at `/api/v1/commands`, discovery at `/api/v1/discovery`,
and MCP Streamable HTTP at `/mcp`. Both transports use the same editor command
dispatcher and bearer token. Current operations cover editor state, allowlisted
workspace/dock/scene selection, bounded search, and script replace/save/undo/redo.

Configure a token in `ARP_EDITOR_REMOTE_TOKEN` and run CLI commands against the
editor's base URL (without `/api/v1`):

```powershell
$env:ARP_EDITOR_REMOTE_TOKEN = '<32..256 byte token>'
& ./build/win-vulkan-debug/arp_editor_cli.exe state
& ./build/win-vulkan-debug/arp_editor_cli.exe search filesystem shaders
& ./build/win-vulkan-debug/arp_editor_cli.exe script.replace 1 'let value = 2'
& ./build/win-vulkan-debug/arp_editor_cli.exe --url http://127.0.0.1:8765 script.save 1
```

`ARP_EDITOR_REMOTE_URL` defaults to `http://127.0.0.1:8765`; `--url` and
`--token` override the environment. `script.replace` accepts the full new text
as one shell argument. CLI requests use the same versioned command endpoint and
authorization contract as REST/MCP clients.

Start the editor with a fresh local token:

```powershell
$bytes = [byte[]]::new(32)
[System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
$env:ARP_EDITOR_REMOTE_TOKEN = [Convert]::ToHexString($bytes)
& ./build/win-vulkan-debug/arp_editor.exe --remote-port 8765
```

The listener accepts at most two connections and two pending requests, limits
request bodies to 16 MiB, and retains up to 128 idempotency results in memory.
Non-loopback access fails closed until the HTTP server has an HTTPS listener and
an explicit server-certificate configuration contract. TLS, CORS and idempotency
persistence are not implemented. All currently exposed commands complete
synchronously; no async task/result endpoint is advertised.

## Interactions

- In 3D, select GameObjects from the scene tree or viewport markers, create or
  delete objects, edit names and Position/Rotation/Scale XYZ values, and
  add/remove Mesh, Camera or Light components.
- Type into scene, file or property filters. Backspace clears a filter; Tab moves focus.
- Browse real folders and `.arp` files under `res://`; use the file list scrollbar or mouse wheel when entries overflow, and double-click an `.arp` file to open it in Script.
- Click Play to launch the WOCC executable found under the project root's build directory. On Windows its live window is embedded in the Game viewport. On macOS and Linux it opens as a separate native window so the OS delivers keyboard and pointer input directly; Stop closes it.
- Scroll long scripts with the editor scrollbar or mouse wheel. Edit scripts with text input, arrows, Home/End, Backspace/Delete, Enter and Tab; use `Ctrl+Z` / `Ctrl+Y` to undo/redo atomic edits, `Ctrl+C` / `Ctrl+X` / `Ctrl+V` to copy/cut/paste, and `Ctrl+S` to save. Drag or Shift+arrow to select text, and double-click to select a word.
- Click resource folders to change the `res://` path and edit property text fields.
- Toggle Box Projection / Interior / Enable Shadows and mask cells.
- Click Update Mode to cycle the three example modes.
- Switch scene, workspace and Inspector / Signals / Groups tabs.
- Toggle bottom docks and open the top menu. Scene save/open commands still
  report that the prototype has no project backend.

Properties are shell-local examples. The Script workspace supports basic text editing and saving with Python-style lexical highlighting for ArpScript; it is not a full language server (there is no completion, diagnostics, or refactoring). Asset import, probe updates, movable docks and scene persistence remain unimplemented. Game preview currently uses the explicit WOCC prototype launcher rather than a general project launch contract.
The 3D workspace hierarchy, object selection, built-in Mesh/Camera/Light
component attachment, and Position/Rotation/Scale edits use the shared editor
command dispatcher. The viewport currently previews authored object positions
as a projected editor grid and markers; it is not yet an asset-backed scene
renderer.

## Build and check

### Windows executable icon

The Windows `arp_editor.exe` embeds the approved blue folded-prism ARP
icon from `resources/arp_editor.ico` as `RT_GROUP_ICON` resource 101. The ICO
contains 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 pixel images, generated from
`resources/arp_editor.png` with Pillow. `resources/arp_editor.icon.yaml` records
the source hashes, color/alpha contract and icon sizes. The lower open notch is
part of the approved artwork and must be preserved.

CMake compiles the icon using the Windows resource compiler (MinGW `windres`
on Windows) and tracks the ICO as a resource-object dependency. Rebuilding the
editor therefore embeds the icon without requiring a loose ICO beside the EXE.
The resource uses the shared `WindowsApplicationIcon/1` binding (group ID 101).
The Win32 window class loads both small and large icons from this group, so the
shell, title bar and taskbar/Alt-Tab use the same artwork. Sizes come from Windows
system metrics; shared handles remain owned by the executable module. A missing
group selects the platform default, while an unloadable declared group fails
window initialization. This does not replace the editor's in-canvas UI artwork.

Configure a desktop Vulkan build on Windows or Linux, or a Metal build on macOS,
then build the editor and its interaction tests:

```powershell
cmake --preset win-vulkan-debug
cmake --build --preset win-vulkan-debug --target arp_editor arp_editor_tests arp_editor_support_tests arp_platform_window_icon_tests -j 8
ctest --test-dir build/win-vulkan-debug -R '^(arp_editor|arp_platform_window_icon)' --output-on-failure
```

`--smoke` draws one hidden native frame. `--smoke --project-root <dir> --open-script <relative.arp>` also exercises the script view. `--smoke --after` applies a deterministic
ordered input sequence before drawing: select ReflectionProbe1, enable Interior,
cycle Update Mode, change Intensity from 1.0 to 1.5, and render Signals, Groups,
Inspector and Output states.

For reproducible assets, pass the original 1920 x 1080 reference image to
`tools/generate_reference_assets.py` (Pillow required). It regenerates the independent
YAML chrome, drawn icon atlas and explicit center crop. Runtime image decoding
uses the existing Arp asset decoder; glyphs use the unified UI atlas.

Analyze native captures using the shared RenderDoc replay helpers:

```powershell
$env:ARP_EDITOR_EVIDENCE = './build/arp-editor-evidence/refactor/final'
$env:ARP_EDITOR_TOOLS = './tests/tools'
& '<qrenderdoc-executable>' --python ./editor/tools/analyze_capture.py
```

The directory must contain `before*.rdc` and `after*.rdc`, with optional
`script*.rdc`. `ARP_EDITOR_BASELINE_BEFORE`, `ARP_EDITOR_BASELINE_AFTER` and
`ARP_EDITOR_BASELINE_SCRIPT` optionally specify original captures for exact
pixel/draw parity checks; keep project root and input scenario identical.

## Validation

Build the editor through `scripts/build.py` and run its registered CTest targets
from the configured build directory. Hidden-window smoke tests are available for
automated startup and interaction-path validation. Visual or backend readiness
must be established with current target-specific test and capture evidence; this
guide does not assert a release status.
The event-driven host waits for completion after each redraw, so this application
does not claim a production editor's rendering performance.

Arp UI also now exposes `WidgetDesc::acceptsPointerInput` and
`Widget::setAcceptsPointerInput()`: decorative labels/images remain visible while
pointer input reaches the control behind them. The default remains enabled for
compatibility with existing UI documents.

## Atlas layout contract

`tools/generate_reference_assets.py` owns atlas packing. Sprite content is normalized to
24 x 24 pixels, trimmed to its visible alpha bounds, aspect-preserved and centered
inside a 32 x 32 cell with a 4-pixel transparent gutter. `resources/reference.yaml` records
every source rectangle; the generated `ui/editor_atlas_layout.h` exposes canonical
runtime regions. Do not hardcode atlas coordinates in controls. The viewport crop
is isolated from sprite cells, and the generator rejects overlapping cells,
non-transparent gutters, off-center sprites, or capacity overflow. Resizable
rounded backgrounds use separate destination offsets and fixed atlas source offsets
for nine-slice rendering.
