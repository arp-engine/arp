# Android Vulkan Bring-Up

Status: Contract Complete, hardware validation pending.

This phase defines the Android runtime contract needed before running on real
devices. The current Windows development environment validates the portable C++
surface/lifecycle rules, build preset shape, logging contract, and packaged asset
requirements. It does not claim that an Android device has cleared and presented
yet.

## Build Preset

Use the `android-arm64-vulkan` CMake preset with these environment variables:

- `ANDROID_NDK`: points to an Android NDK containing `build/cmake/android.toolchain.cmake`
- `SLANGC_EXECUTABLE`: points to the Slang compiler used for offline shader output

The preset targets:

- `CMAKE_SYSTEM_NAME=Android`
- `CMAKE_ANDROID_ARCH_ABI=arm64-v8a`
- `CMAKE_ANDROID_STL_TYPE=c++_static`

## Runtime Lifecycle Contract

Android integration must route platform callbacks through
`platform/android_runtime_contract.h` before touching swapchain or renderer
state.

Required events:

- `Start`
- `Pause`
- `Resume`
- `Stop`
- `SurfaceCreated`
- `SurfaceLost`
- `OrientationChanged`
- `MemoryWarning`

Rules:

- `SurfaceCreated`, `SurfaceLost`, and orientation changes require swapchain
  recreation.
- Pause/resume keeps the runtime object alive but frame submission must be
  skipped while paused or while no surface is valid.
- Surface loss must not destroy imported renderer resources directly.
- Memory warnings must be treated as cache-release signals, not as permission to
  block the frame path with full device idle waits.

## Logging

Android logging should preserve the `ARP.<subsystem>` tag format used by
`makeAndroidLogMessage`. Native log bridging can map severities to `__android_log`
levels, but renderer code should only see the backend-neutral contract.

## Assets

Android packages must include runtime-ready assets:

- compiled shaders
- reflection data
- compressed texture payloads
- pipeline cache directory

Runtime rendering code must not parse source glTF or JSON assets in the frame
path. Source formats belong in offline tools.

## Hardware Validation Checklist

Before marking this phase Production Ready:

- Configure and build `android-arm64-vulkan` with a real NDK.
- Launch a minimal Android sample using SDL3 or the platform abstraction.
- Create a Vulkan surface and clear/present.
- Exercise pause/resume and surface recreation.
- Print device capabilities to Android logs.
- Verify packaged shader/reflection/assets are loaded from the app bundle.
