#include "rhi/frontend/bindless_compute_binding.h"

#include "core/math/unsigned_range.h"
#include "rhi/frontend/bindless_mixed_entry_flow.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/frontend/bindless_root_sequence.h"
#include "rhi/frontend/bindless_workload_device_preflight.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"

namespace arp::rhi {

OriginalBindlessMixedEntryFlowContract::Inspection OriginalBindlessMixedEntryFlowContract::inspect(
    const Roots& roots,
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const Budget& budget) noexcept
{
    const auto reject = [](const char* id) {
        Inspection result;
        result.diagnostic = id;
        return result;
    };
    if (const auto* id = validateIntent(budget))
        return reject(id);
    if (const auto* id = OriginalCommandResourceSources::validateOriginal(inventory))
        return reject(id);
    Inspection result;

    struct Entry {
        std::array<BindlessResourceAccess, 2u> reads{};
        BindlessResourceAccess write{};
        std::size_t writeSource = SIZE_MAX, depthSource = SIZE_MAX, resultsSource = SIZE_MAX;
        std::size_t readCount = 0u, writeCount = 0u, resultCount = 0u;
        std::size_t metadataCount = 0u, faultCount = 0u;
    };

    std::array<Entry, 3u> entries{};
    for (std::size_t step = 0u; step < roots.size(); ++step) {
        const auto& root = roots[step];
        if (const auto* id = Root::validateOriginal(root))
            return reject(id);
        result.rootSources[step] = inventory->originalLeaseIndex(root);
        if (result.rootSources[step] == SIZE_MAX || !root->matchesCanonicalSources(*inventory)
            || !root->ordinaryDomains().empty() || root->originalShaderEntryAccessVersion() != 1u
            || !std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                root->originalPrepared()))
            return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-INVENTORY");
        for (std::size_t previous = 0u; previous < step; ++previous)
            if (core::SharedOwnerProjection::sameOwner(root, roots[previous]))
                return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-DUPLICATE");
        if (!core::SharedOwnerProjection::sameOwner(
                root->originalSnapshot(), roots.front()->originalSnapshot())
            || !core::SharedOwnerProjection::sameOwner(
                root->authorization(), roots.front()->authorization()))
            return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-CORRESPONDENCE");
        const auto count = root->size();
        if (count > budget.maxDeclarationVisits - result.declarationVisits)
            return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-VISIT-CAPACITY");
        result.declarationVisits += count;
        const auto& snapshot = root->originalSnapshot();
        const auto& metadata =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::Metadata);
        const auto& fault =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
        if (!metadata || !fault)
            return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-INTERNAL-BUFFERS");
        // This domain has no samplers. Keeping physical and declaration indices
        // identical makes every depth proof below refer to its exact binding.
        for (const auto& element : root->elements())
            if (element.declaration.write.type == DescriptorType::Sampler)
                return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-SAMPLER");
        auto& entry = entries[step];
        for (std::size_t index = 0u; index < count; ++index) {
            BindlessResourceAccess access;
            std::size_t source = SIZE_MAX;
            bool active = false;
            if (const auto* id = root->readOriginalShaderDeclaration(
                    index, ShaderStage::Compute, access, source, active))
                return reject(id);
            if (!active)
                continue;
            if (access.stages != ShaderStage::Compute
                || access.pipelineStages != PipelineStage::ComputeShader)
                return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-STAGE");
            if (access.kind == BindlessAccessKind::Buffer) {
                if (access.resource == metadata->resource()) {
                    if (access.state != ResourceState::ShaderRead
                        || access.access != ResourceAccess::ShaderRead)
                        return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-METADATA-ACCESS");
                    ++entry.metadataCount;
                    continue;
                }
                if (access.resource == fault->resource()) {
                    if (access.state != ResourceState::ShaderWrite
                        || access.access
                            != (ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite))
                        return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-FAULT-ACCESS");
                    ++entry.faultCount;
                    continue;
                }
                if (step != 2u || access.state != ResourceState::ShaderWrite
                    || access.access != ResourceAccess::ShaderWrite || !access.bufferSize
                    || source == SIZE_MAX)
                    return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-RESULTS");
                entry.resultsSource = source;
                ++entry.resultCount;
                continue;
            }
            const bool write = access.state == ResourceState::ShaderWrite
                && access.access == ResourceAccess::ShaderWrite;
            const bool read = access.state == ResourceState::ShaderRead
                && access.access == ResourceAccess::ShaderRead;
            if ((!write && !read) || index >= root->elements().size())
                return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-TEXTURE-ACCESS");
            const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                &root->elements()[index].owner);
            if (!view || !*view)
                return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-VIEW");
            if (access.textureRange.aspect == TextureAspect::Depth) {
                if (step || !read || entry.depthSource != SIZE_MAX)
                    return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-DEPTH-ROLE");
                const auto proof = root->originalCompletedDepthAt(index);
                if (const auto* id = OriginalCompletedDepthTextureSource::validateOriginal(proof))
                    return reject(id);
                if (!core::SharedOwnerProjection::sameOwner(proof->originalViewSource(), *view))
                    return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-DEPTH-PROOF");
                entry.depthSource = source;
                continue;
            }
            if (const auto* id = OriginalStorageTextureContentPlanContract::inspectViewBinding(
                    (*view)->originalParentLease()->originalObject()->desc(),
                    (*view)->originalObject()->desc(),
                    access.state,
                    access.pipelineStages,
                    access.access,
                    write))
                return reject(id);
            if (write) {
                entry.write = access;
                entry.writeSource = source;
                ++entry.writeCount;
            } else {
                if (entry.readCount == entry.reads.size())
                    return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-READ-COUNT");
                entry.reads[entry.readCount++] = access;
            }
        }
        if (entry.metadataCount != 1u || entry.faultCount != 1u)
            return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-INTERNAL-COVERAGE");
    }
    const auto& copy = entries[0u];
    const auto& reduce = entries[1u];
    const auto& read = entries[2u];
    if (copy.depthSource == SIZE_MAX || copy.readCount || copy.writeCount != 1u || copy.resultCount
        || reduce.depthSource != SIZE_MAX || reduce.readCount != 1u || reduce.writeCount != 1u
        || reduce.resultCount || read.depthSource != SIZE_MAX || read.readCount != 2u
        || read.writeCount || read.resultCount != 1u)
        return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-ENTRY-ROLES");
    const auto sameCell = [](const auto& a, const auto& b) {
        return a.resource == b.resource && a.textureRange == b.textureRange;
    };
    const auto& a = copy.write;
    const auto& b = reduce.write;
    if (a.resource == b.resource && a.textureRange.aspect == b.textureRange.aspect
        && core::overlapsUnsignedRanges(a.textureRange.baseMipLevel,
            a.textureRange.mipLevelCount,
            b.textureRange.baseMipLevel,
            b.textureRange.mipLevelCount)
        && core::overlapsUnsignedRanges(a.textureRange.baseArrayLayer,
            a.textureRange.arrayLayerCount,
            b.textureRange.baseArrayLayer,
            b.textureRange.arrayLayerCount))
        return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-OUTPUT-ALIAS");
    if (!sameCell(copy.write, reduce.reads[0u])
        || !((sameCell(copy.write, read.reads[0u]) && sameCell(reduce.write, read.reads[1u]))
            || (sameCell(copy.write, read.reads[1u]) && sameCell(reduce.write, read.reads[0u]))))
        return reject("RHI-BINDLESS-MIXED-ENTRY-FLOW-CELL-CORRESPONDENCE");
    result.depthSource = copy.depthSource;
    result.copyOutputSource = copy.writeSource;
    result.reduceOutputSource = reduce.writeSource;
    result.resultsSource = read.resultsSource;
    return result;
}

std::shared_ptr<const OriginalBindlessRootExecution>
BindlessCrossExtension::prepareOriginalRootExecution(GpuDevice& device,
    const OriginalBindlessRootExecutionInputs& inputs,
    const OriginalBindlessRootExecutionBudget& budget,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (const auto* id = inputs.initializedGraphics.diagnostic()) {
        diagnostic = id;
        return {};
    }
    if (const auto* id = budget.validate()) {
        diagnostic = id;
        return {};
    }
    if (const auto* id = OriginalBindlessInitializedGraphicsContract::validateExecutionBudget(
            device.backend(), inputs.initializedGraphics, budget.closure)) {
        diagnostic = id;
        return {};
    }
    const auto lowering = semantics::OriginalBindlessExecutionLowering::lowering(
        device.backend(), budget.intent.contractVersion);
    if (!lowering.executionAvailable || !lowering.completionAvailable || lowering.diagnostic) {
        diagnostic = lowering.diagnostic;
        return {};
    }
    if (device.queryInterface(kExtensionBindlessCross) != this) {
        diagnostic = "RHI-BINDLESS-ROOT-EXECUTION-DEVICE-ISSUER";
        return {};
    }
    if (!semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
            capabilities(), budget.intent.contractVersion)
        || !capabilities().faultReadbackContractVersion) {
        diagnostic = "RHI-BINDLESS-ROOT-EXECUTION-CAPABILITY";
        return {};
    }
    if (const auto* id = OriginalBindlessInitializedGraphicsContract::validateRootInput(
            inputs.root, inputs.initializedGraphics)) {
        diagnostic = id;
        return {};
    }
    if (const auto* id = OriginalBindlessRootNativeExecutionContract::validateProfile(
            budget.intent, inputs.root->originalSnapshot()->plan(), budget.lifecycle)) {
        diagnostic = id;
        return {};
    }
    if (const auto* id = inputs.roots.validateInitializedSources(inputs.root,
            budget.closure,
            {inputs.geometry,
                inputs.geometryCount,
                inputs.initializedShader,
                inputs.initializedShaderCount})) {
        diagnostic = id;
        return {};
    }
    const bool geometry = OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(
        inputs.roots.closureBudget(inputs.root, budget.closure));
    if (geometry && capabilities().originalInitializedGeometryExecutionContractVersion != 1u) {
        diagnostic = "RHI-BINDLESS-ROOT-EXECUTION-GEOMETRY-CAPABILITY-OR-INPUTS";
        return {};
    }
    const bool produced = budget.intent.contractVersion == 16u;
    if (const auto* id =
            inputs.producedGeometry.validatePublic(inputs.roots, inputs.transaction, produced)) {
        diagnostic = id;
        return {};
    }
    if (produced) {
        if (!inputs.nextRoot || !inputs.finalRoot) {
            diagnostic = "RHI-BINDLESS-PRODUCED-CALLBACK-EXECUTION-INPUTS";
            return {};
        }
    } else if (budget.intent.contractVersion == 12u) {
        const auto flow = OriginalBindlessMixedEntryFlowContract::inspect(
            {inputs.root, inputs.nextRoot, inputs.finalRoot},
            inputs.inventory,
            budget.mixedEntries);
        if (flow.diagnostic) {
            diagnostic = flow.diagnostic;
            return {};
        }
    } else if (inputs.finalRoot) {
        diagnostic = "RHI-BINDLESS-ROOT-EXECUTION-INPUT-SCHEMA";
        return {};
    } else if (budget.intent.contractVersion == 10u) {
        if (const auto* id = OriginalCommandBindlessRootSource::validateOriginal(inputs.nextRoot)) {
            diagnostic = id;
            return {};
        }
        if (const auto* id = OriginalBindlessStorageExecutionContract::validatePair(
                *inputs.root, *inputs.nextRoot)) {
            diagnostic = id;
            return {};
        }
    } else if (inputs.nextRoot) {
        diagnostic = "RHI-BINDLESS-ROOT-EXECUTION-INPUT-SCHEMA";
        return {};
    }
    if (const auto* id =
            OriginalBindlessRootReadbackSequence::inspectInput(inputs.readback, inputs.readbacks)) {
        diagnostic = id;
        return {};
    }
    const std::array roots{inputs.root, inputs.nextRoot, inputs.finalRoot};
    if (!inputs.readback) {
        diagnostic = "RHI-BINDLESS-ROOT-EXECUTION-INPUTS";
        return {};
    }
    if (const auto* id = OriginalBindlessRootSequenceContract::inspectPublicInput(
            inputs.roots, roots, inputs.inventory, inputs.readback->originalProgram())) {
        diagnostic = id;
        return {};
    }
    OriginalBindlessWorkloadDeviceReport resources;
    if (const auto* id = OriginalBindlessWorkloadDevicePreflight::validate(
            inputs.inventory, device, resources, budget.requireWorkloadRetention)) {
        diagnostic = id;
        return {};
    }
    return prepareOriginalRootExecutionPayload(device, inputs, budget, diagnostic);
}

std::shared_ptr<const OriginalBindlessRootExecution>
BindlessCrossExtension::prepareOriginalRootExecutionPayload(GpuDevice&,
    const OriginalBindlessRootExecutionInputs&,
    const OriginalBindlessRootExecutionBudget&,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-BINDLESS-ROOT-EXECUTION-PAYLOAD-UNAVAILABLE";
    return {};
}

ValidationResult BindlessComputeBindingOwners::validate() const
{
    if (desc_.pipeline && !desc_.pipeline->sharedOwnerMatches(desc_.pipeline))
        return {false,
            bindlessBindingDiagnostic(
                BindlessBindingDomain::Compute, BindlessBindingDiagnostic::OwnersMismatch)};
    return validateBindlessBindingOwners(BindlessBindingDomain::Compute,
        desc_.optIn,
        desc_.snapshot,
        desc_.plan,
        desc_.generation,
        authorization_,
        desc_.pipeline ? desc_.pipeline->bindlessAuthorization() : nullptr);
}

ValidationResult BindlessPreparedComputeBinding::validateForRecord(
    const ComputePipeline& pipeline, const BindlessAccessDeclarations& accesses) const
{
    return validatePreparedBindlessBindingForRecord(
        *this, pipeline, accesses, ShaderStage::Compute, BindlessBindingDomain::Compute);
}

BindlessComputeBindingPool::BindlessComputeBindingPool(
    const BindlessCrossExtension& issuer, BindlessComputeBindingBudget budget)
    : issuer_(&issuer), preparation_(issuer, budget, BindlessBindingDomain::Compute)
{}

std::shared_ptr<const BindlessPreparedComputeBinding> BindlessComputeBindingPool::prepare(
    BindlessComputeBindingDesc desc, core::pmr::String& diagnostic)
{
    if (desc.pipeline && !desc.pipeline->sharedOwnerMatches(desc.pipeline)) {
        diagnostic = bindlessBindingDiagnostic(
            BindlessBindingDomain::Compute, BindlessBindingDiagnostic::OwnersMismatch);
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
    auto owners = core::makeShared<BindlessComputeBindingOwners>(
        BindlessComputeBindingOwners::ConstructionKey{},
        std::move(desc),
        std::move(reservation.authorization),
        std::move(reservation.count),
        std::move(reservation.bytes));
    auto binding = issuer_->prepareComputeBinding(owners, preparation_.budget(), diagnostic);
    if (!binding)
        return {};
    if (!binding->sharedOwnerMatches(binding)) {
        diagnostic = bindlessBindingDiagnostic(
            BindlessBindingDomain::Compute, BindlessBindingDiagnostic::PayloadMismatch);
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

std::shared_ptr<const BindlessPreparedComputeBinding> BindlessCrossExtension::prepareComputeBinding(
    std::shared_ptr<const BindlessComputeBindingOwners>,
    const BindlessComputeBindingBudget&,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-BINDLESS-PREPARED-COMPUTE-UNAVAILABLE";
    return {};
}

} // namespace arp::rhi
