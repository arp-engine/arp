#include "webgpu_whole_buffer_binding_fixture.h"
#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_ordinary_descriptor_observation.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {

class WebGpuOrdinaryDescriptorFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOrdinaryDescriptorBudget budget;
        assert(std::string_view(WebGpuOrdinaryDescriptorObservation::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryDescriptorObservation::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryDescriptorObservation::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-BUDGET");
        budget.maxCpuMetadataBytes = metadataBytes;
        assert(!WebGpuOrdinaryDescriptorObservation::validateBudget(budget));
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        auto arena = createWebGpuRhiDescriptorArena(
            {"ordinary observation host validator", 1u, 1u}, registry, {});
        rhi::DescriptorSetDesc desc;
        desc.layout = {"ordinary observation host layout",
            {{0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Compute}}};
        auto descriptor = arena->allocateDescriptorSet(desc);
        assert(descriptor);
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto resourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        WebGpuOrdinaryResourceBudget resources{1u,
            true,
            {rhi::kCommandRetentionBudgetContractVersion, true, 0u, 0u, 0u, 0u, metadataBytes}};
        const char* diagnostic = nullptr;
        assert(!WebGpuOrdinaryDescriptorObservation::prepare(*descriptor,
            registry,
            nullptr,
            0u,
            budget,
            quota,
            resources,
            resourceQuota,
            diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD"
            && !quota->liveBytes() && !resourceQuota->liveBytes());
#endif
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(const std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry>& registry,
        rhi::GpuBuffer& buffer,
        const rhi::GpuTextureView& view,
        const rhi::GpuSampler& sampler,
        std::uint64_t allocationBytes)
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto arena = createWebGpuRhiDescriptorArena(
            {"ordinary descriptor original arena", 2u, 8u}, registry, {true, true});
        DescriptorSetDesc desc;
        desc.layout.name = "ordinary canonical scalar layout";
        desc.layout.bindings = {{0u, DescriptorType::UniformBuffer, ShaderStage::Compute},
            {1u, DescriptorType::SampledTexture, ShaderStage::Compute},
            {2u, DescriptorType::SampledTexture, ShaderStage::Compute},
            {3u, DescriptorType::Sampler, ShaderStage::Compute}};
        desc.layout.bindings[0].dynamicOffset = true;
        auto descriptor = arena->allocateDescriptorSet(desc);
        assert(descriptor);
        const std::array writes{
            DescriptorBindingWrite{
                0u, DescriptorType::UniformBuffer, buffer.id(), DescriptorResourceKind::Buffer},
            DescriptorBindingWrite{
                1u, DescriptorType::SampledTexture, view.id(), DescriptorResourceKind::TextureView},
            DescriptorBindingWrite{
                2u, DescriptorType::SampledTexture, view.id(), DescriptorResourceKind::TextureView},
            DescriptorBindingWrite{
                3u, DescriptorType::Sampler, sampler.id(), DescriptorResourceKind::Sampler}};
        for (const auto& write : writes)
            assert(descriptor->write(write).success);
        WebGpuOrdinaryDescriptorBudget budget{1u, true, 4u, 4u, 1u, metadataBytes};
        WebGpuOrdinaryResourceBudget resources{1u,
            true,
            {kCommandRetentionBudgetContractVersion, true, 7u, 0u, 0u, 0u, metadataBytes},
            3u,
            1u,
            1u,
            allocationBytes};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto resourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        std::uint32_t offset = 0u;
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return WebGpuOrdinaryDescriptorObservation::prepare(*descriptor,
                registry,
                &offset,
                1u,
                budget,
                quota,
                resources,
                resourceQuota,
                diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes() && !resourceQuota->liveBytes());
        CpuQuotaFactoryFixture resourceAdmission(resourceQuota);
        const auto resourcePeak = resourceAdmission.measure(factory);
        resourceAdmission.verify(resourcePeak, factory);
        assert(!quota->liveBytes() && !resourceQuota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged() && !factory());
        }
        {
            core::CpuMemoryQuotaLease pressure(resourceQuota, resourceQuota->limitBytes());
            assert(pressure.charged() && !factory() && !quota->liveBytes());
        }
        budget.maxBindings = 3u;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-CAPACITY");
        budget.maxBindings = 4u;
        budget.maxWrites = 3u;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-CAPACITY");
        budget.maxWrites = 4u;
        budget.maxDynamicOffsets = 0u;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-CAPACITY");
        budget.maxDynamicOffsets = 1u;
        assert(!WebGpuOrdinaryDescriptorObservation::prepare(*descriptor,
            registry,
            nullptr,
            1u,
            budget,
            quota,
            resources,
            resourceQuota,
            diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-OFFSET-COUNT");
        assert(!WebGpuOrdinaryDescriptorObservation::prepare(*descriptor,
            registry,
            nullptr,
            0u,
            budget,
            quota,
            resources,
            resourceQuota,
            diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-OFFSET-COUNT");
        const std::shared_ptr<WebGpuRhiResourceRegistry> aliasRegistry(quota, registry.get());
        assert(!WebGpuOrdinaryDescriptorObservation::prepare(*descriptor,
            aliasRegistry,
            &offset,
            1u,
            budget,
            quota,
            resources,
            resourceQuota,
            diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-REGISTRY");
        budget.maxWrites = UINT32_MAX;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PREPARATION-ALLOCATION");
        budget.maxWrites = 4u;
        offset = 1u;
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-OFFSET-RANGE-UNPROVEN");
        offset = 0u;
        auto observed = factory();
        assert(observed && !observed->validate() && observed->resources().resources().size() == 3u);
        assert(observed->resources().retainedBytes() == allocationBytes
            && observed->resources().storage().resourceOwnerCount() == 7u);
        offset = UINT32_MAX;
        assert(observed->offsets().size() == 1u && observed->offsets()[0] == 0u
            && !observed->validate());
        offset = 0u;
        auto payload = webGpuNativePayloadFor(*descriptor);
        WebGpuWholeBufferBindingFixture::verifyActual(payload);
        const auto sourceWrite = payload->bindings[0].write;
        const auto originalBuffer = payload->bindings[0].buffer;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && !observed->validate());
            payload->bindings[0].write.resourceState = ResourceState::Undefined;
            assert(
                std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD");
            payload->bindings[0].write = sourceWrite;
            payload->bindings[0].buffer =
                std::shared_ptr<WebGpuNativeBufferPayload>(payload, originalBuffer.get());
            assert(
                std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD");
            payload->bindings[0].buffer = originalBuffer;
            ++payload->dynamicOffsetBindings[0];
            assert(
                std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD");
            --payload->dynamicOffsetBindings[0];
            auto& layout = const_cast<DescriptorLayoutDesc&>(descriptor->layout());
            layout.bindings[0].stages = ShaderStage::Fragment;
            assert(
                std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-LAYOUT");
            layout.bindings[0].stages = ShaderStage::Compute;
            assert(!observed->validate()
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        auto invalidWrite = writes[0];
        invalidWrite.binding = 99u;
        assert(!descriptor->write(invalidWrite).success && !observed->validate());
        assert(descriptor->write(writes[0]).success);
        assert(std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-REVISION");
        observed.reset();
        payload.reset();
        assert(!quota->liveBytes() && !resourceQuota->liveBytes());
        observed = factory();
        assert(observed);
        arena->reset();
        assert(std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-STALE");
        observed.reset();
        descriptor.reset();
        descriptor = arena->allocateDescriptorSet(desc);
        assert(descriptor);
        for (const auto& write : writes)
            assert(descriptor->write(write).success);
        observed = factory();
        assert(observed);
        const auto viewId = view.id();
        registry->unregisterResource(viewId);
        assert(std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING");
        registry->registerTextureView(view);
        assert(!observed->validate());
        std::weak_ptr<WebGpuNativeDescriptorGroupPayload> originalGroup = observed->payload();
        descriptor.reset();
        assert(std::string_view(observed->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
        assert(!originalGroup.expired() && observed->resources().matches());
        observed.reset();
        assert(originalGroup.expired() && !quota->liveBytes() && !resourceQuota->liveBytes());
        originalGroup.reset();
        // Writable descriptor liveness changes independently of descriptor revision.
        DescriptorSetDesc storageDesc;
        storageDesc.layout = {"ordinary writable storage observation",
            {{0u, DescriptorType::StorageBuffer, ShaderStage::Compute}}};
        auto storage = arena->allocateDescriptorSet(storageDesc);
        assert(storage
            && storage
                ->write({0u,
                    DescriptorType::StorageBuffer,
                    buffer.id(),
                    DescriptorResourceKind::Buffer})
                .success);
        auto storageObserved = WebGpuOrdinaryDescriptorObservation::prepare(
            *storage, registry, nullptr, 0u, budget, quota, resources, resourceQuota, diagnostic);
        assert(storageObserved && !storageObserved->validate());
        buffer.sealWrites();
        assert(std::string_view(storageObserved->validate()) == "RHI-BUFFER-GPU-WRITES-SEALED");
        arena.reset();
        assert(std::string_view(storageObserved->validate())
            == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-STALE");
        storageObserved.reset();
        storage.reset();
        assert(!quota->liveBytes() && !resourceQuota->liveBytes());
        std::printf("BINDLESS_RHI_ORDINARY_DESCRIPTOR_OBSERVATION_OK %llu %llu 4 3 7\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(resourcePeak));
    }
#endif
private:
    static constexpr std::uint64_t metadataBytes = 1024u * 1024u;
};

} // namespace arp::test
