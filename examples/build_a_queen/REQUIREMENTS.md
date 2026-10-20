# Build A Queen Replica Requirements

Research snapshot: 2026-10-19. This is a gameplay-inspired implementation; it does not reuse the original game's proprietary assets or claim to reproduce unpublished scoring internals.

## Evidence

- Official Google Play listing: fast fashion runner; swipe left/right; collect matching outfits, hairstyles, and makeup; reach the finish and win a catwalk battle; progress unlocks clothes and challenges. It labels the game single-player, stylized, and offline.
- Official Apple App Store listing: choose a model, customize hair, makeup, outfits, and accessories; show the result in fashion battles; progress unlocks clothes and challenges.
- Official Nintendo listing: body-shaping choices, hair/makeup/outfits/accessories, rival/boyfriend fashion comparison, runway battles, rewards, and skins/dances.
- Third-party gameplay descriptions add a target-look preview, collecting matching items, avoiding clashing items, and comparing with a rival. Treat these as observations, not official specifications.
- User-provided Jopi Level 1 gameplay screenshot (2026-10-19) is the primary visual/gameplay reference for this replica. It shows a full 3D target avatar in a right-side card, an auto-running 3D character, a forward runway, outfit/shoe pickups, and a hold-and-drag horizontal movement tutorial. This direct screenshot evidence takes priority over generic aggregator instructions and unrelated store-version features.
- Live Jopi page inspection reached a Playhop-embedded runtime, but an interstitial ad covered its canvas; no additional gameplay mechanics were treated as observed from that session.
- Bundled character models are attributed to Quaternius and marked CC0 1.0 by the included license text. That text names "Ultimate Modular Males" while the downloaded source package is "Ultimate Modular Women"; preserve this provenance discrepancy when redistributing the assets.

Research URLs checked 2026-10-19:

- Google Play: https://play.google.com/store/apps/details?id=com.Polystation.BuildABabe
- Apple App Store: https://apps.apple.com/us/app/build-a-queen/id1660583025
- Nintendo: https://www.nintendo.com/us/store/products/build-a-queen-switch/

## Requirement Graph

```text
Offline single-player session
  -> level selection / unlocked progression
  -> target look + theme preview
  -> automatic forward movement on a 3D runway
       -> left/right lane control (keyboard and pointer drag)
       -> 3D outfit, hair, makeup and accessory pickups
       -> wrong-style replacement and 3D obstacle avoidance
  -> finish-line runway show
       -> player and rival appear as 3D characters
       -> compare collected look with target and rival
       -> show score, win/loss, reward and next-level unlock
  -> retry / next level / return to level selection

Presentation
  -> 3D level, runway, environment, player, rival, pickups, obstacles
       -> procedural sky with a visible sun and horizon gradient
       -> warm directional key light and ambient fill on mesh normals
       -> directional shadow map cast by static meshes and animated character poses
  -> 2D HUD with level label and sound control
       -> 3D target-avatar card labeled "MATCH THIS OUTFIT"
       -> compact progress and result indicators
  -> fixed camera + deterministic capture state for visual verification

Reference-specific loop (user-provided Level 1 screenshot)
  -> show target look before/during the run
  -> auto-run toward a visible finish; hold-and-drag horizontally to steer
  -> collect clothing/shoe items by body slot
  -> immediately equip the collected slot on the animated player model
  -> compare the final player look against the 3D target look

Runtime boundaries
  -> game rules are engine-independent and unit-testable
  -> 3D scene extraction creates bounded value-semantic draw data
  -> RHI lowering follows explicit L2 intent; no backend handles in game rules
  -> Slang is the only shader source; generated SPIR-V/MSL/WGSL/reflection
  -> platform host owns window, input, frame lifecycle and RenderDoc capture
```

## Replication Decisions

- Use eight authored prototype levels with three lanes, five style slots, a target outfit, rival score, and deterministic course layout. This is prototype content, not a claim about the original level count.
- Each pickup occupies one lane and one style slot. A matching pickup scores better; a conflicting pickup replaces that slot and reduces final look accuracy. Obstacle contact incurs a penalty; passing another lane counts as an avoidance.
- Battle has a visible timed runway phase before the result screen. A versioned prototype score compares outfit matches, collected pieces, obstacle hits, and the authored rival score. It is deliberately transparent and tested; exact original scoring is not public.
- Keyboard A/D or arrows and mouse/touch drag move between lanes. No ads, purchases, online account, or telemetry are needed for the offline example.
- World geometry and characters must be 3D meshes rendered with a perspective camera and depth testing. HUD and target-look card may be 2D.
- Player and rival use the bundled Quaternius Modular Women glTF-derived GLB models. The versioned YAML manifest binds every imported material name to a game role; missing files, materials, geometry, or bindings fail with a diagnostic.
- Runway structural geometry remains deterministic mesh geometry. Outfit pickups are extracted from the imported character material submeshes; obstacles use those authored 3D submeshes with a warning color.
- The user-provided screenshot supersedes the generic five-token target HUD and rival-fashion-battle assumptions as the visual baseline. Preserve a full 3D target look card, level and sound affordances, and horizontal drag steering. The screenshot does not establish exact scoring, damage, gate, or post-run rules; keep those labeled Prototype until verified.

## Acceptance Map

| Area | Acceptance |
| --- | --- |
| Rules | Start only unlocked level; lane clamps; invalid delta rejected; pickups resolve once; matching/mismatch, obstacle hit/avoid, battle timer, result, retry and unlock are tested. |
| 3D scene | Perspective camera; depth attachment and depth test/write; visible runway, animated player, outfit/shoe pickups and environment; no 2D projection of world geometry. Target look is a 3D avatar preview. Each collected slot visibly changes its authored clothing geometry on the player. |
| Lighting | Slang procedural sky, directional diffuse/specular lighting, and a sampled D32 shadow map populated by static geometry and current skinned poses. |
| RHI | Explicit descriptor/pipeline/render-pass state and upload bounds. Backend matrix records Native/Equivalent/Degraded/DiagnosticFailure for Null, Vulkan, Metal and WebGPU. |
| Shader | Slang source only, explicit row-major compilation for all targets, generated SPIR-V/MSL/WGSL/reflection, source/layout/push-size contract tests. |
| HUD/input | Level label, sound affordance, full target outfit card, compact progress/results; automatic forward motion and continuous hold-and-drag lateral steering, with keyboard support for desktop testing. |
| Runtime | Windows MinGW build, Null hidden-frame validator, hidden-window Vulkan smoke, fixed-state RenderDoc capture and machine-readable visual inspection. |
| Status | Report Contract Complete, Prototype Complete, Backend Validated, Platform Validated, Production Ready independently; do not promote from a single backend smoke. |

## Stage Tracking

- Stage 1, research and requirement graph: Complete.
- Stage 2, deterministic game rules and fast tests: Complete (MinGW Debug test passed).
- Stage 3, Wocc-informed 3D frame submission and 3D assets: Prototype complete; player and rival are imported GLB meshes, pickups/obstacles use material-bound character submeshes, and the runway uses perspective rendering with depth testing. A fast GLB importer contract test validates both bundled models. Wocc remains an architecture reference, not a runtime dependency.
- Stage 4, 2D HUD and input host: Prototype complete; keyboard and pointer drag are implemented. The HUD has been migrated from hand-placed sprite draws to ARP Canvas -> RenderExtractor -> DrawPlan -> GuiRenderer commands with the unified text atlas. The target-look card is a framework panel/title over a separately rendered 3D avatar preview; sound interaction and broader visual polish remain incomplete.
- Stage 5, MinGW, hidden-window and RenderDoc verification: Complete for the current Windows Vulkan prototype. All six Queen tests pass, including Null hidden-frame, Vulkan hidden-window, game rules, asset loading, and animation/shader contracts. The current running-scene capture is `build/win-vulkan-debug/captures/build_a_queen_front_facing_capture_capture.rdc`; its inspected image confirms the runway, characters, and framework HUD render together. The target card now contains a live 3D avatar preview.

- Stage 6, skeletal animation: Complete for the Windows Vulkan prototype. Both bundled GLBs have a 62-joint skin and 24 clips (`Run`, `Walk`, `Idle`, `Wave`, among others); `SkinnedMeshPlayer` evaluates the phase-selected clip and the GPU skinning shader consumes the uploaded palette. Locomotion and idle clips wrap at their authored duration so menu wait time cannot pin the player to a terminal pose. The asset contract test verifies pose changes and equivalent poses across a loop boundary. RenderDoc capture `build/win-vulkan-debug/captures/build_a_queen_looping_run_capture_capture.rdc` was inspected; its thumbnail confirms the running character and real shadow pass. These are existing authored animated assets, not Tripo- or Mixamo-generated content.
- Stage 7, reference-matched HUD, steering, and per-slot clothing geometry: Prototype Complete for lane steering, per-slot material variation, and the live 3D target avatar preview. Independent modular clothing geometry remains incomplete. Player steering still snaps among three lanes. The animated player resolves Hair/Top/Bottom/Shoes roles through the character manifest and applies each equipped style only to that role's skinned submesh. RenderDoc capture `build/win-vulkan-debug/captures/build_a_queen_per_slot_appearance_capture.rdc` was inspected with a Gold bottom pickup, showing the Casual body remains and only its lower garment changes color. Current assets lack modular style variants and a player accessory mesh.
- Stage 8, lighting, shadows, and sky: Prototype Complete; Backend Validated on Vulkan; Platform Validated on Windows. Static runway/pickup geometry and the current GPU-skinned character pose write a D32 shadow map in a depth-only pass; the visible scene samples it with 3x3 PCF. Depth sampling uses the explicit `ARP_SAMPLED_DEPTH` source contract, and the old contact ellipses are removed. All seven Queen/shader CTests pass. RenderDoc capture `build/win-vulkan-debug/captures/build_a_queen_realtime_shadow_capture.rdc` reports 2 shadow caster draws (including 1 skinned), 3 visible scene draws, 138,354 non-clear shadow depth samples in `[0.3785379, 0.5837711]`, and confirms the visible pass samples the shadow attachment. Its inspected image is `build/win-vulkan-debug/captures/build_a_queen_realtime_shadow_capture.bmp`. Metal, WebGPU, mobile, and Production Ready remain unvalidated.

Current readiness: the existing game-rule contract is Contract Complete; the current gameplay and 3D/HUD implementation, including the target/final avatar preview, are Prototype Complete; the captured native rendering path is Backend Validated on Vulkan; Windows Vulkan is Platform Validated by the hidden-window smoke and RenderDoc capture. Continuous horizontal steering and independent clothing geometry remain incomplete. Metal, WebGPU, mobile platforms, touch input, and Production Ready are not validated or claimed.

- Stage 9, pickup contact/despawn and final-look presentation: Prototype Complete; Backend Validated on Vulkan; Platform Validated on Windows. The pickup mesh reaches the player Z plane at its authored collision distance, resolved pickups are omitted from scene extraction, and result frames show the equipped animated player beside the rival. The HUD presents the actual equipped style as a live 3D character rather than part-name text. The game-rule test verifies a pickup remains unresolved before contact and resolves once crossed; all six Queen tests pass. The mid-run RenderDoc frame at 22% (`build/win-vulkan-debug/captures/build_a_queen_pickup_despawn_capture.rdc`, inspected image `build/win-vulkan-debug/captures/build_a_queen_pickup_despawn_thumb.png`) shows the center-lane top pickup gone while same-distance pickups in other lanes remain. Hidden-window autoplay completed at `RESULT`; the final-look RenderDoc capture and analysis are recorded under `build/win-vulkan-debug/captures/build_a_queen_final_look_3d_capture_capture.*`. This is a Vulkan/Windows prototype validation, not Production Ready.

- Stage 10, 3D FINAL LOOK card and result-facing pose: Prototype Complete; Backend Validated on Vulkan; Platform Validated on Windows. The HUD removes the outfit-slot text list and keeps the `FINAL LOOK` title over a translucent card. The independently framed skinned avatar faces the preview camera, while the result-stage player is rotated to face the gameplay camera with matching shadow orientation. During the run the card shows the level target; at result it shows the player's equipped appearance. MinGW build and all six Queen tests passed. RenderDoc analysis reports three visible skinned-character draws (player, rival, card preview); the inspected result image `build/win-vulkan-debug/captures/build_a_queen_front_facing_capture_capture.bmp` confirms both the foreground player and card preview face their respective cameras. This is a Vulkan/Windows prototype validation, not Production Ready.

## Wocc Rendering Reference

The implementation reference is `<external-workspace>/app/render_session.cpp`, `app/render_session.h`, `app/wocc_render_frame_submission.h`, and `app/wocc_pbr_pipeline.h`. Reuse the separation between CPU scene extraction, value-semantic frame submission, renderer-owned GPU mesh buffers, and the PBR pass. Queen remains self-contained under `examples/build_a_queen` and uses ARP RHI/renderer APIs rather than a runtime dependency on the Wocc checkout.

## UI Framework Integration

- HUD widgets are authored in `HudRenderer::createWidgets` using `ui::Canvas` and extracted through the framework's render queue and draw-plan lowerer.
- `GuiRenderer` records the resolved UI commands into the same acquired frame after the 3D pass. Font glyphs are supplied by `UnifiedUiAtlasRuntime`; game code does not construct HUD sprite draw items.
- The card title is UI framework text; its target/final avatar is rendered as a skinned 3D scene draw inside the card viewport.
