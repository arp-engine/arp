# Performance, Memory, And Telemetry

Status: Contract Complete.

`src/core/performance_telemetry.h` defines frame telemetry for:

- CPU timing scopes
- GPU timing scopes
- buffer, texture, transient render target, descriptor pool, and pipeline cache memory
- frame capture labels
- mobile budget evaluation and warning diagnostics

Backend timestamp queries and runtime HUD drawing can feed this shared structure
without changing renderer profile logic.

## CPU Allocator PMR Hook

Status: Contract Complete.

`src/core/cpu_allocator.h` defines the CPU allocation contract used by engine
owned `std::pmr` containers and shared ownership helpers. `CpuMemoryResource`
adapts `CpuAllocator` to `std::pmr::memory_resource`, so containers created
through `arp::core::pmr` aliases can route allocation and deallocation through
either the default CPU allocator or a third-party allocator callback adapter.
Use `arp::core::makeShared<T>()` instead of `std::make_shared<T>()` so the
object and control block are allocated through the default CPU PMR resource.

The source contract test rejects direct owning `std` containers, direct
`std::make_shared`, and direct C allocator calls outside the allocator
implementation. Third-party decode memory owned by `stb_image` is explicitly
hooked through `STBI_MALLOC`, `STBI_REALLOC_SIZED`, and `STBI_FREE` with the
`ImageDecode` intent.

Do not override global `operator new/delete` for this contract on the current
MinGW build: dynamic libstdc++ paths can allocate in executable code and release
inside the runtime DLL, which makes a header-based global allocator unsafe
across that boundary. `std::unique_ptr` factories and raw object allocation are
therefore still a Prototype/Legacy fallback boundary until their public
ownership types are migrated to allocator-aware deleters or backend-owned
factory contracts.

The current contract records deterministic allocation intent labels such as
`AssetImport`, `ImageDecode`, `ShaderCompile`, `RhiHostStaging`, and
`Telemetry`. This is not Backend Validated, Platform Validated, or Production
Ready yet: production status requires converting hot engine-owned CPU buffers
to PMR containers, replacing the remaining `std::unique_ptr` object allocation
surface with allocator-aware ownership contracts, wiring platform/runtime
allocator injection, validating third-party allocator adapters, and adding
hidden-window smoke coverage for the converted paths.
