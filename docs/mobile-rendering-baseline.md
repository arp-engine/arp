# Mobile Rendering Baseline

Status: Contract Complete.

`src/renderer/mobile_quality.h` defines profile-controlled rendering budgets for
low, medium, high, and desktop targets. Profiles control resolution scale,
render target count, shadow resolution, light count, vegetation density, optional
depth prepass, postprocess cost, and dynamic resolution.

The baseline frame shape is:

- optional depth prepass
- budgeted shadow pass
- opaque stylized scene
- character NPR
- limited transparent/VFX
- lightweight postprocess
- GUI

Desktop may enable heavier postprocess, while mobile profiles keep bandwidth and
render target count bounded.
