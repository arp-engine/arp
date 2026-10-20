#pragma once
#include "capability_override_device.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "webgpu_async_wait_fixture.h"

namespace arp::test {
// A valid forwarding interface is intentionally insufficient to certify native
// payload adoption. The exact private-factory wrapper is hidden in this facade.
template <class Pipeline> class OriginalPipelineSubstituteFacade final : public Pipeline {
public:
    explicit OriginalPipelineSubstituteFacade(core::UniquePtr<Pipeline> original)
        : original_(std::move(original))
    {}

    const rhi::PipelineLayout* layout() const override { return original_->layout(); }

    const rhi::PipelineValidationReport& validationReport() const override
    {
        return original_->validationReport();
    }

    const rhi::ShaderReflectionDesc& shaderReflection() const override
    {
        return original_->shaderReflection();
    }

    rhi::ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return original_->shaderSourceLanguage();
    }

    bool backendFallbacksHiddenFromUpperLayer() const override
    {
        return original_->backendFallbacksHiddenFromUpperLayer();
    }

    bool nativeHandleExposedToUpperLayer() const override
    {
        return original_->nativeHandleExposedToUpperLayer();
    }

private:
    core::UniquePtr<Pipeline> original_;
};

class OriginalAcceptedPipelineWrapperSealFixture final {
public:
    static void verifyContract()
    {
        static_assert(!std::is_copy_constructible_v<rhi::ComputePipeline>);
        static_assert(!std::is_copy_constructible_v<rhi::RenderPipeline>);
        std::puts(
            "BINDLESS_RHI_ACCEPTED_WRAPPER_SEAL_CONTRACT_OK noncopyable exact-weak-owner private-factories2 backends4");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
private:
    enum class Mode { Substitute, DifferentReceipt, Genuine };

    template <class Pipeline> class Injection final {
        static constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;

    public:
        using Creation = std::conditional_t<compute,
            rhi::OriginalComputePipelineCreation,
            rhi::OriginalGraphicsPipelineCreation>;
        using Desc = std::conditional_t<compute, rhi::ComputePipelineDesc, rhi::RenderPipelineDesc>;
        using Factory = std::conditional_t<compute,
            rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory,
            rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory>;
        Mode mode = Mode::Substitute;
        std::shared_ptr<const Creation> other;

        static core::UniquePtr<Pipeline> create(void* context,
            const Desc& desc,
            const std::shared_ptr<const Creation>& creation,
            const std::shared_ptr<core::CpuMemoryQuota>& quota,
            const char*& diagnostic)
        {
            auto& self = *static_cast<Injection*>(context);
            auto genuine = Factory::create(desc,
                self.mode == Mode::DifferentReceipt ? self.other : creation,
                quota,
                diagnostic);
            assert(
                genuine && genuine->layout() == desc.layout && genuine->validationReport().success);
            if (self.mode != Mode::Substitute)
                return genuine;
            core::CpuMemoryQuotaScope scope(quota);
            return core::makeUnique<OriginalPipelineSubstituteFacade<Pipeline>>(std::move(genuine));
        }
    };

public:
    template <class Pipeline, class Owner, class Context> static void verifyActual(Context& c)
    {
        using Selected = Injection<Pipeline>;
        auto& proxy = static_cast<tests::CapabilityOverrideDevice&>(*c.device);
        Selected injection;
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
            proxy.injectAcceptedFactory(&injection, &Selected::create, nullptr);
        else
            proxy.injectAcceptedFactory(&injection, nullptr, &Selected::create);
        const auto id = std::is_same_v<Pipeline, rhi::ComputePipeline>
            ? "RHI-ACCEPTED-COMPUTE-FACTORY-WRAPPER-SEAL"
            : "RHI-ACCEPTED-GRAPHICS-FACTORY-WRAPPER-SEAL";
        const auto metadataBefore = c.quotas[1]->liveBytes(),
                   wrapperBefore = c.quotas[6]->liveBytes();
        const auto reject = [&] {
            assert(!c.publish() && c.device->lastOriginalResourceRegistrationDiagnostic() == id);
            assert(c.quotas[1]->liveBytes() == metadataBefore
                && c.quotas[6]->liveBytes() == wrapperBefore);
            assert(!c.receipt->validate());
        };
        reject();
        // The same native layout and complete definition, independently accepted
        // by another real actor, cannot certify the requested creation owner.
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto input = c.receipt->originalInput();
        typename Owner::Budget b{1u,
            true,
            Owner::callbackCount,
            64u,
            512u,
            65536u,
            65536u,
            {1u, true, input->artifact()->bytes(shader::ShaderArtifactRole::Wgsl).size(), 65536u}};
        const char* diagnostic = nullptr;
        auto other = [&] {
            if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                return Owner::prepareOriginal(input,
                    c.layout,
                    c.receipt->definition().label,
                    b,
                    quotas[0],
                    quotas[1],
                    quotas[2],
                    diagnostic);
            else
                return Owner::prepareOriginal(input,
                    c.layout,
                    c.receipt->definition(),
                    b,
                    quotas[0],
                    quotas[1],
                    quotas[2],
                    diagnostic);
        }();
        assert(other && !Owner::initialize(other));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return other->scopesObserved();
        }));
        assert(!other->validate());
        assert(!core::SharedOwnerProjection::sameOwner(other, c.receipt));
        injection.other = other;
        injection.mode = Mode::DifferentReceipt;
        reject();
        injection.mode = Mode::Genuine;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto retry = c.publish();
        std::pmr::set_default_resource(previous);
        assert(retry);
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
            assert(!retry->originalArtifactComputeDiagnostic());
        else
            assert(!retry->originalArtifactGraphicsDiagnostic());
        retry.reset();
        assert(c.quotas[1]->liveBytes() == metadataBefore
            && c.quotas[6]->liveBytes() == wrapperBefore);
        // An unregistered private wrapper's seal does not keep its actor alive.
        auto unregistered =
            Selected::Factory::create(other->definition(), other, c.quotas[6], diagnostic);
        assert(unregistered);
        std::weak_ptr<const typename Selected::Creation> observer = other;
        injection.other.reset();
        other.reset();
        assert(observer.expired());
        unregistered.reset();
        observer.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        proxy.injectAcceptedFactory(nullptr, nullptr, nullptr);
        std::printf(
            "BINDLESS_RHI_ACCEPTED_WRAPPER_SEAL_NATIVE_OK %s actual-actors substitute-and-other-receipt-rejected rollback retry weak-custody\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics");
    }
#endif
};
} // namespace arp::test
