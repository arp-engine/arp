#include "rhi/backends/metal/metal_ordinary_graphics_write.h"

#include "rhi/backends/metal/metal_scalar_storage_binding.h"

namespace arp::rhi_metal {

MetalRhiOrdinaryPreparedGraphicsWrite::MetalRhiOrdinaryPreparedGraphicsWrite(ConstructionKey,
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding,
    std::shared_ptr<rhi::GpuBuffer> buffer,
    core::UniquePtr<rhi::GraphicsStorageWriteContract> contract,
    shader::FragmentAtomicWriteProof::Facts proof,
    const rhi::BindlessAccessDeclarations& producer,
    rhi::ResourceBarrierDesc dependency,
    rhi::BindlessResourceAccess consumer,
    MetalRhiStageBufferRange range)
    : binding_(std::move(binding)), buffer_(std::move(buffer)),
      seal_(buffer_->gpuWriteSealToken().lock()), contract_(std::move(contract)), proof_(proof),
      producer_(1u, rhi::BindlessAccessRetention::GraphicsPass), dependency_(dependency),
      consumer_(consumer), range_(std::move(range)), bufferBytes_(buffer_->desc().sizeBytes),
      usage_(buffer_->desc().usage), transferSealed_(buffer_->gpuTransferWritesSealed())
{
    // Factory prevalidates this exact immutable synchronous declaration view.
    const auto copied = producer_.capture(producer);
    if (!copied.success)
        producer_.clear();
    owners_.capture([&](auto&& visit) {
        projectOwners(visit);
    });
}

const char* MetalRhiOrdinaryPreparedGraphicsWrite::captureRange(
    const MetalRhiOrdinaryPreparedGraphicsBinding& binding,
    const rhi::GpuBuffer& buffer,
    const shader::FragmentAtomicWriteProof::Facts& proof,
    MetalRhiStageBufferRange& range)
{
    return MetalRhiScalarStorageBindingValidator::capture(
        binding, buffer, proof.set, proof.binding, rhi::DescriptorType::StorageBuffer, range);
}

std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>
MetalRhiOrdinaryPreparedGraphicsWrite::prepare(
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding,
    std::shared_ptr<rhi::GpuBuffer> buffer,
    const rhi::BindlessAccessDeclarations& producer,
    const rhi::ResourceBarrierDesc& dependency,
    const rhi::BindlessResourceAccess& consumer,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> {
        diagnostic = id;
        return {};
    };
    if (!binding || !buffer)
        return fail("RHI-METAL-ORDINARY-WRITE-OWNER-MISSING");
    const auto observed = binding->validateObservation();
    if (!observed.success) {
        diagnostic = observed.diagnostics;
        return {};
    }
    if (!buffer->hostWritesSealed() || buffer->gpuWritesSealed())
        return fail("RHI-METAL-ORDINARY-WRITE-SEAL-MISMATCH");
    if (buffer->state() != rhi::ResourceState::ShaderWrite)
        return fail("RHI-METAL-ORDINARY-WRITE-PRODUCER-STATE");
    if (!producer.valid() || producer.size() != 1u)
        return fail("RHI-METAL-ORDINARY-WRITE-DECLARATION-DOMAIN");
    const auto& provider = *binding->provider();
    const auto& artifact = provider.artifacts()[1];
    if (!artifact || artifact->stage() != shader::ShaderStage::Fragment)
        return fail("RHI-METAL-ORDINARY-WRITE-ARTIFACT-STAGE");
    shader::FragmentAtomicWriteProof::Facts proof;
    const auto& bytes = artifact->bytes(shader::ShaderArtifactRole::Spirv);
    if (const auto* id = shader::FragmentAtomicWriteProof::verify(
            bytes.data(), bytes.size(), artifact->entry(), proof))
        return fail(id);
    MetalRhiStageBufferRange range;
    if (const auto* id = captureRange(*binding, *buffer, proof, range))
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(binding->quota());
        const auto* set = provider.setLayouts().find(proof.set);
        if (!set || set->bindings.size() != 1u)
            return fail("RHI-METAL-ORDINARY-WRITE-LAYOUT-DOMAIN");
        rhi::PipelineLayoutDesc layout;
        layout.descriptorLayouts.push_back(*set);
        layout.descriptorSets.push_back({proof.set, set->name});
        rhi::GraphicsStorageWriteFootprint footprint;
        footprint.set = proof.set;
        footprint.binding = proof.binding;
        footprint.replacement = proof.replacement;
        auto contract = core::makeUnique<rhi::GraphicsStorageWriteContract>(
            layout, provider.interface().reflection, footprint);
        if (const auto* id = contract->validateBindingRange(buffer->desc().sizeBytes, 0u, false))
            return fail(id);
        const auto declared = contract->validateProducerScope(buffer->id(), producer);
        if (!declared.success) {
            diagnostic = declared.diagnostics;
            return {};
        }
        if (const auto* id = contract->validateDependency(buffer->id(), dependency, consumer, true))
            return fail(id);
        auto result = core::makeShared<MetalRhiOrdinaryPreparedGraphicsWrite>(ConstructionKey{},
            binding,
            buffer,
            std::move(contract),
            proof,
            producer,
            dependency,
            consumer,
            std::move(range));
        const auto validated = result->validateObservation();
        if (!validated.success) {
            diagnostic = validated.diagnostics;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ORDINARY-WRITE-PREPARATION-ALLOCATION-FAILED");
    }
}

rhi::ValidationResult MetalRhiOrdinaryPreparedGraphicsWrite::validateObservation() const
{
    const auto observed = binding_->validateObservation();
    if (!observed.success)
        return observed;
    const auto seal = buffer_->gpuWriteSealToken().lock();
    if (!buffer_->hostWritesSealed() || buffer_->gpuWritesSealed() || !seal_ || seal != seal_
        || seal.owner_before(seal_) || seal_.owner_before(seal)
        || buffer_->gpuTransferWritesSealed() != transferSealed_)
        return {false, "RHI-METAL-ORDINARY-WRITE-SEAL-MISMATCH"};
    if (buffer_->desc().sizeBytes != bufferBytes_ || buffer_->desc().usage != usage_)
        return {false, "RHI-METAL-ORDINARY-WRITE-EXTENT-MISMATCH"};
    if (!contract_ || contract_->diagnostic() || proof_.contractVersion != 1u || proof_.byteOffset
        || proof_.byteCount != rhi::GraphicsStorageWriteContract::footprintBytes
        || !contract_->descriptors().matchesBindings(binding_->provider()->interface().reflection))
        return {false, "RHI-METAL-ORDINARY-WRITE-PROOF-CAPTURE-MISMATCH"};
    const auto declared = contract_->validateProducerScope(buffer_->id(), producer_);
    if (!declared.success)
        return declared;
    if (const auto* id = contract_->validateDependency(buffer_->id(), dependency_, consumer_, true))
        return {false, id};
    MetalRhiStageBufferRange current;
    if (const auto* id = captureRange(*binding_, *buffer_, proof_, current))
        return {false, id};
    if (const auto* id = MetalRhiStageBufferRangeValidator::validate(
            binding_->provider()->native()->graphicsBufferFacts[1],
            rhi::ShaderStage::Fragment,
            current.fact.slot,
            0u,
            bufferBytes_,
            range_))
        return {false, id};
    if (!range_.nativeIdentity.matches(metalRhiNativePayloadFor(*buffer_)->resource))
        return {false, "RHI-METAL-ORDINARY-WRITE-NATIVE-CAPTURE-MISMATCH"};
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return {false, "RHI-METAL-ORDINARY-WRITE-OWNER-MISMATCH"};
    return {true, {}};
}
} // namespace arp::rhi_metal
