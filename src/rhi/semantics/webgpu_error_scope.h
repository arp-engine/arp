#pragma once

#include <array>
#include <cstdint>

// region webgpu_error_scope
namespace arp::rhi_webgpu {
// WebGpuErrorScopeBatch/1. Pure versioned scope-stack contract usable by cold
// planners and Null tests without importing the platform callback ABI.
class WebGpuErrorScopeContract final {
public:
    enum class Filter { Internal, OutOfMemory, Validation };
    static constexpr std::uint32_t contractVersion = 1u;
    static constexpr std::array filters{Filter::Internal, Filter::OutOfMemory, Filter::Validation};
    static constexpr std::uint32_t count = static_cast<std::uint32_t>(filters.size());
};
} // namespace arp::rhi_webgpu

// endregion webgpu_error_scope
