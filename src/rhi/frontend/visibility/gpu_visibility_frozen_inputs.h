#pragma once
#include "core/unsigned_element_span.h"
#include "rhi/frontend/visibility/gpu_visibility_layout.h"

#include <array>
#include <cstring>

namespace arp::rhi::visibility {
// Exact equality to the original initializer's immutable backing bytes. Native
// adapters must authenticate that initializer, its actual buffer lease and its
// pending content stamp; this comparison is not a caller-issued content proof.
class GpuVisibilityFrozenInputContract final {
public:
    struct Bytes {
        const void* data = nullptr;
        std::size_t size = 0u;
    };

    struct Inputs {
        Bytes groups, instances, owners, parameters;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxWordVisits = 0u;
    };

    struct Report {
        const char* diagnostic = nullptr;
        std::uint64_t words = 0u, bytes = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    template <class Instance>
    static Report inspect(const GpuVisibilityLayoutContract::Input& layout,
        const GpuVisibilityLayoutContract::Budget& layoutBudget,
        const Instance* instances,
        const GpuVisibilityParameters& parameters,
        bool queriedNativeBase,
        const rhi::TextureDesc* pyramid,
        const Inputs& frozen,
        const Budget& budget) noexcept
    {
        static_assert(std::is_standard_layout_v<Instance> && sizeof(Instance) == sizeof(Instance64)
            && std::is_same_v<decltype(Instance::positionScale), float[4]>
            && std::is_same_v<decltype(Instance::yawPhaseIdLod), float[4]>
            && std::is_same_v<decltype(Instance::extraTransform), float[4]>
            && std::is_same_v<decltype(Instance::speciesLodState), float[4]>
            && offsetof(Instance, positionScale) == offsetof(Instance64, positionScale)
            && offsetof(Instance, yawPhaseIdLod) == offsetof(Instance64, yawPhaseIdLod)
            && offsetof(Instance, extraTransform) == offsetof(Instance64, extraTransform)
            && offsetof(Instance, speciesLodState) == offsetof(Instance64, speciesLodState));
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (budget.contractVersion != 1u)
            return reject("PCG-GPU-VISIBILITY-FROZEN-VERSION");
        if (!budget.optIn)
            return reject("PCG-GPU-VISIBILITY-FROZEN-OPT-IN");
        GpuVisibilityLayoutContract::Report sizes;
        if (const auto* id = GpuVisibilityLayoutContract::inspect(layout, layoutBudget, sizes))
            return reject(id);
        if (const auto* id = GpuVisibilityLayoutContract::inspectParameters(
                layout, parameters, queriedNativeBase, pyramid))
            return reject(id);
        if (!instances)
            return reject("PCG-GPU-VISIBILITY-FROZEN-INSTANCES");
        const auto total = sizes.drawGroupBytes + sizes.inputInstanceBytes + sizes.ownerMapBytes
            + sizes.parameterBytes;
        if (total / sizeof(std::uint32_t) > budget.maxWordVisits)
            return reject("PCG-GPU-VISIBILITY-FROZEN-WORD-CAPACITY");
        const std::array<Bytes, 4u> buffers{
            frozen.groups, frozen.instances, frozen.owners, frozen.parameters};
        const std::array<std::uint64_t, 4u> bytes{sizes.drawGroupBytes,
            sizes.inputInstanceBytes,
            sizes.ownerMapBytes,
            sizes.parameterBytes};
        for (std::size_t i = 0u; i < buffers.size(); ++i)
            if (!buffers[i].data || buffers[i].size != bytes[i])
                return reject("PCG-GPU-VISIBILITY-FROZEN-COMPLETE-BACKING");
        // These ABIs contain only four-byte scalar lanes and have checked
        // offsets/sizes in their declaring headers. memcpy to native uint32
        // lanes preserves packed float metadata, including NaN payload bits.
        for (std::uint32_t group = 0u; group < layout.groupCount; ++group)
            if (!matchesRecord(layout.groups[group], frozen.groups, group))
                return reject("PCG-GPU-VISIBILITY-FROZEN-GROUPS");
        for (std::uint32_t instance = 0u; instance < layout.instanceCount; ++instance) {
            const auto& value = instances[instance];
            for (const auto lane : value.positionScale)
                if (!std::isfinite(lane))
                    return reject("PCG-GPU-VISIBILITY-FROZEN-TRANSFORM");
            for (const auto lane : value.yawPhaseIdLod)
                if (!std::isfinite(lane))
                    return reject("PCG-GPU-VISIBILITY-FROZEN-TRANSFORM");
            for (const auto lane : value.extraTransform)
                if (!std::isfinite(lane))
                    return reject("PCG-GPU-VISIBILITY-FROZEN-TRANSFORM");
            if (!matchesRecord(value, frozen.instances, instance))
                return reject("PCG-GPU-VISIBILITY-FROZEN-INSTANCES");
        }
        const core::UnsignedElementSpan ownerWords(frozen.owners.data,
            frozen.owners.size,
            core::UnsignedElementSpan::Width::Uint32,
            core::UnsignedElementSpan::ByteOrder::LittleEndian);
        if (!ownerWords.matchesWords(layout.instanceToGroup, layout.instanceCount))
            return reject("PCG-GPU-VISIBILITY-FROZEN-OWNERS");
        if (!matchesRecord(parameters, frozen.parameters, 0u))
            return reject("PCG-GPU-VISIBILITY-FROZEN-PARAMETERS");
        return {nullptr, total / sizeof(std::uint32_t), total};
    }

private:
    template <class Record>
    static bool matchesRecord(const Record& value, const Bytes& frozen, std::size_t index) noexcept
    {
        static_assert(
            std::is_trivially_copyable_v<Record> && sizeof(Record) % sizeof(std::uint32_t) == 0u);
        std::array<std::uint32_t, sizeof(Record) / sizeof(std::uint32_t)> words;
        std::memcpy(words.data(), &value, sizeof(value));
        const auto* bytes = static_cast<const std::uint8_t*>(frozen.data) + index * sizeof(Record);
        const core::UnsignedElementSpan actual(bytes,
            sizeof(Record),
            core::UnsignedElementSpan::Width::Uint32,
            core::UnsignedElementSpan::ByteOrder::LittleEndian);
        return actual.matchesWords(words.data(), words.size());
    }
};
} // namespace arp::rhi::visibility
