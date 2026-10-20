# Queen WebGL game acceptance

The acceptance unit is the actual Queen game, from menu/input through running,
pickup contact/despawn, per-slot appearance, battle and front-facing 3D final look.
An RHI probe page does not satisfy this plan.

## Stages

1. **Contract Complete — portable content.** Reuse the existing GLB import,
   animation clips, material-role manifest, gameplay and UI framework. Extract
   shared scene/HUD authoring from their native execution strategies. Define a
   bounded, explicit Prototype browser product. Keep a fast content/game test
   and the existing Null smoke.
   Shared GLB/skin preparation and 720-frame gameplay smoke passed (2/2).
   Shared HUD compiles for Wasm; native scene/HUD objects also compile.
2. **Prototype Complete — real WebGL rendering.** Use original ARP ordered graphics,
   generated Slang ES100 programs and queried WebGL1 capabilities. CPU linear
   blend skinning is an explicitly selected vertex preparation strategy, not a
   substitute GPU command implementation. Preserve 3D depth, sampled real-shape
   shadow passes, sky and UI framework draws. Test pose/layout/negative gates.
   Slang/matrix, asset, lighting and HUD/game contract tests passed (5/5).
3. **Prototype Complete — browser product.** Add an isolated Emscripten game target and
   keyboard/pointer/touch host, preload the exact assets and generated programs.
   Build and run in a real browser with WebGPU unavailable.
   Release Wasm builds reproducibly through `scripts/build_queen_webgl.py`.
4. **Backend Validated / Platform Validated — scoped gameplay acceptance.** Exercise menu, steering,
   animation, contact/despawn, appearance and result; inspect RenderDoc/ANGLE
   capture output and retain machine-readable receipts and screenshots. Serve
   the actual game locally and give the user its URL. Completed on Windows Chrome
   WebGL1 through ANGLE D3D11, with WebGPU unavailable. Full winning level,
   keyboard/mouse/touch, contact/despawn, five independent appearance slots,
   next level, restart and minimize/restore all passed on the final bundle.

## Platform integration acceptance

The game now uses the engine `platform::createRenderSystem`, public ordered
pipeline loading and platform-owned surface lifecycle. Backend-specific assembly
and visibility subscription were removed from the game host. The generic WebGL
production factory remains closed without explicit Prototype opt-in.

All integration stages are complete at their scoped readiness level: Contract
Complete (profile/five-backend failure gates), Prototype Complete (factory and
game migration), Backend Validated and Platform Validated (Windows Chrome ANGLE
D3D11). The independent smoke passed six GPU frames, resize/zero-size pause,
explicit pause/resume and listener teardown. Full gameplay acceptance passed
again on the migrated Release bundle, including minimize/restore.

Current evidence is under `build/queen-webgl1/platform-renderdoc/`:

- `acceptance.yaml`: combined index and verified current bundle/receipt hashes.
- `gameplay.json`: score 88, five collected parts, input/lifecycle tests and
  `platformIntegration.engineRenderSystem: true`.
- `queen_frame205.rdc` / `queen_frame372.rdc`: running and front-facing result.
- `capture-report.json` / `replay-report.json`: successful capture/replay,
  matching shadow/world geometry, real depth, 3D preview and framework UI.
  Visual inspection confirmed `replayed-0.bmp` and `replayed-1.bmp`.
- `../platform-smoke-final/report.json`: independent public platform smoke.
- `../platform-native-contracts.log` / `../platform-native-smoke.log`: three
  MinGW platform contracts and Queen Null smoke passed. Native platform and
  Queen executable also build successfully.

See [engine integration contract](../../docs/webgl-platform-integration.md).

## Earlier game acceptance evidence

The following receipts predate platform migration and are retained as historical
game evidence. Use the platform integration index above for the current bundle.
All paths below are relative to the repository root:

- `build/queen-webgl1/validated-capture/acceptance.yaml`: combined gameplay and
  visual acceptance index with current bundle and evidence SHA-256 hashes.
- `build/queen-webgl1/queen-build.yaml`: reproducible Release bundle/program hashes.
- `build/queen-webgl1/validated-capture/gameplay.json`: full game receipt and
  matching before/after bundle hashes; winning score 88 with five collected parts.
- `build/queen-webgl1/validated-capture/capture-report.json`: successful captures.
- `build/queen-webgl1/validated-capture/queen_frame201.rdc`: running character,
  real-shape shadow, sky, 3D target and framework HUD (117 GUI draws).
- `build/queen-webgl1/validated-capture/queen_frame408.rdc`: front-facing result
  and 3D final look (118 GUI draws).
- `build/queen-webgl1/validated-capture/replay-report.json`: both frames replayed;
  matching world/shadow geometry, real depth attachments, finite post-VS and no
  medium/high replay errors. Visual inspection confirmed upright `replayed-0.bmp` and
  `replayed-1.bmp` alongside the browser screenshots.
- `build/queen-webgl1/final-contracts-tests.log`: five MinGW contract tests passed;
  standalone shared-content unit/720-frame smoke also passed (2/2).

Native regression boundary: the earlier unrelated bindless compile blocker is
resolved in the current checkout. MinGW platform/Queen builds and the native
Null smoke now pass. Native Vulkan visual acceptance was not repeated for this
WebGL platform integration.

## Contract and limits

The browser product explicitly opts into existing versioned ordered graphics
Prototype contracts. It does not remove the generic production-factory gate or
claim Production Ready. All capacities are named product budgets or derived
from imported content. Unknown asset roles, missing clips, shader interface
mismatch, capability/extension failure and exhausted budgets fail diagnostically.
No WebGPU, native handles or direct GL calls belong in the game renderer.

| Area | Null | Vulkan | Metal | WebGPU | WebGL1 |
| --- | --- | --- | --- | --- | --- |
| Shared rules/content/CPU pose | CPU contract | CPU preparation | CPU preparation | CPU preparation | CPU preparation |
| Existing native Queen strategy | Validator only | Existing native path | Unvalidated | Unvalidated | DiagnosticFailure (timeline/D32 strategy) |
| New ordered browser strategy | No execution issuer | No execution issuer | No execution issuer | No execution issuer | Explicit Prototype opt-in, original queried execution |

No NativeSync, GPU completion, physical GPU budget or other-backend validation
claim follows from the browser strategy. WebGL command order and resource CPU
reuse use the existing original ordered execution/lifecycle contracts.

## Readiness

Contract Complete: the explicit Queen Prototype product/content/shader contracts.
Prototype Complete: the playable browser product. Backend Validated: this ordered
WebGL1 game path through Chrome ANGLE D3D11. Platform Validated: this Windows
Chrome build and its full gameplay/lifecycle acceptance. Metal, WebGPU,
Android/iOS and other browser/platform validation are not claimed. Production
Ready: not claimed; the generic production WebGL factory remains gated.
