#include "rhi/frontend/bindless_graphics_binding.h"

#include "rhi/frontend/bindless_graphics_execution.h"
#include "rhi/frontend/bindless_root_sequence.h"
#include "rhi/frontend/bindless_workload_device_preflight.h"
#include "rhi/gpu_extensions.h"

namespace arp::rhi {
std::shared_ptr<const OriginalBindlessGraphicsExecution>
BindlessCrossExtension::prepareOriginalGraphicsExecution(GpuDevice& device,
    const OriginalBindlessGraphicsExecutionInputs& inputs,
    const OriginalBindlessGraphicsExecutionBudget& budget,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const OriginalBindlessGraphicsExecution> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = OriginalBindlessGraphicsExecutionContract::validateBudget(budget))
        return fail(id);
    if (const auto* id = inputs.initializedGraphics.diagnostic())
        return fail(id);
    if (const auto* id = OriginalBindlessInitializedGraphicsContract::validateExecutionBudget(
            device.backend(), inputs.initializedGraphics, budget.closure))
        return fail(id);
    const auto lowering = semantics::OriginalBindlessGraphicsExecutionLowering::lowering(
        device.backend(), budget.intent.contractVersion);
    if (!lowering.executionAvailable || !lowering.completionAvailable || lowering.diagnostic)
        return fail(lowering.diagnostic);
    if (device.queryInterface(kExtensionBindlessCross) != this)
        return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-DEVICE-ISSUER");
    const auto executionVersion = budget.intent.contractVersion == 2u
        ? capabilities().originalGraphicsDepthExecutionContractVersion
        : capabilities().originalGraphicsExecutionContractVersion;
    if (executionVersion != budget.intent.contractVersion
        || !capabilities().faultReadbackContractVersion)
        return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-CAPABILITY");
    if (const auto* id = OriginalCommandBindlessRootSource::validateOriginal(inputs.root))
        return fail(id);
    if (const auto* id = OriginalBindlessInitializedGraphicsContract::validateRootInput(
            inputs.root, inputs.initializedGraphics))
        return fail(id);
    if (!std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            inputs.root->originalPrepared())
        || !inputs.root->authorization()->issuedBy(this, inputs.root->authorization())
        || !inputs.root->originalSnapshot()->plan().executable)
        return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-ROOT");
    if (const auto* id =
            OriginalCommandBindlessRootSource::validateOriginalGraphicsBufferRanges(inputs.root))
        return fail(id);
    if (!inputs.transaction || !inputs.inventory || !inputs.readback || !inputs.closureQuota
        || !inputs.passQuota || !inputs.submissionQuota || !inputs.viewQuota || !inputs.passCapacity
        || inputs.viewQuota->limitBytes() != budget.maxCpuViewBytes)
        return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-INPUTS");
    if (const auto* id = OriginalBindlessInitializedShaderContract::validatePublicInputs(
            budget.closure.initializedShader,
            inputs.initializedShader,
            inputs.initializedShaderCount))
        return fail(id);
    if (const auto* id =
            OriginalBindlessRootReadbackSequence::inspectInput(inputs.readback, inputs.readbacks))
        return fail(id);
    if (const auto* id = OriginalCommandStateTransaction::validateOriginal(inputs.transaction))
        return fail(id);
    const auto& quota = inputs.viewQuota;
    for (const auto& other : {inputs.closureQuota,
             inputs.passQuota,
             inputs.submissionQuota,
             inputs.root->originalMetadataQuota(),
             inputs.inventory->originalMetadataQuota(),
             inputs.transaction->originalMetadataQuota(),
             inputs.transaction->originalGraph()->originalMetadataQuota(),
             inputs.readback->originalMetadataQuota(),
             inputs.readback->originalProgram()->originalMetadataQuota(),
             inputs.readback->originalResult()->originalMetadataQuota()})
        if (core::SharedOwnerProjection::sameControlBlock(quota, other))
            return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-QUOTA-DOMAIN");
    if ((inputs.geometryCount && !inputs.geometry)
        || (budget.closure.contractVersion == 3u) != bool(inputs.geometryCount)
        || inputs.geometryCount > budget.closure.maxInitializedGeometryActors)
        return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-GEOMETRY");
    for (std::size_t i = 0u; i < inputs.geometryCount; ++i)
        if (!inputs.geometry[i]
            || !inputs.geometry[i]->hasQuotaBoundSharedOwner(inputs.geometry[i]))
            return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-GEOMETRY-OWNER");
    if (const auto* id = OriginalBindlessRootSequenceContract::inspectOriginal(
            &inputs.root, 1u, inputs.inventory, inputs.readback->originalProgram()))
        return fail(id);
    OriginalBindlessWorkloadDeviceReport resources;
    if (const auto* id = OriginalBindlessWorkloadDevicePreflight::validate(
            inputs.inventory, device, resources, budget.requireWorkloadRetention))
        return fail(id);
    return prepareOriginalGraphicsExecutionPayload(device, inputs, budget, diagnostic);
}

std::shared_ptr<const OriginalBindlessGraphicsExecution>
BindlessCrossExtension::prepareOriginalGraphicsExecutionPayload(GpuDevice&,
    const OriginalBindlessGraphicsExecutionInputs&,
    const OriginalBindlessGraphicsExecutionBudget&,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-BINDLESS-GRAPHICS-EXECUTION-PAYLOAD-UNAVAILABLE";
    return {};
}

namespace {
constexpr auto domain = BindlessBindingDomain::Graphics;

bool validGraphicsStages(ShaderStage stages)
{
    return stages == ShaderStage::Vertex || stages == (ShaderStage::Vertex | ShaderStage::Fragment);
}
} // namespace

ValidationResult BindlessGraphicsBindingOwners::validate() const
{
    if (desc_.pipeline && !desc_.pipeline->sharedOwnerMatches(desc_.pipeline))
        return {
            false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::OwnersMismatch)};
    const auto valid = validateBindlessBindingOwners(domain,
        desc_.optIn,
        desc_.snapshot,
        desc_.plan,
        desc_.generation,
        authorization_,
        desc_.pipeline ? desc_.pipeline->bindlessAuthorization() : nullptr);
    if (!valid.success)
        return valid;
    if (!validGraphicsStages(stages_) || desc_.pipeline->bindlessShaderStages() != stages_)
        return {false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::StageMismatch)};
    return {true, {}};
}

ValidationResult BindlessPreparedGraphicsBinding::validateForRecord(
    const RenderPipeline& pipeline, const BindlessAccessDeclarations& accesses) const
{
    return validatePreparedBindlessBindingForRecord(
        *this, pipeline, accesses, owners_ ? owners_->stages() : ShaderStage::None, domain);
}

BindlessGraphicsBindingPool::BindlessGraphicsBindingPool(
    const BindlessCrossExtension& issuer, BindlessGraphicsBindingBudget budget)
    : issuer_(&issuer), preparation_(issuer, budget, domain)
{}

std::shared_ptr<const BindlessPreparedGraphicsBinding> BindlessGraphicsBindingPool::prepare(
    BindlessGraphicsBindingDesc desc, core::pmr::String& diagnostic)
{
    if (desc.pipeline && !desc.pipeline->sharedOwnerMatches(desc.pipeline)) {
        diagnostic = bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::OwnersMismatch);
        return {};
    }
    auto reservation = preparation_.prepare(desc.optIn,
        desc.snapshot,
        bool(desc.pipeline),
        desc.plan,
        desc.generation,
        desc.pipeline ? desc.pipeline->bindlessAuthorization() : nullptr,
        diagnostic);
    if (!reservation)
        return {};
    if (!validGraphicsStages(desc.pipeline->bindlessShaderStages())) {
        diagnostic = bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::StageMismatch);
        return {};
    }
    auto owners = core::makeShared<BindlessGraphicsBindingOwners>(
        BindlessGraphicsBindingOwners::ConstructionKey{},
        std::move(desc),
        std::move(reservation.authorization),
        std::move(reservation.count),
        std::move(reservation.bytes));
    auto binding = issuer_->prepareGraphicsBinding(owners, preparation_.budget(), diagnostic);
    if (!binding)
        return {};
    if (!binding->sharedOwnerMatches(binding)) {
        diagnostic = bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::PayloadMismatch);
        return {};
    }
    const auto payload = preparation_.validatePayload(binding->owners() == owners,
        binding->argumentBufferCount(),
        binding->argumentBufferBytes());
    if (!payload.success) {
        diagnostic = payload.diagnostics;
        return {};
    }
    const auto valid = binding->validatePayload();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    binding->bindOriginalFactoryOwner(binding);
    return binding;
}

std::shared_ptr<const BindlessPreparedGraphicsBinding>
BindlessCrossExtension::prepareGraphicsBinding(std::shared_ptr<const BindlessGraphicsBindingOwners>,
    const BindlessGraphicsBindingBudget&,
    core::pmr::String& diagnostic) const
{
    diagnostic = bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::Unavailable);
    return {};
}

} // namespace arp::rhi
