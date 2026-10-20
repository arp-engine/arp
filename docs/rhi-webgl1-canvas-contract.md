# WebGlCanvasLease/1

L2 intent is a single acquired browser drawing-buffer lease, rendered through
ordinary L2 render commands and released by present or unsubmitted discard.
This version is explicitly Prototype and single-use. It grants no multiframe,
GPU completion, physical memory accounting or generic production capability.
Null validates only; Vulkan/Metal/WebGPU keep their existing swapchain mappings
and reject this WebGL-only route. No native image count or pacing control is
invented. SwapchainDesc must explicitly select imageCount=0, Auto, Rgba8Unorm,
and the exact queried original drawing-buffer extent and canvas selector.

The original context is the sole owner of default framebuffer zero. Its leased
image has a distinct backend payload type, never a WebGlTexturePayload. It has
no sampled/upload/view/native-texture or application-allocation capability.
Framebuffer zero is a real browser target, not a missing FBO fallback. Original
owner, context epoch, lease serial, extent and phase are checked before issue.
Only one canvas owner may exist for a context. Creation and the single image
wrapper/framebuffer are cold allocated in the original bounded resource quota.
Acquire, acquiredImage transfer, record and issue do not grow storage.

Version 1 requires queried sRGB, opaque, depthless, stencil-free, non-MSAA,
non-premultiplied, non-preserved context attributes. Unsupported attributes
reject; no alpha or color conversion is guessed. Initial acquired image state
is explicitly ColorAttachment. Every canvas pass must Clear/Store; Load or
DontCare rejects. One command may record exactly one pass to that lease.
The complete sealed command validates the original acquired phase before any
GL call. Successful native end marks Submitted. Present consumes Submitted,
sets logical state Present and hands the original buffer to browser scheduling
with a nonblocking glFlush. Presented does not attest compositor display or GPU
completion. Caller-supplied completion fences reject. Discard is legal only
before native submission, because implicit browser composition cannot undo a
submitted drawing-buffer write. Failed native work invalidates the domain and
lease. Retained stale image/command wrappers cannot reacquire or execute.

Double acquire/image transfer/present/discard, wrong owner/serial/extent,
external resize and surface mutation while leased reject deterministically.
This single-use boundary stays closed after release; P3 owns repeated leases,
restoration and lifecycle scheduling. No per-frame creation fallback is added.
Acceptance requires a host contract, Null smoke, real default-FBO sampled draw,
full pixel oracle, phase/ownership negative tests and RenderDoc inspection.

Root runtime product closure is independent of the full engine/editor product.
ARP_WEBGL1_PROTOTYPE_PRODUCT=ON plus ARP_WEB_RHI=webgl1 selects it before any
other platform branch. It builds the real core allocator/file/hash component,
original shader library and original L2/cache validators plus WebGL backend.
The ordinary production selector remains closed until full P2 admission review.
The runnable validation executable reuses the native integration workload and
is named validation, never a game/demo claiming production readiness. Final
link pins MIN/MAX_WEBGL_VERSION=1, fixed Wasm memory, no WebGPU/Dawn/native
backend libraries. Host-generated original artifact input is explicit; runtime
shader compilation is limited to declared cold pipeline creation. The CLI
records exact source/bundle/link-map hashes and the explicit Prototype status.

Validated evidence (2026-10-17):

- Root CLI build: build/webgl1-evidence/p2/root-product-final-3/20261017T113336Z-1730e504/build-manifest.yaml.
- Native capture and review: build/webgl1-evidence/p2/canvas-native-final-1/checkpoint.yaml.
- Four native captures: 16 verified draws, including four offscreen and four
  canvas draws; full pixel oracles, no replay errors; AI inspected all outputs.
- MinGW quick contract, Null and original backend/gate regressions, 417 source
  tests, four browser smokes, 26 selector and two build-admission tests passed.
- Prototype Complete and bounded Chrome/ANGLE Backend Validated only. Ordinary
  production factory/product/capability promotion, complete P2 and target miniapp
  Platform Validated / Production Ready remain open.

Reproduce the root build with explicit tools and original program publication:

```powershell
python -X utf8 scripts/build.py webgl1-runtime `
  --program-directory build/win-vulkan-debug/webgl1-program `
  --cmake "C:/Program Files/CMake/bin/cmake.exe" `
  --ninja C:/mingw64/bin/ninja.exe `
  --toolchain C:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake `
  --browser "C:/Program Files/Google/Chrome/Application/chrome.exe"
```
