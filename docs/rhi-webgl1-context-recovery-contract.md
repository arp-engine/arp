# WebGlContextRecovery/1

L2 intent is explicit cold recovery after native context loss. Ready -> Lost
invalidates the original context epoch immediately, including loss first
observed by a public entry before its DOM callback. The native restored callback
only moves Lost -> RestorePending. It does not authorize execution. A caller
must opt in while Ready and declare maxRestorations; enable is single use.
RestorePending -> Rebuilding consumes one attempt. Requery success yields Ready;
failure yields terminal Failed with its original diagnostic. No completion is
reported for accepted or lost work. Duplicate/unexpected callbacks do not revive
epochs or reset the restoration budget.

| Backend | Lowering |
|---|---|
| Null | Validator only; no browser restoration authorization |
| Vulkan | DiagnosticFailure for this contract; original device/surface path remains |
| Metal | DiagnosticFailure for this contract; original device/drawable path remains |
| WebGPU | DiagnosticFailure for this contract; original device/surface path remains |
| WebGL | Explicit Prototype: original browser events + full native requery + new resource domain |

The runtime owns a copy of its initial selector, context attributes, extension
opt-ins, color space and metadata limits. Recovery reuses that exact intent and
requeries identity, limits, precision, extensions, actual context attributes,
color space and format probes. No old query bit is copied into the new profile.
Profile publication is atomic via a new quota-owned immutable snapshot. Failed
query/allocation leaves execution closed. The context quota pays for retained
probe/profile copies and the shared runtime owner; no borrowed reload data is
silently retained and no unbounded attempt history is allocated.

Each original resource domain freezes its context epoch. Recovery creates a
new domain/queue/registry with independent explicit metadata/upload quotas,
reserved before native requery. It can be published only by the original old
domain, once. Old resources, pipelines, descriptors, framebuffers, canvas and
commands remain stale even after the runtime returns Ready. Their destructors
retire CPU owners but never delete stale numeric GL handles in the new epoch.
All factories/record/submit operations continue to check the original domain.
No old cache entry or resource is adopted into the new registry.

Rebuilding data and pipelines is explicit: caller supplies the original stable
Slang publication/reload recipe, creates new layout/resources/pipelines and
uploads initialized contents under the original bounded contracts. Nothing
guesses a replacement texture, mesh, shader or resource identity. Missing recipe
or initialization rejects before execution. Restore does not automatically
rebuild the engine/renderer; production RenderSystem integration remains P4.

Acceptance requires quick request/five-backend negatives and real browser loss,
restore, old-owner rejection, new original pipeline execution, two generations,
quota/attempt exhaustion and exact shutdown ownership. Synthetic WEBGL_lose_context
proves synthetic loss only. Cross-browser/mobile/real GPU reset and complete P3
remain separate gates. Visible recovery uses RenderDoc capture/replay and AI PNG
inspection. No driver physical-memory or GPU completion claim is made.

Accepted 2026-10-17: P3-03/05/06, Contract Complete / Prototype Complete,
Chrome/ANGLE D3D11 bounded Backend Validated. Root build:
`build/webgl1-evidence/p3/recovery-root-final-2/20261017T140506Z-89797513/build-manifest.yaml`.
Final acceptance: `build/webgl1-evidence/p3/recovery-native-final-3/checkpoint.yaml`.
Real browser normal/budget and native-requery-error modes pass, 16 RAF callbacks
per workload with eight leases on each side of recovery. Native captures cover
26 pixel-verified draws across 24 frames, including indexed/non-indexed geometry,
offscreen sampling and both alternating canvas outputs; zero replay errors.
AI inspected all four PNGs. Old domain retains its original immutable profile
under the original context quota, while the new domain publishes the new snapshot.
Validation-only Asyncify stack is 256 KiB, C stack 1 MiB with overflow checks;
fixed Wasm memory remains 64 MiB. No backend async stack policy is introduced.
P3 resize/hidden/pause, remaining budgets/lifetimes/extensions/telemetry/shutdown,
real reset/mobile/mini-program platforms and production promotion remain open.
