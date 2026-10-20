# WindowSystem/1

Status: Contract Complete. Win32 implementation is provided and validated on this
machine (hidden-window integration + contract tests pass, Vulkan surface created
against the real loader). macOS / Linux implementations are provided but
Platform Validated is pending (no local Apple / X11 machine was available for
this delivery). Web input is Prototype Complete (sentinel window + callback
bridge, browser-level validation outstanding). Android / iOS remain Planned and
reserved in the contract; they never had a windowing layer before this contract.

## Scope

WindowSystem/1 is the L2 platform window, display, event, input and GPU-surface
contract in `src/platform/window.h`. It replaces the former third-party
windowing facade (SDL3) entirely; the engine carries no vendored windowing
dependency. Upper layers never observe a native window handle: the only
native-adjacent surface is the opaque `PlatformVulkanSurface` (Vulkan types are
forward-declared, loader entry points are type-erased `void*`) and the opaque
`std::shared_ptr<void> surfaceToken()` (CAMetalLayer on Apple).

Layer ownership: platform windowing lives below `render_system` and is consumed
by `createDefaultRenderSystem(WindowSystem&, ...)` / render-system factories.
RHI swapchains keep receiving `SwapchainDesc::platformSurface` tokens; they do
not depend on this header. `src/input` stays injection-only; the bridge in
`src/platform/input_bridge.h` translates `WindowInputEvent` into
`input::InputFrame` / `input::InputEvent` deterministically, mirroring the
established `audio_lifecycle_bridge` pattern.

## L2 semantics and invariants

- Lifetime: `PlatformWindow` / `PlatformVulkanSurface` must not outlive their
  creating `WindowSystem`. Moved-from and default-constructed objects report
  `invalid()` / zero extents. `PlatformVulkanSurface::reset()` destroys the
  native surface through the same loader instance that created it.
- Threading: `WindowSystem` is main-thread-only, matching AppKit / Win32
  windowing. Callers pump events explicitly on the owning thread; no hidden
  event thread is spawned. `pumpEvents` drains without blocking;
  `waitForEvents(timeoutMs)` blocks up to the timeout; a timeout is a
  successful empty drain and `success=false` is reserved for platform failure.
- Extents: `width()/height()` are logical extents (platform points / DIPs);
  `pixelWidth()/pixelHeight()` are the physical backing-store extents.
  `pixelDensity()` is the logical-to-pixel scale; platforms without a per-window
  density report 1.0 and pixel == logical. DPI queries come from the platform
  API (`GetDpiForWindow` on Windows, NSScreen backingScaleFactor on Apple,
  devicePixelRatio on Emscripten); no engine-side magic numbers.
- `resize()` updates cached extents synchronously; `sync()` commits pending
  geometry / state with the platform compositor. Win32 and X11 calls are
  already synchronous and `sync()` reports success; Cocoa flushes the pending
  AppKit transaction and drains the run loop once.
- Events: `WindowEventType` is the closed set {FocusGained, FocusLost,
  CloseRequested, Resized, Minimized, Restored}. Creation-time geometry is
  folded into the create result; `Resized` is emitted only for post-creation
  geometry changes (deduplicated by pixel extent).
- Input: `WindowInputEvent` covers KeyDown/KeyUp, MouseButtonDown/Up,
  MouseMoved (delta), PointerMoved (absolute client position, top-left origin,
  logical coordinates), MouseWheel (notch units, positive away from the user)
  and TextInput (UTF-8). `WindowKey` is the neutral key set; each platform maps
  its native representation through `window_input_mapping.h`. Keys outside the
  set are dropped deterministically (no silent best-effort guessing). Surrogate
  pairs are reassembled before UTF-8 encoding; control characters below 0x20
  and 0x7F are dropped.
- Key vocabulary: `WindowKey` is a closed vocabulary with a `Count` sentinel;
  every non-Unknown enumerator has an `input::InputKey` equivalent and the
  bridge (`windowKeyToInputKey`) maps all of them 1:1, so a mapped platform key
  is never dropped between the window pump and `input::InputFrame`. The set
  covers A–Z, digits, modifiers (both sides, plus Super), lock keys, F1–F24,
  the punctuation block, a dedicated keypad block (digits, operators,
  `NumpadEnter`, independent of the top-row digits and of NumLock state),
  PrintScreen/ScrollLock/Pause, the menu key and the ISO 102nd key. Platform
  tables map only keys whose native representation is unambiguous: Win32 leaves
  layout-dependent OEM keys (e.g. `VK_OEM_8`) unmapped and discriminates
  keypad Enter from Enter via the `KF_EXTENDED` lParam bit; macOS AppKit
  keycodes have no PrintScreen/ScrollLock/Pause/NumLock equivalent, and
  `KeypadClear` / `KeypadEquals` stay unmapped; X11 leaves `XK_KP_Separator`
  and the layout-dependent ISO 102nd keysyms unmapped; Web `KeyboardEvent.code`
  covers the full set including `IntlBackslash` / `NonUsBackslash`.
  The exhaustive bridge loop in `arp_platform_window_contract_tests` fails when
  a `WindowKey` is added without its `InputKey` mirror or bridge case.
- Mouse capture and drag delivery (desktop):
  - Win32: pressing any button takes the Win32 mouse capture (`SetCapture`)
    for the window; releasing the last held button releases it. While
    captured, `WM_MOUSEMOVE` keeps arriving outside the client area with
    negative client coordinates — deltas and absolute positions stay valid
    for the whole drag, and the closing button-up is delivered even when the
    cursor leaves the window. If capture is taken away by another window
    (`WM_CAPTURECHANGED`), the window synthesizes deterministic releases for
    every button still marked held, so `InputSystem` never observes a button
    stuck down.
  - Linux X11: the first button held takes an explicit pointer grab
    (`xcb_grab_pointer`, owner_events = 0) so Motion/Release keep arriving
    anywhere on screen with negative event coordinates outside the window;
    the last release ungrabs. `LeaveNotify` with mode Ungrab while buttons
    are still held is the server's grab-termination signal and synthesizes
    the same deterministic releases (the `WM_CAPTURECHANGED` analog). X11's
    automatic passive grab remains the delivery baseline while the grab reply
    is in flight and for additional simultaneous windows (the server allows
    one grab per connection).
  - macOS: AppKit implicit capture — the window server keeps sending
    dragged/released events to the mouse-down window wherever the cursor
    goes; no explicit capture exists or is needed, and AppKit exposes no
    grab-loss event, so no synthetic-release repair exists on this platform.
  - On all three platforms button events carry the coordinates delivered
    with their own message or event (never a stale pointer position).
- GPU surfaces: `requiredVulkanInstanceExtensions()` returns the platform's
  Vulkan instance extensions (empty on Apple / Emscripten).
  `createVulkanSurface(window, instance)` fails closed — an unavailable loader,
  window or entry point yields an invalid surface, never a placeholder.
  `vulkanGetInstanceProcAddr()` is resolved from the system Vulkan loader
  (`vulkan-1.dll` on Windows), loaded lazily and released with the state.
- Allocation: all dynamic containers and strings use `core::pmr` aliases over
  `core::defaultCpuMemoryResource()`; records are owned through
  `core::UniquePtr`. No raw `new`/`delete`.

## Per-platform implementation and lowering

Exactly one implementation translation unit is linked per build; the platform
`WindowSystemState` is defined at `arp::platform` scope in that TU.

| Platform | TU | Window | Display bounds | Events / input | Vulkan surface | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Windows | `window_win32.cpp` | RegisterClassExW / CreateWindowExW, Per-Monitor-V2 DPI awareness | MonitorFromPoint + GetMonitorInfoW | PeekMessageW pump; VK → WindowKey table; WM_CHAR text with surrogate reassembly | VK_KHR_surface + VK_KHR_win32_surface via vulkan-1.dll | Provided, validated on this machine (hidden-window tests) |
| macOS | `window_cocoa.mm` | NSWindow / NSView with CAMetalLayer-backed view | NSScreen frame | nextEventMatchingMask pump; HID usage → WindowKey; AppKit implicit capture for drags | n/a (Metal via surfaceToken) | Provided; Platform Validated pending |
| Linux X11 | `window_xcb.cpp` | xcb_create_window + xkbcommon keymap | xcb_randr primary monitor | xcb_poll_for_event pump; xkbcommon keysym → WindowKey; explicit pointer grab for drags + Ungrab repair | VK_KHR_surface + VK_KHR_xcb_surface | Provided; Platform Validated pending |
| Web | `window_web.cpp` | Sentinel window over the browser canvas; canvas sizing owned by the browser | false (browser owns sizing) | emscripten_set_* callback bridge → WindowInputEvent | n/a (WebGPU via canvas) | Prototype Complete; browser input validation outstanding |
| Android | — | Planned: ANativeWindow token + SurfaceCreated/Lost → PlatformLifecycleEvent | Planned | Planned | VK_KHR_android_surface | Planned (reserved in contract) |
| iOS | — | Planned: UIKit view CAMetalLayer token | Planned | Planned | n/a (Metal) | Planned (reserved in contract) |

Failures are deterministic and diagnostic-bearing: `initialize()` fails when the
window class / DPI setup cannot be established; `createWindow` returns an
invalid window when the platform creation call fails; surface creation fails
closed. No dummy windows, no silent fallbacks.

## Migration notes

- `render_system.h`: `PlatformSurfaceDesc.sdlPlatform/sdlWindow` became
  `windowSystem/window`; `createDefaultRenderSystem` / `createRenderSystem`
  take `WindowSystem&`.
- Vulkan / Metal / WebGPU render systems and `render_system_native_shared`
  consume the new types only; the Vulkan TU no longer links or loads SDL.
- `tools/render_feature_lab` pumps events and feeds the input bridge on its
  visible-window path.
- SDL3 is removed from ThirdParty discovery, presets, vendored sources
  (`3rd/SDL3-3.4.8` deleted) and test registration; the architecture boundary
  test now forbids `SDL_` tokens across the whole `src/` tree, so any
  reintroduction fails the build's test gate.

## Required validation per status

- Contract tests (`arp_platform_window_contract_tests`): WindowKey table
  determinism, InputKey mapping, descriptor validation, event enum stability,
  negative boundary assertions (whole-`src/` SDL token ban).
- Integration (`arp_platform_window_tests`, run in a hidden window):
  create / resize / display bounds / Vulkan extension list / real
  `vkCreateWin32SurfaceKHR` against the system loader; event pump idle-drain
  and single-Resized semantics. On Windows the test additionally posts
  synthetic `WM_KEYDOWN` / `WM_KEYUP` / `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` /
  `WM_LBUTTONUP` messages — including keypad-Enter extended-bit
  discrimination, auto-repeat suppression and a drag that moves to negative
  client coordinates before releasing — through the real WndProc and asserts
  the neutral event stream.
- Platform Validated (pending for macOS / Linux): build the `macos-metal` /
  `linux-vulkan` presets and run the migrated smokes
  (`macos_frame_input_smoke`, `macos_metal_surface_smoke`,
  `macos_platform_contract_tests`) / X11 hidden-window smoke on real hardware.
- Production Ready additionally requires mobile bring-up per the Planned rows
  and hot-path evidence (no per-frame window allocations, no blocking waits on
  the render thread).
