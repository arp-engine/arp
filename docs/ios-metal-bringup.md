# iOS Metal Bring-Up

Status: Contract Complete; Xcode/CI host Prototype Complete; device validation pending.

The iOS phase defines the runtime behavior needed by a future UIKit or SDL3 iOS
host. The portable contract covers app lifecycle, CAMetalLayer lifetime, safe
area changes, touch input mapping, memory warnings, and asset bundle layout.

## Lifecycle Rules

Route iOS callbacks through `platform/ios_runtime_contract.h`.

Required events:

- `Active`
- `Inactive`
- `Background`
- `Foreground`
- `LayerCreated`
- `LayerLost`
- `OrientationChanged`
- `MemoryWarning`

Frame submission is allowed only when the app is active, not backgrounded, and a
valid Metal layer is present. Layer creation, layer loss, and orientation changes
require drawable or swapchain recreation.

## Touch And Safe Area

Touch input enters the engine as `IosTouchEvent` values with explicit pointer
ids and phases. Safe area changes must trigger GUI layout reflow so notch and
home-indicator regions are not treated as regular interactive space.

## Asset Bundle

An iOS app bundle must contain:

- compiled shaders
- reflection data
- compressed textures
- UI layout/runtime data

Runtime rendering code should load runtime-ready bundle assets rather than
parsing source project files in frame paths.

## Hardware Validation Checklist

Before marking this phase Production Ready:

- Generate/build an iOS target with signing notes.
- Use a CAMetalLayer-backed surface on device.
- Clear/present through the Metal RHI.
- Drive a minimal GUI interaction with touch input.
- Exercise background/foreground transitions.
- Trigger memory-warning cache release behavior.

## Xcode And CI

The authoritative Xcode project is generated from CMake so target sources,
frameworks, shader artifacts, and bundle metadata cannot drift from the engine
build graph:

```bash
bash ci/bootstrap_apple.sh
python3 ci/apple_build.py ios
open build/ci-apple/ios-metal/AnimeRenderPipeline.xcodeproj
```

The `arp_ios_smoke_app` target provides a UIKit/CAMetalLayer host and routes
active/inactive, background/foreground, layer loss, orientation, safe-area,
and memory-warning events through the iOS runtime contract. CI builds an
unsigned arm64 iPhoneOS app and compiles generated MSL with Apple's compiler.

This evidence is Prototype Complete only. It does not replace signed physical
iPhone execution, lifecycle logs, Xcode GPU capture, or memory/thermal testing.
