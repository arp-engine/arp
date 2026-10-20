#include "rhi/frontend/bindless_root_fence_execution.h"

#include "rhi/frontend/bindless_root_sequence.h"
#include "rhi/frontend/bindless_workload_device_preflight.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"

namespace arp::rhi {
std::shared_ptr<const OriginalBindlessRootExecution>
BindlessCrossExtension::prepareOriginalRootExecution(GpuDevice& device,
    const OriginalBindlessRootFenceExecutionInputs& inputs,
    const OriginalBindlessRootFenceExecutionBudget& budget,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    const auto fail = [&](const char* id) -> std::shared_ptr<const OriginalBindlessRootExecution> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = budget.validate())
        return fail(id);
    const auto lowering = semantics::OriginalBindlessExecutionLowering::lowering(
        device.backend(), budget.intent.contractVersion);
    if (!lowering.executionAvailable || !lowering.completionAvailable || lowering.diagnostic)
        return fail(lowering.diagnostic);
    if (device.queryInterface(kExtensionBindlessCross) != this)
        return fail("RHI-BINDLESS-ROOT-EXECUTION-DEVICE-ISSUER");
    if (!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
            capabilities(), budget.intent.contractVersion)
        || capabilities().faultReadbackContractVersion != 1u)
        return fail("RHI-BINDLESS-ROOT-EXECUTION-CAPABILITY");
    if (const auto* id = OriginalCommandBindlessRootSource::validateOriginal(inputs.root))
        return fail(id);
    if (const auto* id = OriginalBindlessRootNativeExecutionContract::validateProfile(
            budget.intent, inputs.root->originalSnapshot()->plan(), budget.lifecycle))
        return fail(id);
    if (!std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
            inputs.root->originalPrepared())
        || !inputs.root->authorization()->issuedBy(this, inputs.root->authorization()))
        return fail("RHI-BINDLESS-ROOT-FENCE-EXECUTION-ROOT");
    if (!inputs.inventory || !inputs.transaction || !inputs.readback || !inputs.closureQuota
        || !inputs.submissionQuota || !inputs.viewQuota
        || inputs.viewQuota->limitBytes() != budget.maxCpuViewBytes)
        return fail("RHI-BINDLESS-ROOT-FENCE-EXECUTION-INPUTS");
    if (const auto* id = inputs.roots.validateInitializedSources(inputs.root,
            budget.closure,
            {nullptr, 0u, inputs.initializedShader, inputs.initializedShaderCount}))
        return fail(id);
    if (const auto* id =
            OriginalBindlessRootReadbackSequence::inspectInput(inputs.readback, inputs.readbacks))
        return fail(id);
    if (const auto* id = OriginalCommandStateTransaction::validateOriginal(inputs.transaction))
        return fail(id);
    for (const auto& other : {inputs.closureQuota,
             inputs.submissionQuota,
             inputs.root->originalMetadataQuota(),
             inputs.inventory->originalMetadataQuota(),
             inputs.transaction->originalMetadataQuota(),
             inputs.transaction->originalGraph()->originalMetadataQuota(),
             inputs.readback->originalMetadataQuota(),
             inputs.readback->originalProgram()->originalMetadataQuota(),
             inputs.readback->originalResult()->originalMetadataQuota()})
        if (core::SharedOwnerProjection::sameControlBlock(inputs.viewQuota, other))
            return fail("RHI-BINDLESS-ROOT-FENCE-EXECUTION-QUOTA-DOMAIN");
    const bool produced = budget.intent.contractVersion == 15u;
    if (const auto* id =
            inputs.producedGeometry.validatePublic(inputs.roots, inputs.transaction, produced))
        return fail(id);
    if (produced) {
        if (inputs.attachmentCapacity || !inputs.nextRoot || !inputs.finalRoot
            || inputs.producedPasses.empty())
            return fail("RHI-BINDLESS-PRODUCED-FENCE-EXECUTION-INPUTS");
    } else if (budget.intent.contractVersion == 11u) {
        const auto flow = OriginalBindlessMixedEntryFlowContract::inspect(
            {inputs.root, inputs.nextRoot, inputs.finalRoot},
            inputs.inventory,
            budget.mixedEntries);
        if (flow.diagnostic)
            return fail(flow.diagnostic);
        if (!inputs.attachmentCapacity
            || inputs.attachmentCapacity->capacity() != budget.closure.maxAttachmentViews)
            return fail("RHI-BINDLESS-ROOT-MIXED-ATTACHMENT-CAPACITY");
    } else if (inputs.finalRoot)
        return fail("RHI-BINDLESS-ROOT-FENCE-EXECUTION-INPUT-SCHEMA");
    else if (budget.intent.contractVersion == 9u) {
        if (const auto* id =
                OriginalCommandBindlessRootSource::validateOriginal(inputs.nextRoot, device))
            return fail(id);
        if (const auto* id =
                OriginalBindlessStorageExecutionContract::validateEntry(*inputs.root, true))
            return fail(id);
        if (const auto* id =
                OriginalBindlessStorageExecutionContract::validateEntry(*inputs.nextRoot, false))
            return fail(id);
        if (core::SharedOwnerProjection::sameOwner(inputs.root, inputs.nextRoot)
            || !core::SharedOwnerProjection::sameOwner(
                inputs.root->originalSnapshot(), inputs.nextRoot->originalSnapshot())
            || !core::SharedOwnerProjection::sameOwner(
                inputs.root->authorization(), inputs.nextRoot->authorization())
            || !inputs.root->matchesCanonicalSources(*inputs.inventory)
            || !inputs.nextRoot->matchesCanonicalSources(*inputs.inventory)
            || inputs.inventory->originalLeaseIndex(inputs.nextRoot) == SIZE_MAX)
            return fail("RHI-BINDLESS-ROOT-STORAGE-EXECUTION-CORRESPONDENCE");
        if (!inputs.attachmentCapacity
            || inputs.attachmentCapacity->capacity() != budget.closure.maxAttachmentViews)
            return fail("RHI-BINDLESS-ROOT-STORAGE-ATTACHMENT-CAPACITY");
    } else if (inputs.nextRoot || inputs.attachmentCapacity)
        return fail("RHI-BINDLESS-ROOT-FENCE-EXECUTION-INPUT-SCHEMA");
    const std::array roots{inputs.root, inputs.nextRoot, inputs.finalRoot};
    if (const auto* id = OriginalBindlessRootSequenceContract::inspectPublicInput(
            inputs.roots, roots, inputs.inventory, inputs.readback->originalProgram()))
        return fail(id);
    if (const auto* id = OriginalBindlessRootReadbackSequence::inspectRootCoverage(
            inputs.readback,
            inputs.readbacks,
            inputs.roots.count,
            [&](std::size_t i) noexcept -> const auto& {
                return inputs.roots.roots[i];
            },
            inputs.roots.budget.snapshotScope))
        return fail(id);
    OriginalBindlessWorkloadDeviceReport resources;
    if (const auto* id = OriginalBindlessWorkloadDevicePreflight::validate(
            inputs.inventory, device, resources, budget.requireWorkloadRetention))
        return fail(id);
    return prepareOriginalRootExecutionPayload(device, inputs, budget, diagnostic);
}

std::shared_ptr<const OriginalBindlessRootExecution>
BindlessCrossExtension::prepareOriginalRootExecutionPayload(GpuDevice&,
    const OriginalBindlessRootFenceExecutionInputs&,
    const OriginalBindlessRootFenceExecutionBudget&,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-BINDLESS-ROOT-FENCE-EXECUTION-PAYLOAD-UNAVAILABLE";
    return {};
}
} // namespace arp::rhi
