#include "rhi/backends/vulkan/vulkan_original_compute_pipeline.h"

#include "rhi/frontend/pipeline_state_copy.h"
#include "shader/spirv_instruction_view.h"

namespace arp::rhi_vulkan {

namespace {
const char* validateComputeBinary(const core::ByteBuffer& bytes,
    const rhi::ShaderEntrypoint& entry,
    const VkPhysicalDeviceLimits& limits) noexcept
{
    const std::array size{
        entry.computeWorkgroupSizeX, entry.computeWorkgroupSizeY, entry.computeWorkgroupSizeZ};
    std::uint64_t invocations = 1u;
    for (std::size_t i = 0u; i < size.size(); ++i) {
        if (!size[i] || size[i] > limits.maxComputeWorkGroupSize[i]
            || invocations > limits.maxComputeWorkGroupInvocations / size[i])
            return "RHI-SYNC-COMPUTE-CREATION-WORKGROUP-LIMIT";
        invocations *= size[i];
    }
    shader::SpirvInstructionView module(bytes.data(), bytes.size());
    if (const auto* id = module.diagnostic())
        return id;
    std::uint32_t entries = 0u, entryId = 0u, localId = 0u, localModes = 0u;
    for (auto offset = shader::SpirvInstructionView::headerWords; offset < module.sizeWords();) {
        shader::SpirvInstructionView::Instruction op;
        if (!module.read(offset, op))
            return "RHI-SYNC-COMPUTE-CREATION-SPIRV-INSTRUCTION";
        // SPIR-V grammar: Capability Shader=1, GLCompute=5, LocalSize=17.
        if (op.opcode() == 17u && (op.operands() != 1u || op.operand(0u) != 1u))
            return "RHI-SYNC-COMPUTE-CREATION-SPIRV-CAPABILITY";
        if (op.opcode() == 15u) {
            ++entries;
            const auto stringWords =
                (entry.entryPoint.size() + sizeof(std::uint32_t)) / sizeof(std::uint32_t);
            if (op.operands() < 2u + stringWords || op.operand(0u) != 5u
                || !op.stringEquals(2u, entry.entryPoint, op.operands() - 2u - stringWords))
                return "RHI-SYNC-COMPUTE-CREATION-SPIRV-ENTRY";
            entryId = op.operand(1u);
        }
        if (op.opcode() == 16u) {
            if (op.operands() != 5u || op.operand(1u) != 17u || op.operand(2u) != size[0]
                || op.operand(3u) != size[1] || op.operand(4u) != size[2])
                return "RHI-SYNC-COMPUTE-CREATION-SPIRV-LOCAL-SIZE";
            localId = op.operand(0u);
            ++localModes;
        }
        // LocalSizeId needs a separate specialization contract; /1 refuses it.
        if (op.opcode() == 331u)
            return "RHI-SYNC-COMPUTE-CREATION-SPIRV-LOCAL-SIZE";
        offset = op.next();
    }
    return entries == 1u && localModes == 1u && entryId == localId
        ? nullptr
        : "RHI-SYNC-COMPUTE-CREATION-SPIRV-ENTRY";
}
} // namespace

VulkanOriginalArtifactComputeCreation::VulkanOriginalArtifactComputeCreation(ConstructionKey,
    std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> input,
    std::shared_ptr<rhi::PipelineLayout> layout,
    const rhi::ComputePipelineDesc& definition,
    Budget budget,
    std::shared_ptr<core::CpuMemoryQuota> nativeQuota,
    std::shared_ptr<VulkanRhiRuntime> runtime)
    : input_(std::move(input)), layout_(std::move(layout)), budget_(budget),
      nativeQuota_(std::move(nativeQuota)), runtime_(std::move(runtime)),
      device_(runtime_->logicalDevice()),
      definition_(rhi::ComputePipelineStateCopy::copy(definition))
{}

std::shared_ptr<const VulkanOriginalArtifactComputeCreation>
VulkanOriginalArtifactComputeCreation::create(const rhi::GpuDevice& device,
    VulkanRhiNativeResourceAllocator& allocator,
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
    const std::shared_ptr<rhi::PipelineLayout>& layout,
    const rhi::ComputePipelineDesc& authored,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
    rhi::semantics::DirectObjectBudgetLease lease,
    const char*& diagnostic)
{
    diagnostic = Contract::validateBudget(budget);
    if (diagnostic || (diagnostic = rhi::OriginalSingleEntryPipelineInput::validateOriginal(input)))
        return {};
    if (input->artifact()->stage() != shader::ShaderStage::Compute || !layout
        || !layout->sharedOwnerMatches(layout) || authored.layout != layout.get()
        || authored.shaderPackage != &input->package()) {
        diagnostic = "RHI-SYNC-COMPUTE-CREATION-INPUT";
        return {};
    }
    if ((diagnostic = device.validateOriginalResourceRegistration(*layout))
        || (diagnostic = Contract::validateProfile(authored)))
        return {};
    if (authored.label.size() > budget.maxLabelBytes) {
        diagnostic = "RHI-SYNC-COMPUTE-CREATION-LABEL-CAPACITY";
        return {};
    }
    if (!ownerQuota || ownerQuota->limitBytes() != budget.maxCpuOwnerBytes || !nativeQuota
        || nativeQuota->limitBytes() != budget.maxCpuNativeBytes) {
        diagnostic = "RHI-SYNC-COMPUTE-CREATION-QUOTA";
        return {};
    }
    const std::array domains{ownerQuota, nativeQuota, input->quota(), input->artifact()->quota()};
    if (!core::SharedOwnerProjection::hasIndependentOwners(domains.size(),
            [&](std::size_t i) -> const auto& {
                return domains[i];
            })
        || device.overlapsOriginalDeviceCpuOwner(ownerQuota)
        || device.overlapsOriginalDeviceCpuOwner(nativeQuota)
        || layout->originalResourceRegistration().independentCpuQuotaDiagnostic(ownerQuota)
        || layout->originalResourceRegistration().independentCpuQuotaDiagnostic(nativeQuota)) {
        diagnostic = "RHI-SYNC-COMPUTE-CREATION-QUOTA-DOMAIN";
        return {};
    }
    const auto runtime = allocator.runtime();
    Contract::Facts facts;
    facts.device = runtime && runtime->valid() && !runtime->syntheticRuntime()
        && runtime->logicalDevice() && !runtime->coreQueueCapabilityDiagnostic()
        && runtime->coreQueueCapabilities().compute;
    facts.methods = facts.device;
    for (const auto* name : {"vkCreateShaderModule",
             "vkDestroyShaderModule",
             "vkCreateDescriptorSetLayout",
             "vkDestroyDescriptorSetLayout",
             "vkCreatePipelineLayout",
             "vkDestroyPipelineLayout",
             "vkCreateComputePipelines",
             "vkDestroyPipeline"})
        if (facts.device && !runtime->getDeviceProcAddr(name))
            facts.methods = false;
    const auto decision = Contract::lowering(device.backend(), budget, facts);
    if (!decision.creationEligible) {
        diagnostic = decision.diagnostic;
        return {};
    }
    const auto target =
        rhi::OriginalSingleEntryPipelineInput::selectTarget(input, device.backend());
    if (target.diagnostic || !target.bytes || target.validatorOnly) {
        diagnostic = target.diagnostic ? target.diagnostic : "RHI-SYNC-COMPUTE-CREATION-TARGET";
        return {};
    }
    if (target.bytes->size() > budget.maxTargetBytes) {
        diagnostic = "RHI-SYNC-COMPUTE-CREATION-TARGET-CAPACITY";
        return {};
    }
    if ((diagnostic = validateComputeBinary(
             *target.bytes, authored.computeShader, runtime->nativeLimits())))
        return {};
    try {
        core::CpuMemoryQuotaScope ownerScope(ownerQuota);
        auto receipt = core::makeUnique<VulkanOriginalArtifactComputeCreation>(
            ConstructionKey{}, input, layout, authored, budget, nativeQuota, runtime);
        {
            core::CpuMemoryQuotaScope nativeScope(nativeQuota);
            const auto report =
                validateVulkanOriginalComputeDefinition(input, receipt->definition_);
            if (!report.success) {
                diagnostic = "RHI-SYNC-COMPUTE-CREATION-DEFINITION";
                return {};
            }
            auto token =
                core::makeShared<rhi::semantics::DirectObjectBudgetLease>(std::move(lease));
            core::UnsignedElementSpan source(target.bytes->data(),
                target.bytes->size(),
                core::UnsignedElementSpan::Width::Uint32,
                core::UnsignedElementSpan::ByteOrder::LittleEndian);
            core::pmr::Vector<std::uint32_t> words(
                source.size(), &core::defaultCpuMemoryResource());
            for (std::size_t i = 0u; i < words.size(); ++i)
                source.read(i, words[i]);
            auto native = allocator.createComputePipelineFromBytes(
                receipt->definition_, words.data(), words.size());
            if (!native.success || !native.payload) {
                diagnostic = "RHI-SYNC-COMPUTE-CREATION-NATIVE-FAILED";
                return {};
            }
            native.payload->directObjectBudgetReleaseToken = std::move(token);
            receipt->payload_ = std::move(native.payload);
        }
        if ((diagnostic = receipt->validate()))
            return {};
        return shareOriginal(std::move(receipt));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-SYNC-COMPUTE-CREATION-ALLOCATION";
        return {};
    }
}

const char* VulkanOriginalArtifactComputeCreation::validate() const noexcept
{
    if (const auto* id = Contract::validateBudget(budget_))
        return id;
    if (!originalMetadataQuota()
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuOwnerBytes || !nativeQuota_
        || nativeQuota_->limitBytes() != budget_.maxCpuNativeBytes)
        return "RHI-SYNC-COMPUTE-CREATION-QUOTA-LIMIT";
    if (const auto* id = rhi::OriginalSingleEntryPipelineInput::validateOriginal(input_))
        return id;
    if (!layout_ || !layout_->sharedOwnerMatches(layout_))
        return "RHI-SYNC-COMPUTE-CREATION-LAYOUT-OWNER";
    if (const auto* id = layout_->originalResourceRegistration().diagnostic())
        return id;
    if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime() || !device_
        || runtime_->coreQueueCapabilityDiagnostic() || runtime_->logicalDevice() != device_
        || !payload_ || payload_->backendVulkanDeviceHandle() != device_
        || !payload_->backendVulkanComputePipelineHandle()
        || !payload_->backendVulkanComputePipelineLayoutHandle())
        return "RHI-SYNC-COMPUTE-CREATION-NATIVE-IDENTITY";
    return definition_.layout == layout_.get() && definition_.shaderPackage == &input_->package()
        ? nullptr
        : "RHI-SYNC-COMPUTE-CREATION-DEFINITION-IDENTITY";
}

bool VulkanOriginalArtifactComputeCreation::usesCpuQuotaDomain(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    for (const auto& domain : std::array{
             originalMetadataQuota(), nativeQuota_, input_->quota(), input_->artifact()->quota()})
        if (core::SharedOwnerProjection::overlapsOwner(quota, domain))
            return true;
    return false;
}

} // namespace arp::rhi_vulkan
