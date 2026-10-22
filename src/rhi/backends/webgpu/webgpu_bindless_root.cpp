#include "rhi/backends/webgpu/webgpu_bindless_root.h"

#include "core/algorithm/row_major_index_grid.h"
#include "core/ownership/shared_identity_projection.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_bindless_preparation.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_completed_texture_publication.h"
#include "rhi/frontend/bindless_produced_image.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"

namespace arp::rhi_webgpu {
namespace {
constexpr bool canonicalGraphicsClosure(std::uint32_t version) noexcept
{
    return rhi::OriginalBindlessRootNativeClosureContract::canonicalGraphicsVersion(version);
}

std::shared_ptr<WebGpuNativePipelineOwnership> originalPipeline(
    const rhi::OriginalCommandBindlessRootSource& root) noexcept
{
    return std::visit(
        [](const auto& prepared) -> std::shared_ptr<WebGpuNativePipelineOwnership> {
            return prepared ? webGpuNativePayloadFor(*prepared->owners()->desc().pipeline)
                            : nullptr;
        },
        root.originalPrepared());
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> originalDescriptors(
    const rhi::OriginalCommandBindlessRootSource& root, std::uint32_t version) noexcept
{
    return version == 10u || version == 12u || version == 13u || version == 15u
            || rhi::semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                root.originalSnapshot()->plan())
        ? prepareWebGpuOriginalImageCellDescriptors(*root.originalSnapshot())
        : webGpuNativePayloadFor(*root.originalObject());
}
} // namespace

const std::shared_ptr<const WebGpuOriginalGeometryContent>&
WebGpuBindlessRootNativeClosure::initializedGeometry(std::size_t index) const noexcept
{
    for (const auto& entry : entries_)
        if (entry.sourceIndex == index && entry.initializedGeometry)
            return entry.initializedGeometry;
    static const std::shared_ptr<const GeometryContent> empty;
    return empty;
}

const char* WebGpuBindlessRootNativeClosure::inspectInitializedGeometry(
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) const noexcept
{
    std::uint32_t count = 0u;
    std::uint64_t bytes = 0u;
    for (const auto& entry : entries_) {
        if (!entry.initializedGeometry) {
            if (entry.initializedGeometryState)
                return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-STATE";
            continue;
        }
        if (!hasInitializedGeometry() || !entry.initializedGeometryState)
            return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-SCHEMA";
        const auto& actor = entry.initializedGeometry;
        if (const auto* id = seal ? GeometryContent::validateOriginalPending(
                                        actor, seal, *entry.initializedGeometryState)
                                  : GeometryContent::validateOriginal(actor))
            return id;
        if (inventory_->originalLeaseIndex(actor->originalLease()) != entry.sourceIndex)
            return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-INVENTORY";
        if (!seal)
            if (const auto* id =
                    actor->originalLease()->originalObject()->validateOriginalCommittedState(
                        *entry.initializedGeometryState))
                return id;
        if (count == budget_.maxInitializedGeometryActors
            || actor->initializedBytes() > budget_.maxInitializedGeometryBytes - bytes)
            return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-CAPACITY";
        ++count;
        bytes += actor->initializedBytes();
    }
    if (!hasInitializedGeometry())
        return count ? "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-SCHEMA" : nullptr;
    std::size_t expected = 0u;
    for (std::size_t i = 0u; i < inventory_->sources().size(); ++i)
        if (const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                &inventory_->sources()[i])) {
            const auto usage = (*lease)->layout().usage;
            if ((usage & (rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::IndexBuffer))
                != rhi::ResourceUsage::None) {
                if (!initializedGeometry(i))
                    return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-COVERAGE";
                ++expected;
            }
        }
    return count && count == expected ? nullptr : "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-COVERAGE";
}

WebGpuBindlessRootNativeClosure::WebGpuBindlessRootNativeClosure(ConstructionKey,
    std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    Budget budget,
    std::size_t count)
    : root_(std::move(root)), inventory_(std::move(inventory)), budget_(budget)
{
    entries_.reserve(count);
}

std::shared_ptr<WebGpuNativePipelineOwnership>
WebGpuBindlessRootNativeClosure::originalPipelineOwnership() const noexcept
{
    return originalPipeline(*root_);
}

const char* WebGpuBindlessRootNativeClosure::captureGeometryEntry(
    std::size_t index, Entry& entry) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using namespace rhi;
    if (index >= inventory_->sources().size())
        return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-SOURCE";
    const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
        &inventory_->sources()[index]);
    if (!lease || !*lease)
        return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-SOURCE";
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(*lease))
        return id;
    const auto& object = *(*lease)->originalObject();
    if (object.desc().usage != ResourceUsage::VertexBuffer
        && object.desc().usage != ResourceUsage::IndexBuffer)
        return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-USAGE";
    const auto registration = object.originalResourceRegistration();
    if (const auto* id = registration.diagnostic())
        return id;
    if (!registration.matchesIdentity(
            &object, (*lease)->resource().value, OriginalRegisteredResourceKind::Buffer))
        return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-REGISTRATION";
    const auto pipeline = originalPipeline(*root_);
    entry.sourceIndex = index;
    entry.resource = (*lease)->resource();
    entry.generation = registration.generation();
    entry.owner = webGpuNativePayloadFor(object);
    if (!pipeline || !pipeline->issuedContract() || !entry.owner
        || !entry.owner->sharedOwnerMatches(entry.owner) || !entry.owner->buffer
        || !entry.owner->allocationReleaseToken
        || entry.owner->ownerDevice != pipeline->issuedContract()->nativeDevice()
        || entry.owner->sizeBytes != object.desc().sizeBytes)
        return "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-PAYLOAD";
    entry.handle = entry.owner->buffer;
    entry.device = entry.owner->ownerDevice;
    entry.bytes = entry.owner->sizeBytes;
    return nullptr;
#else
    (void)index;
    (void)entry;
    return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-UNAVAILABLE";
#endif
}

std::size_t WebGpuBindlessRootNativeClosure::expectedEntryCount() const noexcept
{
    auto count = root_->elements().size() + 1u;
    for (const auto& domain : root_->ordinaryDomains())
        count += domain.source->frozenElements().size();
    return count;
}

const char* WebGpuBindlessRootNativeClosure::validateOrdinaryGroups() const noexcept
{
    if (ordinaryGroups_.size() != root_->ordinaryDomains().size())
        return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-COUNT";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto pipeline = originalPipeline(*root_);
    const auto& issued = *pipeline->issuedContract();
    for (std::size_t i = 0u; i < ordinaryGroups_.size(); ++i) {
        const auto& group = ordinaryGroups_[i];
        const auto& domain = root_->ordinaryDomains()[i];
        const auto current = canonicalGraphicsClosure(budget_.contractVersion)
            ? prepareWebGpuOriginalOrdinaryGraphicsDescriptors(
                  root_, domain.set, budget_.contractVersion)
            : webGpuNativePayloadFor(*domain.source->originalObject());
        const auto mapping =
            std::find_if(issued.sets().begin(), issued.sets().end(), [&](const auto& m) {
                return m.logicalSet == domain.set;
            });
        if (group.logicalSet != domain.set || !group.descriptors
            || group.descriptors.get() != current.get()
            || !core::SharedOwnerProjection::sameOwner(group.descriptors, current)
            || group.descriptors->bindings.size() != domain.source->frozenElements().size())
            return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-CORRESPONDENCE";
        std::size_t activeCount = 0u;
        for (const auto& element : domain.source->frozenElements()) {
            const auto* declared = rhi::findDescriptorSetBinding(
                domain.source->originalObject()->layout(), element.declaration.write.binding);
            if (!declared)
                return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-BINDING";
            bool active = false;
            if (const auto* id = inspectWebGpuOriginalBindlessBindingActivity(issued,
                    *root_->authorization(),
                    domain.set,
                    element.declaration.write.binding,
                    active))
                return id;
            activeCount += active;
            const auto fact =
                std::find_if(issued.facts().begin(), issued.facts().end(), [&](const auto& f) {
                    return f.mapping.logicalSet == domain.set
                        && f.mapping.logicalBinding == element.declaration.write.binding;
                });
            if (active
                    ? fact == issued.facts().end() || fact->mapping.webGpuGroup != group.groupIndex
                    : fact != issued.facts().end())
                return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-MAPPING";
            for (const auto& payload : {group.physical, group.descriptors}) {
                const bool native = payload.get() == group.physical.get();
                if (!payload) {
                    if (active)
                        return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-BINDING";
                    continue;
                }
                const auto binding = std::find_if(
                    payload->bindings.begin(), payload->bindings.end(), [&](const auto& b) {
                        return rhi::descriptorBindingWriteMatches(
                            b.write, element.declaration.write);
                    });
                if (native && !active) {
                    if (binding != payload->bindings.end())
                        return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-BINDING";
                    continue;
                }
                if (binding == payload->bindings.end()
                    || !rhi::descriptorBindingLayoutMatches(*declared, binding->binding)
                    || (active
                        && !rhi::descriptorBindingLayoutMatches(fact->shape, binding->binding)))
                    return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-BINDING";
            }
        }
        if (!activeCount) {
            if (mapping != issued.sets().end() || group.physical || group.group || group.layout
                || group.groupIndex != UINT32_MAX)
                return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-INACTIVE-GROUP";
        } else if (mapping == issued.sets().end() || group.groupIndex != mapping->webGpuGroup
            || !group.physical || !group.group || !group.layout
            || group.group != group.physical->group || group.layout != group.physical->layout
            || group.physical->bindings.size() != activeCount)
            return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-GROUP-CORRESPONDENCE";
    }
    const auto primary = std::visit(
        [](const auto& prepared) {
            return webGpuOriginalBindlessPhysicalGroup(prepared);
        },
        root_->originalPrepared());
    if (!primary)
        return "RHI-WEBGPU-BINDLESS-ROOT-PHYSICAL-GROUP";
    const auto groupAt = [&](std::size_t i) -> const WebGpuNativeDescriptorGroupPayload* {
        return i ? ordinaryGroups_[i - 1u].physical.get() : primary.get();
    };
    for (std::size_t i = 0u; i <= ordinaryGroups_.size(); ++i) {
        for (std::size_t j = i; j <= ordinaryGroups_.size(); ++j) {
            if (groupAt(i) && groupAt(j))
                if (const auto* id =
                        validateWebGpuOriginalBindlessPhysicalAliases(*groupAt(i), *groupAt(j)))
                    return id;
        }
    }
#endif
    return nullptr;
}

const char* WebGpuBindlessRootNativeClosure::captureEntry(
    std::size_t index, Entry& entry) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using namespace rhi;
    std::shared_ptr<const OriginalCommandBufferLease> lease;
    const auto pipeline = originalPipeline(*root_);
    const auto descriptors = originalDescriptors(*root_, budget_.contractVersion);
    if (!descriptors)
        return "RHI-WEBGPU-BINDLESS-ROOT-PHYSICAL-RESOURCE";
    if (index < root_->elements().size()) {
        const auto& element = root_->elements()[index];
        const auto* buffer =
            std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&element.owner);
        entry.sourceIndex = element.declaration.sourceIndex;
        const auto physical = std::find_if(
            descriptors->bindings.begin(), descriptors->bindings.end(), [&](const auto& binding) {
                return binding.write.binding == element.declaration.write.binding
                    && binding.write.arrayElement == element.declaration.write.arrayElement;
            });
        if (physical == descriptors->bindings.end()
            || !descriptorBindingWriteMatches(physical->write, element.declaration.write))
            return "RHI-WEBGPU-BINDLESS-ROOT-PHYSICAL-RESOURCE";
        if (!buffer) {
            if (budget_.contractVersion != 5u && budget_.contractVersion != 6u
                && budget_.contractVersion != 7u && budget_.contractVersion != 8u
                && budget_.contractVersion != 10u && budget_.contractVersion != 12u
                && budget_.contractVersion != 13u && budget_.contractVersion != 15u
                && budget_.contractVersion != 21u)
                return "RHI-WEBGPU-BINDLESS-ROOT-BUFFER-SCOPE";
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.owner)) {
                const auto& parent = (*view)->originalParentLease();
                const auto* object = (*view)->originalObject();
                const auto* texture = parent->originalObject();
                const bool storageCell = budget_.contractVersion == 10u
                    || budget_.contractVersion == 12u
                    || ((budget_.contractVersion == 13u || budget_.contractVersion == 15u)
                        && !physical->binding.depthTexture)
                    || rhi::semantics::OriginalBindlessPreparationLowering::
                        hasProducedImageReadPlan(root_->originalSnapshot()->plan());
                if (storageCell) {
                    const auto proof =
                        std::find_if(descriptors->completedTexturePublications.begin(),
                            descriptors->completedTexturePublications.end(),
                            [&](const auto& value) {
                                return descriptorBindingWriteMatches(value.write, physical->write);
                            });
                    if (proof == descriptors->completedTexturePublications.end()
                        || !proof->cellIdentity
                        || !core::SharedOwnerProjection::sameOwner(proof->source, *view))
                        return "RHI-WEBGPU-ROOT-STORAGE-CELL-PUBLICATION";
                    if (const auto* id =
                            validateWebGpuOriginalTexturePublication(*proof, *physical))
                        return id;
                    if (rhi::semantics::OriginalBindlessPreparationLowering::
                            hasProducedImageReadPlan(root_->originalSnapshot()->plan())) {
                        if (!rhi::OriginalTextureCellDescriptorContract::isProducedImageRead(
                                proof->cellRequest.contractVersion))
                            return "RHI-BINDLESS-PRODUCED-IMAGE-DESCRIPTOR-PROFILE";
                        if (const auto* id =
                                rhi::OriginalBindlessProducedImageReadContract::inspectView(
                                    texture->desc(), object->desc()))
                            return id;
                        entry.sampledStages = element.declaration.stages;
                        const auto& d = object->desc();
                        entry.sampledRange = {d.baseMipLevel,
                            d.mipLevelCount,
                            d.baseArrayLayer,
                            d.arrayLayerCount,
                            d.aspect};
                    }
                } else if (object->desc().usage != TextureUsage::Sampled
                    || (object->desc().dimension != TextureViewDimension::Texture2D
                        && (budget_.contractVersion != 6u
                            || object->desc().dimension != TextureViewDimension::Texture2DArray))
                    || texture->desc().format
                        != (budget_.contractVersion == 7u || budget_.contractVersion == 8u
                                    || ((budget_.contractVersion == 13u
                                            || budget_.contractVersion == 15u)
                                        && physical->binding.depthTexture)
                                ? TextureFormat::D32Float
                                : TextureFormat::Rgba8Unorm)
                    || object->desc().baseMipLevel || object->desc().baseArrayLayer
                    || object->desc().mipLevelCount != texture->desc().mipLevelCount
                    || object->desc().arrayLayerCount != texture->desc().arrayLayerCount)
                    return "RHI-WEBGPU-ROOT-SAMPLED-SCOPE";
                if (budget_.contractVersion == 7u || budget_.contractVersion == 8u
                    || ((budget_.contractVersion == 13u || budget_.contractVersion == 15u)
                        && physical->binding.depthTexture)) {
                    const auto proof =
                        std::find_if(descriptors->completedTexturePublications.begin(),
                            descriptors->completedTexturePublications.end(),
                            [&](const auto& value) {
                                return descriptorBindingWriteMatches(value.write, physical->write);
                            });
                    if (proof == descriptors->completedTexturePublications.end()
                        || !proof->depthContent)
                        return "RHI-WEBGPU-ROOT-DEPTH-INPUT-MISSING";
                    if (const auto* id =
                            validateWebGpuCompletedTexturePublication(*proof, *physical))
                        return id;
                    entry.depthInput = {proof->depthContent,
                        element.declaration.write.resourceState,
                        element.declaration.stages,
                        element.declaration.access};
                    if (const auto* id =
                            WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                                entry.depthInput, *view, budget_.completedDepthInputs))
                        return id;
                }
                entry.resource = (*view)->resource();
                entry.generation = object->originalResourceRegistration().generation();
                entry.parentSourceIndex = inventory_->originalLeaseIndex(parent);
                entry.parentResource = parent->resource();
                entry.parentGeneration = texture->originalResourceRegistration().generation();
                entry.viewOwner = webGpuNativePayloadFor(*object);
                entry.parentOwner = webGpuNativePayloadFor(*texture);
                if (entry.parentSourceIndex >= inventory_->sources().size() || !entry.viewOwner
                    || !entry.viewOwner->view || !entry.parentOwner || !entry.parentOwner->texture
                    || entry.parentOwner->browserSurfaceTexture
                    || !entry.viewOwner->matchesDescription(object->desc())
                    || !core::SharedOwnerProjection::sameOwner(
                        entry.viewOwner->parentTexture, entry.parentOwner)
                    || !core::SharedOwnerProjection::sameOwner(
                        physical->textureView, entry.viewOwner)
                    || physical->buffer || physical->sampler)
                    return "RHI-WEBGPU-ROOT-SAMPLED-PHYSICAL";
                entry.view = entry.viewOwner->view;
                entry.texture = entry.parentOwner->texture;
                entry.device = entry.parentOwner->ownerDevice;
            } else if (const auto* sampler =
                           std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(
                               &element.owner)) {
                const auto* object = (*sampler)->originalObject();
                entry.resource = (*sampler)->resource();
                entry.generation = object->originalResourceRegistration().generation();
                entry.samplerOwner = webGpuNativePayloadFor(*object);
                if (!entry.samplerOwner || !entry.samplerOwner->sampler || physical->buffer
                    || physical->textureView
                    || !core::SharedOwnerProjection::sameOwner(
                        physical->sampler, entry.samplerOwner))
                    return "RHI-WEBGPU-ROOT-SAMPLED-PHYSICAL";
                entry.sampler = entry.samplerOwner->sampler;
                entry.device = entry.samplerOwner->ownerDevice;
            } else
                return "RHI-WEBGPU-ROOT-SAMPLED-SCOPE";
            return entry.device == pipeline->issuedContract()->nativeDevice()
                ? nullptr
                : "RHI-WEBGPU-ROOT-SAMPLED-DEVICE";
        }
        lease = *buffer;
        if (physical->textureView || physical->sampler || !physical->buffer
            || !core::SharedOwnerProjection::sameOwner(
                physical->buffer, webGpuNativePayloadFor(*lease->originalObject())))
            return "RHI-WEBGPU-BINDLESS-ROOT-PHYSICAL-RESOURCE";
    } else if (index == root_->elements().size()) {
        lease = root_->originalSnapshot()->originalBufferSource(
            BindlessSnapshot::OriginalBufferRole::FaultReadback);
        if (!lease)
            return "RHI-WEBGPU-BINDLESS-ROOT-READBACK-SOURCE";
        entry.sourceIndex = inventory_->originalLeaseIndex(lease);
    } else {
        auto offset = index - root_->elements().size() - 1u;
        const rhi::OriginalCommandDescriptorSetFrozenElement* selected = nullptr;
        const OrdinaryGroup* group = nullptr;
        for (std::size_t i = 0u; i < root_->ordinaryDomains().size(); ++i) {
            const auto& elements = root_->ordinaryDomains()[i].source->frozenElements();
            if (offset < elements.size()) {
                selected = &elements[offset];
                group = &ordinaryGroups_[i];
                break;
            }
            offset -= elements.size();
        }
        if (!selected || !group)
            return "RHI-WEBGPU-BINDLESS-ROOT-ELEMENT-RANGE";
        const auto* buffer = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
            &selected->resourceOwner);
        if (!buffer || !*buffer)
            return captureOrdinaryImage(*selected, *group, entry);
        lease = *buffer;
        entry.sourceIndex = selected->declaration.sourceIndex;
        bool active = false;
        if (const auto* id =
                inspectWebGpuOriginalBindlessBindingActivity(*pipeline->issuedContract(),
                    *root_->authorization(),
                    group->logicalSet,
                    selected->declaration.write.binding,
                    active))
            return id;
        if (active && !group->physical)
            return "RHI-WEBGPU-BINDLESS-ROOT-PHYSICAL-RESOURCE";
        const auto& native = group->physical ? group->physical : group->descriptors;
        const auto physical =
            std::find_if(native->bindings.begin(), native->bindings.end(), [&](const auto& b) {
                return descriptorBindingWriteMatches(b.write, selected->declaration.write);
            });
        const auto original = std::find_if(group->descriptors->bindings.begin(),
            group->descriptors->bindings.end(),
            [&](const auto& b) {
                return descriptorBindingWriteMatches(b.write, selected->declaration.write);
            });
        const auto owner = webGpuNativePayloadFor(*lease->originalObject());
        if (original == group->descriptors->bindings.end()
            || (active ? physical == native->bindings.end() || physical->textureView
                        || physical->sampler
                        || !core::SharedOwnerProjection::sameOwner(physical->buffer, owner)
                       : group->physical && physical != native->bindings.end())
            || original->textureView || original->sampler
            || !core::SharedOwnerProjection::sameOwner(original->buffer, owner))
            return "RHI-WEBGPU-BINDLESS-ROOT-PHYSICAL-RESOURCE";
    }
    if (!lease || entry.sourceIndex >= inventory_->sources().size())
        return "RHI-WEBGPU-BINDLESS-ROOT-RESOURCE-INVENTORY";
    const auto* canonical = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
        &inventory_->sources()[entry.sourceIndex]);
    if (!canonical || !core::SharedOwnerProjection::sameOwner(lease, *canonical))
        return "RHI-WEBGPU-BINDLESS-ROOT-RESOURCE-INVENTORY";
    entry.resource = lease->resource();
    entry.generation = lease->originalObject()->originalResourceRegistration().generation();
    entry.owner = webGpuNativePayloadFor(*lease->originalObject());
    if (!entry.owner || !entry.owner->buffer
        || entry.owner->ownerDevice != pipeline->issuedContract()->nativeDevice()
        || entry.owner->sizeBytes != lease->layout().sizeBytes)
        return "RHI-WEBGPU-BINDLESS-ROOT-RESOURCE-DEVICE";
    entry.handle = entry.owner->buffer;
    entry.device = entry.owner->ownerDevice;
    entry.bytes = entry.owner->sizeBytes;
    return nullptr;
#else
    (void)index;
    (void)entry;
    return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-UNAVAILABLE";
#endif
}

const char* WebGpuBindlessRootNativeClosure::captureOrdinaryImage(
    const rhi::OriginalCommandDescriptorSetFrozenElement& selected,
    const OrdinaryGroup& group,
    Entry& entry) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using namespace rhi;
    if (!canonicalGraphicsClosure(budget_.contractVersion))
        return "RHI-WEBGPU-BINDLESS-ROOT-BUFFER-SCOPE";
    entry.sourceIndex = selected.declaration.sourceIndex;
    if (entry.sourceIndex >= inventory_->sources().size())
        return "RHI-WEBGPU-BINDLESS-ROOT-RESOURCE-INVENTORY";
    const auto& canonical = inventory_->sources()[entry.sourceIndex];
    for (const auto& payload : {group.descriptors, group.physical}) {
        const auto binding =
            std::find_if(payload->bindings.begin(), payload->bindings.end(), [&](const auto& b) {
                return descriptorBindingWriteMatches(b.write, selected.declaration.write);
            });
        if (binding == payload->bindings.end())
            return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-COLOR-BINDING";
        const bool canonicalDepth =
            budget_.contractVersion == 19u || budget_.contractVersion == 20u;
        const bool depth = canonicalDepth && binding->binding.depthTexture;
        const bool depthDeclaration =
            depth || (canonicalDepth && binding->binding.comparisonSampler);
        if (depthDeclaration
                ? semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                      Backend::WebGPU, binding->binding)
                    != nullptr
                : !semantics::OriginalBindlessOrdinaryColorDomain::acceptsGraphicsBinding(
                      binding->binding))
            return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-COLOR-BINDING";
        if (const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                &selected.resourceOwner)) {
            const auto* original =
                std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&canonical);
            const auto proof = std::find_if(payload->completedTexturePublications.begin(),
                payload->completedTexturePublications.end(),
                [&](const auto& p) {
                    return descriptorBindingWriteMatches(p.write, selected.declaration.write);
                });
            if (!*view || !original || !core::SharedOwnerProjection::sameOwner(*view, *original)
                || proof == payload->completedTexturePublications.end() || proof->cellIdentity
                || (depth ? !proof->depthContent || proof->content
                          : proof->colorPublicationVersion != 6u || !proof->content
                            || proof->depthContent)
                || !core::SharedOwnerProjection::sameOwner(proof->source, *view))
                return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-COLOR-PROVENANCE";
            if (const auto* id = validateWebGpuCompletedTexturePublication(*proof, *binding))
                return id;
            const auto& parent = (*view)->originalParentLease();
            const auto* object = (*view)->originalObject();
            const auto* texture = parent->originalObject();
            if (depth) {
                if (proof->depthContent.get() != selected.completedDepth.get()
                    || !core::SharedOwnerProjection::sameOwner(
                        proof->depthContent, selected.completedDepth))
                    return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-DEPTH-PROVENANCE";
                entry.depthInput = {proof->depthContent,
                    selected.declaration.write.resourceState,
                    selected.declaration.stages,
                    selected.declaration.access};
                if (const auto* id =
                        WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                            entry.depthInput, *view, budget_.completedDepthInputs))
                    return id;
            } else if (const auto* id =
                           semantics::OriginalBindlessOrdinaryColorDomain::completedViewDiagnostic(
                               texture->desc(),
                               object->desc(),
                               proof->content->controlVersion(),
                               3u))
                return id;
            entry.resource = (*view)->resource();
            entry.generation = object->originalResourceRegistration().generation();
            entry.parentSourceIndex = inventory_->originalLeaseIndex(parent);
            entry.parentResource = parent->resource();
            entry.parentGeneration = texture->originalResourceRegistration().generation();
            entry.viewOwner = webGpuNativePayloadFor(*object);
            entry.parentOwner = webGpuNativePayloadFor(*texture);
            entry.sampledStages = depth ? PipelineStage::None : selected.declaration.stages;
            entry.sampledRange = {object->desc().baseMipLevel,
                object->desc().mipLevelCount,
                object->desc().baseArrayLayer,
                object->desc().arrayLayerCount,
                object->desc().aspect};
            if (entry.parentSourceIndex >= inventory_->sources().size() || !entry.viewOwner
                || !entry.viewOwner->view || !entry.parentOwner || !entry.parentOwner->texture
                || entry.parentOwner->browserSurfaceTexture
                || !entry.viewOwner->matchesDescription(object->desc())
                || !core::SharedOwnerProjection::sameOwner(
                    entry.viewOwner->parentTexture, entry.parentOwner)
                || !core::SharedOwnerProjection::sameOwner(binding->textureView, entry.viewOwner)
                || binding->buffer || binding->sampler)
                return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-COLOR-PHYSICAL";
            entry.view = entry.viewOwner->view;
            entry.texture = entry.parentOwner->texture;
            entry.device = entry.parentOwner->ownerDevice;
        } else if (const auto* sampler =
                       std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(
                           &selected.resourceOwner)) {
            const auto* original =
                std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(&canonical);
            if (!*sampler || !original
                || !core::SharedOwnerProjection::sameOwner(*sampler, *original))
                return "RHI-WEBGPU-BINDLESS-ROOT-RESOURCE-INVENTORY";
            const auto* object = (*sampler)->originalObject();
            entry.resource = (*sampler)->resource();
            entry.generation = object->originalResourceRegistration().generation();
            entry.samplerOwner = webGpuNativePayloadFor(*object);
            if (!entry.samplerOwner || !entry.samplerOwner->sampler || binding->buffer
                || binding->textureView
                || !core::SharedOwnerProjection::sameOwner(binding->sampler, entry.samplerOwner))
                return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-COLOR-PHYSICAL";
            entry.sampler = entry.samplerOwner->sampler;
            entry.device = entry.samplerOwner->ownerDevice;
        } else
            return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-COLOR-SCOPE";
        if (entry.device != originalPipeline(*root_)->issuedContract()->nativeDevice())
            return "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-COLOR-DEVICE";
    }
    return nullptr;
#else
    (void)selected;
    (void)group;
    (void)entry;
    return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-UNAVAILABLE";
#endif
}

std::size_t WebGpuBindlessRootNativeClosure::nativeOwnerReferences() const noexcept
{
    return 2u * entries_.size() + 3u + 2u * attachments_.size() + 2u * ordinaryGroups_.size();
}

core::SharedIdentityProjection::Record WebGpuBindlessRootNativeClosure::nativeOwnerAt(
    std::size_t i) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto pipeline = originalPipeline(*root_);
    const auto descriptors = originalDescriptors(*root_, budget_.contractVersion);
    const auto physical = std::visit(
        [](const auto& prepared) {
            return webGpuOriginalBindlessPhysicalGroup(prepared);
        },
        root_->originalPrepared());
    if (!pipeline || !pipeline->issuedContract() || !descriptors || !physical || !physical->group)
        return {1u, 1u, {}, nullptr, 1u};
    const auto key = [](const auto* handle) {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
    };
    const auto primaryCount = 2u * entries_.size();
    const auto attachmentEnd = primaryCount + 3u + 2u * attachments_.size();
    if (i >= attachmentEnd) {
        const auto& g = ordinaryGroups_[(i - attachmentEnd) / 2u];
        return (i - attachmentEnd) % 2u
            ? core::SharedIdentityProjection::Record{key(g.descriptors.get()),
                  1u,
                  g.descriptors,
                  g.descriptors.get(),
                  4u}
            : g.physical ? core::SharedIdentityProjection::Record{key(g.group),
                               1u,
                               g.physical,
                               g.physical.get(),
                               3u}
                         : core::SharedIdentityProjection::Record{};
    }
    if (i == primaryCount)
        return {key(pipeline->issuedContract()->nativeObject()), 1u, pipeline, pipeline.get(), 2u};
    if (i == primaryCount + 1u)
        return {key(physical->group), 1u, physical, physical.get(), 3u};
    if (i == primaryCount + 2u)
        return {key(descriptors.get()), 1u, descriptors, descriptors.get(), 4u};
    if (i > primaryCount + 2u) {
        const auto index = i - primaryCount - 3u;
        const auto& a = attachments_[index / 2u];
        if (index % 2u)
            return {key(a.texture), 1u, a.parent, a.parent.get(), 6u};
        return {key(a.handle), 1u, a.owner, a.owner.get(), 5u};
    }
    const auto& e = entries_[i / 2u];
    if (i % 2u)
        return e.parentOwner ? core::SharedIdentityProjection::Record{key(e.texture),
                                   1u,
                                   e.parentOwner,
                                   e.parentOwner.get(),
                                   6u}
                             : core::SharedIdentityProjection::Record{};
    if (e.owner)
        return {key(e.handle), 1u, e.owner, e.owner.get(), 1u};
    if (e.viewOwner)
        return {key(e.view), 1u, e.viewOwner, e.viewOwner.get(), 5u};
    return {key(e.sampler), 1u, e.samplerOwner, e.samplerOwner.get(), 7u};

#else
    (void)i;
    return {};
#endif
}

const char* WebGpuBindlessRootNativeClosure::combinedNativeOwnerCount(
    const WebGpuBindlessRootNativeClosure& other,
    std::uint32_t limit,
    std::uint32_t& out,
    const WebGpuBindlessRootNativeClosure* final) const noexcept
{
    out = 0u;
    const auto first = nativeOwnerReferences(), second = other.nativeOwnerReferences();
    const auto third = final ? final->nativeOwnerReferences() : 0u;
    if (second > SIZE_MAX - first || third > SIZE_MAX - first - second)
        return "RHI-WEBGPU-ROOT-COMBINED-NATIVE-OWNER-CAPACITY";
    const auto count = first + second + third;
    const auto result =
        core::SharedIdentityProjection::inspect(count, count, limit, [&](std::size_t i) noexcept {
            return i < first         ? nativeOwnerAt(i)
                : i - first < second ? other.nativeOwnerAt(i - first)
                                     : final->nativeOwnerAt(i - first - second);
        });
    if (result.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-WEBGPU-ROOT-COMBINED-NATIVE-OWNER-CAPACITY";
    out = static_cast<std::uint32_t>(result.identities);
    return nullptr;
}

const char* WebGpuBindlessRootNativeClosure::inspectGeometryOwners(
    std::uint32_t& output) const noexcept
{
    output = 0u;
    const auto key = [](const auto* object) {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object));
    };
    const core::RowMajorIndexGrid actors(
        entries_.size(), 1u + GeometryContent::exclusiveCpuQuotaSlots, SIZE_MAX);
    if (!actors.valid())
        return "RHI-WEBGPU-BINDLESS-ROOT-OWNER-CAPACITY";
    const auto geometryOwners = core::SharedIdentityProjection::inspect(actors.size(),
        actors.size(),
        budget_.maxRetainedOwners,
        [&](std::size_t i) -> core::SharedIdentityProjection::Record {
            std::uint64_t row = 0u, slot = 0u;
            actors.coordinates(i, row, slot);
            std::shared_ptr<const void> owner;
            if (const auto& actor = entries_[row].initializedActor())
                owner =
                    slot ? std::shared_ptr<const void>(actor->exclusiveCpuQuota(slot - 1u)) : actor;
            return owner ? core::SharedIdentityProjection::Record{key(owner.get()),
                               1u,
                               owner,
                               owner.get(),
                               8u}
                         : core::SharedIdentityProjection::Record{};
        });
    if (geometryOwners.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-WEBGPU-BINDLESS-ROOT-OWNER-CAPACITY";
    output = static_cast<std::uint32_t>(geometryOwners.identities);
    return nullptr;
}

std::uint32_t WebGpuBindlessRootNativeClosure::initializedGeometryOwnerCount() const noexcept
{
    std::uint32_t count = 0u;
    return inspectGeometryOwners(count) ? 0u : count;
}

const char* WebGpuBindlessRootNativeClosure::validateGeometryFetch(
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& stream,
    const rhi::OriginalIndexedDrawFetchContract::Budget& budget) const noexcept
{
    if (const auto* id = rhi::OriginalCommandStreamProgram::validateOriginal(stream))
        return id;
    if (!hasInitializedGeometry())
        return "RHI-WEBGPU-ROOT-GEOMETRY-FETCH-PROFILE";
    const auto result =
        stream->inspectInitializedGeometry(budget, [&](std::size_t source) noexcept {
            return initializedGeometry(source);
        });
    if (result.diagnostic)
        return result.diagnostic;
    return result.draws && result.streams && result.analyzedIndices
        ? nullptr
        : "RHI-WEBGPU-ROOT-GEOMETRY-FETCH-COVERAGE";
}

const char* WebGpuBindlessRootNativeClosure::inspectOwners(
    std::uint32_t& native, std::uint32_t& retained) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto pipeline = originalPipeline(*root_);
    const auto descriptors = originalDescriptors(*root_, budget_.contractVersion);
    if (!descriptors)
        return "RHI-WEBGPU-BINDLESS-ROOT-PHYSICAL-RESOURCE";
    const auto key = [](const auto* handle) {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
    };
    const auto count = nativeOwnerReferences();
    const auto projection = core::SharedIdentityProjection::inspect(
        count, count, budget_.maxNativeOwners, [&](std::size_t i) noexcept {
            return nativeOwnerAt(i);
        });
    if (projection.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY";
    native = static_cast<std::uint32_t>(projection.identities);
    const auto depthSources = core::SharedIdentityProjection::inspect(entries_.size(),
        entries_.size(),
        entries_.size(),
        [&](std::size_t i) -> core::SharedIdentityProjection::Record {
            const auto& source = entries_[i].depthInput.source;
            return source ? core::SharedIdentityProjection::Record{key(source.get()),
                                1u,
                                source,
                                source.get(),
                                8u}
                          : core::SharedIdentityProjection::Record{};
        });
    if (depthSources.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-WEBGPU-ROOT-DEPTH-INPUT-OWNERS";
    std::uint32_t geometryOwners = 0u;
    if (const auto* id = inspectGeometryOwners(geometryOwners))
        return id;
    // A conservative complete custody charge per distinct proof. Canonical
    // view/parent owners remain in the original inventory; duplicate padding
    // does not charge another proof. No new capacity or resource identity.
    const auto custody = std::uint64_t(native) + geometryOwners
        + depthSources.identities
            * rhi::OriginalCompletedDepthTextureSource::completeRetainedOwnerCount;
    if (custody > budget_.maxRetainedOwners
        || inventory_->cpuOwnerCount(
            3u, budget_.maxRetainedOwners - static_cast<std::uint32_t>(custody), retained))
        return "RHI-WEBGPU-BINDLESS-ROOT-OWNER-CAPACITY";
    retained += static_cast<std::uint32_t>(custody);
    return nullptr;
#else
    (void)native;
    (void)retained;
    return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-UNAVAILABLE";
#endif
}

const char* WebGpuBindlessRootNativeClosure::captureAttachment(
    std::size_t source, Attachment& out, bool cold) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using namespace rhi;
    if ((!canonicalGraphicsClosure(budget_.contractVersion) && budget_.contractVersion != 4u
            && budget_.contractVersion != 5u && budget_.contractVersion != 6u
            && budget_.contractVersion != 12u && budget_.contractVersion != 15u
            && budget_.producedAttachments.empty())
        || source >= inventory_->sources().size())
        return "RHI-WEBGPU-ROOT-ATTACHMENT-SCOPE";
    const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
        &inventory_->sources()[source]);
    if (!view || !*view)
        return "RHI-WEBGPU-ROOT-ATTACHMENT-SOURCE";
    if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(*view))
        return id;
    const auto& parent = (*view)->originalParentLease();
    const auto* object = (*view)->originalObject();
    const auto* texture = parent->originalObject();
    const auto& desc = texture->desc();
    // Content is observed before the original journal proposes it. Pending
    // content is authenticated by that same Content9 transaction, never through
    // a public completed-content observation. The retained parent/control cannot
    // be replaced while its resource generation remains registered.
    const bool storage = budget_.contractVersion == 12u || budget_.contractVersion == 15u;
    const bool produced = !budget_.producedAttachments.empty();
    const auto& viewDesc = object->desc();
    if (cold) {
        const TextureSubresourceRange visible{viewDesc.baseMipLevel,
            viewDesc.mipLevelCount,
            viewDesc.baseArrayLayer,
            viewDesc.arrayLayerCount,
            viewDesc.aspect};
        const auto content = texture->observeOriginalContent(
            storage ? visible : defaultTextureSubresourceRange(desc));
        if (!content
            || (produced ? !OriginalBindlessProducedAttachmentContract::acceptsContentControl(
                               content->controlVersion())
                         : content->controlVersion() != (storage ? 7u : 5u)))
            return "RHI-WEBGPU-ROOT-ATTACHMENT-CONTENT-PROFILE";
    }
    if (produced) {
        if (!texture->originalContentExclusive())
            return "RHI-WEBGPU-ROOT-ATTACHMENT-CONTENT-PROFILE";
        if (const auto* id =
                OriginalBindlessProducedAttachmentContract::inspectView(desc, viewDesc))
            return id;
    } else if (object->desc().usage != TextureUsage::RenderAttachment
        || !texture->originalContentExclusive()
        || desc.format != (storage ? TextureFormat::Rgba16Float : TextureFormat::Rgba8Unorm)
        || desc.sampleCount != 1u || (!storage && desc.mipLevelCount != 1u)
        || desc.arrayLayerCount != 1u || object->desc().dimension != TextureViewDimension::Texture2D
        || (!storage && object->desc().baseMipLevel) || object->desc().baseArrayLayer
        || object->desc().mipLevelCount != 1u || object->desc().arrayLayerCount != 1u)
        return "RHI-WEBGPU-ROOT-ATTACHMENT-DESCRIPTION";
    out.sourceIndex = source;
    out.parentSourceIndex = inventory_->originalLeaseIndex(parent);
    if (out.parentSourceIndex >= inventory_->sources().size())
        return "RHI-WEBGPU-ROOT-ATTACHMENT-INVENTORY";
    out.resource = (*view)->resource();
    out.parentResource = parent->resource();
    out.generation = object->originalResourceRegistration().generation();
    out.parentGeneration = texture->originalResourceRegistration().generation();
    out.owner = webGpuNativePayloadFor(*object);
    out.parent = webGpuNativePayloadFor(*texture);
    const auto pipeline = originalPipeline(*root_);
    if (!out.owner || !out.owner->view || !out.owner->matchesDescription(object->desc())
        || !out.parent || !out.parent->texture || out.parent->browserSurfaceTexture
        || !core::SharedOwnerProjection::sameOwner(out.owner->parentTexture, out.parent)
        || out.parent->ownerDevice != pipeline->issuedContract()->nativeDevice()
        || out.parent->rhiFormat != desc.format || out.parent->sampleCount != desc.sampleCount
        || out.parent->extent.width != desc.extent.width
        || out.parent->extent.height != desc.extent.height)
        return "RHI-WEBGPU-ROOT-ATTACHMENT-NATIVE";
    out.handle = out.owner->view;
    out.texture = out.parent->texture;
    out.device = out.parent->ownerDevice;
    return nullptr;
#else
    (void)source;
    (void)out;
    return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-UNAVAILABLE";
#endif
}

std::shared_ptr<const WebGpuBindlessRootNativeClosure>
WebGpuBindlessRootNativeClosure::prepareOriginal(std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    rhi::GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    return prepareOriginal(
        std::move(root), std::move(inventory), device, budget, std::move(quota), diagnostic, {});
}

std::shared_ptr<const WebGpuBindlessRootNativeClosure>
WebGpuBindlessRootNativeClosure::prepareOriginal(std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    rhi::GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    GeometryContents contents)
{
    diagnostic = rhi::OriginalBindlessRootNativeClosureContract::validateBudget(budget);
    if (diagnostic)
        return {};
    if ((diagnostic = rhi::OriginalBindlessRootNativeClosureContract::validateGeometryBackend(
             device.backend(), budget)))
        return {};
    const auto lowering = rhi::semantics::OriginalBindlessRootNativeClosureLowering::lowering(
        device.backend(), budget.contractVersion);
    if (!lowering.nativeCorrespondenceAvailable || lowering.diagnostic) {
        diagnostic = lowering.diagnostic;
        return {};
    }
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-QUOTA";
        return {};
    }
    if ((diagnostic = Root::validateOriginal(root, device))
        || (diagnostic = Sources::validateRegisteredLifetime(inventory, device)))
        return {};
    if (std::holds_alternative<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
            root->originalPrepared())
        && (diagnostic = Root::validateOriginalGraphicsBufferRanges(root)))
        return {};
    if ((diagnostic = rhi::OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
             root->originalSnapshot()->plan(), budget.contractVersion)))
        return {};
    if ((diagnostic = rhi::OriginalBindlessRootNativeClosureContract::mixedRootDiagnostic(
             *root, budget.contractVersion)))
        return {};
    if ((diagnostic = rhi::OriginalBindlessRootNativeClosureContract::
                canonicalGraphicsColorRootDiagnostic(*root, budget.contractVersion)))
        return {};
    if ((diagnostic = rhi::OriginalBindlessRootNativeClosureContract::
                canonicalGraphicsDepthRootDiagnostic(*root, budget.contractVersion)))
        return {};
    if ((diagnostic = rhi::semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(
             rhi::Backend::WebGPU, root->sourceContractVersion())))
        return {};
    if (core::SharedOwnerProjection::sameControlBlock(quota, inventory->originalMetadataQuota())) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-QUOTA-DOMAIN";
        return {};
    }
    if (!root->matchesCanonicalSources(*inventory)
        || inventory->originalLeaseIndex(root) == SIZE_MAX) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-INVENTORY";
        return {};
    }
    if (device.backend() != rhi::Backend::WebGPU || !webGpuNativeDeviceAvailable()
        || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-UNAVAILABLE";
        return {};
    }
    const auto& plan = root->originalSnapshot()->plan();
    const bool compute =
        std::holds_alternative<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>>(
            root->originalPrepared());
    const bool depth = budget.contractVersion == 7u || budget.contractVersion == 8u;
    const bool produced = budget.contractVersion
        == rhi::OriginalBindlessRootNativeClosureContract::producedGeometryVersion;
    if ((diagnostic = rhi::OriginalBindlessRootNativeClosureContract::producedRootDiagnostic(
             *root, budget.contractVersion)))
        return {};
    if (!(produced ? rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 16u)
                : budget.contractVersion == 3u ? !compute
                    && rhi::OriginalBindlessRootNativeExecutionContract::hasPreparedPlan(plan, 1u)
                : budget.contractVersion == 20u ? !compute
                    && rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 14u)
                : budget.contractVersion == 19u ? !compute
                    && rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 12u)
                : budget.contractVersion == 18u ? !compute
                    && rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 13u)
                : budget.contractVersion == 17u ? !compute
                    && rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 11u)
                : budget.contractVersion == 13u ? compute
                    && rhi::semantics::OriginalBindlessPreparationLowering::
                        hasMixedImageBindingPlan(plan)
                : budget.contractVersion == 15u ? compute
                    && rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 12u)
                : budget.contractVersion == 12u ? compute
                    && rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 10u)
                : budget.contractVersion == 10u
                ? compute && rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 8u)
                : depth   ? budget.contractVersion == 7u
                      ? rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 5u)
                      : compute
                        && rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                            plan, 5u)
                  : compute ? rhi::OriginalBindlessRootNativeExecutionContract::hasPreparedPlan(plan)
                          : rhi::OriginalBindlessRootNativeExecutionContract::hasPreparedPlan(plan,
                                budget.contractVersion == 6u       ? 4u
                                    : budget.contractVersion == 5u ? 3u
                                                                   : 2u))) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-PREPARATION-SCOPE";
        return {};
    }
    if ((diagnostic = rhi::OriginalBindlessInitializedShaderContract::validateInputs(*root,
             *inventory,
             device.backend(),
             budget.initializedShader,
             contents.shaderData,
             contents.shaderCount)))
        return {};
    if (rhi::OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget)
            != bool(contents.count)
        || (contents.count && !contents.data)
        || contents.count > budget.maxInitializedGeometryActors) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-SCHEMA";
        return {};
    }
    std::uint64_t initializedBytes = 0u;
    for (std::size_t i = 0u; i < contents.count; ++i) {
        const auto& actor = contents.data[i];
        if ((diagnostic = GeometryContent::validateOriginal(actor)))
            return {};
        if (inventory->originalLeaseIndex(actor->originalLease()) == SIZE_MAX) {
            diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-INVENTORY";
            return {};
        }
        for (std::size_t j = 0u; j < i; ++j)
            if (core::SharedOwnerProjection::sameOwner(
                    actor->originalLease(), contents.data[j]->originalLease())) {
                diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-DUPLICATE";
                return {};
            }
        for (std::size_t slot = 0u; slot < GeometryContent::exclusiveCpuQuotaSlots; ++slot)
            if (core::SharedOwnerProjection::sameControlBlock(
                    quota, actor->exclusiveCpuQuota(slot))) {
                diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-QUOTA-DOMAIN";
                return {};
            }
        if (actor->initializedBytes() > budget.maxInitializedGeometryBytes - initializedBytes) {
            diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-BYTE-CAPACITY";
            return {};
        }
        initializedBytes += actor->initializedBytes();
    }
    std::size_t count = root->elements().size() + 1u;
    for (const auto& domain : root->ordinaryDomains()) {
        if (domain.source->frozenElements().size() > budget.maxResourceEntries
            || count > budget.maxResourceEntries - domain.source->frozenElements().size()) {
            diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
            return {};
        }
        count += domain.source->frozenElements().size();
    }
    if (count > budget.maxResourceEntries) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
        return {};
    }
    if (contents.count > budget.maxResourceEntries - count) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
        return {};
    }
    if (!produced && !canonicalGraphicsClosure(budget.contractVersion)
        && ((budget.contractVersion == 4u)
                != (!compute && plan.originalExecutionContractVersion == 2u)
            || (budget.contractVersion == 5u)
                != (!compute && plan.originalExecutionContractVersion == 3u)
            || (budget.contractVersion == 6u)
                != (!compute && plan.originalExecutionContractVersion == 4u))) {
        diagnostic = "RHI-WEBGPU-ROOT-ATTACHMENT-PROFILE";
        return {};
    }
    if (depth
        != (rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 5u)
            || rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 5u))) {
        diagnostic = "RHI-WEBGPU-ROOT-DEPTH-INPUT-PROFILE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<WebGpuBindlessRootNativeClosure>(ConstructionKey{},
            std::move(root),
            std::move(inventory),
            budget,
            count + contents.count);
        result->ordinaryGroups_.reserve(result->root_->ordinaryDomains().size());
        for (const auto& domain : result->root_->ordinaryDomains()) {
            OrdinaryGroup group;
            group.logicalSet = domain.set;
            group.descriptors = canonicalGraphicsClosure(budget.contractVersion)
                ? prepareWebGpuOriginalOrdinaryGraphicsDescriptors(
                      result->root_, domain.set, budget.contractVersion)
                : webGpuNativePayloadFor(*domain.source->originalObject());
            const auto valid = prepareWebGpuOriginalBindlessPhysicalGroup(
                *originalPipeline(*result->root_)->issuedContract(),
                *domain.source->originalObject(),
                group.descriptors,
                *result->root_->authorization(),
                domain.set,
                group.physical,
                group.groupIndex,
                canonicalGraphicsClosure(budget.contractVersion) ? budget.contractVersion : 0u);
            if (!valid.success) {
                diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-ORDINARY-PHYSICAL-GROUP";
                return {};
            }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            group.group = group.physical ? group.physical->group : nullptr;
            group.layout = group.physical ? group.physical->layout : nullptr;
#endif
            result->ordinaryGroups_.push_back(std::move(group));
        }
        std::uint64_t shaderVisits = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            Entry entry;
            if ((diagnostic = result->captureEntry(i, entry)))
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
        for (std::size_t i = 0u; i < contents.count; ++i) {
            Entry entry;
            entry.initializedGeometry = contents.data[i];
            entry.sourceIndex =
                result->inventory_->originalLeaseIndex(contents.data[i]->originalLease());
            if ((diagnostic = result->captureGeometryEntry(entry.sourceIndex, entry)))
                return {};
            const auto state = contents.data[i]
                                   ->originalLease()
                                   ->originalObject()
                                   ->observeOriginalCommittedState();
            if (!state) {
                diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-STATE";
                return {};
            }
            entry.initializedGeometryState.emplace(*state);
            result->entries_.push_back(std::move(entry));
        }
        if ((diagnostic = result->inspectInitializedGeometry({})))
            return {};
        if (budget.contractVersion == 4u || budget.contractVersion == 5u
            || budget.contractVersion == 6u || budget.contractVersion == 12u
            || budget.contractVersion == 15u || canonicalGraphicsClosure(budget.contractVersion)
            || !budget.producedAttachments.empty()) {
            for (std::size_t i = 0u; i < result->inventory_->sources().size(); ++i) {
                const auto* view =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        &result->inventory_->sources()[i]);
                if (!view
                    || (*view)->originalObject()->desc().usage
                        != rhi::TextureUsage::RenderAttachment)
                    continue;
                if (result->attachments_.size() == budget.maxAttachmentViews
                    || result->resourceEntryCount() == budget.maxResourceEntries) {
                    diagnostic = "RHI-WEBGPU-ROOT-ATTACHMENT-CAPACITY";
                    return {};
                }
                Attachment attachment;
                if ((diagnostic = result->captureAttachment(i, attachment, true)))
                    return {};
                result->attachments_.push_back(std::move(attachment));
            }
            if (result->attachments_.empty()) {
                diagnostic = "RHI-WEBGPU-ROOT-ATTACHMENT-MISSING";
                return {};
            }
        }
        if ((diagnostic = result->inspectOwners(result->nativeOwners_, result->retainedOwners_)))
            return {};
        auto shared = shareOriginal(std::move(result));
        if ((diagnostic = shared->validate()))
            return {};
        return shared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ALLOCATION";
    }
    return {};
}

const char* WebGpuBindlessRootNativeClosure::validateOriginal(
    const std::shared_ptr<const WebGpuBindlessRootNativeClosure>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-OWNER"
                                                          : self->validate();
}

const char* WebGpuBindlessRootNativeClosure::validate() const noexcept
{
    return validatePhase({});
}

const char* WebGpuBindlessRootNativeClosure::validateOriginalPending(
    const std::shared_ptr<const WebGpuBindlessRootNativeClosure>& self,
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self) || !seal
        || !seal->hasQuotaBoundSharedOwner(seal))
        return "RHI-WEBGPU-BINDLESS-ROOT-PENDING-OWNER";
    return self->validatePhase(seal);
}

const char* WebGpuBindlessRootNativeClosure::validatePhase(
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
            rhi::OriginalBindlessRootNativeClosureContract::canonicalGraphicsColorRootDiagnostic(
                *root_, budget_.contractVersion))
        return id;
    if (const auto* id =
            rhi::OriginalBindlessRootNativeClosureContract::canonicalGraphicsDepthRootDiagnostic(
                *root_, budget_.contractVersion))
        return id;
    if (const auto* id =
            rhi::semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(
                rhi::Backend::WebGPU, root_->sourceContractVersion()))
        return id;
    if (const auto* id = Sources::validateOriginal(inventory_))
        return id;
    if (!root_->matchesCanonicalSources(*inventory_)
        || inventory_->originalLeaseIndex(root_) == SIZE_MAX)
        return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-INVENTORY";
    if (webGpuNativeDeviceLost())
        return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-UNAVAILABLE";
    if (const auto* id = validateOrdinaryGroups())
        return id;
    std::size_t geometryCount = 0u;
    for (const auto& entry : entries_)
        geometryCount += bool(entry.initializedGeometry);
    if (entries_.size() != expectedEntryCount() + geometryCount)
        return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
    for (std::size_t i = 0u; i < entries_.size(); ++i) {
        Entry current;
        if (const auto* id = entries_[i].initializedGeometry
                ? captureGeometryEntry(entries_[i].sourceIndex, current)
                : captureEntry(i, current))
            return id;
        const auto& frozen = entries_[i];
        if (frozen.sourceIndex != current.sourceIndex || frozen.resource != current.resource
            || frozen.generation != current.generation || frozen.bytes != current.bytes
            || !core::SharedOwnerProjection::sameOwner(frozen.owner, current.owner)
            || frozen.parentSourceIndex != current.parentSourceIndex
            || frozen.parentResource != current.parentResource
            || frozen.parentGeneration != current.parentGeneration
            || frozen.sampledStages != current.sampledStages
            || frozen.sampledRange != current.sampledRange
            || !core::SharedOwnerProjection::sameOwner(frozen.viewOwner, current.viewOwner)
            || !core::SharedOwnerProjection::sameOwner(frozen.parentOwner, current.parentOwner)
            || !core::SharedOwnerProjection::sameOwner(frozen.samplerOwner, current.samplerOwner)
            || frozen.depthInput.source.get() != current.depthInput.source.get()
            || !core::SharedOwnerProjection::sameOwner(
                frozen.depthInput.source, current.depthInput.source)
            || frozen.depthInput.state != current.depthInput.state
            || frozen.depthInput.stages != current.depthInput.stages
            || frozen.depthInput.access != current.depthInput.access
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            || frozen.handle != current.handle || frozen.device != current.device
            || frozen.view != current.view || frozen.texture != current.texture
            || frozen.sampler != current.sampler
#endif
        )
            return "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-CORRESPONDENCE";
    }
    if ((budget_.contractVersion == 4u || budget_.contractVersion == 5u
            || budget_.contractVersion == 6u || budget_.contractVersion == 12u
            || budget_.contractVersion == 15u || canonicalGraphicsClosure(budget_.contractVersion)
            || !budget_.producedAttachments.empty())
            != !attachments_.empty()
        || attachments_.size() > budget_.maxAttachmentViews
        || resourceEntryCount() > budget_.maxResourceEntries)
        return "RHI-WEBGPU-ROOT-ATTACHMENT-CAPACITY";
    for (const auto& frozen : attachments_) {
        Attachment current;
        if (const auto* id = captureAttachment(frozen.sourceIndex, current))
            return id;
        if (current.parentSourceIndex != frozen.parentSourceIndex
            || current.resource != frozen.resource
            || current.parentResource != frozen.parentResource
            || current.generation != frozen.generation
            || current.parentGeneration != frozen.parentGeneration
            || !core::SharedOwnerProjection::sameOwner(current.owner, frozen.owner)
            || !core::SharedOwnerProjection::sameOwner(current.parent, frozen.parent)
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            || current.handle != frozen.handle || current.texture != frozen.texture
            || current.device != frozen.device
#endif
        )
            return "RHI-WEBGPU-ROOT-ATTACHMENT-CORRESPONDENCE";
    }
    if (const auto* id =
            rhi::OriginalBindlessInitializedShaderContract::validateRetained<GeometryContent>(
                *inventory_, budget_.initializedShader, entries_, seal))
        return id;
    if (const auto* id = inspectInitializedGeometry(seal))
        return id;
    std::uint32_t native = 0u, retained = 0u;
    if (const auto* id = inspectOwners(native, retained))
        return id;
    return native == nativeOwners_ && retained == retainedOwners_
        ? nullptr
        : "RHI-WEBGPU-BINDLESS-ROOT-OWNER-CAPACITY";
}
} // namespace arp::rhi_webgpu
