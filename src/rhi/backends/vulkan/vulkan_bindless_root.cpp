#include "rhi/backends/vulkan/vulkan_bindless_root.h"

#include "core/algorithm/row_major_index_grid.h"
#include "core/ownership/shared_identity_projection.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/frontend/bindless_graphics_depth_content.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/completed_color_texture_view.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

namespace arp::rhi_vulkan {
namespace {
template <class T> std::uint64_t handleKey(T handle) noexcept
{
    if constexpr (std::is_pointer_v<T>)
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
    else
        return static_cast<std::uint64_t>(handle);
}
} // namespace

VulkanBindlessRootNativeClosure::Observation VulkanBindlessRootNativeClosure::Observation::capture(
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload)
{
    Observation o;
    o.payload = std::move(payload);
    if (o.payload) {
        o.device = o.payload->backendVulkanDeviceHandle();
        o.buffer = o.payload->backendVulkanBufferHandle();
        o.image = o.payload->backendVulkanImageHandle();
        o.sampler = o.payload->backendVulkanSamplerHandle();
        o.format = o.payload->backendVulkanImageFormat();
        o.extent = o.payload->backendVulkanImageExtent();
    }
    return o;
}

bool VulkanBindlessRootNativeClosure::Observation::matches(
    const std::shared_ptr<VulkanRhiNativeResourcePayload>& current) const noexcept
{
    if (!current || !core::SharedOwnerProjection::sameOwner(payload, current))
        return false;
    const auto e = current->backendVulkanImageExtent();
    return device == current->backendVulkanDeviceHandle()
        && buffer == current->backendVulkanBufferHandle()
        && image == current->backendVulkanImageHandle()
        && sampler == current->backendVulkanSamplerHandle()
        && format == current->backendVulkanImageFormat() && extent.width == e.width
        && extent.height == e.height && extent.depth == e.depth;
}

VulkanBindlessRootNativeClosure::VulkanBindlessRootNativeClosure(ConstructionKey,
    std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    std::shared_ptr<VulkanRhiRuntime> runtime,
    Budget budget,
    std::size_t count)
    : root_(std::move(root)), inventory_(std::move(inventory)), runtime_(std::move(runtime)),
      budget_(budget)
{
    entries_.reserve(count);
    ordinaryDescriptors_.reserve(root_->ordinaryDomains().size());
}

std::shared_ptr<VulkanRhiNativeResourcePayload> VulkanBindlessRootNativeClosure::pipelinePayload(
    const Root& root) noexcept
{
    return std::visit(
        [](const auto& binding) {
            return vulkanRhiNativePayload(*binding->owners()->desc().pipeline);
        },
        root.originalPrepared());
}

std::size_t VulkanBindlessRootNativeClosure::descriptorEntryCount() const noexcept
{
    auto count = root_->elements().size()
        + (root_->originalSnapshot()->originalBufferSource(
               rhi::BindlessSnapshot::OriginalBufferRole::FaultReadback)
                ? 1u
                : 0u);
    for (const auto& domain : root_->ordinaryDomains())
        count += domain.source->frozenElements().size();
    return count;
}

const char* VulkanBindlessRootNativeClosure::resolveDescriptorEntry(
    std::size_t index, DescriptorEntry& entry) const noexcept
{
    using namespace rhi;
    entry = {};
    if (index < root_->elements().size()) {
        entry.declaration = &root_->elements()[index].declaration;
        entry.owner = &root_->elements()[index].owner;
        entry.layout = &root_->originalObject()->layout();
        entry.descriptors = descriptors_;
        return nullptr;
    }
    const bool readback = bool(root_->originalSnapshot()->originalBufferSource(
        BindlessSnapshot::OriginalBufferRole::FaultReadback));
    if (readback && index == root_->elements().size())
        return "RHI-VULKAN-BINDLESS-ROOT-ELEMENT-RANGE";
    auto ordinal = index - root_->elements().size() - (readback ? 1u : 0u);
    for (std::size_t i = 0u; i < root_->ordinaryDomains().size(); ++i) {
        const auto& domain = root_->ordinaryDomains()[i];
        const auto& frozen = domain.source->frozenElements();
        if (ordinal >= frozen.size()) {
            ordinal -= frozen.size();
            continue;
        }
        if (i >= ordinaryDescriptors_.size())
            return "RHI-VULKAN-BINDLESS-ROOT-ORDINARY-COVERAGE";
        entry.declaration = &frozen[ordinal].declaration;
        entry.owner = &frozen[ordinal].resourceOwner;
        entry.layout = &domain.source->originalObject()->layout();
        entry.descriptors = ordinaryDescriptors_[i].payload;
        entry.set = domain.set;
        entry.ordinary = true;
        return nullptr;
    }
    return "RHI-VULKAN-BINDLESS-ROOT-ELEMENT-RANGE";
}

const char* VulkanBindlessRootNativeClosure::captureEntry(
    std::size_t index, Entry& entry) const noexcept
{
    using namespace rhi;
    const auto& root = *root_;
    const auto device = runtime_->logicalDevice();
    const auto inspect = [&](const auto& lease) -> const char* {
        entry.resource = lease->originalObject()->id();
        entry.registrationGeneration =
            lease->originalObject()->originalResourceRegistration().generation();
        entry.resourcePayload =
            Observation::capture(vulkanRhiNativePayload(*lease->originalObject()));
        const auto& p = entry.resourcePayload;
        if (!p.payload || p.device != device)
            return "RHI-VULKAN-BINDLESS-ROOT-RESOURCE-DEVICE";
        using T = std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
        if constexpr (std::is_same_v<T, OriginalCommandTextureViewSource>) {
            entry.parentPayload = Observation::capture(
                vulkanRhiNativePayload(*lease->originalParentLease()->originalObject()));
            const auto* view = p.payload->backendVulkanOriginalTextureViewDesc();
            if (!p.image || p.format == VK_FORMAT_UNDEFINED
                || !p.payload->backendVulkanTextureViewDescAvailable() || !view
                || !lease->originalViewLease()->layout().matches(*view)
                // Existing descriptor storage creates its VkImageView with
                // the parent VkFormat; reinterpretation has no proven lowering.
                || lease->originalViewLease()->layout().format
                    != lease->originalParentLease()->layout().format
                || !entry.parentPayload.matches(p.payload->backendVulkanTextureViewParentPayload())
                || entry.parentPayload.device != device || entry.parentPayload.image != p.image)
                return "RHI-VULKAN-BINDLESS-ROOT-VIEW-PARENT";
        } else if constexpr (std::is_same_v<T, OriginalCommandBufferLease>) {
            if (!p.buffer)
                return "RHI-VULKAN-BINDLESS-ROOT-BUFFER-PAYLOAD";
        } else {
            if (!p.sampler)
                return "RHI-VULKAN-BINDLESS-ROOT-SAMPLER-PAYLOAD";
        }
        return nullptr;
    };
    const auto& readback = root.originalSnapshot()->originalBufferSource(
        BindlessSnapshot::OriginalBufferRole::FaultReadback);
    if (readback && index == root.elements().size()) {
        return inspect(readback);
    }
    DescriptorEntry resolved;
    if (const auto* id = resolveDescriptorEntry(index, resolved))
        return id;
    const auto* declaration = resolved.declaration;
    const auto* owner = resolved.owner;
    const auto* layout = resolved.layout;
    const auto& descriptors = resolved.descriptors;
    const auto* shape = findDescriptorSetBinding(*layout, declaration->write.binding);
    if (!shape)
        return "RHI-VULKAN-BINDLESS-ORDINARY-RESOURCE-UNSUPPORTED";
    const bool ordinaryDepth = resolved.ordinary
        && budget_.contractVersion
            == OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthVersion
        && (shape->depthTexture || shape->comparisonSampler);
    if (resolved.ordinary) {
        if (ordinaryDepth) {
            if (!semantics::OriginalBindlessOrdinaryDepthDomain::acceptsBinding(*shape))
                return "RHI-VULKAN-BINDLESS-ORDINARY-DEPTH-DECLARATION";
            const auto& graphics = std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                root.originalPrepared());
            const auto* selected = graphics->owners()->desc().pipeline->bindlessShaderModule(
                ShaderStage::Fragment, 1u);
            if (!selected)
                return "RHI-VULKAN-BINDLESS-ORDINARY-DEPTH-SELECTED-ENTRY";
            BindlessOrdinaryOpaqueBinding binding;
            if (const auto* id = Root::originalOrdinaryOpaqueBinding(
                    root_, selected->entry, resolved.set, shape->binding, binding))
                return id;
            if (binding.selectedStage != ShaderStage::Fragment
                || binding.sourceIndex != declaration->sourceIndex
                || binding.resource != declaration->write.resource)
                return "RHI-VULKAN-BINDLESS-ORDINARY-DEPTH-SELECTED-ENTRY";
        } else if (const auto* id = inspectVulkanBindlessOrdinaryBinding(*shape))
            return id;
        const auto& limits = runtime_->descriptorIndexing().limits;
        if (declaration->bufferAccessBytes > (shape->type == DescriptorType::UniformBuffer
                    ? limits.maxUniformBufferRange
                    : limits.maxStorageBufferRange))
            return "RHI-VULKAN-BINDLESS-BUFFER-RANGE-LIMIT";
    }
    entry.sourceIndex = declaration->sourceIndex;
    if (const auto* id = std::visit(inspect, *owner))
        return id;
    if (resolved.ordinary) {
        if (const auto* view =
                std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(owner)) {
            const auto* id = ordinaryDepth
                ? OriginalBindlessGraphicsDepthContentPlanContract::inspectDepthRead(
                      (*view)->originalParentLease()->originalObject()->desc(),
                      (*view)->originalObject()->desc(),
                      declaration->write.resourceState,
                      declaration->stages,
                      declaration->access)
                : OriginalCompletedTextureReadPlanContract::inspectViewBinding(
                      (*view)->originalParentLease()->originalObject()->desc(),
                      (*view)->originalObject()->desc(),
                      declaration->write.resourceState,
                      declaration->stages,
                      declaration->access,
                      (*view)->originalObject()->desc().format == TextureFormat::Rgba16Float ? 17u
                                                                                             : 8u);
            if (id)
                return id;
        }
    }
    const auto physical = descriptorElementIndex(
        *layout, declaration->write.binding, declaration->write.arrayElement);
    if (physical == UINT32_MAX
        || !core::SharedOwnerProjection::sameOwner(entry.resourcePayload.payload,
            descriptors->backendVulkanDescriptorElementResource(physical)))
        return "RHI-VULKAN-BINDLESS-ROOT-PHYSICAL-RESOURCE";
    entry.descriptorImageView = descriptors->backendVulkanDescriptorElementImageView(physical);
    entry.completedDepth = index < root.elements().size() || ordinaryDepth
        ? root.originalCompletedDepthAt(index)
        : nullptr;
    const auto publishedDepth = descriptors->backendVulkanDescriptorElementDepthContent(physical);
    if (entry.completedDepth.get() != publishedDepth.get()
        || (entry.completedDepth
            && !core::SharedOwnerProjection::sameOwner(entry.completedDepth, publishedDepth)))
        return "RHI-VULKAN-BINDLESS-ROOT-DEPTH-PUBLICATION";
    if (entry.resourcePayload.image && !entry.descriptorImageView)
        return "RHI-VULKAN-BINDLESS-ROOT-PHYSICAL-VIEW";
    if (!entry.resourcePayload.image && entry.descriptorImageView)
        return "RHI-VULKAN-BINDLESS-ROOT-PHYSICAL-KIND";
    return nullptr;
}

const char* VulkanBindlessRootNativeClosure::inspectCompletedDepth(
    const Entry& entry, std::size_t index) const noexcept
{
    using namespace rhi;
    if (index >= descriptorEntryCount())
        return entry.completedDepth ? "RHI-VULKAN-BINDLESS-ROOT-DEPTH-SCHEMA" : nullptr;
    if (index == root_->elements().size()
        && root_->originalSnapshot()->originalBufferSource(
            BindlessSnapshot::OriginalBufferRole::FaultReadback))
        return entry.completedDepth ? "RHI-VULKAN-BINDLESS-ROOT-DEPTH-SCHEMA" : nullptr;
    DescriptorEntry resolved;
    if (const auto* id = resolveDescriptorEntry(index, resolved))
        return id;
    const auto* shape =
        findDescriptorSetBinding(*resolved.layout, resolved.declaration->write.binding);
    const bool depth =
        shape && shape->type == DescriptorType::SampledTexture && shape->depthTexture;
    if (!depth)
        return entry.completedDepth ? "RHI-VULKAN-BINDLESS-ROOT-DEPTH-SCHEMA" : nullptr;
    if (budget_.contractVersion != 9u && budget_.contractVersion != 13u
        && budget_.contractVersion != 14u
        && budget_.contractVersion
            != OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthVersion)
        return "RHI-VULKAN-BINDLESS-ROOT-DEPTH-PROFILE";
    if ((budget_.contractVersion
            == OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthVersion)
        != resolved.ordinary)
        return "RHI-VULKAN-BINDLESS-ROOT-DEPTH-PROFILE";
    const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
        &inventory_->sources()[entry.sourceIndex]);
    if (!view)
        return "RHI-VULKAN-BINDLESS-ROOT-DEPTH-VIEW";
    const auto& e = *resolved.declaration;
    return OriginalCompletedDepthReadInputContract::inspect(
        {entry.completedDepth, e.write.resourceState, e.stages, e.access},
        *view,
        budget_.completedDepthInputs)
        .diagnostic;
}

bool VulkanBindlessRootNativeClosure::isExtraGeometrySource(
    const Root& root, const Sources& inventory, std::size_t index) noexcept
{
    if (index >= inventory.sources().size())
        return false;
    const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
        &inventory.sources()[index]);
    if (!lease || !*lease)
        return false;
    const auto usage = (*lease)->layout().usage;
    if ((usage & (rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::IndexBuffer))
        == rhi::ResourceUsage::None)
        return false;
    for (const auto& element : root.elements())
        if (element.declaration.sourceIndex == index)
            return false;
    const auto& readback = root.originalSnapshot()->originalBufferSource(
        rhi::BindlessSnapshot::OriginalBufferRole::FaultReadback);
    return !readback || !core::SharedOwnerProjection::sameOwner(readback, *lease);
}

const char* VulkanBindlessRootNativeClosure::captureGeometryEntry(
    const Sources& inventory, std::size_t index, VkDevice device, Entry& entry) noexcept
{
    using namespace rhi;
    if (index >= inventory.sources().size())
        return "RHI-VULKAN-BINDLESS-ROOT-GEOMETRY-SOURCE";
    const auto* lease =
        std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&inventory.sources()[index]);
    if (!lease)
        return "RHI-VULKAN-BINDLESS-ROOT-GEOMETRY-SOURCE";
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(*lease))
        return id;
    const auto registration = (*lease)->originalObject()->originalResourceRegistration();
    if (const auto* id = registration.diagnostic())
        return id;
    if (!registration.matchesIdentity((*lease)->originalObject(),
            (*lease)->resource().value,
            OriginalRegisteredResourceKind::Buffer))
        return "RHI-VULKAN-BINDLESS-ROOT-GEOMETRY-REGISTRATION";
    entry.sourceIndex = index;
    entry.resource = (*lease)->resource();
    entry.registrationGeneration = registration.generation();
    entry.resourcePayload =
        Observation::capture(vulkanRhiNativePayload(*(*lease)->originalObject()));
    if (!entry.resourcePayload.payload || !entry.resourcePayload.buffer
        || entry.resourcePayload.device != device)
        return "RHI-VULKAN-BINDLESS-ROOT-GEOMETRY-PAYLOAD";
    return nullptr;
}

const char* VulkanBindlessRootNativeClosure::inspectOwners(
    std::uint32_t& native, std::uint32_t& retained) const noexcept
{
    const core::RowMajorIndexGrid grid(
        entries_.size(), 2u, SIZE_MAX - 2u - ordinaryDescriptors_.size());
    if (!grid.valid())
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY";
    const auto count = grid.size() + 2u + ordinaryDescriptors_.size();
    const auto projection = core::SharedIdentityProjection::inspect(count,
        count,
        budget_.maxNativeOwners,
        [&](std::size_t i) -> core::SharedIdentityProjection::Record {
            if (i == grid.size())
                return {handleKey(pipelineHandle_), 1u, pipeline_, pipeline_.get(), 4u};
            if (i == grid.size() + 1u)
                return {handleKey(descriptorSet_), 1u, descriptors_, descriptors_.get(), 5u};
            if (i >= grid.size() + 2u) {
                const auto& domain = ordinaryDescriptors_[i - grid.size() - 2u];
                return {handleKey(domain.handle), 1u, domain.payload, domain.payload.get(), 5u};
            }
            std::uint64_t row = 0u, slot = 0u;
            grid.coordinates(i, row, slot);
            const auto& e = entries_[row];
            const auto& o = slot ? e.parentPayload : e.resourcePayload;
            if (!o.payload)
                return {};
            // View wrapper and parent allocation are distinct owners of one
            // image. Freeze both; only the parent is keyed by VkImage identity.
            if (!slot && e.parentPayload.payload)
                return {handleKey(o.payload.get()), 1u, o.payload, o.payload.get(), 6u};
            const auto key = o.buffer ? handleKey(o.buffer)
                : o.image             ? handleKey(o.image)
                                      : handleKey(o.sampler);
            return {key, 1u, o.payload, o.payload.get(), o.buffer ? 1u : o.image ? 2u : 3u};
        });
    if (projection.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY";
    native = static_cast<std::uint32_t>(projection.identities);
    if (native > budget_.maxRetainedOwners)
        return "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY";
    // Actor objects and their CPU quota domains are deduplicated. Their exact
    // leases, native payloads and runtime are already counted by this closure.
    const core::RowMajorIndexGrid actors(
        entries_.size(), 2u + GeometryContent::exclusiveCpuQuotaSlots, SIZE_MAX);
    if (!actors.valid())
        return "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY";
    const auto actorOwners = core::SharedIdentityProjection::inspect(actors.size(),
        actors.size(),
        budget_.maxRetainedOwners - native,
        [&](std::size_t i) -> core::SharedIdentityProjection::Record {
            std::uint64_t row = 0u, slot = 0u;
            actors.coordinates(i, row, slot);
            if (slot == 1u + GeometryContent::exclusiveCpuQuotaSlots) {
                const auto& proof = entries_[row].completedDepth;
                return proof ? core::SharedIdentityProjection::Record{handleKey(proof.get()),
                                   1u,
                                   proof,
                                   proof.get(),
                                   8u}
                             : core::SharedIdentityProjection::Record{};
            }
            const auto& actor = entries_[row].initializedActor();
            if (!actor)
                return {};
            std::shared_ptr<const void> owner =
                slot ? std::shared_ptr<const void>(actor->exclusiveCpuQuota(slot - 1u)) : actor;
            return {handleKey(owner.get()), 1u, owner, owner.get(), 7u};
        });
    if (actorOwners.status != core::SharedIdentityProjection::Status::Complete
        || actorOwners.identities > UINT32_MAX - 4u)
        return "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY";
    // Inventory, this object, runtime and this independently charged quota.
    if (inventory_->cpuOwnerCount(4u + static_cast<std::uint32_t>(actorOwners.identities),
            budget_.maxRetainedOwners - native,
            retained))
        return "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY";
    retained += native;
    return nullptr;
}

std::shared_ptr<const VulkanBindlessRootNativeClosure>
VulkanBindlessRootNativeClosure::prepareOriginal(std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    rhi::GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    return prepareOriginal(
        std::move(root), std::move(inventory), device, budget, std::move(quota), diagnostic, {});
}

std::shared_ptr<const VulkanBindlessRootNativeClosure>
VulkanBindlessRootNativeClosure::prepareOriginal(std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    rhi::GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    GeometryContents contents)
{
    using namespace rhi;
    diagnostic = OriginalBindlessRootNativeClosureContract::validateBudget(budget);
    if (diagnostic)
        return {};
    if ((diagnostic = OriginalBindlessRootNativeClosureContract::validateGeometryBackend(
             device.backend(), budget)))
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-QUOTA";
        return {};
    }
    if ((diagnostic = Root::validateOriginal(root, device))
        || (diagnostic = Sources::validateRegisteredLifetime(inventory, device)))
        return {};
    if (std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            root->originalPrepared())
        && (diagnostic = Root::validateOriginalGraphicsBufferRanges(root)))
        return {};
    if ((diagnostic = semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(
             device.backend(), root->sourceContractVersion())))
        return {};
    // Closure3 retains initialized shader/geometry actors for the public
    // InitializedGraphics/2 profile, including its authenticated ordinary domains.
    if (root->sourceContractVersion() == 2u && budget.contractVersion != 1u
        && budget.contractVersion != 3u
        && budget.contractVersion
            != OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthVersion) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-ORDINARY-PROFILE-UNAVAILABLE";
        return {};
    }
    if (core::SharedOwnerProjection::sameControlBlock(quota, inventory->originalMetadataQuota())) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-QUOTA-DOMAIN";
        return {};
    }
    if ((diagnostic = rhi::OriginalBindlessInitializedShaderContract::validateInputs(*root,
             *inventory,
             device.backend(),
             budget.initializedShader,
             contents.shaderData,
             contents.shaderCount)))
        return {};
    if (OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget)) {
        if (!contents.data || !contents.count
            || contents.count > budget.maxInitializedGeometryActors) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-CAPACITY";
            return {};
        }
        std::uint64_t bytes = 0u;
        for (std::size_t i = 0u; i < contents.count; ++i) {
            const auto& actor = contents.data[i];
            if ((diagnostic = GeometryContent::validateOriginal(actor)))
                return {};
            const auto index = inventory->originalLeaseIndex(actor->originalLease());
            if (index == SIZE_MAX || !isExtraGeometrySource(*root, *inventory, index)) {
                diagnostic = "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-INVENTORY";
                return {};
            }
            for (std::size_t j = 0u; j < i; ++j)
                if (core::SharedOwnerProjection::overlapsOwner(actor, contents.data[j])
                    || core::SharedOwnerProjection::sameOwner(
                        actor->originalLease(), contents.data[j]->originalLease())) {
                    diagnostic = "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-DUPLICATE";
                    return {};
                }
            for (std::size_t slot = 0u; slot < GeometryContent::exclusiveCpuQuotaSlots; ++slot)
                if (core::SharedOwnerProjection::overlapsOwner(
                        quota, actor->exclusiveCpuQuota(slot))
                    || core::SharedOwnerProjection::overlapsOwner(
                        inventory->originalMetadataQuota(), actor->exclusiveCpuQuota(slot))) {
                    diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-QUOTA-DOMAIN";
                    return {};
                }
            if (actor->initializedBytes() > budget.maxInitializedGeometryBytes - bytes) {
                diagnostic = "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-BYTE-CAPACITY";
                return {};
            }
            bytes += actor->initializedBytes();
        }
    } else if (contents.data || contents.count) {
        diagnostic = "RHI-BINDLESS-ROOT-INITIALIZED-GEOMETRY-SCHEMA";
        return {};
    }
    if (!root->matchesCanonicalSources(*inventory)
        || inventory->originalLeaseIndex(root) == SIZE_MAX) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-INVENTORY";
        return {};
    }
    if ((diagnostic = OriginalBindlessRootNativeClosureContract::producedRootDiagnostic(
             *root, budget.contractVersion)))
        return {};
    if ((diagnostic = OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
             root->originalSnapshot()->plan(), budget.contractVersion)))
        return {};
    if ((diagnostic = OriginalBindlessRootNativeClosureContract::mixedRootDiagnostic(
             *root, budget.contractVersion)))
        return {};
    if ((diagnostic =
                OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthRootDiagnostic(
                    *root, budget.contractVersion)))
        return {};
    if (budget.contractVersion == 9u
        && ((!semantics::OriginalBindlessPreparationLowering::hasPlan(
                 root->originalSnapshot()->plan(), 6u)
                && !OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                    root->originalSnapshot()->plan(), 6u))
            || !std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                root->originalPrepared()))) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-DEPTH-PREPARED-PROFILE";
        return {};
    }
    const auto decision = semantics::OriginalBindlessRootNativeClosureLowering::lowering(
        device.backend(), budget.contractVersion);
    if (!decision.nativeCorrespondenceAvailable || decision.diagnostic) {
        diagnostic = decision.diagnostic;
        return {};
    }
    const auto* extension = dynamic_cast<const VulkanBindlessCrossExtension*>(
        static_cast<const BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross)));
    if (!extension || !extension->runtime_ || !extension->nativeResources_
        || !extension->runtime_->valid() || extension->runtime_->syntheticRuntime()) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-RUNTIME";
        return {};
    }
    auto runtime = extension->nativeResources_->runtime();
    if (!runtime || runtime.get() != extension->runtime_) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-RUNTIME";
        return {};
    }
    const auto extra = root->originalSnapshot()->originalBufferSource(
                           BindlessSnapshot::OriginalBufferRole::FaultReadback)
        ? 1u
        : 0u;
    if (root->elements().size() > budget.maxResourceEntries
        || extra > budget.maxResourceEntries - root->elements().size()) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
        return {};
    }
    auto count = root->elements().size() + extra;
    for (const auto& domain : root->ordinaryDomains()) {
        if (domain.source->frozenElements().size() > budget.maxResourceEntries - count) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
            return {};
        }
        count += domain.source->frozenElements().size();
    }
    if (budget.contractVersion == 2u
        || OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget)) {
        if (!std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                root->originalPrepared())) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-GEOMETRY-PROFILE";
            return {};
        }
        const auto descriptors = count;
        for (std::size_t i = 0u; i < inventory->sources().size(); ++i) {
            if (!isExtraGeometrySource(*root, *inventory, i))
                continue;
            if (count == budget.maxResourceEntries) {
                diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
                return {};
            }
            ++count;
        }
        if (count == descriptors) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-GEOMETRY-EMPTY";
            return {};
        }
        if (OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget)
            && contents.count != count - descriptors) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-COVERAGE";
            return {};
        }
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<VulkanBindlessRootNativeClosure>(ConstructionKey{},
            std::move(root),
            std::move(inventory),
            std::move(runtime),
            budget,
            count);
        result->pipeline_ = pipelinePayload(*result->root_);
        result->descriptors_ =
            (budget.contractVersion == 10u || budget.contractVersion == 11u
                || budget.contractVersion == 13u || budget.contractVersion == 14u
                || semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                    result->root_->originalSnapshot()->plan()))
            ? prepareVulkanOriginalImageCellDescriptors(*result->root_->originalSnapshot())
            : vulkanRhiNativePayload(*result->root_->originalObject());
        if (!result->pipeline_ || !result->descriptors_
            || result->pipeline_->backendVulkanDeviceHandle() != result->runtime_->logicalDevice()
            || result->descriptors_->backendVulkanDeviceHandle()
                != result->runtime_->logicalDevice()) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-PAYLOAD";
            return {};
        }
        const bool compute =
            std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                result->root_->originalPrepared());
        result->pipelineHandle_ = compute ? result->pipeline_->backendVulkanComputePipelineHandle()
                                          : result->pipeline_->backendVulkanRenderPipelineHandle();
        result->pipelineLayout_ = compute
            ? result->pipeline_->backendVulkanComputePipelineLayoutHandle()
            : result->pipeline_->backendVulkanRenderPipelineLayoutHandle();
        result->descriptorSet_ = result->descriptors_->backendVulkanDescriptorSetHandle();
        result->descriptorLayout_ = result->descriptors_->backendVulkanDescriptorSetLayoutHandle();
        if (!result->pipelineHandle_ || !result->pipelineLayout_ || !result->descriptorSet_
            || !result->descriptorLayout_) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-HANDLE";
            return {};
        }
        for (const auto& domain : result->root_->ordinaryDomains()) {
            auto payload = vulkanRhiNativePayload(*domain.source->originalObject());
            if (!payload
                || payload->backendVulkanDeviceHandle() != result->runtime_->logicalDevice()
                || !payload->backendVulkanDescriptorSetHandle()
                || !payload->backendVulkanDescriptorSetLayoutHandle()) {
                diagnostic = "RHI-VULKAN-BINDLESS-ROOT-ORDINARY-PAYLOAD";
                return {};
            }
            result->ordinaryDescriptors_.push_back({domain.set,
                payload,
                payload->backendVulkanDescriptorSetHandle(),
                payload->backendVulkanDescriptorSetLayoutHandle()});
        }
        std::uint64_t shaderVisits = 0u;
        for (std::size_t i = 0u; i < result->descriptorEntryCount(); ++i) {
            Entry entry;
            if ((diagnostic = result->captureEntry(i, entry)))
                return {};
            if (extra && i == result->root_->elements().size()) {
                entry.sourceIndex = result->inventory_->originalLeaseIndex(
                    result->root_->originalSnapshot()->originalBufferSource(
                        BindlessSnapshot::OriginalBufferRole::FaultReadback));
                if (entry.sourceIndex == SIZE_MAX) {
                    diagnostic = "RHI-VULKAN-BINDLESS-ROOT-READBACK-INVENTORY";
                    return {};
                }
            }
            if ((diagnostic = result->inspectCompletedDepth(entry, i)))
                return {};
            if ((diagnostic =
                        rhi::OriginalBindlessInitializedShaderContract::capture(*result->inventory_,
                            budget.initializedShader,
                            contents.shaderData,
                            contents.shaderCount,
                            entry,
                            shaderVisits)))
                return {};
            result->entries_.push_back(std::move(entry));
        }
        if ((budget.contractVersion == 9u || budget.contractVersion == 13u
                || budget.contractVersion == 14u
                || budget.contractVersion
                    == OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthVersion)
            && std::none_of(
                result->entries_.begin(), result->entries_.end(), [](const auto& entry) {
                    return bool(entry.completedDepth);
                })) {
            diagnostic = "RHI-VULKAN-BINDLESS-ROOT-DEPTH-COVERAGE";
            return {};
        }
        if (budget.contractVersion == 2u
            || OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget))
            for (std::size_t i = 0u; i < result->inventory_->sources().size(); ++i) {
                if (!isExtraGeometrySource(*result->root_, *result->inventory_, i))
                    continue;
                Entry entry;
                if ((diagnostic = captureGeometryEntry(
                         *result->inventory_, i, result->runtime_->logicalDevice(), entry)))
                    return {};
                if (OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget)) {
                    const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                        result->inventory_->sources()[i]);
                    for (std::size_t j = 0u; j < contents.count; ++j)
                        if (core::SharedOwnerProjection::sameOwner(
                                lease, contents.data[j]->originalLease()))
                            entry.initializedGeometry = contents.data[j];
                    if (!entry.initializedGeometry) {
                        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-COVERAGE";
                        return {};
                    }
                    const auto state = lease->originalObject()->observeOriginalCommittedState();
                    if (!state) {
                        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-STATE";
                        return {};
                    }
                    entry.initializedGeometryState.emplace(*state);
                }
                result->entries_.push_back(std::move(entry));
            }
        if ((diagnostic = result->inspectInitializedGeometry(
                 result->initializedGeometryCount_, result->initializedGeometryBytes_)))
            return {};
        if ((diagnostic = result->inspectOwners(result->nativeOwners_, result->retainedOwners_)))
            return {};
        auto shared = shareOriginal(std::move(result));
        if ((diagnostic = shared->validate()))
            return {};
        return shared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ALLOCATION";
    }
    return {};
}

const char* VulkanBindlessRootNativeClosure::validateOriginal(
    const std::shared_ptr<const VulkanBindlessRootNativeClosure>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER"
                                                          : self->validate();
}

const char* VulkanBindlessRootNativeClosure::validate() const noexcept
{
    return validatePhase({});
}

const char* VulkanBindlessRootNativeClosure::validateOriginalPending(
    const std::shared_ptr<const VulkanBindlessRootNativeClosure>& self,
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self) || !seal)
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER";
    return self->validatePhase(seal);
}

const char* VulkanBindlessRootNativeClosure::validatePhase(
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) const noexcept
{
    if (const auto* id = Root::validateOriginal(root_))
        return id;
    if (std::holds_alternative<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
            root_->originalPrepared()))
        if (const auto* id = Root::validateOriginalGraphicsBufferRanges(root_))
            return id;
    if (const auto* id = rhi::OriginalBindlessRootNativeClosureContract::producedRootDiagnostic(
            *root_, budget_.contractVersion))
        return id;
    if (const auto* id = rhi::OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
            root_->originalSnapshot()->plan(), budget_.contractVersion))
        return id;
    if (const auto* id = rhi::OriginalBindlessRootNativeClosureContract::mixedRootDiagnostic(
            *root_, budget_.contractVersion))
        return id;
    if (const auto* id =
            rhi::OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthRootDiagnostic(
                *root_, budget_.contractVersion))
        return id;
    if (const auto* id =
            rhi::semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(
                rhi::Backend::Vulkan, root_->sourceContractVersion()))
        return id;
    if (const auto* id = Sources::validateOriginal(inventory_))
        return id;
    if (!root_->matchesCanonicalSources(*inventory_)
        || inventory_->originalLeaseIndex(root_) == SIZE_MAX)
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-INVENTORY";
    if (!runtime_->valid() || runtime_->syntheticRuntime())
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-RUNTIME";
    if (!core::SharedOwnerProjection::sameOwner(pipeline_, pipelinePayload(*root_))
        || !core::SharedOwnerProjection::sameOwner(descriptors_,
            (budget_.contractVersion == 10u || budget_.contractVersion == 11u
                || budget_.contractVersion == 13u || budget_.contractVersion == 14u
                || rhi::semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                    root_->originalSnapshot()->plan()))
                ? prepareVulkanOriginalImageCellDescriptors(*root_->originalSnapshot())
                : vulkanRhiNativePayload(*root_->originalObject()))
        || pipeline_->backendVulkanDeviceHandle() != runtime_->logicalDevice()
        || descriptors_->backendVulkanDeviceHandle() != runtime_->logicalDevice())
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-PAYLOAD";
    const bool compute =
        std::holds_alternative<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>>(
            root_->originalPrepared());
    if (pipelineHandle_
            != (compute ? pipeline_->backendVulkanComputePipelineHandle()
                        : pipeline_->backendVulkanRenderPipelineHandle())
        || pipelineLayout_
            != (compute ? pipeline_->backendVulkanComputePipelineLayoutHandle()
                        : pipeline_->backendVulkanRenderPipelineLayoutHandle())
        || descriptorSet_ != descriptors_->backendVulkanDescriptorSetHandle()
        || descriptorLayout_ != descriptors_->backendVulkanDescriptorSetLayoutHandle())
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-HANDLE";
    if (ordinaryDescriptors_.size() != root_->ordinaryDomains().size())
        return "RHI-VULKAN-BINDLESS-ROOT-ORDINARY-COVERAGE";
    for (std::size_t i = 0u; i < ordinaryDescriptors_.size(); ++i) {
        const auto& frozen = ordinaryDescriptors_[i];
        const auto& domain = root_->ordinaryDomains()[i];
        const auto current = vulkanRhiNativePayload(*domain.source->originalObject());
        if (!core::SharedOwnerProjection::sameOwner(frozen.payload, current) || !current
            || current->backendVulkanDeviceHandle() != runtime_->logicalDevice()
            || frozen.set != domain.set
            || frozen.handle != current->backendVulkanDescriptorSetHandle()
            || frozen.layout != current->backendVulkanDescriptorSetLayoutHandle())
            return "RHI-VULKAN-BINDLESS-ROOT-ORDINARY-CORRESPONDENCE";
    }
    const auto descriptorCount = descriptorEntryCount();
    auto count = descriptorCount;
    if (budget_.contractVersion == 2u
        || rhi::OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget_))
        for (std::size_t i = 0u; i < inventory_->sources().size(); ++i)
            if (isExtraGeometrySource(*root_, *inventory_, i))
                ++count;
    if (entries_.size() != count)
        return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
    std::size_t geometryIndex = 0u;
    for (std::size_t i = 0u; i < entries_.size(); ++i) {
        Entry current;
        if (i < descriptorCount) {
            if (const auto* id = captureEntry(i, current))
                return id;
        } else {
            while (geometryIndex < inventory_->sources().size()
                && !isExtraGeometrySource(*root_, *inventory_, geometryIndex))
                ++geometryIndex;
            if (const auto* id = captureGeometryEntry(
                    *inventory_, geometryIndex++, runtime_->logicalDevice(), current))
                return id;
        }
        const auto& frozen = entries_[i];
        if (const auto* id = inspectCompletedDepth(current, i))
            return id;
        if (frozen.completedDepth.get() != current.completedDepth.get()
            || (frozen.completedDepth
                && !core::SharedOwnerProjection::sameOwner(
                    frozen.completedDepth, current.completedDepth)))
            return "RHI-VULKAN-BINDLESS-ROOT-DEPTH-PUBLICATION";
        if (i == root_->elements().size()) {
            const auto& readback = root_->originalSnapshot()->originalBufferSource(
                rhi::BindlessSnapshot::OriginalBufferRole::FaultReadback);
            if (readback)
                current.sourceIndex = inventory_->originalLeaseIndex(readback);
        }
        if (frozen.sourceIndex != current.sourceIndex || frozen.resource != current.resource
            || frozen.registrationGeneration != current.registrationGeneration
            || frozen.descriptorImageView != current.descriptorImageView
            || !frozen.resourcePayload.matches(current.resourcePayload.payload)
            || (frozen.parentPayload.payload
                && !frozen.parentPayload.matches(current.parentPayload.payload)))
            return "RHI-VULKAN-BINDLESS-ROOT-NATIVE-CORRESPONDENCE";
    }
    std::uint32_t native = 0u, retained = 0u;
    std::uint32_t initialized = 0u;
    std::uint64_t bytes = 0u;
    if (const auto* id =
            rhi::OriginalBindlessInitializedShaderContract::validateRetained<GeometryContent>(
                *inventory_, budget_.initializedShader, entries_, seal))
        return id;
    if (const auto* id = inspectInitializedGeometry(initialized, bytes, seal))
        return id;
    if (initialized != initializedGeometryCount_ || bytes != initializedGeometryBytes_)
        return "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-COVERAGE";
    if (const auto* id = inspectOwners(native, retained))
        return id;
    return native == nativeOwners_ && retained == retainedOwners_
        ? nullptr
        : "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY";
}

const std::shared_ptr<const VulkanBindlessRootNativeClosure::GeometryContent>&
VulkanBindlessRootNativeClosure::initializedGeometry(std::size_t sourceIndex) const noexcept
{
    for (const auto& entry : entries_)
        if (entry.sourceIndex == sourceIndex && entry.initializedGeometry)
            return entry.initializedGeometry;
    static const std::shared_ptr<const GeometryContent> empty;
    return empty;
}

const char* VulkanBindlessRootNativeClosure::inspectInitializedGeometry(std::uint32_t& count,
    std::uint64_t& bytes,
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) const noexcept
{
    count = 0u;
    bytes = 0u;
    for (const auto& entry : entries_) {
        const bool geometry = isExtraGeometrySource(*root_, *inventory_, entry.sourceIndex);
        if (!rhi::OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget_)
            || !geometry) {
            if (entry.initializedGeometry || entry.initializedGeometryState)
                return "RHI-BINDLESS-ROOT-INITIALIZED-GEOMETRY-SCHEMA";
            continue;
        }
        if (!entry.initializedGeometryState)
            return "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-STATE";
        if (const auto* id = seal
                ? GeometryContent::validateOriginalPending(
                      entry.initializedGeometry, seal, *entry.initializedGeometryState)
                : GeometryContent::validateOriginal(entry.initializedGeometry))
            return id;
        const auto& lease = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
            inventory_->sources()[entry.sourceIndex]);
        if (!core::SharedOwnerProjection::sameOwner(
                lease, entry.initializedGeometry->originalLease()))
            return "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-INVENTORY";
        if (!seal)
            if (const auto* id = lease->originalObject()->validateOriginalCommittedState(
                    *entry.initializedGeometryState))
                return id;
        if (count == budget_.maxInitializedGeometryActors)
            return "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-CAPACITY";
        if (entry.initializedGeometry->initializedBytes()
            > budget_.maxInitializedGeometryBytes - bytes)
            return "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-BYTE-CAPACITY";
        ++count;
        bytes += entry.initializedGeometry->initializedBytes();
    }
    return rhi::OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget_) && !count
        ? "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-COVERAGE"
        : nullptr;
}

} // namespace arp::rhi_vulkan
