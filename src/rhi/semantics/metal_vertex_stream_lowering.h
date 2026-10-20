#pragma once

#include "rhi/gpu_pipeline.h"

#include <array>

namespace arp::rhi::semantics {

// VertexStreamBinding/1 four-backend lowering:
// Null records logical stream state (validation only). Vulkan vertex bindings
// and WebGPU vertex slots are separate from descriptors/push-constant lowering.
// Metal shares 31 vertex-stage buffer indices with shader resources. Reserve
// the union of reflected buffer resources across entries/stages: current
// MetalBindingSpace/1 has no per-entry physical liveness. This conservative
// union is explicit; exhaustion is DiagnosticFailure, never a slot alias.
// Texture/sampler slots do not reserve buffer slots. Argument-buffer, push-
// constant, tessellation and counted-indirect ABI buffer indices do.
struct MetalVertexStreamMapping {
    static constexpr std::uint32_t capacity = 31u;
    std::array<std::uint32_t, capacity> slots = [] {
        std::array<std::uint32_t, capacity> value;
        value.fill(UINT32_MAX);
        return value;
    }();
    bool success = false;
    const char* diagnostics = "RHI-METAL-VERTEX-STREAM-LOWERING-MISSING";
};

inline MetalVertexStreamMapping lowerMetalVertexStreams(
    const core::pmr::Vector<VertexBufferLayoutDesc>& streams, std::uint32_t reservedBuffers)
{
    MetalVertexStreamMapping result;
    if ((reservedBuffers & (1u << MetalVertexStreamMapping::capacity)) != 0u) {
        result.diagnostics = "RHI-METAL-VERTEX-RESOURCE-SLOT-INVALID";
        return result;
    }
    std::uint32_t logical = 0u;
    for (const auto& stream : streams) {
        if (stream.slot >= result.capacity || (logical & (1u << stream.slot)) != 0u) {
            result.diagnostics = "RHI-METAL-VERTEX-STREAM-SLOT-INVALID";
            return result;
        }
        logical |= 1u << stream.slot;
    }
    for (std::uint32_t slot = 0; slot < result.capacity; ++slot) {
        if ((logical & (1u << slot)) == 0u)
            continue;
        std::uint32_t physical = 0u;
        while (physical < result.capacity && (reservedBuffers & (1u << physical)) != 0u)
            ++physical;
        if (physical == result.capacity) {
            result.diagnostics = "RHI-METAL-VERTEX-STREAM-SLOTS-EXHAUSTED";
            return result;
        }
        result.slots[slot] = physical;
        reservedBuffers |= 1u << physical;
    }
    result.success = true;
    result.diagnostics = "";
    return result;
}

} // namespace arp::rhi::semantics
