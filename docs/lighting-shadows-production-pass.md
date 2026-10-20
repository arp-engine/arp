# Lighting And Shadows Production Pass

Status: Backend Validated locally for the Character Viewer stylized real-shadow path on Windows/MinGW Vulkan hidden and visible smoke. Platform Validated and Production Ready are not claimed.

The former `src/renderer/lighting_shadow.h` helper has been retired during
renderer module governance cleanup because it had no production caller outside
its own obsolete contract test. Active stylized shadow policy is owned by
sample/game modules such as `samples/stylized/stylized_shadows.h`,
`samples/stylized/stylized_shadow_draw_plan.h`, and
`games/pika/pika_standard/stylized_shadows.h`.

Those active contracts cover:

- bounded visible lights per profile
- CSM cascade count and resolution
- character shadow atlas resolution
- contact shadow enablement
- static shadow cache policy flag

Renderer-level shadow work should enter through explicit RHI frame, graph,
shader, descriptor, and telemetry contracts rather than reviving a detached
renderer helper.

Character Viewer now enables real light-driven stylized shadows by default through
explicit preview CSM setup, shadow atlas metadata, light-space sampling data, and
separate PiKa planar-shadow comparison controls. Local validation on 2026-05-20
passed the focused shader/render graph/Character Viewer test matrix, source
native-handle scans, and hidden plus visible two-frame Vulkan smoke. Browser
WebGPU, macOS/iOS Metal, Android Vulkan lifecycle, hardware driver validation,
and visual parity review remain external validation gaps before any Platform
Validated or Production Ready status.
