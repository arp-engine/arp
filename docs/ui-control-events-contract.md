# UI Control Events/1

Status: Contract Complete. Vulkan Backend Validated and Windows Platform Validated
for the Events/1 verification scene. Not Production Ready across all platforms.

## Plan and evidence

- [x] Phase 1: polymorphic runtime objects, routed event interface, single interaction state owner; fast object contract and headless smoke.
- [x] Phase 2: ordered InputFrame ingress, capture/target/bubble, typed defaults and notifications, input ownership; fast routing contracts and platform hidden-window smoke.
- [x] Phase 3: sourceId factories, deferred mutations/lifecycle, render extraction and examples; factory/lifecycle contracts, YAML smoke, RenderDoc capture and analysis.

Phase 1 evidence: MinGW Debug `arp_ui_control_object_contract_tests` and
`arp_ui_control_object_smoke` both passed (assertions enabled). Derived identity,
allocator-aware destruction, embedded state, mismatch rejection, notification
policy, cancellation and removal verified. Phase 2 evidence: `arp_ui_control_event_routing_tests` and
`arp_ui_control_input_hidden_smoke` passed in isolated MinGW Debug
`build/ui-control-events-debug` (Assimp disabled for this focused UI build).
Native Win32 messages exercised button/slider/text, repeated backspace and focus
loss, including press followed by focus loss in the same platform pump.

Phase 3 evidence (2026-10-08): all 29 selected assertion-enabled MinGW Debug
contracts/regressions/smokes passed. Includes factories, document isolation,
callback removal/creation/reparenting, mutation/widget/input capacity exhaustion,
notification reentrancy, typed controls, YAML resource/package loaders, GUI
forwarding/boundary, input controllers, scene UI, and Null hidden-frame smoke.
The executable YAML example loads, interacts and unloads successfully.
Full test output: `build/ui-control-events-evidence/tests.txt`.
Existing source-only UI tests were registered in CMake. Old fixture paths were
replaced with shared deterministic YAML fixtures. Mixed-domain scene smoke now
checks the 2D sink's foreign-payload rejection and explicitly selects its tile-map
snapshot using the fixture's object ID; no production rendering gate was bypassed.

## Contract

Product event ownership stays in src/ui. GUI is a forwarding/debug facade.
Runtime owns stable-address core::UniquePtr<Control> objects; the UiControl
compatibility view is embedded state, not another runtime. The runtime owns
focus, hover, pressed and pointer capture. IDs do not get reused.

Routed input visits ancestors root-to-parent (Capture), then Target, then
parent-to-root (Bubble). Default actions run afterwards, unless prevented.
Handled records UI consumption, stopPropagation stops routing, preventDefault
cancels defaults; none implies another. Scroll defaults try the closest eligible
control on the route. Semantic notifications are direct and occur once after a
successful commit. Unhandled input is normal, not a contract failure.
`interact()` commands also use the route before their typed default actions.
The deprecated Canvas snapshot route is explicitly named `routeLegacySnapshot()`;
its old `route()` spelling forwards to it. Repository input-routing callers now
use the ordered runtime entry. GUI forwards product UI to the same runtime;
the deprecated GuiContext name is only a DebugGuiContext compatibility alias.

Programmatic value updates are silent unless Notify is requested. Unchanged
values do not notify. Text input accepts canonical UTF-8; backspace removes one
Unicode scalar, not a byte. Composition/grapheme editing is outside Events/1.

InputFrame order is authoritative. Pointer positions must be explicit or come
from previously confirmed positions in the same window. UI/game press ownership
lasts through repeats and release. Physical release always clears held state.
WindowEventBatch::addWindowEvent mirrors focus-loss/minimize/close barriers at
their actual input position; platform producers use this entry. Win32, Cocoa
and DOM key repeats reach InputFrame instead of being discarded. DOM blur also
enqueues cancellation. The existing web text bridge remains Prototype, without
IME composition support.

SourceId factories are scoped to one document load, must match declared control
types, and fail on duplicates, absent IDs, null objects or mismatches. No factory
binding means a standard typed object. YAML remains ui.document/v1.
Both file/text and package loader descriptors accept factories and an optional
`maxWidgetCount`. Zero derives the exact document node count plus Canvas root;
specify a larger explicit count to allow dynamic creation. YAML Label/TextInput
values initialize from their authored text, keeping the object and render view
consistent. Factory registries are used synchronously, not retained by results.

Structural changes during dispatch are deferred; pending removal immediately
invalidates the affected subtree for routing/defaults. Reentrant dispatch fails.
Hide, disable, remove, unload and focus loss cancel interaction state and send
direct lifecycle notifications. Event route and mutation capacity is bounded by
CanvasDesc::maxWidgetCount; input frame budget is explicit. All runtime owned
allocations use core/cpu_allocator.
Hit testing, release-inside checks and render extraction share the resolved
ancestor clip, computed with core::intersectRect. Capture records its mouse
button so another button's release cannot clear it. Reparenting into an inactive
ancestor cancels interaction immediately. Low-level direct Canvas mutation is
a legacy authoring surface; runtime mutations should use the runtime entrypoints.

## Readiness

Null validation is not native evidence. Vulkan, Metal and WebGPU keep the
existing rendering contract; this feature does not add shaders or RHI semantics.
| Backend/platform | Evidence | Status/limit |
| --- | --- | --- |
| Null | `arp_rhi_null_hidden_frame_smoke_tests`, object/YAML headless smokes | Contract validation; no native visual claim |
| Vulkan / Windows | MinGW Debug hidden Win32 input smoke and real Vulkan GUI draws, replayed RenderDoc captures | Backend Validated / Platform Validated for this feature scene |
| WebGPU / Wasm | `web-webgpu-debug` build of `arp_ui_runtime` and `arp_platform_window_mapping`; Emscripten syntax compilation of the actual `window_web.cpp` | Compile compatible; browser execution/visual verification pending |
| Metal / Apple | Shared backend-neutral UI sources compile in host/Wasm builds; renderer UI integration checks the four-backend semantic boundary; Cocoa producer updated with the same lifecycle/repeat interface | Apple native compilation and execution unavailable on this Windows host; no Metal Backend/Platform Validated claim |

Cross-platform Production Ready remains unclaimed until native Apple/browser
validation is available. Complex dropdown popups, drag/drop, IME composition and
ArpScript inheritance remain outside Events/1 as agreed. Unrelated RHI/WebGPU and
vegetation working-tree changes were preserved.

## RenderDoc visual evidence

Captured the same YAML scene before input and after one ordered InputFrame.
Derived button/slider/text hooks update labels to `Button clicked`, `Slider
changed`, and `Typed through InputFrame`; slider value commits to 0.75 and button
callback fires once. The TextInput's own text is clipped by its authored 160px
width; the adjacent status label displays the full committed text.

- `build/ui-control-events-evidence/final/before_capture.rdc`
- `build/ui-control-events-evidence/final/after_capture.rdc`
- `build/ui-control-events-evidence/final/before_capture.png`
- `build/ui-control-events-evidence/final/after_capture.png`
- `build/ui-control-events-evidence/final/analysis.txt`

Replay verified 720x240 final targets, 39 before / 70 after indexed GUI draws,
fragment atlas resources, and zero high/error replay diagnostics. Changed pixels
in YAML-defined regions: button status 674, slider status 809, text status 2062,
TextInput 1122. AI inspected both exported backbuffers and confirmed readable
text/status changes. Analysis reuses `analyze_ui_text_renderdoc.py` helpers.

## Reproduction and example

`examples/ui/control_events.h` demonstrates semantic-only overrides of Button,
Slider and TextInput. `examples/ui/control_events.arpui` uses existing YAML v1.
Run `build/ui-control-events-debug/arp_ui_control_events_example.exe` for the
load/ordered-input/unload scenario. A normal frame consumer calls
`runtime.dispatchFrame(frame, {windowId, maxEvents})`, checks the diagnostic,
then `inputSystem.beginFrame(frame, result.eventConsumption)`.

```powershell
python -c "import scripts.build as b; b._cmake(['--preset','win-vulkan-debug','-B','build/ui-control-events-debug','-DARP_ENABLE_VENDORED_ASSIMP=OFF'])"
python -c "import scripts.build as b; b._cmake(['--build','build/ui-control-events-debug','--target','arp_ui_control_events_example','arp_ui_control_event_routing_tests','arp_ui_control_factory_lifecycle_tests','arp_ui_control_input_hidden_smoke','arp_ui_control_events_vulkan_hidden_smoke','-j','4'])"
ctest --test-dir build/ui-control-events-debug --output-on-failure -R 'arp_ui_control_(object|event|factory|input|events)'
```

The isolated build avoids concurrent access to the shared Ninja build directory;
Assimp is disabled only for these focused UI builds. This is not a full engine/game
Release rebuild. Complete selected-test and build evidence is retained alongside
the captures, including `web-build.log` and `web-window-compile.txt`.

For capture set `ARP_RENDERDOC_CAPTURE_PATH` and launch the Vulkan smoke with
`renderdoccmd capture -w`. Set `ARP_UI_EVENTS_BEFORE=1` for the baseline; omit it
for the interaction capture. To replay, set `ARP_UI_EVENTS_EVIDENCE` to the
directory containing before/after captures and `ARP_UI_EVENTS_TOOLS` to
`D:/arp/tests/tools`, then run qrenderdoc with `--python
D:/arp/tests/tools/analyze_ui_control_events_renderdoc.py` and the after capture.
