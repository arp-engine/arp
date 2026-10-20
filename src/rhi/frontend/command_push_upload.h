#pragma once
#include "rhi/frontend/command_model.h"
#include "rhi/semantics/push_constant_upload.h"

namespace arp::rhi {
// Whole-stream cold preflight accumulator. It stores only bounded byte counts,
// not push data, resources or handles. Every block must first pass the existing
// frozen-byte/selected-stage OriginalCommandPushConsumption validation.
class OriginalCommandPushUploadPlan final {
public:
    using Budget = OriginalCommandPushUploadBudget;
    using Limits = semantics::PushConstantUploadLimits;
    using Slice = semantics::PushConstantUploadSlice;

    constexpr OriginalCommandPushUploadPlan(Budget budget, Limits limits) noexcept
        : budget_(budget), limits_(limits)
    {}

    constexpr const char* append(
        std::uint32_t payloadBytes, std::uint32_t bindingBytes, Slice& output) noexcept
    {
        output = {};
        if (const auto* id = budget_.diagnostic())
            return id;
        if (updates_ >= budget_.maxUpdates || bytes_ > budget_.maxPayloadBytes
            || payloadBytes > budget_.maxPayloadBytes - bytes_)
            return "RHI-ORIGINAL-PUSH-UPLOAD-PAYLOAD-CAPACITY";
        Slice candidate;
        if (const auto* id = semantics::planPushConstantUploadSlice(
                cursor_, payloadBytes, bindingBytes, budget_.maxResidentBytes, limits_, candidate))
            return id;
        ++updates_;
        bytes_ += payloadBytes;
        cursor_ = candidate.writeEnd;
        if (candidate.bindingEnd > resident_)
            resident_ = candidate.bindingEnd;
        output = candidate;
        return nullptr;
    }

    constexpr const char* validateComplete(
        std::uint32_t nativePayloads, std::uint32_t retainedOwners) const noexcept
    {
        if (const auto* id = budget_.diagnostic())
            return id;
        if (!updates_)
            return "RHI-ORIGINAL-PUSH-UPLOAD-EMPTY";
        if (!nativePayloads || nativePayloads > budget_.maxNativePayloadObjects)
            return "RHI-ORIGINAL-PUSH-UPLOAD-NATIVE-CAPACITY";
        if (!retainedOwners || retainedOwners > budget_.maxRetainedOwners)
            return "RHI-ORIGINAL-PUSH-UPLOAD-OWNER-CAPACITY";
        return nullptr;
    }

    constexpr std::uint64_t updates() const noexcept { return updates_; }

    constexpr std::uint64_t payloadBytes() const noexcept { return bytes_; }

    constexpr std::uint64_t residentBytes() const noexcept { return resident_; }

private:
    const Budget budget_;
    const Limits limits_;
    std::uint64_t updates_ = 0u, bytes_ = 0u, cursor_ = 0u, resident_ = 0u;
};
} // namespace arp::rhi
