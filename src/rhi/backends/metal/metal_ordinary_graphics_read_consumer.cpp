#include "rhi/backends/metal/metal_ordinary_graphics_read_consumer.h"

#include "rhi/backends/metal/metal_scalar_storage_binding.h"

namespace arp::rhi_metal {
MetalRhiOrdinaryPreparedGraphicsReadConsumer::MetalRhiOrdinaryPreparedGraphicsReadConsumer(
    ConstructionKey,
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> write,
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding,
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> pass,
    shader::FragmentStorageReadProof::Facts proof,
    MetalRhiStageBufferRange range)
    : write_(std::move(write)), binding_(std::move(binding)), pass_(std::move(pass)), proof_(proof),
      range_(std::move(range))
{
    owners_.capture([&](auto&& visit) {
        projectOwners(visit);
    });
}

std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer>
MetalRhiOrdinaryPreparedGraphicsReadConsumer::prepare(
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> write,
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding,
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> pass,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer> {
        diagnostic = id;
        return {};
    };
    if (!write || !binding || !pass || write->binding()->registry() != binding->registry()
        || pass->registry() != binding->registry() || pass->provider() != binding->provider()
        || write->quota() != binding->quota() || pass->quota() != binding->quota())
        return fail("RHI-METAL-ORDINARY-READ-CONSUMER-OWNER-MISMATCH");
    for (const auto& valid :
        {write->validateObservation(), binding->validateObservation(), pass->validateObservation()})
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
    const auto& consumer = write->consumer();
    if (consumer.state != rhi::ResourceState::ShaderRead
        || consumer.stages != rhi::ShaderStage::Fragment
        || consumer.pipelineStages != rhi::PipelineStage::FragmentShader
        || consumer.access != rhi::ResourceAccess::ShaderRead)
        return fail("RHI-METAL-ORDINARY-READ-CONSUMER-SCOPE");
    const auto& artifact = binding->provider()->artifacts()[1];
    if (!artifact || artifact->stage() != shader::ShaderStage::Fragment)
        return fail("RHI-METAL-ORDINARY-READ-CONSUMER-STAGE");
    const auto& bytes = artifact->bytes(shader::ShaderArtifactRole::Spirv);
    shader::FragmentStorageReadProof::Facts proof;
    const auto& reflected = binding->provider()->interface().reflection.descriptorBindings;
    if (reflected.size() != 1u)
        return fail("RHI-METAL-ORDINARY-READ-CONSUMER-DESCRIPTOR-DOMAIN");
    const auto type = reflected.front().type;
    if (type != rhi::DescriptorType::UniformBuffer
        && type != rhi::DescriptorType::ReadOnlyStorageBuffer)
        return fail("RHI-METAL-ORDINARY-READ-CONSUMER-DESCRIPTOR-DOMAIN");
    const auto* proofId = type == rhi::DescriptorType::UniformBuffer
        ? shader::FragmentUniformReadProof::verify(
              bytes.data(), bytes.size(), artifact->entry(), proof)
        : shader::FragmentStorageReadProof::verify(
              bytes.data(), bytes.size(), artifact->entry(), proof);
    if (proofId)
        return fail(proofId);
    if (proof.replacement != write->proof().replacement)
        return fail("RHI-METAL-ORDINARY-READ-CONSUMER-EXPECTED-WORD");
    MetalRhiStageBufferRange range;
    if (const auto* id = MetalRhiScalarStorageBindingValidator::capture(
            *binding, write->resource(), proof.set, proof.binding, type, range))
        return fail(id);
    try {
        core::CpuMemoryQuotaScope scope(binding->quota());
        auto result = core::makeShared<MetalRhiOrdinaryPreparedGraphicsReadConsumer>(
            ConstructionKey{}, write, binding, pass, proof, std::move(range));
        const auto valid = result->validateObservation();
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ORDINARY-READ-CONSUMER-ALLOCATION-FAILED");
    }
}

rhi::ValidationResult MetalRhiOrdinaryPreparedGraphicsReadConsumer::validateObservation() const
{
    const auto writer = write_->validateObservation();
    if (!writer.success)
        return writer;
    const auto binding = binding_->validateObservation();
    if (!binding.success)
        return binding;
    const auto pass = pass_->validateObservation();
    if (!pass.success)
        return pass;
    if (proof_.contractVersion != 1u || proof_.byteOffset
        || proof_.byteCount != sizeof(std::uint32_t)
        || (proof_.storageClass != shader::ScalarStorageBinaryProof::uniformStorageClass
            && proof_.storageClass != shader::ScalarStorageBinaryProof::storageBufferClass)
        || proof_.replacement != write_->proof().replacement)
        return {false, "RHI-METAL-ORDINARY-READ-CONSUMER-PROOF"};
    MetalRhiStageBufferRange current;
    if (const auto* id = MetalRhiScalarStorageBindingValidator::capture(*binding_,
            write_->resource(),
            proof_.set,
            proof_.binding,
            proof_.storageClass == shader::ScalarStorageBinaryProof::uniformStorageClass
                ? rhi::DescriptorType::UniformBuffer
                : rhi::DescriptorType::ReadOnlyStorageBuffer,
            current))
        return {false, id};
    if (const auto* id = MetalRhiStageBufferRangeValidator::validate(
            binding_->provider()->native()->graphicsBufferFacts[1],
            rhi::ShaderStage::Fragment,
            current.fact.slot,
            0u,
            write_->resource().desc().sizeBytes,
            range_))
        return {false, id};
    if (!range_.nativeIdentity.matches(metalRhiNativePayloadFor(write_->resource())->resource))
        return {false, "RHI-METAL-ORDINARY-READ-CONSUMER-NATIVE-CAPTURE"};
    if (!owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return {false, "RHI-METAL-ORDINARY-READ-CONSUMER-OWNERS"};
    return {true, {}};
}
} // namespace arp::rhi_metal
