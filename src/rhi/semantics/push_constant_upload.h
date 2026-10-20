#pragma once
#include "core/unsigned_range.h"
#include "rhi/gpu_common.h"

#include <cstdint>
#include <limits>

namespace arp::rhi::semantics {
// Actual device facts only. This is sizing/validation, never an allocation or
// proof that a native resource, selected shader ABI or execution gate exists.
struct PushConstantUploadLimits {
    std::uint64_t maxBufferBytes = 0u, maxBindingBytes = 0u;
    std::uint32_t offsetAlignment = 0u;
};

struct PushConstantUploadSlice {
    std::uint32_t dynamicOffset = 0u;
    std::uint64_t writeEnd = 0u, bindingEnd = 0u;
};

// A dynamic uniform binding covers bindingBytes even when fewer bytes are
// uploaded. In particular the last slice must fit that entire binding span.
// Failure clears the result and never advances the caller's cursor.
inline constexpr const char* planPushConstantUploadSlice(std::uint64_t cursor,
    std::uint32_t payloadBytes,
    std::uint32_t bindingBytes,
    std::uint64_t capacity,
    const PushConstantUploadLimits& limits,
    PushConstantUploadSlice& output) noexcept
{
    output = {};
    if (!limits.maxBufferBytes || !limits.maxBindingBytes || !limits.offsetAlignment
        || (limits.offsetAlignment & (limits.offsetAlignment - 1u)))
        return "RHI-ORIGINAL-PUSH-UPLOAD-LIMITS";
    if (!payloadBytes || payloadBytes % 4u || payloadBytes > kL2AtomicPushConstantLimitBytes
        || bindingBytes < payloadBytes || bindingBytes % 4u
        || bindingBytes > limits.maxBindingBytes)
        return "RHI-ORIGINAL-PUSH-UPLOAD-BINDING-RANGE";
    if (!capacity || capacity > limits.maxBufferBytes)
        return "RHI-ORIGINAL-PUSH-UPLOAD-RESIDENT-CAPACITY";
    std::uint64_t aligned = 0u;
    if (!core::roundUpUnsigned(cursor, std::uint64_t(limits.offsetAlignment), aligned)
        || aligned > std::numeric_limits<std::uint32_t>::max() || aligned % 4u)
        return "RHI-ORIGINAL-PUSH-UPLOAD-OFFSET";
    if (aligned > capacity || bindingBytes > capacity - aligned)
        return "RHI-ORIGINAL-PUSH-UPLOAD-RESIDENT-CAPACITY";
    output = {static_cast<std::uint32_t>(aligned), aligned + payloadBytes, aligned + bindingBytes};
    return nullptr;
}
} // namespace arp::rhi::semantics
