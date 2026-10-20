#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/descriptor_buffer_declaration_validation.h"
#include "rhi/frontend/resource_leases.h"

#include <cassert>

namespace arp::test {
class OriginalTargetMemoryIntervalFixture final {
    using C = rhi::OriginalTargetMemoryIntervalContract;
    using Binding = rhi::OriginalDescriptorBufferBindingContract;

public:
    static void verifyContract()
    {
        assert(!C::validateIntent({1u, true}) && C::validateIntent({})
            && !C::validateIntent({2u, true}) && C::validateIntent({3u, true})
            && C::validateIntent({1u, false}));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = C::lowering(backend);
            assert(!d.executionAvailable && !d.writeAvailable);
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            assert(d.diagnosticAvailable == (backend == rhi::Backend::WebGPU));
            assert(bool(d.diagnostic) != d.diagnosticAvailable);
        }
        for (const auto type :
            {rhi::DescriptorType::StorageBuffer, rhi::DescriptorType::ReadOnlyStorageBuffer})
            assert(!C::validateType(type));
        for (const auto type : {rhi::DescriptorType::UniformBuffer,
                 rhi::DescriptorType::Sampler,
                 rhi::DescriptorType::SampledTexture})
            assert(C::validateType(type));
        assert(!C::validateType(rhi::DescriptorType::UniformBuffer, 2u));
        assert(C::validateType(rhi::DescriptorType::UniformBuffer, 3u));
        assert(C::validateType(rhi::DescriptorType::Sampler, 2u));
        const Binding::Limits limits{1u, 256u, 256u, 1024u, 1024u};
        const auto check = [&](rhi::DescriptorBufferBindingRange range,
                               std::uint64_t bytes,
                               std::uint32_t dynamic,
                               std::uint64_t minimum) noexcept {
            rhi::DescriptorBufferBindingRangeContract::Resolved out{UINT64_MAX, UINT64_MAX};
            const auto* id = Binding::validateFiniteInterval(
                rhi::DescriptorType::StorageBuffer, range, bytes, dynamic, minimum, limits, out);
            if (id)
                assert(!out.offsetBytes && !out.sizeBytes);
            return id;
        };
        assert(!check({2u, 0u, 4u}, 16u, 0u, 4u));
        assert(check({2u, 0u, 3u}, 16u, 0u, 4u) && check({2u, 0u, 0u}, 16u, 0u, 4u));
        assert(check({2u, 0u, 4u}, 16u, 0u, 0u) && check({2u, 0u, 1025u}, 2048u, 0u, 4u));
        assert(check({2u, 1u, 4u}, 1024u, 0u, 4u) && check({2u, 0u, 4u}, 1024u, 1u, 4u));
        assert(check({2u, UINT64_MAX, 4u}, UINT64_MAX, 0u, 4u));
        assert(!check({2u, 256u, 4u}, 1024u, 256u, 4u));
        assert(check({2u, 256u, 4u}, 512u, 256u, 4u));
        assert(check({1u, 0u, 0u}, 16u, 0u, 4u) && check({99u, 0u, 4u}, 16u, 0u, 4u));
        auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                assert(!check({2u, 0u, 4u}, 16u, 0u, 4u) && check({2u, 0u, 3u}, 16u, 0u, 4u));
                return nullptr;
            }));
    }

    template <class Artifact>
    static void verifyDeclaration(const std::shared_ptr<const Artifact>& original,
        std::uint32_t set,
        std::uint32_t binding,
        rhi::DescriptorType type)
    {
        using D = rhi::OriginalTargetBufferDeclarationContract;
        // The original entry/module fixture already exhausts this exact quota
        // and installs hostile PMR. A nested zero-byte pressure lease is invalid.
        assert(original->quota()->liveBytes() == original->quota()->limitBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        typename D::Facts facts;
        assert(!D::diagnose(original, set, binding, type, facts));
        assert(facts.stageMask && facts.target.minimumBindingBytes);
        const auto reject = [&](const auto& owner,
                                std::uint32_t badSet,
                                std::uint32_t badBinding,
                                rhi::DescriptorType badType) {
            facts.target.minimumBindingBytes = UINT64_MAX;
            facts.stageMask = UINT32_MAX;
            assert(D::diagnose(owner, badSet, badBinding, badType, facts));
            assert(!facts.target.minimumBindingBytes && !facts.stageMask);
        };
        auto forged = std::shared_ptr<const Artifact>(original->quota(), original.get());
        reject(forged, set, binding, type);
        reject(std::shared_ptr<const Artifact>{}, set, binding, type);
        reject(original, UINT32_MAX, binding, type);
        reject(original, set, UINT32_MAX, type);
        reject(original, set, binding, rhi::DescriptorType::Sampler);
        const auto limit = original->quota()->limitBytes();
        original->quota()->setLimitBytes(limit - 1u);
        reject(original, set, binding, type);
        original->quota()->setLimitBytes(limit);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }

    template <class Device, class Pipeline>
    static void verifyActual(const Device& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
        std::uint32_t set)
    {
        C::Observation out;
        const C::Request request{1u, true};
        using Artifact = shader::SingleEntryShaderArtifact;
        shader::TargetBufferMemoryLayout original;
        assert(!Artifact::originalTargetBufferMemory(
            pipeline->originalComputeCreation()->originalInput()->artifact(), set, 0u, original));
        const rhi::DescriptorBufferBindingRange range{2u, 0u, original.target.minimumBindingBytes};
        const auto diagnose = [&](auto intent,
                                  const auto& owner,
                                  const auto& lease,
                                  std::uint32_t logicalSet,
                                  auto view,
                                  std::uint32_t displacement) {
            out.minimumBindingBytes = UINT64_MAX;
            out.stageMask = UINT32_MAX;
            const auto* id = device.diagnoseOriginalTargetMemoryBufferInterval(
                intent, owner, lease, logicalSet, 0u, view, displacement, out);
            if (id)
                assert(!out.minimumBindingBytes && !out.stageMask && !out.range.sizeBytes
                    && !out.runtimeStrideBytes);
            return id;
        };
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{buffer->originalMetadataQuota()}, [&]() noexcept -> const char* {
                assert(!diagnose(request, pipeline, buffer, set, range, 0u));
                assert(out.minimumBindingBytes == range.sizeBytes
                    && out.range.sizeBytes == range.sizeBytes && !out.range.offsetBytes
                    && out.group == original.target.group && out.binding == original.target.binding
                    && out.runtimeStrideBytes == original.target.runtimeArrayStrideBytes
                    && !out.nativeExecutionProven());
                auto wrong = request;
                wrong.contractVersion = 2u;
                assert(diagnose(wrong, pipeline, buffer, set, range, 0u));
                wrong = request;
                wrong.prototypeOptIn = false;
                assert(diagnose(wrong, pipeline, buffer, set, range, 0u));
                auto small = range;
                --small.sizeBytes;
                assert(diagnose(request, pipeline, buffer, set, small, 0u));
                auto moved = range;
                ++moved.offsetBytes;
                assert(diagnose(request, pipeline, buffer, set, moved, 0u));
                assert(diagnose(request, pipeline, buffer, set, range, 1u));
                assert(diagnose(request, pipeline, buffer, UINT32_MAX, range, 0u));
                auto forged =
                    std::shared_ptr<Pipeline>(buffer->originalMetadataQuota(), pipeline.get());
                assert(diagnose(request, forged, buffer, set, range, 0u));
                auto forgedBuffer = std::shared_ptr<const rhi::OriginalCommandBufferLease>(
                    buffer->originalMetadataQuota(), buffer.get());
                assert(diagnose(request, pipeline, forgedBuffer, set, range, 0u));
                const auto limit = buffer->originalMetadataQuota()->limitBytes();
                buffer->originalMetadataQuota()->setLimitBytes(limit - 1u);
                assert(diagnose(request, pipeline, buffer, set, range, 0u));
                buffer->originalMetadataQuota()->setLimitBytes(limit);
                return nullptr;
            }));
    }
};
} // namespace arp::test
