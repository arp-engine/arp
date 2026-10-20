#include "rhi/backends/null/null_bindless.h"

#include "rhi/backends/null/null_backend_internal.h"
#include "rhi/frontend/bindless_scene_content.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "rhi/semantics/bindless_scene_raster.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <algorithm>

namespace arp::rhi::null_backend {
namespace {
template <class Base> class BindlessPipelineBase : public Base {
public:
    explicit BindlessPipelineBase(std::shared_ptr<const BindlessPipelineState> state)
        : state_(std::move(state))
    {}

    const PipelineLayout* layout() const override { return state_->layout.get(); }

    const PipelineValidationReport& validationReport() const override { return state_->validation; }

    const ShaderReflectionDesc& shaderReflection() const override
    {
        return state_->packageView.reflection;
    }

    ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    std::shared_ptr<const BindlessAuthorization> bindlessAuthorization() const noexcept override
    {
        return state_->authorization;
    }

    const std::shared_ptr<const BindlessPipelineState>& state() const noexcept { return state_; }

protected:
    std::shared_ptr<const BindlessPipelineState> state_;
};

class BindlessRenderPipeline final : public BindlessPipelineBase<RenderPipeline> {
public:
    using BindlessPipelineBase::BindlessPipelineBase;

    PrimitiveTopology primitiveTopology() const override { return state_->topology; }

    ShaderStage bindlessShaderStages() const noexcept override { return state_->activeStages; }

    const BindlessPackageModule* bindlessShaderModule(
        ShaderStage stage, std::uint32_t version) const noexcept override
    {
        return state_->graphicsEntries.module(stage, version);
    }

    const char* bindlessOrdinaryBufferRequirement(ShaderStage stage,
        std::uint32_t set,
        std::uint32_t binding,
        ShaderBufferMemoryRequirement& output,
        std::uint32_t version) const noexcept override
    {
        output = {};
        if (version != 1u && version != 2u)
            return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
        return state_->graphicsEntries.bufferRequirement(stage,
            set,
            binding,
            state_->authorization->package().artifact().execution == BindlessExecution::Emulated,
            output,
            version);
    }

    const char* bindlessOrdinaryBufferMemberMemory(ShaderStage stage,
        std::uint32_t set,
        std::uint32_t binding,
        ShaderBufferMemberMemory& output,
        std::uint32_t version) const noexcept override
    {
        output = {};
        if (!state_->authorization
            || state_->authorization->package().artifact().execution != BindlessExecution::Emulated)
            return "RHI-NULL-MEMBER-MEMORY-NATIVE-TARGET-UNAVAILABLE";
        return state_->graphicsEntries.bufferMemberMemory(
            stage, set, binding, false, output, version);
    }

    const core::pmr::Vector<VertexBufferLayoutDesc>* bindlessVertexBuffers(
        std::uint32_t version) const noexcept override
    {
        return version == 1u ? &state_->renderState.vertexBuffers : nullptr;
    }
};

class BindlessComputePipeline final : public BindlessPipelineBase<ComputePipeline> {
public:
    using BindlessPipelineBase::BindlessPipelineBase;
};

template <class Base, class Pipeline, BindlessBindingDomain Domain>
class PreparedBinding final : public Base {
public:
    template <class Owners>
    explicit PreparedBinding(std::shared_ptr<const Owners> value) : Base(std::move(value))
    {}

    ValidationResult validatePayload() const override
    {
        const auto valid = this->validateOwners();
        if (!valid.success)
            return valid;
        return this->owners()->desc().plan.execution == BindlessExecution::Validator
                && !this->owners()->desc().plan.executable
                && dynamic_cast<const Pipeline*>(this->owners()->desc().pipeline.get())
            ? ValidationResult{true, {}}
            : ValidationResult{false,
                  bindlessBindingDiagnostic(Domain, BindlessBindingDiagnostic::PayloadMismatch)};
    }

    std::uint32_t argumentBufferCount() const noexcept override { return 0u; }

    std::uint64_t argumentBufferBytes() const noexcept override { return 0u; }
};

using PreparedComputeBinding = PreparedBinding<BindlessPreparedComputeBinding,
    BindlessComputePipeline,
    BindlessBindingDomain::Compute>;
using PreparedGraphicsBinding = PreparedBinding<BindlessPreparedGraphicsBinding,
    BindlessRenderPipeline,
    BindlessBindingDomain::Graphics>;

std::shared_ptr<BindlessPipelineState> prepareState(const BindlessCrossExtension& issuer,
    std::shared_ptr<const BindlessAuthorization> authorization,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!authorization || !authorization->issuedBy(&issuer, authorization)
        || authorization->plan().execution != BindlessExecution::Validator
        || authorization->plan().executable) {
        diagnostic = "RHI-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return {};
    }
    auto state = core::makeShared<BindlessPipelineState>();
    state->authorization = std::move(authorization);
    const auto& artifact = state->authorization->package().artifact();
    if (!artifact.additionalDomains.empty()) {
        const auto lowering = semantics::BindlessPipelineInterfaceContract::lowering(Backend::Null);
        if (!lowering.interfaceInspectionAvailable || !lowering.validatorOnly
            || lowering.executionAvailable) {
            diagnostic = lowering.diagnostic;
            return {};
        }
    }
    const auto layout = bindlessPipelineLayout(artifact);
    state->layout = createPipelineLayout(layout);
    if (!state->layout) {
        diagnostic = "RHI-BINDLESS-PIPELINE-LAYOUT-INVALID";
        return {};
    }
    state->packageView.label = artifact.physicalLayout.name;
    state->packageView.generatedReflectionHydrated = true;
    return state;
}

bool finishState(BindlessPipelineState& state, std::uint64_t key, core::pmr::String& diagnostic)
{
    const auto& reflection = state.packageView.reflection;
    const auto matches =
        validatePipelineLayoutAgainstShaderReflection(state.layout->desc(), reflection);
    if (!matches.success) {
        diagnostic = matches.diagnostics;
        return false;
    }
    auto& report = state.validation;
    report.success = true;
    report.backend = Backend::Null;
    report.shaderPackageValidated = true;
    report.reflectionMatched = true;
    report.entrypointCount = static_cast<std::uint32_t>(state.packageView.entrypoints.size());
    report.descriptorLayoutCount =
        static_cast<std::uint32_t>(state.layout->desc().descriptorLayouts.size());
    report.descriptorSetCount =
        static_cast<std::uint32_t>(state.layout->desc().descriptorSets.size());
    report.descriptorBindingCount =
        static_cast<std::uint32_t>(reflection.descriptorBindings.size());
    report.pipelineCacheKey = hashShaderValue(key, state.authorization->plan().identity);
    report.pipelineCacheKeyStable = true;
    report.selectedComputeWorkgroupSizeX = reflection.computeWorkgroupSizeX;
    report.selectedComputeWorkgroupSizeY = reflection.computeWorkgroupSizeY;
    report.selectedComputeWorkgroupSizeZ = reflection.computeWorkgroupSizeZ;
    return true;
}

} // namespace

std::shared_ptr<const BindlessPipelineState> bindlessState(const ComputePipeline& pipeline)
{
    const auto* indexed = dynamic_cast<const BindlessComputePipeline*>(&pipeline);
    return indexed ? indexed->state() : nullptr;
}

std::shared_ptr<const BindlessPreparedComputeBinding> prepareComputeBinding(
    std::shared_ptr<const BindlessComputeBindingOwners> owners, core::pmr::String& diagnostic)
{
    if (!owners || !owners->sharedOwnerMatches(owners)) {
        diagnostic = owners ? "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH"
                            : "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISSING";
        return {};
    }
    const auto admitted = owners->validate();
    if (!admitted.success) {
        diagnostic = admitted.diagnostics;
        return {};
    }
    auto binding = core::makeShared<PreparedComputeBinding>(std::move(owners));
    const auto valid = binding->validatePayload();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    return binding;
}

bool isPreparedComputeBinding(const BindlessPreparedComputeBinding& binding) noexcept
{
    return dynamic_cast<const PreparedComputeBinding*>(&binding) != nullptr;
}

std::shared_ptr<const BindlessPreparedGraphicsBinding> prepareGraphicsBinding(
    std::shared_ptr<const BindlessGraphicsBindingOwners> owners, core::pmr::String& diagnostic)
{
    if (!owners || !owners->sharedOwnerMatches(owners)) {
        diagnostic = owners ? "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH"
                            : "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISSING";
        return {};
    }
    const auto admitted = owners->validate();
    if (!admitted.success) {
        diagnostic = admitted.diagnostics;
        return {};
    }
    auto binding = core::makeShared<PreparedGraphicsBinding>(std::move(owners));
    const auto valid = binding->validatePayload();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    return binding;
}

bool isPreparedGraphicsBinding(const BindlessPreparedGraphicsBinding& binding) noexcept
{
    return dynamic_cast<const PreparedGraphicsBinding*>(&binding) != nullptr;
}

std::shared_ptr<const BindlessPipelineState> bindlessState(const RenderPipeline& pipeline)
{
    const auto* indexed = dynamic_cast<const BindlessRenderPipeline*>(&pipeline);
    return indexed ? indexed->state() : nullptr;
}

core::UniquePtr<ComputePipeline> createBindlessComputePipeline(const BindlessCrossExtension& issuer,
    std::shared_ptr<const BindlessAuthorization> authorization,
    core::StringView entry,
    core::pmr::String& diagnostic)
{
    auto state = prepareState(issuer, std::move(authorization), diagnostic);
    if (!state
        || !appendBindlessShaderEntry(state->authorization->package(),
            entry,
            ShaderStage::Compute,
            state->packageView,
            diagnostic))
        return {};
    state->activeStages = ShaderStage::Compute;
    ComputePipelineDesc desc{state->packageView.label,
        state->layout.get(),
        &state->packageView,
        state->packageView.entrypoints[0]};
    const auto decision = semantics::lowerComputePipelineState(
        semantics::nativeBackendCapabilityContract(Backend::Null), desc);
    if (!decision.success) {
        diagnostic = decision.diagnostics;
        return {};
    }
    if (!finishState(*state, decision.pipelineCacheKey, diagnostic))
        return {};
    return core::makeUnique<BindlessComputePipeline>(std::move(state));
}

core::UniquePtr<RenderPipeline> createBindlessRenderPipeline(const BindlessCrossExtension& issuer,
    std::shared_ptr<const BindlessAuthorization> authorization,
    const RenderPipelineDesc& authored,
    core::StringView vertex,
    core::StringView fragment,
    core::pmr::String& diagnostic,
    const BindlessGraphicsPushRequest* push)
{
    if (push)
        if (const auto* id = push->diagnostic()) {
            diagnostic = id;
            return {};
        }
    auto state = prepareState(issuer, std::move(authorization), diagnostic);
    if (!state)
        return {};
    const auto authoredValidation =
        validateBindlessAuthoredRenderState(authored, !fragment.empty());
    if (!authoredValidation.success) {
        diagnostic = authoredValidation.diagnostics;
        return {};
    }
    const auto scene = OriginalBindlessSceneContentContract::selectEntries(vertex, fragment);
    if (scene.diagnostic) {
        diagnostic = scene.diagnostic;
        return {};
    }
    if (scene.recognized)
        if (const auto* id =
                semantics::BindlessSceneRasterContract::inspect(Backend::Null, authored)) {
            diagnostic = id;
            return {};
        }
    if (push) {
        PipelineLayoutDesc layout;
        const auto composed =
            BindlessGraphicsPipelineEntries::compose(state->authorization->originalPackageOwner(),
                vertex,
                fragment,
                state->packageView,
                layout,
                state->graphicsEntries);
        if (!composed.success) {
            diagnostic = composed.diagnostics;
            return {};
        }
        const auto bytes = state->packageView.reflection.pushConstantByteCount;
        if (!bytes || bytes > push->maxBytes) {
            diagnostic = "RHI-BINDLESS-GRAPHICS-PUSH-REQUEST-SIZE";
            return {};
        }
        state->layout = createPipelineLayout(layout);
        if (!state->layout) {
            diagnostic = "RHI-BINDLESS-PIPELINE-LAYOUT-INVALID";
            return {};
        }
    } else {
        if (!appendBindlessShaderEntry(state->authorization->package(),
                vertex,
                ShaderStage::Vertex,
                state->packageView,
                diagnostic))
            return {};
        if (!fragment.empty()
            && !appendBindlessShaderEntry(state->authorization->package(),
                fragment,
                ShaderStage::Fragment,
                state->packageView,
                diagnostic))
            return {};
    }
    state->activeStages =
        ShaderStage::Vertex | (fragment.empty() ? ShaderStage::None : ShaderStage::Fragment);
    auto desc = RenderPipelineStateCopy::copy(authored);
    desc.layout = state->layout.get();
    desc.shaderPackage = &state->packageView;
    desc.vertexShader = state->packageView.entrypoints[0];
    if (!fragment.empty())
        desc.fragmentShader = state->packageView.entrypoints[1];
    if (const auto* id = BindlessGraphicsPipelineEntries::capture(
            state->authorization->package(), desc, state->graphicsEntries)) {
        diagnostic = id;
        return {};
    }
    const auto decision = semantics::lowerRenderPipelineState(
        semantics::nativeBackendCapabilityContract(Backend::Null), desc);
    if (!decision.success) {
        diagnostic = decision.diagnostics;
        return {};
    }
    applyPipelineStateValidation(state->validation, validateRenderPipelineState(desc));
    if (!finishState(*state, decision.pipelineCacheKey, diagnostic))
        return {};
    state->topology = desc.topology;
    state->renderState = std::move(desc);
    return core::makeUnique<BindlessRenderPipeline>(std::move(state));
}

} // namespace arp::rhi::null_backend
