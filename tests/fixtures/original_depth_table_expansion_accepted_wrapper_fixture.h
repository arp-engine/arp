#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"

namespace arp::test {
class OriginalDepthTableExpansionAcceptedWrapperFixture final {
public:
    template <class Actor> static void refuse(const std::shared_ptr<Actor>& actor)
    {
        using Desc = std::remove_cv_t<std::remove_reference_t<decltype(actor->definition())>>;
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        using Factory = std::conditional_t<compute,
            rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory,
            rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory>;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const char* id = nullptr;
        const auto count = core::defaultCpuAllocator().stats().allocationCount;
        assert(
            !Factory::create(actor->definition(), actor, quota, id) && id && !quota->liveBytes());
        assert(count == core::defaultCpuAllocator().stats().allocationCount);
    }
    template <class Actor> static void verify(const std::shared_ptr<Actor>& actor)
    {
        using Desc = std::remove_cv_t<std::remove_reference_t<decltype(actor->definition())>>;
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        using Factory = std::conditional_t<compute,
            rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory,
            rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory>;
        using NativeFactory = std::conditional_t<compute,
            rhi_webgpu::WebGpuOriginalArtifactComputeFactory,
            rhi_webgpu::WebGpuOriginalArtifactGraphicsFactory>;
        const auto& d = actor->expansionDefinition();
        const auto& input = actor->originalInput();
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const char* id = nullptr;
        const auto create = [&] {
            return Factory::create(actor->definition(), actor, quota, id);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measureSingleUse([] {}, create);
        pressure.verifySingleUse(peak, [] {}, create);
        assert(!quota->liveBytes());
        for (const auto& domain : std::array{actor->originalMetadataQuota(),
                 actor->nativeQuota(),
                 actor->callbackQuota(),
                 d->quota(),
                 d->space()->quota(),
                 input->quota(),
                 input->tableExpansion()->quota()}) {
            const auto count = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Factory::create(actor->definition(), actor, domain, id) && id);
            assert(count == core::defaultCpuAllocator().stats().allocationCount);
        }
        assert(!Factory::create(actor->definition(), actor, {}, id));
        auto forged = std::shared_ptr<Actor>(quota, actor.get());
        assert(!Factory::create(actor->definition(), forged, quota, id));
        forged.reset();
        Desc changed;
        if constexpr (compute)
            changed = rhi::ComputePipelineStateCopy::copy(actor->definition());
        else
            changed = rhi::RenderPipelineStateCopy::copy(actor->definition());
        changed.label = "substituted wrapper definition";
        assert(!Factory::create(changed, actor, quota, id));
        auto package = rhi::ShaderPackageStateCopy::copy(input->package());
        if constexpr (compute)
            changed = rhi::ComputePipelineStateCopy::copy(actor->definition());
        else
            changed = rhi::RenderPipelineStateCopy::copy(actor->definition());
        changed.shaderPackage = &package;
        {
            auto copy = Factory::create(changed, actor, quota, id);
            assert(copy && !id);
        }
        package.reflection.descriptorBindings[0].depthTexture = false;
        assert(!Factory::create(changed, actor, quota, id));
        package.reflection.descriptorBindings[0].depthTexture = true;
        assert(!quota->liveBytes());
        auto wrapper = create();
        assert(wrapper && !id);
        const auto& report = wrapper->validationReport();
        assert(report.success && report.backend == rhi::Backend::WebGPU
            && report.shaderPackageValidated && report.reflectionMatched
            && report.entrypointCount == (compute ? 1u : 2u) && report.descriptorTableCount == 1u
            && report.descriptorSetCount == (compute ? 1u : 0u) && !report.pushConstantBytes
            && report.pipelineCacheKeyStable);
        assert(
            report.descriptorBindingCount == input->package().reflection.descriptorBindings.size());
        if constexpr (compute)
            assert(report.selectedComputeWorkgroupSizeX
                    == actor->definition().computeShader.computeWorkgroupSizeX
                && report.selectedComputeWorkgroupSizeY
                    == actor->definition().computeShader.computeWorkgroupSizeY
                && report.selectedComputeWorkgroupSizeZ
                    == actor->definition().computeShader.computeWorkgroupSizeZ);
        else
            assert(report.colorFormats.size() == report.colorTargetCount
                && report.colorFormats[0] == rhi::TextureFormat::Rgba16Float
                && report.depthStencilFormat == actor->definition().depthStencil.format);
        assert(rhi::PipelineDefinitionIdentity::matches(
            wrapper->shaderReflection(), input->package().reflection));
        assert(wrapper->shaderReflection().descriptorBindings.get_allocator().resource()
            == &core::defaultCpuMemoryResource());
        assert(!wrapper->nativeHandleExposedToUpperLayer()
            && wrapper->backendFallbacksHiddenFromUpperLayer()
            && wrapper->shaderSourceLanguage() == rhi::ShaderSourceLanguage::Slang
            && wrapper->layout() == actor->originalLayout().get());
        auto payload = rhi_webgpu::webGpuNativePayloadFor(*wrapper);
        assert(payload && !NativeFactory::validateConsumedTableExpansion(payload));
        assert(core::SharedOwnerProjection::sameOwner(
            payload->issuedContract()->expansionDefinition(), d));
        assert(wrapper
                ->originalFullDefinitionDiagnostic()); // Backend-private wrapper remains unpublished.
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        EM_ASM({
            // clang-format off
                Module.tableWrapperOriginalDevice = Module.preinitializedWebGPUDevice;
                Module.preinitializedWebGPUDevice = Module.tableEmissionForeignDevice;
            // clang-format on
        });
        assert(!create());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice = Module.tableWrapperOriginalDevice;
            // clang-format on
        });
        // Missing actual queried members refuse; no numeric limit is replaced.
        EM_ASM({
            // clang-format off
                Module.tableWrapperLimits = Module.preinitializedWebGPUDevice.limits;
            // clang-format on
        });
        for (const auto* member : {"minUniformBufferOffsetAlignment",
                 "minStorageBufferOffsetAlignment",
                 "maxUniformBufferBindingSize",
                 "maxStorageBufferBindingSize"}) {
            EM_ASM({
                // clang-format off
                    const missing = UTF8ToString($0);
                    Object.defineProperty(Module.preinitializedWebGPUDevice, 'limits', {
                        configurable : true,
                        value : new Proxy(Module.tableWrapperLimits, {
                            get(target, name) {
                                if (name === missing)
                                    return undefined;
                                return Reflect.get(target, name, target);
                            }
                        })
                    });
        // clang-format on
    },member);
    assert(!create() && core::StringView(id) == "RHI-ORIGINAL-TABLE-WRAPPER-DESCRIPTOR-LIMITS");
    EM_ASM({
        // clang-format off
            delete Module.preinitializedWebGPUDevice.limits;
        // clang-format on
    });
}
#endif
assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota,
                                                          actor->originalMetadataQuota(),
                                                          actor->nativeQuota(),
                                                          actor->callbackQuota(),
                                                          d->quota(),
                                                          d->space()->quota(),
                                                          input->quota(),
                                                          input->tableExpansion()->quota()},
    [&]() noexcept {
        return actor->validate();
    }));
wrapper.reset();
payload.reset();
assert(!quota->liveBytes());
std::printf(
    "ORIGINAL_TABLE_EXPANSION_ACCEPTED_WRAPPER_OK %s existing-seal copied-full-definition reflection-core table-report actual-limits eight-domains exact-less-exhausted foreign-device hot0 unpublished commands-closed\n",
    compute ? "Compute" : "Graphics");
}
}
;
} // namespace arp::test
