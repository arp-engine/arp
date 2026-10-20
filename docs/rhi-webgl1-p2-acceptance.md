# P2 single-frame acceptance

Accepted 2026-10-17 at source commit `52bbcfd5`. This audit uses the original
section 7 exit: a bounded single-frame backend and an independent Wasm smoke.
It does not require P3 multi-frame recovery or P4 production RenderSystem.

| Main task | Implemented original path and acceptance |
|---|---|
| P2-01 | WebGlRuntime original context/query/thread/epoch ownership; WebGlDeviceComposition/1 owns resources, no Null delegate; host factory rejects |
| P2-02 | Owned buffer, RGBA8 Texture2D, exact metadata view, sampler; incompatible format/range/usage rejects |
| P2-03 | Explicit format/type/alignment/pitch/mip/initialization upload contract; packed depth/stencil exact 24/8 query |
| P2-04 | Original Slang artifact identity, compile/link hash/log; prebuilt full raster/depth/stencil/blend state and pipeline switching |
| P2-05 | Original descriptor owners/revisions, immutable per-draw bindings/constants; missing/conflicting/out-of-range bindings reject before draw |
| P2-06 | CommandEncoderBase, bounded PreparedRenderCommands/1-3, original state journal, retained resources and one-use finish |
| P2-07 | Original graphics queue, complete preflight before issue; partial native failure retains accepted-work outcome and closes domain |
| P2-08 | Real draw/drawIndexed, exact vertex/index range checks, independent draw constant snapshots; unsupported semantics reject |
| P2-09 | Original complete FBO, color + packed depth/stencil, explicit Clear/Load/Store/viewport/scissor and write-mask reset |
| P2-10 | Original zero-FBO canvas payload and distinct image, exact single-use acquire/present/discard; sampled view rejects |
| P2-11 | Queried opaque/non-premultiplied/depthless/stencil-free/non-MSAA/non-preserved sRGB policy; browser handoff never completion |
| P2-12 | Each canvas pass requires Clear/Store; implicit preserved-content Load rejects |
| P2-13 | Root arp_rhi_webgl/core/shader/L2 component libraries and build.py webgl1-runtime; real JS/Wasm/data/map, WebGL1-only flags, no foreign backend linkage |

Final native evidence:
`build/webgl1-evidence/p2/canvas-native-final-1/checkpoint.yaml`, its capture
directory (4 captures, 16 pixel-verified draws including offscreen and canvas,
zero replay errors), and
`build/webgl1-evidence/p2/root-product-final-3/20261017T113336Z-1730e504/build-manifest.yaml`.
The earlier `device-final-1/manifest.yaml` and `device-native-final-1/capture/`
cover 36 verified draws and the full multi-pass Device chain. Red fixtures and
failed build/capture attempts remain in their evidence directories.

This turn rechecked 7,237 non-document inputs against the root manifest without
drift, passed all six focused runtime/resource/profile/production-rejection/
backend-dispatch/Null hidden-frame tests, and AI inspected the existing final
capture's prepared.png and canvas.png. Previous same-source acceptance includes
8/8 host/backend regressions, 4/4 browser smokes, 417 source contracts, 26 build
selection and 2 runtime-build admission tests; see the checkpoint for scope.

Status: Contract Complete / Prototype Complete; the original Chrome WebGL1/ANGLE
chain is narrowly Backend Validated. Explicit versioned Prototype requests are
mandatory. DeviceCapabilities deliberately denies generic production graphics
and optional native features; it is not an execution token for this component.
Compatibility-only runtime profiles remain non-executable. Ordinary production
factory, RenderSystem, NativeSync and foreign generic planners stay closed.
P4 must introduce its own authenticated admission before opening those paths.
No mini-program Platform Validated or Production Ready claim is made.

P2 closes 13 original tasks together (total 43/74). P3 remains 0/11 until its
own complete lifecycle/reuse/recovery exits are met. Full engine linkage, mobile
platforms, other browsers and repeated-frame performance are not inferred here.
