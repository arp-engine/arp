#pragma once
#include "pcg/gpu_visibility_frozen_inputs.h"

#include <cassert>

namespace arp::test {
class GpuVisibilityFrozenInputsFixture final {
    template <class Record> static auto encode(const Record& value)
    {
        std::array<std::uint32_t, sizeof(Record) / 4u> words;
        std::array<std::uint8_t, sizeof(Record)> bytes;
        std::memcpy(words.data(), &value, sizeof(value));
        core::UnsignedElementSpan::encodeWordsLittleEndian(words, bytes);
        return bytes;
    }

public:
    static void verify()
    {
        using Span = core::UnsignedElementSpan;
        const std::array<std::uint8_t, 4u> encoded{0x12u, 0x34u, 0x56u, 0x78u};
        const Span big16(
            encoded.data(), encoded.size(), Span::Width::Uint16, Span::ByteOrder::BigEndian);
        const std::array<std::uint32_t, 2u> expected16{0x1234u, 0x5678u};
        assert(big16.matchesWords(expected16.data(), expected16.size()));
        assert(!big16.matchesWords(expected16.data(), 1u) && !big16.matchesWords(nullptr, 2u));
        const Span little32(
            encoded.data(), encoded.size(), Span::Width::Uint32, Span::ByteOrder::LittleEndian);
        const std::uint32_t expected32 = 0x78563412u;
        assert(little32.matchesWords(&expected32, 1u));
        assert(!little32.matchesWords(expected16.data(), 2u));
        using Frozen = pcg::GpuVisibilityFrozenInputContract;
        std::array<pcg::GpuVisibilityDrawGroup, 1u> groups{};
        groups[0u].globalInstanceCount = 2u;
        groups[0u].indexCount = 3u;
        groups[0u].localBoundsCenterRadius[3u] = 1.f;
        groups[0u].cullDistance = 100.f;
        const std::array<std::uint32_t, 2u> owners{0u, 0u};
        const std::array<std::uint32_t, 1u> indexCounts{3u};
        const pcg::GpuVisibilityLayoutContract::Input layout{
            groups.data(), owners.data(), indexCounts.data(), 1u, 2u, 1u, 1u, 2u, 1u, 1u};
        const pcg::GpuVisibilityLayoutContract::Budget layoutBudget{1u, true, 5u, 5u, 1u};
        std::array<pcg::InstanceDataGpu, 2u> instances;
        pcg::packInstanceMetadataFloat(instances[1u].speciesLodState[0u], 0x7fc12345u);
        pcg::GpuVisibilityParameters parameters{};
        parameters.counts[0u] = parameters.capacities[0u] = 2u;
        parameters.counts[1u] = parameters.counts[2u] = 1u;
        parameters.capacities[1u] = parameters.capacities[2u] = parameters.capacities[3u] = 1u;
        parameters.policy[3u] = 1u;
        for (auto& plane : parameters.frustumPlanes)
            plane[0u] = 1.f;
        auto groupBytes = encode(groups);
        auto instanceBytes = encode(instances);
        const auto ownerBytes = encode(owners);
        auto parameterBytes = encode(parameters);
        Frozen::Inputs inputs{{groupBytes.data(), groupBytes.size()},
            {instanceBytes.data(), instanceBytes.size()},
            {ownerBytes.data(), ownerBytes.size()},
            {parameterBytes.data(), parameterBytes.size()}};
        const Frozen::Budget budget{1u, true, 174u};
        const auto inspect = [&](const Frozen::Budget& b) {
            return Frozen::inspect(
                layout, layoutBudget, instances.data(), parameters, false, nullptr, inputs, b);
        };
        const auto baseline = core::defaultCpuAllocator().stats().allocationCount;
        const auto good = inspect(budget);
        assert(!good.diagnostic && good.words == 174u && good.bytes == 696u
            && !good.nativeExecutionProven());
        // The native admission consumes the same exact ABI without a PCG
        // dependency or a reinterpret_cast between unrelated C++ records.
        std::array<rhi::visibility::Instance64, 2u> nativeInstances;
        static_assert(sizeof(nativeInstances) == sizeof(instances));
        std::memcpy(nativeInstances.data(), instances.data(), sizeof(instances));
        const auto nativeGood = Frozen::inspect(layout,
            layoutBudget,
            nativeInstances.data(),
            parameters,
            false,
            nullptr,
            inputs,
            budget);
        assert(!nativeGood.diagnostic && nativeGood.words == good.words
            && nativeGood.bytes == good.bytes);
        nativeInstances[0u].positionScale[0u] = 1.f;
        assert(core::StringView(Frozen::inspect(layout,
                   layoutBudget,
                   nativeInstances.data(),
                   parameters,
                   false,
                   nullptr,
                   inputs,
                   budget)
                       .diagnostic)
            == "PCG-GPU-VISIBILITY-FROZEN-INSTANCES");
        const auto reject = [&](const Frozen::Budget& b, const char* expected) {
            const auto result = inspect(b);
            assert(result.diagnostic && core::StringView(result.diagnostic) == expected
                && !result.words && !result.bytes && !result.nativeExecutionProven());
        };
        reject({1u, true, 173u}, "PCG-GPU-VISIBILITY-FROZEN-WORD-CAPACITY");
        reject({0u, true, 174u}, "PCG-GPU-VISIBILITY-FROZEN-VERSION");
        reject({1u, false, 174u}, "PCG-GPU-VISIBILITY-FROZEN-OPT-IN");
        --inputs.groups.size;
        reject(budget, "PCG-GPU-VISIBILITY-FROZEN-COMPLETE-BACKING");
        ++inputs.groups.size;
        groupBytes[0u] ^= 1u;
        reject(budget, "PCG-GPU-VISIBILITY-FROZEN-GROUPS");
        groupBytes[0u] ^= 1u;
        // Compare metadata bit-for-bit; NaNs are intentionally legal metadata.
        instanceBytes[64u + 48u] ^= 1u;
        reject(budget, "PCG-GPU-VISIBILITY-FROZEN-INSTANCES");
        instanceBytes[64u + 48u] ^= 1u;
        parameterBytes[sizeof(parameters) - 4u] ^= 1u;
        reject(budget, "PCG-GPU-VISIBILITY-FROZEN-PARAMETERS");
        parameterBytes[sizeof(parameters) - 4u] ^= 1u;
        instances[0u].positionScale[0u] = std::numeric_limits<float>::infinity();
        reject(budget, "PCG-GPU-VISIBILITY-FROZEN-TRANSFORM");
        instances[0u].positionScale[0u] = 0.f;
        parameters.counts[3u] = 1u;
        reject(budget, "PCG-GPU-VISIBILITY-FIRST-INSTANCE-CAPABILITY");
        parameters.counts[3u] = 0u;
        assert(!inspect(budget).diagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == baseline);
    }
};
} // namespace arp::test
