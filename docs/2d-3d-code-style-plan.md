# 2D / 3D code formatting

Scope: all C++ headers and implementation files in `src/2d` and `src/3d`.
Use the shared root `.clang-format` rules and clang-format 21.1.8.
Preserve existing uncommitted edits and public/runtime semantics.

- [x] F0: Inspect RHI rules and tooling; snapshot current sources under `build/scene-formatting/baseline/`.
- [x] F1: Format both modules, verify idempotence, unchanged code tokens/literals and conditional include membership.
- [x] F2: Rebuild with MinGW Release and run existing focused unit/contract and smoke tests.

Status: Contract Complete for this formatting change; F0/F1/F2 complete. No new backend or platform readiness claim.
No shader or visual behavior changes; no pressure tests.

F1: 46 files checked; 42 reformatted; token/literal/conditional-include audit and formatter idempotence passed. Evidence: `build/scene-formatting/format-audit.txt`. The shared formatter rules were subsequently consolidated at repository root.

F2: MinGW Release successfully rebuilt `arp_2d`, `arp_3d`, 2D resource/runtime tests, ScriptComponent dual-mode tests and scene runtime smoke. CTest 11/11 passed (3.15 seconds), including 2D resource/services/codec/world/runtime contracts, component Interpreter/AOT and domain tests, Null smoke, and combined scene runtime smoke. Evidence: `build/scene-formatting/build.log` / `tests.log`. Final token/literal/conditional-include and idempotence checks passed again for all 46 files; `git diff --check` passed for both modules. Existing unrelated work was preserved. No new Backend Validated, Platform Validated or Production Ready claim is made.
