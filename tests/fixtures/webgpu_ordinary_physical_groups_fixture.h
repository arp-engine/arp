#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_physical_groups.h"
#include "webgpu_generic_command_projection_fixture.h"
#include "webgpu_ordinary_shader_validation_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace arp::test {
class WebGpuOrdinaryPhysicalGroupsFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOrdinaryPhysicalGroupBudget budget;
        assert(std::string_view(WebGpuOrdinaryPhysicalGroups::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-GROUP-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryPhysicalGroups::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-GROUP-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryPhysicalGroups::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-GROUP-BUDGET");
        budget.maxCpuMetadataBytes = metadataBytes;
        assert(!WebGpuOrdinaryPhysicalGroups::validateBudget(budget));
        const char* diagnostic = nullptr;
        assert(!WebGpuOrdinaryPhysicalGroups::prepare({}, nullptr, 0u, budget, {}, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-GROUP-QUOTA");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        assert(!WebGpuOrdinaryPhysicalGroups::prepare({}, nullptr, 0u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-GROUP-NATIVE-UNAVAILABLE"
            && !quota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(core::UniquePtr<rhi_webgpu::WebGpuOrdinaryPipelineObservation> input,
        const char* name,
        std::uint64_t bindingBytes = bufferBytes,
        bool expectedNativeFailure = false,
        bool smallRange = false)
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto observationQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto resourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline;
        {
            core::CpuMemoryQuotaScope scope(observationQuota);
            pipeline = WebGpuOrdinaryPipelineObservation::shareOriginal(std::move(input));
        }
        assert(pipeline && pipeline->hasQuotaBoundSharedOwner(pipeline));
        const auto& issued = pipeline->contract();
        const bool empty = issued.facts().empty();
        assert(issued.sets().size() <= 1u);
        const auto bindingCount = static_cast<std::uint32_t>(issued.facts().size());
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        WebGpuRhiAllocator allocator(
            {"ordinary physical original allocation", bindingBytes, false});
        BufferDesc bufferDesc{"ordinary physical original buffer",
            bindingBytes,
            ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer
                | ResourceUsage::VertexBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu};
        auto allocation =
            allocator.allocate({bufferDesc.label, bufferDesc.sizeBytes, bufferDesc.memory});
        auto buffer = createWebGpuRhiBuffer(
            allocateGpuResourceId(), bufferDesc, allocation, {true, true}, registry);
        assert(buffer);
        registry->registerBuffer(*buffer);
        allocation.releaseToken.reset();
        auto arena = createWebGpuRhiDescriptorArena(
            {"ordinary physical descriptors", 3u, 8u}, registry, {true, true});
        core::UniquePtr<DescriptorSet> descriptor;
        DescriptorSetDesc desc;
        WebGpuOrdinaryDescriptorBudget descriptorBudget{
            1u, true, bindingCount, bindingCount, 1u, metadataBytes};
        WebGpuOrdinaryResourceBudget resources{1u,
            true,
            {kCommandRetentionBudgetContractVersion, true, 3u, 0u, 0u, 0u, metadataBytes},
            1u,
            0u,
            0u,
            bindingBytes};
        const char* diagnostic = nullptr;
        const std::uint32_t zeroOffset = 0u;
        auto observeDescriptor = [&](DescriptorSet& source) {
            const auto offsetCount = source.layout().bindings[0].dynamicOffset ? 1u : 0u;
            core::CpuMemoryQuotaScope scope(observationQuota);
            auto unique = WebGpuOrdinaryDescriptorObservation::prepare(source,
                registry,
                offsetCount ? &zeroOffset : nullptr,
                offsetCount,
                descriptorBudget,
                observationQuota,
                resources,
                resourceQuota,
                diagnostic);
            return WebGpuOrdinaryDescriptorObservation::shareOriginal(std::move(unique));
        };
        std::array<WebGpuOrdinaryObservedSet, 2> sets;
        std::size_t count = empty ? 0u : 1u;
        if (!empty) {
            desc.layout.name = "ordinary physical exact descriptor shape";
            for (const auto& fact : issued.facts())
                desc.layout.bindings.push_back(fact.shape);
            descriptor = arena->allocateDescriptorSet(desc);
            assert(descriptor);
            for (const auto& fact : issued.facts())
                assert(descriptor
                        ->write({fact.shape.binding,
                            fact.shape.type,
                            buffer->id(),
                            DescriptorResourceKind::Buffer})
                        .success);
            sets[0] = {issued.sets()[0].logicalSet, observeDescriptor(*descriptor)};
            assert(sets[0].observation
                && sets[0].observation->sharedOwnerMatches(sets[0].observation));
        }
        WebGpuOrdinaryPhysicalGroupBudget budget{1u,
            true,
            2u,
            bindingCount,
            1u,
            1u,
            1u,
            1u,
            bindingCount * bindingBytes,
            empty ? 0u : bindingBytes,
            metadataBytes};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto factory = [&] {
            return WebGpuOrdinaryPhysicalGroups::prepare(
                pipeline, sets.data(), count, budget, quota, diagnostic);
        };
        auto expect = [&](const char* id) {
            const auto before = quota->liveBytes();
            const auto nativeBefore = nativeCreateCount();
            assert(
                !factory() && std::string_view(diagnostic) == id && quota->liveBytes() == before);
            assert(nativeCreateCount() == nativeBefore);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        const auto beforeExact = nativeCreateCount();
        admission.verify(peak, factory);
        assert(nativeCreateCount() == beforeExact + issued.semantics().groupCount);
        assert(!quota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            expect("RHI-WEBGPU-ORDINARY-GROUP-PREPARATION-ALLOCATION");
        }
        budget.maxCpuMetadataBytes--;
        expect("RHI-WEBGPU-ORDINARY-GROUP-QUOTA");
        budget.maxCpuMetadataBytes++;
        auto originalPipeline = pipeline;
        pipeline =
            std::shared_ptr<const WebGpuOrdinaryPipelineObservation>(quota, originalPipeline.get());
        expect("RHI-WEBGPU-ORDINARY-GROUP-PIPELINE-OWNER");
        pipeline = originalPipeline;
        originalPipeline.reset();
        WebGpuOrdinaryPipelineBudget sourceBudget{2u,
            true,
            issued.semantics().groupCount + 1u,
            static_cast<std::uint32_t>(issued.sets().size()),
            static_cast<std::uint32_t>(issued.bindings().size()),
            static_cast<std::uint32_t>(issued.tables().size()),
            issued.quota()->limitBytes(),
            pipeline->originalMetadataQuota()->limitBytes()};
        WebGpuNativePipelineShaderSourceContract::Inventory inventory;
        assert(!WebGpuNativePipelineShaderSourceContract::inspect(
            issued.stages(), issued.graphics().graphics, inventory));
        sourceBudget.maxShaderStages = issued.stages().size();
        sourceBudget.maxShaderModules = inventory.modules;
        sourceBudget.maxShaderSourceBytes = inventory.bytes;
        sourceBudget.maxColorTargets = issued.graphics().colorFormats.size();
        auto unshared = pipeline->compute()
            ? WebGpuOrdinaryPipelineObservation::prepare(
                  pipeline->compute(), sourceBudget, pipeline->originalMetadataQuota(), diagnostic)
            : WebGpuOrdinaryPipelineObservation::prepare(pipeline->graphics(),
                  sourceBudget,
                  pipeline->originalMetadataQuota(),
                  diagnostic);
        auto directShare = core::shareUnique(std::move(unshared));
        assert(directShare && directShare->sharedOwnerMatches(directShare)
            && !directShare->hasQuotaBoundSharedOwner(directShare));
        originalPipeline = pipeline;
        pipeline = directShare;
        expect("RHI-WEBGPU-ORDINARY-GROUP-PIPELINE-OWNER");
        pipeline = originalPipeline;
        originalPipeline.reset();
        directShare.reset();
        if (!empty) {
            budget.maxSets = 0u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-CAPACITY");
            budget.maxSets = 2u;
            budget.maxBindings = 0u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-CAPACITY");
            budget.maxBindings = bindingCount;
            budget.maxGroups = 0u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-CAPACITY");
            budget.maxGroups = 1u;
            budget.maxNativeGroupLayouts = 0u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-CAPACITY");
            budget.maxNativeGroupLayouts = 1u;
            budget.maxNativeGroups = 0u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-CAPACITY");
            budget.maxNativeGroups = 1u;
            budget.maxResources = 0u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-RESOURCE-CAPACITY");
            budget.maxResources = 1u;
            budget.maxBufferBindingBytes--;
            expect("RHI-WEBGPU-ORDINARY-GROUP-BINDING-BYTE-CAPACITY");
            budget.maxBufferBindingBytes++;
            budget.maxRetainedAllocationBytes--;
            expect("RHI-WEBGPU-ORDINARY-GROUP-ALLOCATION-BYTE-CAPACITY");
            budget.maxRetainedAllocationBytes++;
            count = 0u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-SET-COVERAGE");
            count = 1u;
            sets[1] = sets[0];
            count = 2u;
            expect("RHI-WEBGPU-ORDINARY-GROUP-DUPLICATE-SET");
            ++sets[1].logicalSet;
            expect("RHI-WEBGPU-ORDINARY-GROUP-SET-COVERAGE");
            sets[1].observation.reset();
            count = 1u;
            ++sets[0].logicalSet;
            expect("RHI-WEBGPU-ORDINARY-GROUP-BINDING-COVERAGE");
            --sets[0].logicalSet;
            auto canonical = sets[0].observation;
            sets[0].observation =
                std::shared_ptr<const WebGpuOrdinaryDescriptorObservation>(quota, canonical.get());
            expect("RHI-WEBGPU-ORDINARY-GROUP-DESCRIPTOR-OWNER");
            sets[0].observation = canonical;
            canonical.reset();
            // Another independently valid snapshot with an incompatible scalar shape.
            auto wrongDesc = desc;
            wrongDesc.layout.bindings[0].type = DescriptorType::UniformBuffer;
            wrongDesc.layout.bindings[0].dynamicOffset = false;
            auto wrong = arena->allocateDescriptorSet(wrongDesc);
            assert(wrong
                && wrong
                    ->write({wrongDesc.layout.bindings[0].binding,
                        DescriptorType::UniformBuffer,
                        buffer->id(),
                        DescriptorResourceKind::Buffer})
                    .success);
            for (std::size_t i = 1u; i < wrongDesc.layout.bindings.size(); ++i)
                assert(wrong
                        ->write({wrongDesc.layout.bindings[i].binding,
                            wrongDesc.layout.bindings[i].type,
                            buffer->id(),
                            DescriptorResourceKind::Buffer})
                        .success);
            auto wrongObservation = observeDescriptor(*wrong);
            assert(wrongObservation);
            canonical = sets[0].observation;
            sets[0].observation = wrongObservation;
            expect("RHI-WEBGPU-ORDINARY-GROUP-BINDING-SHAPE");
            sets[0].observation = canonical;
            canonical.reset();
            wrongObservation.reset();
            wrong.reset();
        }
        std::shared_ptr<WebGpuOrdinaryPhysicalGroups> groups;
        {
            core::CpuMemoryQuotaScope scope(quota);
            groups = WebGpuOrdinaryPhysicalGroups::shareOriginal(factory());
        }
        assert(
            groups && !groups->validate() && groups->groupCount() == issued.semantics().groupCount);
        assert(groups->bufferBindingBytes() == budget.maxBufferBindingBytes
            && groups->retainedAllocationBytes() == budget.maxRetainedAllocationBytes);
        assert(std::string_view(groups->executionDiagnostic())
            == (empty ? "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED"
                      : "RHI-WEBGPU-ORDINARY-GROUP-SHADER-SIZE-UNPROVEN"));
        if (!empty) {
            assert(groups->bindingRangeBytes(issued.facts()[0].mapping.webGpuGroup,
                       issued.facts()[0].mapping.webGpuBinding)
                == bindingBytes);
            assert(groups->dynamicOffsetCount(0u)
                == (issued.facts()[0].shape.dynamicOffset ? 1u : 0u));
        }
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto nativeBeforeHot = nativeCreateCount();
        auto payload = webGpuNativePayloadFor(*buffer);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && !groups->validate());
            if (!empty) {
                ++payload->sizeBytes;
                assert(groups->validate());
                --payload->sizeBytes;
                assert(!groups->validate());
                registry->unregisterResource(buffer->id());
                assert(
                    std::string_view(groups->validate()) == "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING");
            }
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            assert(nativeCreateCount() == nativeBeforeHot);
        }
        if (!empty)
            registry->registerBuffer(*buffer);
        const auto nativeBeforeSource = nativeCreateCount();
        WebGpuOriginalScalarAccessFixture::verifyPhysical(groups, name);
        assert(nativeCreateCount() == nativeBeforeSource);
        WebGpuGenericCommandProjectionFixture::verifyGroups(groups, name);
        if (std::string_view(name) == "ordinary-graphics")
            WebGpuOriginalCommandInputsFixture::verifyGraphics(groups);
        WebGpuOrdinaryShaderValidationFixture::verifyActual(
            pipeline, groups, *registry, buffer->id(), name, expectedNativeFailure, smallRange);
        if (std::string_view(name) == "ordinary-graphics") {
            auto originalPayload = webGpuNativePayloadFor(*buffer);
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            {
                core::CpuMemoryQuotaScope scope(quota);
                core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
                assert(pressure.charged());
                ++originalPayload->sizeBytes;
                WebGpuOriginalCommandInputsFixture::verifyGraphicsRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED");
                WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED");
                WebGpuOrdinaryGraphicsRecordingFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED");
                --originalPayload->sizeBytes;
                registry->unregisterResource(buffer->id());
                WebGpuOriginalCommandInputsFixture::verifyGraphicsRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING");
                WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING");
                WebGpuOrdinaryGraphicsRecordingFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING");
                assert(core::defaultCpuAllocator().stats().allocationCount == before);
            }
            registry->registerBuffer(*buffer);
        }
        std::weak_ptr<const WebGpuOrdinaryPipelineObservation> pipelineOwner = pipeline;
        std::weak_ptr<WebGpuNativeBufferPayload> bufferOwner = payload;
        payload.reset();
        pipeline.reset();
        sets[0].observation.reset();
        assert(!pipelineOwner.expired() && !groups->validate());
        if (!empty) {
            descriptor.reset();
            assert(
                std::string_view(groups->validate()) == "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
            if (std::string_view(name) == "ordinary-compute")
                WebGpuFrozenCommandScalarsFixture::verifyRetainedDescriptorExpiry();
            if (pipelineOwner.lock()->compute() && !smallRange)
                WebGpuOrdinaryScalarComputeRangeFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
            if (pipelineOwner.lock()->compute() && !smallRange)
                WebGpuOrdinaryComputeRecordingFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
            if (std::string_view(name) == "ordinary-graphics") {
                WebGpuOriginalCommandInputsFixture::verifyGraphicsRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_INPUTS_GRAPHICS_STALE_OK identity missing descriptor allocation0");
                WebGpuOrdinaryGraphicsTriangleRangeFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
                WebGpuOrdinaryGraphicsRecordingFixture::verifyRetainedDiagnostic(
                    "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED");
            }
        }
        buffer.reset();
        if (!empty)
            assert(
                !bufferOwner.expired() && allocator.memoryStats().allocatedBytes == bindingBytes);
        const bool retainedScalar = pipelineOwner.lock()->compute() && !smallRange;
        const bool retainedGraphics = std::string_view(name) == "ordinary-graphics";
        groups.reset();
        pipelineOwner.reset();
        bufferOwner.reset();
        if (retainedScalar) {
            assert(
                quota->liveBytes() && observationQuota->liveBytes() && resourceQuota->liveBytes());
            WebGpuOrdinaryScalarComputeRangeFixture::retainGraphQuotas(
                {quota, observationQuota, resourceQuota});
        } else if (retainedGraphics) {
            assert(
                quota->liveBytes() && observationQuota->liveBytes() && resourceQuota->liveBytes());
            WebGpuOrdinaryGraphicsTriangleRangeFixture::retainGraphQuotas(
                {quota, observationQuota, resourceQuota});
        } else {
            if (!empty)
                assert(!quota->liveBytes());
            assert(!observationQuota->liveBytes() && !resourceQuota->liveBytes());
        }
        // The two-stream zero-group loss probe deliberately retains its real
        // vertex allocation until terminal loss and late callback verification.
        assert(allocator.memoryStats().allocatedBytes
            == ((empty || retainedScalar || retainedGraphics) ? bindingBytes : 0u));
        std::printf("BINDLESS_RHI_ORDINARY_PHYSICAL_GROUP_CASE_OK %s %llu %llu\n",
            name,
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(empty ? 0u : bindingBytes));
    }
#endif
private:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static std::uint32_t nativeCreateCount() noexcept
    {
        return EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        });
    }
#endif
    static constexpr std::uint64_t metadataBytes = 65536u, bufferBytes = 64u;
};
} // namespace arp::test
