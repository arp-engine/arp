# macOS Metal Hardware Validation

Status: Contract Complete, Apple hardware validation pending.

The GitHub Actions entry point is `.github/workflows/apple-metal.yml`; its
repository-owned implementation is `python3 ci/apple_build.py macos`. It builds
the shader, iOS lifecycle, and Metal device contracts, runs CTest, and validates
each generated MSL artifact with the macOS Metal compiler. A hosted runner may
only be promoted to Backend Validated when its logs prove a native Metal device
and successful runtime/hash smoke; compilation alone remains Prototype Complete.

The Metal backend now exposes a hardware validation gate through
`queryMetalMacHardwareValidationPlan()`. This keeps contract tests honest on
non-Apple hosts: Windows/Linux builds can verify RHI parity plans and creation
plans, but they cannot mark Metal Production Ready.

## Required Hardware Run

Run on macOS with the `macos-metal` preset:

- Build Objective-C++ Metal implementation units.
- Launch `arp_stylized_showcase`.
- Use the CAMetalLayer-backed swapchain.
- Validate Slang-generated MSL in real shader library and pipeline creation.
- Clear/present, mesh draw, and GUI draw through the abstract RHI.
- Print Metal capability information.

## Production Gate

The phase can only move from Contract Complete to Production Ready after the
hardware validation plan passes on Apple hardware. Contract tests must keep
`canClaimProductionReadyWithoutHardware` false.
