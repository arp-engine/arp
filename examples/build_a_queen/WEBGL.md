# Build a Queen — WebGL game

The browser build runs the existing ARP game and GLB character assets. It uses
the original ordered WebGL1 execution path, with an explicit Prototype opt-in.
It is independent of WebGPU and does not open the generic production WebGL gate.
The game now enters through `platform::createRenderSystem` with the explicit
`OrderedRenderSystem/1` profile. Platform owns backend assembly, shader artifact
loading and surface lifecycle; game code uses public platform/L2 APIs.

## Build and play

From `.`:

```powershell
python -X utf8 scripts/build_queen_webgl.py
python -m http.server 8092 --bind 127.0.0.1 --directory ./build/queen-webgl1
```

Open <http://127.0.0.1:8092/arp_build_a_queen_webgl.html>.
The HTTP server must stay running; opening the HTML through `file://` is unsupported.

Press Enter/Space or tap to start. Use A/D, arrow keys, dragging or touch to
select a lane. Collect the target outfit's individual parts. Tap/Enter after
the result to continue; R returns to the menu.

The builder accepts explicit `--emsdk`, `--ninja`, `--slangc`, `--reflect`,
`--spirv-cross`, `--output` and `--programs` paths. It compiles all four programs
from Slang to SPIR-V/MSL/WGSL/reflection and ES100, then builds the Release Wasm
game. `queen-build.yaml` records the bundle and program hashes. Build completion
does not automatically claim gameplay or visual acceptance.
The builder also produces `arp_webgl_platform_smoke.js` and `platform.html`.

## Rendering contract

- Shared GLB import, material-role metadata, game rules and clip sampling.
- Explicit CPU linear blend skinning, then real GPU 3D vertex/depth rendering.
- Four immutable pipelines and five passes: geometry shadow, sky, lit world,
  front-facing 3D look, framework UI composition.
- `WEBGL_depth_texture` must be queried and explicitly enabled. The shadow map
  stores actual geometry depth in D24S8. ES100 samples its raw normalized depth
  channel, using the existing ordered depth-receiver contract and nine PCF taps.
- The shadow pass also writes a depth visualization to its real RGBA8 attachment;
  this satisfies WebGL1's active color-output contract. Receivers sample D24S8.
- UI uses Canvas/RenderExtractor/DrawPlanner and GuiOrderedRenderer. Text uses
  a cooked bitmap font with explicit glyph/atlas metadata and deterministic errors.
- CPU matrices remain row-major. Generated ES100 applies WebGlShaderCoordinates/1;
  attachment sampling and capture readback use its explicit Y conventions.
- The canvas is 1280x720 with CSS scaling; preview is 300x184 and shadow is
  1024x1024. Wasm has a fixed 512 MiB address-space ceiling. Resource, frame,
  uniform, draw and owner capacities are explicitly bounded. No physical GPU
  memory or native GPU completion guarantee is claimed.
- Visibility changes queue the surface update until the next RAF, after document
  event dispatch. Paused time does not advance the game.

## Validation

The independent engine platform smoke runs with:

```powershell
python tests/contract/webgl_platform_browser.py --directory build/queen-webgl1 --output build/queen-webgl1/platform-smoke-final
```

It tests public factory gates, pipeline loading, six real GPU frames, zero-size
suspension, resize, explicit pause/resume and complete visibility teardown.

`tests/browser_game.py` runs the actual game through keyboard, mouse and touch
event handlers with `navigator.gpu` unavailable. It checks contact timing,
resolved pickups, exact per-slot appearance, a winning full level, 3D result
draw counts, next level, pause/resume and restart. Steering runs on the browser
RAF clock so screenshot/CDP round trips cannot skip pickup intervals. It records
bundle hashes before and after the test.
It also asserts that the game uses the engine RenderSystem with ordered graphics
and platform surface lifecycle, without claiming native GPU completion.

`tests/captured_browser_game.py` runs the same acceptance under RenderDoc.
`tests/renderdoc_game.py` injects a dedicated local Chrome process with its
normal sandbox settings, captures running/result frames, and replays ANGLE's
D3D11 output. It checks real depth targets, matching shadow/world geometry,
finite post-VS positions, a separate 3D preview, framework UI draws and replay
diagnostics. The capture tool receives its dedicated output path through
`ARP_QUEEN_EVIDENCE`; its configuration/receipts are test transport.

See [WEBGL_PLAN.md](WEBGL_PLAN.md) for current evidence and readiness limits.
