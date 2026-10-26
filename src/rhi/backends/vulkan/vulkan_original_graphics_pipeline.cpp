#include "rhi/backends/vulkan/vulkan_original_graphics_pipeline.h"

#include "rhi/frontend/pipeline_state_copy.h"
#include "shader/spirv_instruction_view.h"

namespace arp::rhi_vulkan {

VulkanOriginalArtifactGraphicsCreation::VulkanOriginalArtifactGraphicsCreation(ConstructionKey,
    std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> input,
    std::shared_ptr<rhi::PipelineLayout> layout,
    const rhi::RenderPipelineDesc& definition,
    Budget budget,
    std::shared_ptr<core::CpuMemoryQuota> nativeQuota,
    std::shared_ptr<VulkanRhiRuntime> runtime)
    : input_(std::move(input)), layout_(std::move(layout)), budget_(budget),
      nativeQuota_(std::move(nativeQuota)), runtime_(std::move(runtime)),
      device_(runtime_->logicalDevice()),
      definition_(rhi::RenderPipelineStateCopy::copy(definition))
{}

std::shared_ptr<const VulkanOriginalArtifactGraphicsCreation>
VulkanOriginalArtifactGraphicsCreation::create(const rhi::GpuDevice& device,
    VulkanRhiNativeResourceAllocator& allocator,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
    const std::shared_ptr<rhi::PipelineLayout>& layout,
    const rhi::RenderPipelineDesc& authored,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
    rhi::semantics::DirectObjectBudgetLease lease,
    const char*& diagnostic)
{
    diagnostic = Contract::validateBudget(budget);
    if (diagnostic || (diagnostic = rhi::OriginalGraphicsPipelineInput::validateOriginal(input)))
        return {};
    if (!layout || !layout->sharedOwnerMatches(layout)) {
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-LAYOUT-OWNER";
        return {};
    }
    if ((diagnostic = device.validateOriginalResourceRegistration(*layout)))
        return {};
    if (authored.layout != layout.get() || authored.shaderPackage != &input->package()) {
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-INPUT";
        return {};
    }
    if ((diagnostic = Contract::validateProfile(authored)))
        return {};
    if (authored.label.size() > budget.maxLabelBytes) {
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-LABEL-CAPACITY";
        return {};
    }
    if (!ownerQuota || ownerQuota->limitBytes() != budget.maxCpuOwnerBytes || !nativeQuota
        || nativeQuota->limitBytes() != budget.maxCpuNativeBytes) {
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-QUOTA";
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
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-QUOTA-DOMAIN";
        return {};
    }
    auto runtime = allocator.runtime();
    Contract::Facts facts;
    facts.device = runtime && runtime->valid() && !runtime->syntheticRuntime()
        && runtime->logicalDevice() != VK_NULL_HANDLE && !runtime->coreQueueCapabilityDiagnostic();
    facts.methods = facts.device;
    for (const auto* name : {"vkCreateShaderModule",
             "vkDestroyShaderModule",
             "vkCreateDescriptorSetLayout",
             "vkDestroyDescriptorSetLayout",
             "vkCreatePipelineLayout",
             "vkDestroyPipelineLayout",
             "vkCreateRenderPass",
             "vkDestroyRenderPass",
             "vkCreateGraphicsPipelines",
             "vkDestroyPipeline"})
        if (facts.device && !runtime->getDeviceProcAddr(name))
            facts.methods = false;
    const auto decision = Contract::lowering(device.backend(), budget, facts);
    if (!decision.creationEligible) {
        diagnostic = decision.diagnostic;
        return {};
    }
    const auto target = rhi::OriginalGraphicsPipelineInput::selectTarget(input, device.backend());
    if (target.diagnostic || target.sourceCount != 2u || target.combinedModule
        || target.validatorOnly) {
        diagnostic = target.diagnostic ? target.diagnostic : "RHI-SYNC-GRAPHICS-CREATION-TARGET";
        return {};
    }
    const auto firstBytes = target.sources[0]->size(), secondBytes = target.sources[1]->size();
    if (firstBytes > budget.maxTargetBytes || secondBytes > budget.maxTargetBytes - firstBytes) {
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-TARGET-CAPACITY";
        return {};
    }
    for (std::size_t stage = 0u; stage < 2u; ++stage) {
        const auto& bytes = *target.sources[stage];
        shader::SpirvInstructionView module(bytes.data(), bytes.size());
        if ((diagnostic = module.diagnostic()))
            return {};
        std::uint32_t entries = 0u;
        for (auto offset = shader::SpirvInstructionView::headerWords;
            offset < module.sizeWords();) {
            shader::SpirvInstructionView::Instruction instruction;
            if (!module.read(offset, instruction)) {
                diagnostic = "RHI-SYNC-GRAPHICS-CREATION-SPIRV-INSTRUCTION";
                return {};
            }
            // OpCapability = 17. Unknown optional capabilities fail before
            // any native creation; driver acceptance is not a capability fact.
            if (instruction.opcode() == 17u) {
                if (instruction.operands() != 1u) {
                    diagnostic = "RHI-SYNC-GRAPHICS-CREATION-SPIRV-CAPABILITY";
                    return {};
                }
                diagnostic = rhi::semantics::PipelineCreationLowering::
                    synchronousVulkanGraphicsShaderCapability(
                        instruction.operand(0u), runtime->shaderDrawParametersEnabled());
                if (diagnostic)
                    return {};
            }
            // SPIR-V OpEntryPoint = 15, ExecutionModel Vertex = 0, Fragment = 4.
            if (instruction.opcode() == 15u) {
                ++entries;
                const auto& entry = stage ? authored.fragmentShader : authored.vertexShader;
                // Entry point interfaces are trailing operands, identified by
                // exact literal length rather than inferred from reflection.
                const auto stringWords =
                    (entry.entryPoint.size() + sizeof(std::uint32_t)) / sizeof(std::uint32_t);
                if (instruction.operands() < 2u + stringWords
                    || instruction.operand(0u) != (stage ? 4u : 0u)
                    || !instruction.stringEquals(
                        2u, entry.entryPoint, instruction.operands() - 2u - stringWords)) {
                    diagnostic = "RHI-SYNC-GRAPHICS-CREATION-SPIRV-ENTRY";
                    return {};
                }
            }
            offset = instruction.next();
        }
        if (entries != 1u) {
            diagnostic = "RHI-SYNC-GRAPHICS-CREATION-SPIRV-ENTRY";
            return {};
        }
    }
    try {
        core::CpuMemoryQuotaScope ownerScope(ownerQuota);
        auto receipt = core::makeUnique<VulkanOriginalArtifactGraphicsCreation>(
            ConstructionKey{}, input, layout, authored, budget, nativeQuota, runtime);
        {
            core::CpuMemoryQuotaScope nativeScope(nativeQuota);
            const auto report =
                validateVulkanOriginalGraphicsDefinition(input, receipt->definition_);
            if (!report.success) {
                diagnostic = "RHI-SYNC-GRAPHICS-CREATION-DEFINITION";
                return {};
            }
            auto token =
                core::makeShared<rhi::semantics::DirectObjectBudgetLease>(std::move(lease));
            std::array<core::pmr::Vector<std::uint32_t>, 2u> words{
                core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()},
                core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()}};
            for (std::size_t i = 0u; i < words.size(); ++i) {
                const auto& bytes = *target.sources[i];
                core::UnsignedElementSpan source(bytes.data(),
                    bytes.size(),
                    core::UnsignedElementSpan::Width::Uint32,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian);
                words[i].resize(source.size());
                for (std::size_t j = 0u; j < words[i].size(); ++j)
                    source.read(j, words[i][j]);
            }
            auto native = allocator.createRenderPipelineFromBytes(receipt->definition_,
                words[0].data(),
                words[0].size(),
                words[1].data(),
                words[1].size());
            if (!native.success || !native.payload) {
                diagnostic = "RHI-SYNC-GRAPHICS-CREATION-NATIVE-FAILED";
                return {};
            }
            native.payload->directObjectBudgetReleaseToken = std::move(token);
            receipt->payload_ = std::move(native.payload);
        }
        if ((diagnostic = receipt->validate()))
            return {};
        return shareOriginal(std::move(receipt));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-ALLOCATION";
        return {};
    }
}

const char* VulkanOriginalArtifactGraphicsCreation::validate() const noexcept
{
    if (const auto* id = Contract::validateBudget(budget_))
        return id;
    if (!originalMetadataQuota()
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuOwnerBytes || !nativeQuota_
        || nativeQuota_->limitBytes() != budget_.maxCpuNativeBytes)
        return "RHI-SYNC-GRAPHICS-CREATION-QUOTA-LIMIT";
    if (const auto* id = rhi::OriginalGraphicsPipelineInput::validateOriginal(input_))
        return id;
    if (!layout_ || !layout_->sharedOwnerMatches(layout_))
        return "RHI-SYNC-GRAPHICS-CREATION-LAYOUT-OWNER";
    if (const auto* id = layout_->originalResourceRegistration().diagnostic())
        return id;
    if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime() || !device_
        || runtime_->coreQueueCapabilityDiagnostic() || runtime_->logicalDevice() != device_
        || !payload_ || payload_->backendVulkanDeviceHandle() != device_
        || !payload_->backendVulkanRenderPipelineHandle()
        || !payload_->backendVulkanRenderPipelineLayoutHandle())
        return "RHI-SYNC-GRAPHICS-CREATION-NATIVE-IDENTITY";
    return definition_.layout == layout_.get() && definition_.shaderPackage == &input_->package()
        ? nullptr
        : "RHI-SYNC-GRAPHICS-CREATION-DEFINITION-IDENTITY";
}

bool VulkanOriginalArtifactGraphicsCreation::usesCpuQuotaDomain(
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    for (const auto& domain : std::array{
             originalMetadataQuota(), nativeQuota_, input_->quota(), input_->artifact()->quota()})
        if (core::SharedOwnerProjection::overlapsOwner(quota, domain))
            return true;
    return false;
}

} // namespace arp::rhi_vulkan
