#pragma once

#include "core/runtime/creation_receipt_lifecycle.h"
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preflight.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

#include "core/ownership/external_object_slot.h"
#include "core/ownership/shared_owner_projection.h"
#include "core/ownership/versioned_shared_owner.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_original_depth_table_expansion_pipeline_definition.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/original_artifact_compute_creation_lowering.h"
#include "rhi/semantics/original_artifact_graphics_creation_lowering.h"

// region Webgpu artifact creation scope support
namespace arp::rhi_webgpu {
// Cold reserve + actual callback custody shared by both typed creation actors.
// Actors grant only this helper access to their private scope-observation hook.
template <class Actor> class WebGpuArtifactCreationScopeCallbacks final {
    struct Callback {
        std::shared_ptr<Actor> owner;
        std::uint32_t participant = 0u;
    };

public:
    static constexpr auto count = WebGpuNativeErrorScopeBatch::count;

    void prepare(const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        core::CpuMemoryQuotaScope scope(quota);
        for (std::uint32_t i = 0u; i < count; ++i) {
            callbacks_[i] = core::makeUnique<Callback>();
            callbacks_[i]->participant = i;
        }
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    void pop(WGPUDevice device, const std::shared_ptr<Actor>& owner) noexcept
    {
        WebGpuNativeErrorScopeBatch::pop(device, owner, callbacks_, 0u, &completeScope);
    }

private:
    static void completeScope(WGPUPopErrorScopeStatus status,
        WGPUErrorType type,
        WGPUStringView,
        void* userdata,
        void*) noexcept
    {
        auto* original = static_cast<Callback*>(userdata);
        core::UniquePtr<Callback> callback(original,
            core::CpuAllocatedDeleter<Callback>{
                original, &core::destroyCpuAllocatedUnique<Callback>});
        if (callback && callback->owner)
            callback->owner->observeCreationScope(callback->participant,
                status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
    }
#endif
private:
    std::array<core::UniquePtr<Callback>, count> callbacks_;
};

// endregion Webgpu artifact creation scope support

// region Webgpu original artifact creation source

// A trivial envelope discriminates the two typed requests. It is storage only;
// native execution consumes the matching public budget, never the envelope.
template <class OrdinaryBudget, class Input> class WebGpuOriginalArtifactCreationSource final {
public:
    using Definition = WebGpuOriginalDepthTableExpansionPipelineDefinition;
    using ExpansionBudget = rhi::OriginalDepthTableExpansionCreationContract::Budget;

    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxCallbacks = 0u;
        std::uint64_t maxLabelBytes = 0u, maxDiagnosticBytes = 0u, maxCpuOwnerBytes = 0u,
                      maxCpuCallbackBytes = 0u;

        struct Native {
            std::uint32_t contractVersion = 0u;
            bool prototypeOptIn = false;
            std::uint64_t maxTargetBytes = 0u, maxCpuMetadataBytes = 0u;
        } native;
    };

    WebGpuOriginalArtifactCreationSource(OrdinaryBudget budget, std::shared_ptr<const Input> input)
        : source_(envelope(budget, 1u), std::move(input))
    {}

    WebGpuOriginalArtifactCreationSource(
        ExpansionBudget budget, std::shared_ptr<const Definition> definition)
        : source_(envelope(budget, 2u), std::move(definition))
    {}

    bool expanded() const noexcept { return request().contractVersion == 2u; }

    const Request& request() const noexcept { return source_.request(); }

    const auto& expansionDefinition() const noexcept { return source_.expanded(); }

    const std::shared_ptr<const Input>& input() const noexcept
    {
        return source_.visit([](const auto& owner) noexcept -> const std::shared_ptr<const Input>& {
            if constexpr (std::is_same_v<std::decay_t<decltype(owner)>,
                              std::shared_ptr<const Input>>)
                return owner;
            else {
                if (!owner)
                    return missingInput_;
                if constexpr (std::is_same_v<Input, rhi::OriginalSingleEntryPipelineInput>)
                    return owner->space()->computeInput();
                else {
                    static_assert(std::is_same_v<Input, rhi::OriginalGraphicsPipelineInput>);
                    return owner->space()->graphicsInput();
                }
            }
        });
    }

    OrdinaryBudget ordinaryBudget() const noexcept
    {
        if (expanded())
            return {};
        return typed<OrdinaryBudget>();
    }

    ExpansionBudget expansionBudget() const noexcept
    {
        if (!expanded())
            return {};
        return typed<ExpansionBudget>();
    }

private:
    template <class B> static Request envelope(const B& b, std::uint32_t version) noexcept
    {
        return {version,
            b.prototypeOptIn,
            b.maxCallbacks,
            b.maxLabelBytes,
            b.maxDiagnosticBytes,
            b.maxCpuOwnerBytes,
            b.maxCpuCallbackBytes,
            {b.native.contractVersion,
                b.native.prototypeOptIn,
                b.native.maxTargetBytes,
                b.native.maxCpuMetadataBytes}};
    }

    template <class B> B typed() const noexcept
    {
        const auto& r = request();
        return {1u,
            r.prototypeOptIn,
            r.maxCallbacks,
            r.maxLabelBytes,
            r.maxDiagnosticBytes,
            r.maxCpuOwnerBytes,
            r.maxCpuCallbackBytes,
            {r.native.contractVersion,
                r.native.prototypeOptIn,
                r.native.maxTargetBytes,
                r.native.maxCpuMetadataBytes}};
    }

    core::VersionedSharedOwner<Request, Input, Definition, 2u> source_;
    inline static const std::shared_ptr<const Input> missingInput_{};
    static_assert(sizeof(Request) == sizeof(OrdinaryBudget));
    static_assert(sizeof(Request) == sizeof(ExpansionBudget));
};

// endregion Webgpu original artifact creation source

// region Webgpu original artifact compute factory

// Backend-private cold factory; no original L2 registration or command proof.
class WebGpuOriginalArtifactComputeFactory final {
public:
    using Budget = rhi::OriginalPipelineArtifactConsumptionContract::Budget;
    using ExpansionBudget = rhi::OriginalDepthTableExpansionNativeEmissionContract::Budget;
    static std::shared_ptr<WebGpuNativeComputePipelinePayload> createTableExpansion(
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>&,
        ExpansionBudget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String&);
    static const char* validateConsumedTableExpansion(
        const std::shared_ptr<WebGpuNativeComputePipelinePayload>&) noexcept;

    static std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>
    prepareTableExpansionDefinition(
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::ComputePipelineDesc& desc,
        const WebGpuOriginalDepthTableExpansionPipelineDefinition::Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        return WebGpuOriginalDepthTableExpansionPipelineDefinition::prepareOriginal(
            rhi::Backend::WebGPU, space, layout, desc, budget, quota, diagnostic);
    }

    static const char* validateTableExpansionDefinition(
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::ComputePipelineDesc& desc,
        const WebGpuOriginalDepthTableExpansionPipelinePreflight::Budget& budget)
    {
        WebGpuOriginalDepthTableExpansionPipelinePreflight::Requirements required;
        if (const auto* id = WebGpuOriginalDepthTableExpansionPipelinePreflight::inspect(
                input, space, layout, desc, budget, required))
            return id;
        return WebGpuOriginalDepthTableExpansionPipelinePreflight::query(required).diagnostic;
    }

    static std::shared_ptr<WebGpuNativeComputePipelinePayload> create(
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&,
        const rhi::PipelineLayout&,
        core::StringView label,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String& diagnostic);
    // Exact byte consumption only; asynchronous creation validation and
    // canonical command execution remain separately closed.
    static const char* validateConsumedOriginal(
        const std::shared_ptr<WebGpuNativeComputePipelinePayload>&) noexcept;
};

// endregion Webgpu original artifact compute factory

// region Webgpu original artifact graphics factory

class WebGpuOriginalArtifactGraphicsFactory final {
public:
    using Budget = rhi::OriginalGraphicsArtifactConsumptionContract::Budget;
    using ExpansionBudget = rhi::OriginalDepthTableExpansionNativeEmissionContract::Budget;
    static std::shared_ptr<WebGpuNativeRenderPipelinePayload> createTableExpansion(
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>&,
        ExpansionBudget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String&);
    static const char* validateConsumedTableExpansion(
        const std::shared_ptr<WebGpuNativeRenderPipelinePayload>&) noexcept;

    static std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>
    prepareTableExpansionDefinition(
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::RenderPipelineDesc& desc,
        const WebGpuOriginalDepthTableExpansionPipelineDefinition::Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        return WebGpuOriginalDepthTableExpansionPipelineDefinition::prepareOriginal(
            rhi::Backend::WebGPU, space, layout, desc, budget, quota, diagnostic);
    }

    static const char* validateTableExpansionDefinition(
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionBindingSpace>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::RenderPipelineDesc& desc,
        const WebGpuOriginalDepthTableExpansionPipelinePreflight::Budget& budget)
    {
        WebGpuOriginalDepthTableExpansionPipelinePreflight::Requirements required;
        if (const auto* id = WebGpuOriginalDepthTableExpansionPipelinePreflight::inspect(
                input, space, layout, desc, budget, required))
            return id;
        return WebGpuOriginalDepthTableExpansionPipelinePreflight::query(required).diagnostic;
    }

    static bool validateDefinition(const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        const rhi::RenderPipelineDesc&,
        core::pmr::String& diagnostic,
        std::uint32_t profileVersion = 1u);
    static std::shared_ptr<WebGpuNativeRenderPipelinePayload> create(
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        const rhi::RenderPipelineDesc&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String& diagnostic);
    static const char* validateConsumedOriginal(
        const std::shared_ptr<WebGpuNativeRenderPipelinePayload>&) noexcept;
};

// endregion Webgpu original artifact graphics factory

// region Webgpu accepted original compute pipeline factory

// Only this backend-private factory may consume an accepted actor's payload.
class WebGpuAcceptedOriginalComputePipelineFactory final {
public:
    static core::UniquePtr<rhi::ComputePipeline> create(const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const rhi::OriginalComputePipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota,
        const char*& diagnostic);
};

// endregion Webgpu accepted original compute pipeline factory

// region Webgpu accepted original graphics pipeline factory

class WebGpuAcceptedOriginalGraphicsPipelineFactory final {
public:
    static core::UniquePtr<rhi::RenderPipeline> create(const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota,
        const char*& diagnostic);
};
} // namespace arp::rhi_webgpu

// endregion Webgpu accepted original graphics pipeline factory

// region Webgpu accepted original pipeline validation
namespace arp::rhi_webgpu {
class WebGpuAcceptedOriginalPipelineValidation final {
public:
    template <class Actor, class Desc, class Creation>
    static const Actor* validate(const Desc& desc,
        const std::shared_ptr<const Creation>& creation,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota,
        const char* actorId,
        const char* definitionId,
        const char* domainId,
        const char*& diagnostic) noexcept
    {
        diagnostic = Creation::validateOriginal(creation);
        if (diagnostic)
            return nullptr;
        const auto* actor = dynamic_cast<const Actor*>(creation.get());
        if (!actor || desc.layout != actor->originalLayout().get()) {
            diagnostic = actorId;
            return nullptr;
        }
        if (!rhi::PipelineDefinitionIdentity::matches(desc, actor->definition())) {
            diagnostic = definitionId;
            return nullptr;
        }
        if (actor->isTableExpansion()) {
            if (!wrapperQuota || !wrapperQuota.use_count()
                || actor->usesCpuQuotaDomain(wrapperQuota)) {
                diagnostic = domainId;
                return nullptr;
            }
            return actor;
        }
        const std::array domains{wrapperQuota,
            actor->originalMetadataQuota(),
            actor->nativeQuota(),
            actor->callbackQuota(),
            actor->originalInput()->quota(),
            actor->originalInput()->artifact()->quota()};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                domains.size(), [&](std::size_t i) -> const auto& {
                    return domains[i];
                })) {
            diagnostic = domainId;
            return nullptr;
        }
        return actor;
    }
};
} // namespace arp::rhi_webgpu

// endregion Webgpu accepted original pipeline validation

// region Webgpu original depth table expansion creation validation

namespace arp::rhi_webgpu {
// Shared cold admission and allocation-free immutable actor custody validation.
class WebGpuOriginalDepthTableExpansionCreationValidation final {
public:
    using Contract = rhi::OriginalDepthTableExpansionCreationContract;
    using Definition = WebGpuOriginalDepthTableExpansionPipelineDefinition;

    template <class Desc> static const auto& definition(const Definition& d) noexcept
    {
        if constexpr (std::is_same_v<Desc, rhi::ComputePipelineDesc>)
            return d.computeDefinition();
        else {
            static_assert(std::is_same_v<Desc, rhi::RenderPipelineDesc>);
            return d.graphicsDefinition();
        }
    }

    template <class Desc>
    static const char* validate(const std::shared_ptr<const Definition>& d,
        const Contract::Budget& b,
        const std::shared_ptr<core::CpuMemoryQuota>& owner,
        const std::shared_ptr<core::CpuMemoryQuota>& native,
        const std::shared_ptr<core::CpuMemoryQuota>& callback,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const Desc& desc) noexcept
    {
        if (const auto* id = Contract::validateBudget(b))
            return id;
        if (const auto* id = Definition::validateOriginal(d))
            return id;
        if (d->backend() != rhi::Backend::WebGPU)
            return "RHI-ORIGINAL-TABLE-CREATION-DEFINITION-BACKEND";
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        const auto& input = [&]() -> const auto& {
            if constexpr (compute)
                return d->space()->computeInput();
            else
                return d->space()->graphicsInput();
        }();
        if (!input || d->requirements().compute != compute)
            return "RHI-ORIGINAL-TABLE-CREATION-PROFILE";
        if (!core::SharedOwnerProjection::sameOwner(layout, d->layout())
            || !rhi::PipelineDefinitionIdentity::matches(desc, definition<Desc>(*d)))
            return "RHI-ORIGINAL-TABLE-CREATION-DEFINITION";
        if (desc.label.size() > b.maxLabelBytes)
            return "RHI-ORIGINAL-TABLE-CREATION-LABEL";
        if (b.native.maxTargetBytes < d->requirements().shaderSourceBytes)
            return "RHI-ORIGINAL-TABLE-CREATION-TARGET-BUDGET";
        if (!owner || owner->limitBytes() != b.maxCpuOwnerBytes || !native
            || native->limitBytes() != b.native.maxCpuMetadataBytes || !callback
            || callback->limitBytes() != b.maxCpuCallbackBytes)
            return "RHI-ORIGINAL-TABLE-CREATION-QUOTA-LIMIT";
        const std::array domains{owner,
            native,
            callback,
            d->quota(),
            d->space()->quota(),
            input->quota(),
            input->tableExpansion()->quota()};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                domains.size(), [&](std::size_t i) -> const auto& {
                    return domains[i];
                }))
            return "RHI-ORIGINAL-TABLE-CREATION-QUOTA-DOMAIN";
        if constexpr (!compute)
            if (const auto* id =
                    rhi::OriginalDepthTableExpansionNativeEmissionContract::validateState(desc))
                return id;
        return nullptr;
    }

    static Contract::Decision query(const Definition& definition) noexcept
    {
        return Contract::lowering(rhi::Backend::WebGPU,
            rhi::OriginalDepthTableExpansionNativeEmissionContract::lowering(
                rhi::Backend::WebGPU, Definition::Preflight::query(definition.requirements())));
    }

    template <class Source>
    static bool usesQuotaDomain(const Source& source,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const std::shared_ptr<core::CpuMemoryQuota>& owner,
        const std::shared_ptr<core::CpuMemoryQuota>& native,
        const std::shared_ptr<core::CpuMemoryQuota>& callback) noexcept
    {
        const auto& d = source.expansionDefinition();
        for (const auto& domain : std::array{owner,
                 native,
                 callback,
                 d->quota(),
                 d->space()->quota(),
                 source.input()->quota(),
                 source.input()->tableExpansion()->quota()})
            if (core::SharedOwnerProjection::overlapsOwner(quota, domain))
                return true;
        return false;
    }
};

// endregion Webgpu original depth table expansion creation validation

// region Webgpu original artifact compute creation

// Externally serialized cold actor. Ready proves creation only, never commands.
class WebGpuOriginalArtifactComputeCreation final : public rhi::OriginalComputePipelineCreation {
    friend class WebGpuAcceptedOriginalComputePipelineFactory;

    class ConstructionKey {
        friend class WebGpuOriginalArtifactComputeCreation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    friend class WebGpuArtifactCreationScopeCallbacks<WebGpuOriginalArtifactComputeCreation>;

public:
    using Budget = rhi::OriginalArtifactComputeCreationContract::Budget;
    using Source =
        WebGpuOriginalArtifactCreationSource<Budget, rhi::OriginalSingleEntryPipelineInput>;
    using ExpansionBudget = Source::ExpansionBudget;
    using ExpansionDefinition = Source::Definition;
    using ExpansionValidation = WebGpuOriginalDepthTableExpansionCreationValidation;
    using Lowering = rhi::semantics::OriginalArtifactComputeCreationLowering;
    static constexpr auto callbackCount = WebGpuNativeErrorScopeBatch::count;
    using Status = WebGpuArtifactCreationNativeMethods::Status;

    WebGpuOriginalArtifactComputeCreation(ConstructionKey,
        Budget budget,
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> input,
        std::shared_ptr<rhi::PipelineLayout> layout,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota,
        core::StringView label)
        : source_(budget, std::move(input)), layout_(std::move(layout)), nativeQuota_(nativeQuota),
          callbackQuota_(callbackQuota), device_(&releaseDevice)
    {
        definition_.label = label;
        definition_.layout = layout_.get();
        definition_.shaderPackage = &source_.input()->package();
        definition_.computeShader = source_.input()->package().entrypoints.front();
        sourceDiagnostic_.reserve(budget.maxDiagnosticBytes);
    }

    WebGpuOriginalArtifactComputeCreation(ConstructionKey,
        ExpansionBudget budget,
        std::shared_ptr<const ExpansionDefinition> definition,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota)
        : source_(budget, std::move(definition)), layout_(source_.expansionDefinition()->layout()),
          nativeQuota_(nativeQuota), callbackQuota_(callbackQuota), device_(&releaseDevice)
    {
        definition_ = rhi::ComputePipelineStateCopy::copy(
            ExpansionValidation::definition<rhi::ComputePipelineDesc>(
                *source_.expansionDefinition()));
        sourceDiagnostic_.reserve(budget.maxDiagnosticBytes);
    }

    static const char* validateBudget(const Budget& budget) noexcept
    {
        return rhi::OriginalArtifactComputeCreationContract::validateBudget(budget);
    }

    static Lowering::Facts nativeFacts() noexcept
    {
        const auto f = WebGpuArtifactCreationNativeMethods::query(
            WebGpuArtifactCreationNativeMethods::Kind::Compute);
        return {f.device,
            f.shader,
            f.pipeline,
            f.bindGroupLayout,
            f.pipelineLayout,
            f.pushScope,
            f.popScope};
    }

    static std::shared_ptr<WebGpuOriginalArtifactComputeCreation> prepareOriginal(
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        core::StringView label,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (webGpuNativeDeviceLost()) {
            diagnostic = "RHI-ARTIFACT-CREATION-DEVICE-LOST";
            return {};
        }
        if ((diagnostic = rhi::OriginalSingleEntryPipelineInput::validateOriginal(input)))
            return {};
        if (input->artifact()->stage() != shader::ShaderStage::Compute) {
            diagnostic = "RHI-ARTIFACT-CREATION-STAGE";
            return {};
        }
        if (!layout || !layout->sharedOwnerMatches(layout)) {
            diagnostic = "RHI-ARTIFACT-CREATION-LAYOUT-OWNER";
            return {};
        }
        if (label.size() > budget.maxLabelBytes) {
            diagnostic = "RHI-ARTIFACT-CREATION-LABEL-CAPACITY";
            return {};
        }
        if (!ownerQuota || ownerQuota->limitBytes() != budget.maxCpuOwnerBytes || !nativeQuota
            || nativeQuota->limitBytes() != budget.native.maxCpuMetadataBytes || !callbackQuota
            || callbackQuota->limitBytes() != budget.maxCpuCallbackBytes) {
            diagnostic = "RHI-ARTIFACT-CREATION-QUOTA";
            return {};
        }
        const std::array domains{
            ownerQuota, nativeQuota, callbackQuota, input->quota(), input->artifact()->quota()};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                domains.size(), [&](std::size_t i) -> const auto& {
                    return domains[i];
                })) {
            diagnostic = "RHI-ARTIFACT-CREATION-QUOTA-DOMAIN";
            return {};
        }
        const auto decision = Lowering::decide(rhi::Backend::WebGPU, nativeFacts());
        if (!decision.creationEligible) {
            diagnostic = decision.diagnostic;
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(ownerQuota);
            auto result = core::makeUnique<WebGpuOriginalArtifactComputeCreation>(
                ConstructionKey{}, budget, input, layout, nativeQuota, callbackQuota, label);
            result->callbacks_.prepare(callbackQuota);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ARTIFACT-CREATION-ALLOCATION";
            return {};
        }
    }

    static std::shared_ptr<WebGpuOriginalArtifactComputeCreation> prepareTableExpansion(
        const std::shared_ptr<const ExpansionDefinition>& definition,
        const ExpansionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota,
        const char*& diagnostic)
    {
        diagnostic = rhi::OriginalDepthTableExpansionCreationContract::validateBudget(budget);
        if (diagnostic)
            return {};
        if (webGpuNativeDeviceLost()) {
            diagnostic = "RHI-ORIGINAL-TABLE-CREATION-DEVICE-LOST";
            return {};
        }
        if ((diagnostic = ExpansionDefinition::validateOriginal(definition)))
            return {};
        if ((diagnostic = ExpansionValidation::validate(definition,
                 budget,
                 ownerQuota,
                 nativeQuota,
                 callbackQuota,
                 definition->layout(),
                 ExpansionValidation::definition<rhi::ComputePipelineDesc>(*definition))))
            return {};
        const auto decision = ExpansionValidation::query(*definition);
        if (!decision.creationEligible) {
            diagnostic = decision.diagnostic;
            return {};
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        try {
            core::CpuMemoryQuotaScope scope(ownerQuota);
            auto result = core::makeUnique<WebGpuOriginalArtifactComputeCreation>(
                ConstructionKey{}, budget, definition, nativeQuota, callbackQuota);
            result->callbacks_.prepare(callbackQuota);
            auto device = emscripten_webgpu_get_device();
            if (!device || !result->device_.adopt(device)) {
                if (device)
                    wgpuDeviceRelease(device);
                diagnostic = "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
                return {};
            }
            if (!WebGpuArtifactCreationNativeMethods::matchesCurrentDevice(device)) {
                diagnostic = "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
                return {};
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-TABLE-CREATION-ALLOCATION";
            return {};
        }
#else
        diagnostic = "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
        return {};
#endif
    }

    static const char* cancelTableExpansion(
        const std::shared_ptr<WebGpuOriginalArtifactComputeCreation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-TABLE-CREATION-OWNER";
        if (!self->source_.expanded())
            return "RHI-ORIGINAL-TABLE-CREATION-PROFILE";
        self->reject("RHI-ORIGINAL-TABLE-CREATION-CANCELLED");
        return nullptr;
    }

    // Native construction is cold and quota-bounded. Observation/callbacks are hot.
    static const char* initialize(
        const std::shared_ptr<WebGpuOriginalArtifactComputeCreation>& self)
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ARTIFACT-CREATION-OWNER";
        if (webGpuNativeDeviceLost()) {
            if (self->source_.expanded())
                self->reject("RHI-ORIGINAL-TABLE-CREATION-DEVICE-LOST");
            return "RHI-ARTIFACT-CREATION-DEVICE-LOST";
        }
        if (self->status() != Status::Prepared)
            return "RHI-ARTIFACT-CREATION-START-STATUS";
        if (const auto* id = self->validateInputs()) {
            if (self->source_.expanded())
                self->reject(id);
            return id;
        }
        if (self->source_.expanded()) {
            if (!WebGpuArtifactCreationNativeMethods::matchesCurrentDevice(self->device_.get())) {
                self->reject("RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE");
                return self->lifecycle_.failure();
            }
            const auto selected = ExpansionValidation::query(*self->source_.expansionDefinition());
            if (!selected.creationEligible) {
                self->reject(selected.diagnostic);
                return self->lifecycle_.failure();
            }
        }
        if (!self->source_.expanded()) {
            const auto decision = Lowering::decide(rhi::Backend::WebGPU, nativeFacts());
            if (!decision.creationEligible)
                return decision.diagnostic;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto device = self->source_.expanded() ? static_cast<WGPUDevice>(self->device_.get())
                                               : emscripten_webgpu_get_device();
        if (!device || (!self->source_.expanded() && !self->device_.adopt(device))) {
            if (device)
                wgpuDeviceRelease(device);
            return "RHI-ARTIFACT-CREATION-NATIVE-DEVICE";
        }
        (void)self->lifecycle_.begin();
        WebGpuNativeErrorScopeBatch::push(device);
        try {
            if (self->source_.expanded())
                self->pipeline_ = WebGpuOriginalArtifactComputeFactory::createTableExpansion(
                    self->source_.expansionDefinition(),
                    self->source_.expansionBudget().native,
                    self->nativeQuota_,
                    self->sourceDiagnostic_);
            else
                self->pipeline_ =
                    WebGpuOriginalArtifactComputeFactory::create(self->source_.input(),
                        *self->layout_,
                        self->definition_.label,
                        self->source_.ordinaryBudget().native,
                        self->nativeQuota_,
                        self->sourceDiagnostic_);
            if (self->sourceDiagnostic_.size() > self->source_.request().maxDiagnosticBytes)
                self->reject("RHI-ARTIFACT-CREATION-DIAGNOSTIC-CAPACITY");
            if (!self->pipeline_)
                self->reject("RHI-ARTIFACT-CREATION-FACTORY-REJECTED");
        } catch (const std::bad_alloc&) {
            self->reject("RHI-ARTIFACT-CREATION-NATIVE-ALLOCATION");
        }
        self->callbacks_.pop(device, self);
        return self->lifecycle_.failure();
#else
        return "RHI-ARTIFACT-CREATION-WEBGPU-DEVICE";
#endif
    }

    static const char* validateReadyOriginal(
        const std::shared_ptr<const WebGpuOriginalArtifactComputeCreation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ARTIFACT-CREATION-OWNER";
        return self->validate();
    }

    Status status() const noexcept
    {
        const auto result = WebGpuArtifactCreationNativeMethods::status(lifecycle_);
        if (source_.expanded() && result == Status::DeviceLost)
            pipeline_.reset();
        return result;
    }

    bool isTableExpansion() const noexcept { return source_.expanded(); }

    const auto& expansionDefinition() const noexcept { return source_.expansionDefinition(); }

    bool scopesObserved() const noexcept { return lifecycle_.evidence().allReceived(); }

    bool scopeObserved(std::uint32_t participant) const noexcept
    {
        return lifecycle_.evidence().hasReceived(participant);
    }

    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&
    originalInput() const noexcept override
    {
        return source_.input();
    }

    const std::shared_ptr<rhi::PipelineLayout>& originalLayout() const noexcept override
    {
        return layout_;
    }

    const rhi::ComputePipelineDesc& definition() const noexcept override { return definition_; }

    const auto& nativeQuota() const noexcept { return nativeQuota_; }

    const auto& callbackQuota() const noexcept { return callbackQuota_; }

    const auto& sourceDiagnostic() const noexcept { return sourceDiagnostic_; }

    bool usesCpuQuotaDomain(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        if (source_.expanded())
            return ExpansionValidation::usesQuotaDomain(
                source_, quota, originalMetadataQuota(), nativeQuota_, callbackQuota_);
        for (const auto& domain : std::array{originalMetadataQuota(),
                 nativeQuota_,
                 callbackQuota_,
                 source_.input()->quota(),
                 source_.input()->artifact()->quota()})
            if (quota.get() == domain.get()
                || core::SharedOwnerProjection::sameControlBlock(quota, domain))
                return true;
        return false;
    }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-ARTIFACT-CREATION-CANONICAL-JOIN-MISSING";
    }

    const char* validate() const noexcept override
    {
        switch (status()) {
        case Status::Prepared:
            return "RHI-ARTIFACT-CREATION-UNINITIALIZED";
        case Status::Pending:
            return "RHI-ARTIFACT-CREATION-PENDING";
        case Status::Rejected:
            return lifecycle_.failure();
        case Status::DeviceLost:
            return "RHI-ARTIFACT-CREATION-DEVICE-LOST";
        case Status::Ready:
            if (!lifecycle_.evidence().proven())
                return "RHI-ARTIFACT-CREATION-EVIDENCE";
            if (const auto* id = validateInputs())
                return id;
            return validateConsumption();
        }
        return "RHI-ARTIFACT-CREATION-STATUS";
    }

private:
    const char* validateConsumption() const noexcept
    {
        if (source_.expanded()) {
            if (const auto* id =
                    WebGpuOriginalArtifactComputeFactory::validateConsumedTableExpansion(pipeline_))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(
                    pipeline_->issuedContract()->expansionDefinition(),
                    source_.expansionDefinition()))
                return "RHI-ORIGINAL-TABLE-CREATION-DEFINITION";
            if (pipeline_->issuedContract()->nativeDevice() != device_.get()
                || !WebGpuArtifactCreationNativeMethods::matchesCurrentDevice(device_.get()))
                return "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
            return nullptr;
        }
        if (const auto* id =
                WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(pipeline_))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                pipeline_->issuedContract()->artifactInput(), source_.input()))
            return "RHI-ARTIFACT-CREATION-INPUT";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto device = static_cast<WGPUDevice>(device_.get());
        if (!device || pipeline_->issuedContract()->nativeDevice() != device
            || !EM_ASM_INT(
                {
                    // clang-format off
                    return WebGPU.getJsObject(Number($0)) === Module.preinitializedWebGPUDevice;
                    // clang-format on
                },
                device))
            return "RHI-ARTIFACT-CREATION-NATIVE-DEVICE";
#endif
        return nullptr;
    }

    const char* validateInputs() const noexcept
    {
        if (source_.expanded())
            return ExpansionValidation::validate(source_.expansionDefinition(),
                source_.expansionBudget(),
                originalMetadataQuota(),
                nativeQuota_,
                callbackQuota_,
                layout_,
                definition_);
        const auto& input = source_.input();
        const auto& budget = source_.request();
        if (const auto* id = rhi::OriginalSingleEntryPipelineInput::validateOriginal(input))
            return id;
        if (!layout_ || !layout_->sharedOwnerMatches(layout_) || definition_.layout != layout_.get()
            || definition_.shaderPackage != &input->package())
            return "RHI-ARTIFACT-CREATION-INPUT";
        if (originalMetadataQuota()->limitBytes() != budget.maxCpuOwnerBytes
            || nativeQuota_->limitBytes() != budget.native.maxCpuMetadataBytes
            || callbackQuota_->limitBytes() != budget.maxCpuCallbackBytes)
            return "RHI-ARTIFACT-CREATION-QUOTA-LIMIT";
        return nullptr;
    }

    void reject(const char* id) noexcept
    {
        lifecycle_.reject(id);
        if (source_.expanded())
            pipeline_.reset();
    }

    void observeCreationScope(std::uint32_t participant, bool successful) noexcept
    {
        lifecycle_.observe(participant,
            successful,
            "RHI-ARTIFACT-CREATION-NATIVE-SCOPE",
            [&]() noexcept -> const char* {
                if (webGpuNativeDeviceLost())
                    return "RHI-ARTIFACT-CREATION-DEVICE-LOST";
                if (const auto* id = validateInputs())
                    return id;
                return validateConsumption();
            });
        if (source_.expanded()
            && lifecycle_.phase() == core::runtime::CreationReceiptLifecycle::Phase::Rejected)
            pipeline_.reset();
    }

    static void releaseDevice(void* object) noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        wgpuDeviceRelease(static_cast<WGPUDevice>(object));
#endif
    }

    const Source source_;
    const std::shared_ptr<rhi::PipelineLayout> layout_;
    const std::shared_ptr<core::CpuMemoryQuota> nativeQuota_, callbackQuota_;
    rhi::ComputePipelineDesc definition_;
    core::pmr::String sourceDiagnostic_{&core::defaultCpuMemoryResource()};
    core::ExternalObjectSlot device_;
    mutable std::shared_ptr<WebGpuNativeComputePipelinePayload> pipeline_;
    WebGpuArtifactCreationScopeCallbacks<WebGpuOriginalArtifactComputeCreation> callbacks_;
    core::runtime::CreationReceiptLifecycle lifecycle_{callbackCount};
};

// endregion Webgpu original artifact compute creation

// region Webgpu original artifact graphics creation

// Externally serialized cold actor. Ready proves creation only, never commands.
class WebGpuOriginalArtifactGraphicsCreation final : public rhi::OriginalGraphicsPipelineCreation {
    friend class WebGpuAcceptedOriginalGraphicsPipelineFactory;

    class ConstructionKey {
        friend class WebGpuOriginalArtifactGraphicsCreation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    friend class WebGpuArtifactCreationScopeCallbacks<WebGpuOriginalArtifactGraphicsCreation>;

public:
    using Budget = rhi::OriginalArtifactGraphicsCreationContract::Budget;
    using Source = WebGpuOriginalArtifactCreationSource<Budget, rhi::OriginalGraphicsPipelineInput>;
    using ExpansionBudget = Source::ExpansionBudget;
    using ExpansionDefinition = Source::Definition;
    using ExpansionValidation = WebGpuOriginalDepthTableExpansionCreationValidation;
    using Lowering = rhi::semantics::OriginalArtifactGraphicsCreationLowering;
    static constexpr auto callbackCount = WebGpuNativeErrorScopeBatch::count;
    using Status = WebGpuArtifactCreationNativeMethods::Status;

    WebGpuOriginalArtifactGraphicsCreation(ConstructionKey,
        Budget budget,
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> input,
        std::shared_ptr<rhi::PipelineLayout> layout,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota,
        const rhi::RenderPipelineDesc& authored)
        : source_(budget, std::move(input)), layout_(std::move(layout)), nativeQuota_(nativeQuota),
          callbackQuota_(callbackQuota), device_(&releaseDevice)
    {
        definition_ = rhi::RenderPipelineStateCopy::copy(authored);
        sourceDiagnostic_.reserve(budget.maxDiagnosticBytes);
    }

    WebGpuOriginalArtifactGraphicsCreation(ConstructionKey,
        ExpansionBudget budget,
        std::shared_ptr<const ExpansionDefinition> definition,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota)
        : source_(budget, std::move(definition)), layout_(source_.expansionDefinition()->layout()),
          nativeQuota_(nativeQuota), callbackQuota_(callbackQuota), device_(&releaseDevice)
    {
        definition_ = rhi::RenderPipelineStateCopy::copy(
            ExpansionValidation::definition<rhi::RenderPipelineDesc>(
                *source_.expansionDefinition()));
        sourceDiagnostic_.reserve(budget.maxDiagnosticBytes);
    }

    static const char* validateBudget(const Budget& budget) noexcept
    {
        return rhi::OriginalArtifactGraphicsCreationContract::validateBudget(budget);
    }

    static Lowering::Facts nativeFacts() noexcept
    {
        const auto f = WebGpuArtifactCreationNativeMethods::query(
            WebGpuArtifactCreationNativeMethods::Kind::Graphics);
        return {f.device,
            f.shader,
            f.pipeline,
            f.bindGroupLayout,
            f.pipelineLayout,
            f.pushScope,
            f.popScope};
    }

    static std::shared_ptr<WebGpuOriginalArtifactGraphicsCreation> prepareOriginal(
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::RenderPipelineDesc& authored,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (webGpuNativeDeviceLost()) {
            diagnostic = "RHI-GRAPHICS-CREATION-DEVICE-LOST";
            return {};
        }
        if ((diagnostic = rhi::OriginalGraphicsPipelineInput::validateOriginal(input)))
            return {};
        if (!layout || !layout->sharedOwnerMatches(layout)) {
            diagnostic = "RHI-GRAPHICS-CREATION-LAYOUT-OWNER";
            return {};
        }
        if (authored.label.size() > budget.maxLabelBytes) {
            diagnostic = "RHI-GRAPHICS-CREATION-LABEL-CAPACITY";
            return {};
        }
        if (!ownerQuota || ownerQuota->limitBytes() != budget.maxCpuOwnerBytes || !nativeQuota
            || nativeQuota->limitBytes() != budget.native.maxCpuMetadataBytes || !callbackQuota
            || callbackQuota->limitBytes() != budget.maxCpuCallbackBytes) {
            diagnostic = "RHI-GRAPHICS-CREATION-QUOTA";
            return {};
        }
        const std::array domains{
            ownerQuota, nativeQuota, callbackQuota, input->quota(), input->artifact()->quota()};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                domains.size(), [&](std::size_t i) -> const auto& {
                    return domains[i];
                })) {
            diagnostic = "RHI-GRAPHICS-CREATION-QUOTA-DOMAIN";
            return {};
        }
        if (authored.layout != layout.get() || authored.shaderPackage != &input->package()) {
            diagnostic = "RHI-GRAPHICS-CREATION-INPUT";
            return {};
        }
        const auto decision = Lowering::decide(rhi::Backend::WebGPU, nativeFacts());
        if (!decision.creationEligible) {
            diagnostic = decision.diagnostic;
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(ownerQuota);
            auto result = core::makeUnique<WebGpuOriginalArtifactGraphicsCreation>(
                ConstructionKey{}, budget, input, layout, nativeQuota, callbackQuota, authored);
            if (!WebGpuOriginalArtifactGraphicsFactory::validateDefinition(input,
                    result->definition_,
                    result->sourceDiagnostic_,
                    budget.native.contractVersion)) {
                diagnostic = "RHI-GRAPHICS-CREATION-DEFINITION";
                return {};
            }
            result->callbacks_.prepare(callbackQuota);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-GRAPHICS-CREATION-ALLOCATION";
            return {};
        }
    }

    static std::shared_ptr<WebGpuOriginalArtifactGraphicsCreation> prepareTableExpansion(
        const std::shared_ptr<const ExpansionDefinition>& definition,
        const ExpansionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& callbackQuota,
        const char*& diagnostic)
    {
        diagnostic = rhi::OriginalDepthTableExpansionCreationContract::validateBudget(budget);
        if (diagnostic)
            return {};
        if (webGpuNativeDeviceLost()) {
            diagnostic = "RHI-ORIGINAL-TABLE-CREATION-DEVICE-LOST";
            return {};
        }
        if ((diagnostic = ExpansionDefinition::validateOriginal(definition)))
            return {};
        if ((diagnostic = ExpansionValidation::validate(definition,
                 budget,
                 ownerQuota,
                 nativeQuota,
                 callbackQuota,
                 definition->layout(),
                 ExpansionValidation::definition<rhi::RenderPipelineDesc>(*definition))))
            return {};
        const auto decision = ExpansionValidation::query(*definition);
        if (!decision.creationEligible) {
            diagnostic = decision.diagnostic;
            return {};
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        try {
            core::CpuMemoryQuotaScope scope(ownerQuota);
            auto result = core::makeUnique<WebGpuOriginalArtifactGraphicsCreation>(
                ConstructionKey{}, budget, definition, nativeQuota, callbackQuota);
            result->callbacks_.prepare(callbackQuota);
            auto device = emscripten_webgpu_get_device();
            if (!device || !result->device_.adopt(device)) {
                if (device)
                    wgpuDeviceRelease(device);
                diagnostic = "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
                return {};
            }
            if (!WebGpuArtifactCreationNativeMethods::matchesCurrentDevice(device)) {
                diagnostic = "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
                return {};
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-TABLE-CREATION-ALLOCATION";
            return {};
        }
#else
        diagnostic = "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
        return {};
#endif
    }

    static const char* cancelTableExpansion(
        const std::shared_ptr<WebGpuOriginalArtifactGraphicsCreation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-TABLE-CREATION-OWNER";
        if (!self->source_.expanded())
            return "RHI-ORIGINAL-TABLE-CREATION-PROFILE";
        self->reject("RHI-ORIGINAL-TABLE-CREATION-CANCELLED");
        return nullptr;
    }

    // Native construction is cold and quota-bounded. Observation/callbacks are hot.
    static const char* initialize(
        const std::shared_ptr<WebGpuOriginalArtifactGraphicsCreation>& self)
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-GRAPHICS-CREATION-OWNER";
        if (webGpuNativeDeviceLost()) {
            if (self->source_.expanded())
                self->reject("RHI-ORIGINAL-TABLE-CREATION-DEVICE-LOST");
            return "RHI-GRAPHICS-CREATION-DEVICE-LOST";
        }
        if (self->status() != Status::Prepared)
            return "RHI-GRAPHICS-CREATION-START-STATUS";
        if (const auto* id = self->validateInputs()) {
            if (self->source_.expanded())
                self->reject(id);
            return id;
        }
        if (self->source_.expanded()) {
            if (!WebGpuArtifactCreationNativeMethods::matchesCurrentDevice(self->device_.get())) {
                self->reject("RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE");
                return self->lifecycle_.failure();
            }
            const auto selected = ExpansionValidation::query(*self->source_.expansionDefinition());
            if (!selected.creationEligible) {
                self->reject(selected.diagnostic);
                return self->lifecycle_.failure();
            }
        }
        if (!self->source_.expanded()) {
            const auto decision = Lowering::decide(rhi::Backend::WebGPU, nativeFacts());
            if (!decision.creationEligible)
                return decision.diagnostic;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto device = self->source_.expanded() ? static_cast<WGPUDevice>(self->device_.get())
                                               : emscripten_webgpu_get_device();
        if (!device || (!self->source_.expanded() && !self->device_.adopt(device))) {
            if (device)
                wgpuDeviceRelease(device);
            return "RHI-GRAPHICS-CREATION-NATIVE-DEVICE";
        }
        (void)self->lifecycle_.begin();
        WebGpuNativeErrorScopeBatch::push(device);
        try {
            if (self->source_.expanded())
                self->pipeline_ = WebGpuOriginalArtifactGraphicsFactory::createTableExpansion(
                    self->source_.expansionDefinition(),
                    self->source_.expansionBudget().native,
                    self->nativeQuota_,
                    self->sourceDiagnostic_);
            else
                self->pipeline_ =
                    WebGpuOriginalArtifactGraphicsFactory::create(self->source_.input(),
                        self->definition_,
                        self->source_.ordinaryBudget().native,
                        self->nativeQuota_,
                        self->sourceDiagnostic_);
            if (self->sourceDiagnostic_.size() > self->source_.request().maxDiagnosticBytes)
                self->reject("RHI-GRAPHICS-CREATION-DIAGNOSTIC-CAPACITY");
            if (!self->pipeline_)
                self->reject("RHI-GRAPHICS-CREATION-FACTORY-REJECTED");
        } catch (const std::bad_alloc&) {
            self->reject("RHI-GRAPHICS-CREATION-NATIVE-ALLOCATION");
        }
        self->callbacks_.pop(device, self);
        return self->lifecycle_.failure();
#else
        return "RHI-GRAPHICS-CREATION-WEBGPU-DEVICE";
#endif
    }

    static const char* validateReadyOriginal(
        const std::shared_ptr<const WebGpuOriginalArtifactGraphicsCreation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-GRAPHICS-CREATION-OWNER";
        return self->validate();
    }

    Status status() const noexcept
    {
        const auto result = WebGpuArtifactCreationNativeMethods::status(lifecycle_);
        if (source_.expanded() && result == Status::DeviceLost)
            pipeline_.reset();
        return result;
    }

    bool isTableExpansion() const noexcept { return source_.expanded(); }

    const auto& expansionDefinition() const noexcept { return source_.expansionDefinition(); }

    bool scopesObserved() const noexcept { return lifecycle_.evidence().allReceived(); }

    bool scopeObserved(std::uint32_t participant) const noexcept
    {
        return lifecycle_.evidence().hasReceived(participant);
    }

    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&
    originalInput() const noexcept override
    {
        return source_.input();
    }

    const std::shared_ptr<rhi::PipelineLayout>& originalLayout() const noexcept override
    {
        return layout_;
    }

    const rhi::RenderPipelineDesc& definition() const noexcept override { return definition_; }

    const auto& nativeQuota() const noexcept { return nativeQuota_; }

    const auto& callbackQuota() const noexcept { return callbackQuota_; }

    const auto& sourceDiagnostic() const noexcept { return sourceDiagnostic_; }

    bool usesCpuQuotaDomain(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        if (source_.expanded())
            return ExpansionValidation::usesQuotaDomain(
                source_, quota, originalMetadataQuota(), nativeQuota_, callbackQuota_);
        for (const auto& domain : std::array{originalMetadataQuota(),
                 nativeQuota_,
                 callbackQuota_,
                 source_.input()->quota(),
                 source_.input()->artifact()->quota()})
            if (quota.get() == domain.get()
                || core::SharedOwnerProjection::sameControlBlock(quota, domain))
                return true;
        return false;
    }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-GRAPHICS-CREATION-CANONICAL-JOIN-MISSING";
    }

    const char* validate() const noexcept override
    {
        switch (status()) {
        case Status::Prepared:
            return "RHI-GRAPHICS-CREATION-UNINITIALIZED";
        case Status::Pending:
            return "RHI-GRAPHICS-CREATION-PENDING";
        case Status::Rejected:
            return lifecycle_.failure();
        case Status::DeviceLost:
            return "RHI-GRAPHICS-CREATION-DEVICE-LOST";
        case Status::Ready:
            if (!lifecycle_.evidence().proven())
                return "RHI-GRAPHICS-CREATION-EVIDENCE";
            if (const auto* id = validateInputs())
                return id;
            return validateConsumption();
        }
        return "RHI-GRAPHICS-CREATION-STATUS";
    }

private:
    const char* validateConsumption() const noexcept
    {
        if (source_.expanded()) {
            if (const auto* id =
                    WebGpuOriginalArtifactGraphicsFactory::validateConsumedTableExpansion(
                        pipeline_))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(
                    pipeline_->issuedContract()->expansionDefinition(),
                    source_.expansionDefinition()))
                return "RHI-ORIGINAL-TABLE-CREATION-DEFINITION";
            if (pipeline_->issuedContract()->nativeDevice() != device_.get()
                || !WebGpuArtifactCreationNativeMethods::matchesCurrentDevice(device_.get()))
                return "RHI-ORIGINAL-TABLE-CREATION-NATIVE-DEVICE";
            return nullptr;
        }
        if (const auto* id =
                WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(pipeline_))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                pipeline_->issuedContract()->graphicsInput(), source_.input()))
            return "RHI-GRAPHICS-CREATION-INPUT";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto device = static_cast<WGPUDevice>(device_.get());
        if (!device || pipeline_->issuedContract()->nativeDevice() != device
            || !EM_ASM_INT(
                {
                    // clang-format off
                    return WebGPU.getJsObject(Number($0)) === Module.preinitializedWebGPUDevice;
                    // clang-format on
                },
                device))
            return "RHI-GRAPHICS-CREATION-NATIVE-DEVICE";
#endif
        return nullptr;
    }

    const char* validateInputs() const noexcept
    {
        if (source_.expanded())
            return ExpansionValidation::validate(source_.expansionDefinition(),
                source_.expansionBudget(),
                originalMetadataQuota(),
                nativeQuota_,
                callbackQuota_,
                layout_,
                definition_);
        const auto& input = source_.input();
        const auto& budget = source_.request();
        if (const auto* id = rhi::OriginalGraphicsPipelineInput::validateOriginal(input))
            return id;
        if (!layout_ || !layout_->sharedOwnerMatches(layout_) || definition_.layout != layout_.get()
            || definition_.shaderPackage != &input->package())
            return "RHI-GRAPHICS-CREATION-INPUT";
        if (originalMetadataQuota()->limitBytes() != budget.maxCpuOwnerBytes
            || nativeQuota_->limitBytes() != budget.native.maxCpuMetadataBytes
            || callbackQuota_->limitBytes() != budget.maxCpuCallbackBytes)
            return "RHI-GRAPHICS-CREATION-QUOTA-LIMIT";
        return nullptr;
    }

    void reject(const char* id) noexcept
    {
        lifecycle_.reject(id);
        if (source_.expanded())
            pipeline_.reset();
    }

    void observeCreationScope(std::uint32_t participant, bool successful) noexcept
    {
        lifecycle_.observe(participant,
            successful,
            "RHI-GRAPHICS-CREATION-NATIVE-SCOPE",
            [&]() noexcept -> const char* {
                if (webGpuNativeDeviceLost())
                    return "RHI-GRAPHICS-CREATION-DEVICE-LOST";
                if (const auto* id = validateInputs())
                    return id;
                return validateConsumption();
            });
        if (source_.expanded()
            && lifecycle_.phase() == core::runtime::CreationReceiptLifecycle::Phase::Rejected)
            pipeline_.reset();
    }

    static void releaseDevice(void* object) noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        wgpuDeviceRelease(static_cast<WGPUDevice>(object));
#endif
    }

    const Source source_;
    const std::shared_ptr<rhi::PipelineLayout> layout_;
    const std::shared_ptr<core::CpuMemoryQuota> nativeQuota_, callbackQuota_;
    rhi::RenderPipelineDesc definition_;
    core::pmr::String sourceDiagnostic_{&core::defaultCpuMemoryResource()};
    core::ExternalObjectSlot device_;
    mutable std::shared_ptr<WebGpuNativeRenderPipelinePayload> pipeline_;
    WebGpuArtifactCreationScopeCallbacks<WebGpuOriginalArtifactGraphicsCreation> callbacks_;
    core::runtime::CreationReceiptLifecycle lifecycle_{callbackCount};
};
} // namespace arp::rhi_webgpu

// endregion Webgpu original artifact graphics creation
