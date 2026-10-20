#include "rhi/backends/metal/metal_bindless_root.h"

#include "core/row_major_index_grid.h"
#include "core/shared_identity_projection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/frontend/bindless_graphics_depth_content.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/transfer_validation.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

namespace arp::rhi_metal {
using namespace rhi;

const char* MetalRhiBindlessRootNativeClosure::captureGeometryEntry(
    std::size_t index, Entry& entry) const noexcept
{
    if (index >= inventory_->sources().size())
        return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-SOURCE";
    const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
        &inventory_->sources()[index]);
    if (!lease || !*lease)
        return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-SOURCE";
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(*lease))
        return id;
    const auto& object = *(*lease)->originalObject();
    if (object.desc().usage != ResourceUsage::VertexBuffer
        && object.desc().usage != ResourceUsage::IndexBuffer)
        return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-USAGE";
    const auto registration = object.originalResourceRegistration();
    if (const auto* id = registration.diagnostic())
        return id;
    if (!registration.matchesIdentity(
            &object, (*lease)->resource().value, OriginalRegisteredResourceKind::Buffer))
        return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-REGISTRATION";
    entry.sourceIndex = index;
    entry.groupIndex = SIZE_MAX;
    entry.resource = (*lease)->resource();
    entry.generation = registration.generation();
    entry.buffer = metalRhiNativePayloadFor(object);
    if (!entry.buffer || !entry.buffer->resource.nativeRuntimeBacked
        || entry.buffer->resource.nativeRuntimeDiagnosticOnly
        || !entry.buffer->resource.allocationReleaseToken
        || !core::SharedOwnerProjection::sameOwner(
            entry.buffer->resource.nativeDevice, pipeline_->nativeDevice))
        return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-PAYLOAD";
    if (const auto* id = validateMetalRhiTrackedNativeBufferObject(
            entry.buffer->resource, object.desc().sizeBytes))
        return id;
    entry.identity = MetalRhiNativeResourceObservation(entry.buffer->resource);
    return nullptr;
}

const std::shared_ptr<const MetalOriginalGeometryContent>&
MetalRhiBindlessRootNativeClosure::initializedGeometry(std::size_t index) const noexcept
{
    for (const auto& entry : entries_)
        if (entry.sourceIndex == index && entry.initializedGeometry)
            return entry.initializedGeometry;
    static const std::shared_ptr<const GeometryContent> empty;
    return empty;
}

const char* MetalRhiBindlessRootNativeClosure::inspectInitializedGeometry(
    const std::shared_ptr<const OriginalCommandRecordingSeal>& seal) const noexcept
{
    std::uint32_t count = 0u;
    std::uint64_t bytes = 0u;
    for (const auto& entry : entries_) {
        if (!entry.initializedGeometry) {
            if (entry.initializedGeometryState)
                return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-STATE";
            continue;
        }
        if (!OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget_)
            || !entry.initializedGeometryState)
            return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-SCHEMA";
        const auto& actor = entry.initializedGeometry;
        if (const auto* id = seal ? GeometryContent::validateOriginalPending(
                                        actor, seal, *entry.initializedGeometryState)
                                  : GeometryContent::validateOriginal(actor))
            return id;
        if (inventory_->originalLeaseIndex(actor->originalLease()) != entry.sourceIndex)
            return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-INVENTORY";
        if (!seal)
            if (const auto* id =
                    actor->originalLease()->originalObject()->validateOriginalCommittedState(
                        *entry.initializedGeometryState))
                return id;
        if (count == budget_.maxInitializedGeometryActors
            || actor->initializedBytes() > budget_.maxInitializedGeometryBytes - bytes)
            return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-CAPACITY";
        ++count;
        bytes += actor->initializedBytes();
    }
    if (!OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget_))
        return count ? "RHI-METAL-BINDLESS-ROOT-GEOMETRY-SCHEMA" : nullptr;
    std::size_t expected = 0u;
    for (std::size_t i = 0u; i < inventory_->sources().size(); ++i)
        if (const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &inventory_->sources()[i])) {
            const auto usage = (*lease)->layout().usage;
            if ((usage & (ResourceUsage::VertexBuffer | ResourceUsage::IndexBuffer))
                != ResourceUsage::None) {
                if (!initializedGeometry(i))
                    return "RHI-METAL-BINDLESS-ROOT-GEOMETRY-COVERAGE";
                ++expected;
            }
        }
    return count && count == expected ? nullptr : "RHI-METAL-BINDLESS-ROOT-GEOMETRY-COVERAGE";
}

MetalRhiBindlessRootNativeClosure::MetalRhiBindlessRootNativeClosure(ConstructionKey,
    std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    Budget budget,
    std::size_t entries)
    : root_(std::move(root)), inventory_(std::move(inventory)), budget_(budget)
{
    entries_.reserve(entries);
    groups_.reserve(root_->ordinaryDomains().size() + 1u);
}

const char* MetalRhiBindlessRootNativeClosure::captureEntry(
    std::size_t index, Entry& entry) const noexcept
{
    const OriginalCommandDescriptorSetElement* declaration = nullptr;
    const OriginalCommandDescriptorDependencies::Owner* owner = nullptr;
    std::size_t groupIndex = 0u;
    const auto& readback = root_->originalSnapshot()->originalBufferSource(
        BindlessSnapshot::OriginalBufferRole::FaultReadback);
    std::shared_ptr<const OriginalCommandBufferLease> lease;
    if (index < root_->elements().size()) {
        declaration = &root_->elements()[index].declaration;
        owner = &root_->elements()[index].owner;
    } else if (readback && index == root_->elements().size())
        lease = readback;
    else {
        auto ordinal = index - root_->elements().size() - (readback ? 1u : 0u);
        for (const auto& domain : root_->ordinaryDomains()) {
            ++groupIndex;
            if (ordinal >= domain.source->frozenElements().size()) {
                ordinal -= domain.source->frozenElements().size();
                continue;
            }
            const auto& element = domain.source->frozenElements()[ordinal];
            declaration = &element.declaration;
            owner = &element.resourceOwner;
            break;
        }
    }
    if (owner) {
        const auto* id = std::visit(
            [&](const auto& source) -> const char* {
                using T = typename std::decay_t<decltype(source)>::element_type;
                entry.sourceIndex = inventory_->originalLeaseIndex(source);
                entry.resource = source->resource();
                entry.generation =
                    source->originalObject()->originalResourceRegistration().generation();
                if constexpr (std::is_same_v<T, const OriginalCommandBufferLease>) {
                    entry.buffer = metalRhiNativePayloadFor(*source->originalObject());
                } else if constexpr (std::is_same_v<T, const OriginalCommandTextureViewSource>) {
                    entry.kind = DescriptorResourceKind::TextureView;
                    const auto& parent = source->originalParentLease();
                    if (inventory_->originalLeaseIndex(parent) == SIZE_MAX)
                        return "RHI-METAL-BINDLESS-ROOT-TEXTURE-PARENT-INVENTORY";
                    entry.parentGeneration =
                        parent->originalObject()->originalResourceRegistration().generation();
                    entry.texture = metalRhiNativePayloadFor(*parent->originalObject());
                    entry.view = metalRhiNativePayloadFor(*source->originalObject());
                    if (!entry.texture || !entry.view)
                        return "RHI-METAL-BINDLESS-ROOT-NATIVE-TEXTURE";
                    const auto& p = parent->originalObject()->desc();
                    const auto& v = source->originalObject()->desc();
                    const auto& texture = *entry.texture;
                    const auto& view = *entry.view;
                    // Source validates the immutable logical range. Native payload
                    // metadata must describe that exact parent and view as well.
                    if (texture.format != p.format || texture.extent.width != p.extent.width
                        || texture.extent.height != p.extent.height
                        || texture.extent.depth != p.extent.depth
                        || texture.mipLevelCount != p.mipLevelCount
                        || texture.arrayLayerCount != p.arrayLayerCount
                        || texture.sampleCount != p.sampleCount
                        || texture.cubeCompatible != p.cubeCompatible || view.format != v.format
                        || view.mipLevelCount != v.mipLevelCount
                        || view.arrayLayerCount != v.arrayLayerCount
                        || view.sampleCount != p.sampleCount
                        || view.extent.width
                            != semantics::TextureTransferValidation::mipExtent(
                                p.extent.width, v.baseMipLevel)
                        || view.extent.height
                            != semantics::TextureTransferValidation::mipExtent(
                                p.extent.height, v.baseMipLevel)
                        || view.extent.depth
                            != semantics::TextureTransferValidation::mipExtent(
                                p.extent.depth, v.baseMipLevel))
                        return "RHI-METAL-BINDLESS-ROOT-NATIVE-TEXTURE-SHAPE";
                    if (!texture.resource.nativeRuntimeBacked
                        || texture.resource.nativeRuntimeDiagnosticOnly
                        || !texture.resource.nativeObject
                        || !texture.resource.allocationReleaseToken
                        || !core::SharedOwnerProjection::sameOwner(
                            texture.resource.nativeDevice, pipeline_->nativeDevice)
                        || !core::SharedOwnerProjection::sameOwner(
                            texture.resource.allocationReleaseToken,
                            view.resource.allocationReleaseToken))
                        return "RHI-METAL-BINDLESS-ROOT-NATIVE-TEXTURE-OWNER";
                    entry.parentIdentity = MetalRhiNativeResourceObservation(texture.resource);
                    entry.heap = texture.aliasHeap;
                } else if constexpr (std::is_same_v<T, const OriginalCommandSamplerLease>) {
                    entry.kind = DescriptorResourceKind::Sampler;
                    entry.sampler = metalRhiNativePayloadFor(*source->originalObject());
                    if (!entry.sampler || !entry.sampler->argumentBufferCompatible)
                        return "RHI-METAL-BINDLESS-ROOT-NATIVE-SAMPLER";
                } else
                    return "RHI-METAL-BINDLESS-ROOT-RESOURCE-PROFILE";
                return nullptr;
            },
            *owner);
        if (id)
            return id;
    } else if (lease) {
        entry.sourceIndex = inventory_->originalLeaseIndex(lease);
        entry.resource = lease->resource();
        entry.generation = lease->originalObject()->originalResourceRegistration().generation();
        entry.buffer = metalRhiNativePayloadFor(*lease->originalObject());
    } else
        return "RHI-METAL-BINDLESS-ROOT-ELEMENT-RANGE";
    if (entry.sourceIndex == SIZE_MAX
        || (declaration && entry.sourceIndex != declaration->sourceIndex))
        return "RHI-METAL-BINDLESS-ROOT-RESOURCE-INVENTORY";
    const auto* native = entry.nativeResource();
    if (!native || !native->nativeRuntimeBacked || native->nativeRuntimeDiagnosticOnly
        || !native->nativeObject
        || (entry.kind != DescriptorResourceKind::Sampler && !native->allocationReleaseToken)
        || !core::SharedOwnerProjection::sameOwner(native->nativeDevice, pipeline_->nativeDevice))
        return "RHI-METAL-BINDLESS-ROOT-NATIVE-RESOURCE";
    entry.identity = MetalRhiNativeResourceObservation(*native);
    if (!declaration)
        return nullptr;
    if (groupIndex >= groups_.size())
        return "RHI-METAL-BINDLESS-ROOT-GROUP-COVERAGE";
    const auto& group = groups_[groupIndex];
    if ((!group.prepared && !group.graphics) || !group.descriptors())
        return "RHI-METAL-BINDLESS-ROOT-GROUP-COVERAGE";
    const auto bindingAt =
        [&](const auto& bindings) -> const MetalRhiNativeArgumentBindingPayload* {
        const MetalRhiNativeArgumentBindingPayload* result = nullptr;
        for (const auto& binding : bindings) {
            if (binding.binding != declaration->write.binding
                || binding.arrayElement != declaration->write.arrayElement)
                continue;
            if (result)
                return nullptr;
            result = &binding;
        }
        return result;
    };
    const auto* frozen = bindingAt(group.descriptors()->bindings);
    entry.groupIndex = groupIndex;
    entry.completedDepth = index < root_->elements().size() || budget_.contractVersion == 16u
        ? root_->originalCompletedDepthAt(index)
        : nullptr;
    if (entry.completedDepth) {
        if ((budget_.contractVersion != 9u && budget_.contractVersion != 13u
                && budget_.contractVersion != 14u && budget_.contractVersion != 16u)
            || ((budget_.contractVersion == 16u) != (groupIndex != 0u))
            || entry.kind != DescriptorResourceKind::TextureView)
            return "RHI-METAL-BINDLESS-ROOT-DEPTH-PROFILE";
        const auto& element = *declaration;
        const auto& source =
            std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(*owner);
        if (const auto* id = OriginalCompletedDepthReadInputContract::inspect(
                {entry.completedDepth, element.write.resourceState, element.stages, element.access},
                source,
                budget_.completedDepthInputs)
                .diagnostic)
            return id;
        bool exact = false;
        for (const auto& proof : group.descriptors()->originalCompletedDepthSources)
            exact = exact
                || (proof.get() == entry.completedDepth.get()
                    && core::SharedOwnerProjection::sameOwner(proof, entry.completedDepth));
        if (!exact)
            return "RHI-METAL-BINDLESS-ROOT-DEPTH-PUBLICATION";
    } else if (entry.kind == DescriptorResourceKind::TextureView && budget_.contractVersion == 9u)
        return "RHI-METAL-BINDLESS-ROOT-DEPTH-PROOF";
    const auto& layout = groupIndex
        ? root_->ordinaryDomains()[groupIndex - 1u].source->originalObject()->layout()
        : root_->originalObject()->layout();
    const DescriptorBindingDesc* shape = nullptr;
    for (const auto& binding : layout.bindings)
        if (binding.binding == declaration->write.binding)
            shape = &binding;
    if (budget_.contractVersion == 16u && shape && groupIndex
        && (shape->depthTexture || shape->comparisonSampler)) {
        if (const auto* id = semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                Backend::Metal, *shape))
            return id;
        const auto& graphics = std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            root_->originalPrepared());
        const auto* selected =
            graphics->owners()->desc().pipeline->bindlessShaderModule(ShaderStage::Fragment, 1u);
        BindlessOrdinaryOpaqueBinding binding;
        if (!selected)
            return "RHI-METAL-BINDLESS-ORDINARY-DEPTH-SELECTED-ENTRY";
        if (const auto* id = Root::originalOrdinaryOpaqueBinding(root_,
                selected->entry,
                root_->ordinaryDomains()[groupIndex - 1u].set,
                shape->binding,
                binding))
            return id;
        if (binding.selectedStage != ShaderStage::Fragment
            || binding.sourceIndex != declaration->sourceIndex
            || binding.resource != declaration->write.resource)
            return "RHI-METAL-BINDLESS-ORDINARY-DEPTH-SELECTED-ENTRY";
        if (shape->depthTexture && !entry.completedDepth)
            return "RHI-METAL-BINDLESS-ROOT-DEPTH-PROOF";
    }
    if ((budget_.contractVersion == 13u || budget_.contractVersion == 14u
            || semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                root_->originalSnapshot()->plan()))
        && shape
        && (shape->type == DescriptorType::SampledTexture
            || shape->type == DescriptorType::StorageTexture)) {
        const auto* source =
            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(owner);
        const bool cell = source
            && std::any_of(group.descriptors()->originalTextureCellSources.begin(),
                group.descriptors()->originalTextureCellSources.end(),
                [&](const auto& retained) {
                    return core::SharedOwnerProjection::sameOwner(retained, *source);
                });
        if (const auto* id = semantics::OriginalBindlessPreparationLowering::imageProofDiagnostic(
                root_->originalSnapshot()->plan(), *shape, bool(entry.completedDepth), cell))
            return id;
    }
    const auto matches = [&](const auto* binding) {
        const auto argument = entry.kind == DescriptorResourceKind::Buffer
            ? MetalRhiArgumentKind::Buffer
            : entry.kind == DescriptorResourceKind::TextureView ? MetalRhiArgumentKind::Texture
                                                                : MetalRhiArgumentKind::Sampler;
        return binding && binding->type == declaration->write.type
            && binding->resourceId == entry.resource && binding->resourceKind == entry.kind
            && binding->argumentKind == argument && !binding->bufferOffsetBytes
            && !binding->bufferOffsetAlignment && !binding->bufferRequiredBytes
            && core::SharedOwnerProjection::sameOwner(binding->buffer, entry.buffer)
            && core::SharedOwnerProjection::sameOwner(binding->textureView, entry.view)
            && core::SharedOwnerProjection::sameOwner(binding->sampler, entry.sampler);
    };
    return matches(frozen) && shape && frozen->stages == shape->stages
            && frozen->arrayCount == shape->arrayCount
        ? nullptr
        : "RHI-METAL-BINDLESS-ROOT-PHYSICAL-RESOURCE";
}

const char* MetalRhiBindlessRootNativeClosure::inspectOwners(
    std::uint32_t& native, std::uint32_t& retained) const noexcept
{
    const core::RowMajorIndexGrid resources(entries_.size(), 10u, SIZE_MAX);
    constexpr std::size_t pipelineSlots = 9u;
    if (!resources.valid() || resources.size() > SIZE_MAX - pipelineSlots)
        return "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY";
    const core::RowMajorIndexGrid groups(
        groups_.size(), 8u, SIZE_MAX - resources.size() - pipelineSlots);
    if (!groups.valid())
        return "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY";
    auto references = resources.size() + groups.size() + pipelineSlots;
    for (const auto& group : groups_) {
        for (std::size_t i = 0u; i < 2u; ++i) {
            const auto* function = group.function(i);
            if (!function)
                continue;
            if (function->arrays.size() > SIZE_MAX - references)
                return "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY";
            references += function->arrays.size();
        }
    }
    const auto projection = core::SharedIdentityProjection::inspect(references,
        references,
        budget_.maxNativeOwners,
        [&](std::size_t index) -> core::SharedIdentityProjection::Record {
            std::shared_ptr<const void> owner;
            if (index < resources.size()) {
                std::uint64_t row = 0u, slot = 0u;
                resources.coordinates(index, row, slot);
                const auto& entry = entries_[row];
                owner = slot < 5u
                    ? MetalRhiResourceOwnerProjection::owners(
                          entry.kind, entry.buffer, {}, entry.view, entry.sampler)[slot]
                    : MetalRhiResourceOwnerProjection::owners(DescriptorResourceKind::TextureView,
                          {},
                          entry.texture,
                          {},
                          {})[slot - 5u];
            } else if ((index -= resources.size()) < groups.size()) {
                std::uint64_t row = 0u, slot = 0u;
                groups.coordinates(index, row, slot);
                const auto& group = groups_[row];
                switch (slot) {
                case 0u:
                    owner = group.prepared;
                    break;
                case 1u:
                    owner = group.descriptors();
                    break;
                case 2u:
                    owner = group.descriptors()->nativeObject;
                    break;
                case 3u:
                    owner = group.graphics;
                    break;
                case 4u:
                    owner = group.frozenStages[0];
                    break;
                case 5u:
                    owner = group.frozenStages[1];
                    break;
                case 6u:
                    owner = group.frozenLayouts[0];
                    break;
                case 7u:
                    owner = group.frozenLayouts[1];
                    break;
                }
            } else if ((index -= groups.size()) < pipelineSlots) {
                switch (index) {
                case 0u:
                    owner = pipeline_;
                    break;
                case 1u:
                    owner = pipeline_->nativeDevice;
                    break;
                case 2u:
                    owner = pipeline_->pipelineState;
                    break;
                case 3u:
                    owner = pipeline_->computeFunction;
                    break;
                case 4u:
                    owner = pipeline_->shaderLibrary;
                    break;
                case 5u:
                    owner = pipeline_->fragmentShaderLibrary;
                    break;
                case 6u:
                    owner = pipeline_->vertexFunction;
                    break;
                case 7u:
                    owner = pipeline_->fragmentFunction;
                    break;
                case 8u:
                    owner = pipeline_->depthStencilState;
                    break;
                }
            } else {
                index -= pipelineSlots;
                for (const auto& group : groups_) {
                    for (std::size_t i = 0u; i < 2u; ++i) {
                        const auto* function = group.function(i);
                        if (!function)
                            continue;
                        if (index < function->arrays.size()) {
                            owner = function->arrays[index].buffer;
                            break;
                        }
                        index -= function->arrays.size();
                    }
                    if (owner)
                        break;
                }
            }
            return owner
                ? core::SharedIdentityProjection::Record{static_cast<std::uint64_t>(
                                                             reinterpret_cast<std::uintptr_t>(
                                                                 owner.get())),
                      1u,
                      owner,
                      owner.get(),
                      1u}
                : core::SharedIdentityProjection::Record{};
        });
    if (projection.status != core::SharedIdentityProjection::Status::Complete
        || projection.identities > UINT32_MAX)
        return "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY";
    native = static_cast<std::uint32_t>(projection.identities);
    if (native > budget_.maxRetainedOwners)
        return "RHI-METAL-BINDLESS-ROOT-OWNER-CAPACITY";
    const core::RowMajorIndexGrid actors(
        entries_.size(), 2u + GeometryContent::exclusiveCpuQuotaSlots, SIZE_MAX);
    if (!actors.valid())
        return "RHI-METAL-BINDLESS-ROOT-OWNER-CAPACITY";
    const auto proofs = core::SharedIdentityProjection::inspect(actors.size(),
        actors.size(),
        budget_.maxRetainedOwners - native,
        [&](std::size_t i) -> core::SharedIdentityProjection::Record {
            std::uint64_t row = 0u, slot = 0u;
            actors.coordinates(i, row, slot);
            std::shared_ptr<const void> owner;
            if (slot == 1u + GeometryContent::exclusiveCpuQuotaSlots)
                owner = entries_[row].completedDepth;
            else if (const auto& actor = entries_[row].initializedActor())
                owner =
                    slot ? std::shared_ptr<const void>(actor->exclusiveCpuQuota(slot - 1u)) : actor;
            return owner
                ? core::SharedIdentityProjection::Record{static_cast<std::uint64_t>(
                                                             reinterpret_cast<std::uintptr_t>(
                                                                 owner.get())),
                      1u,
                      owner,
                      owner.get(),
                      8u}
                : core::SharedIdentityProjection::Record{};
        });
    if (proofs.status != core::SharedIdentityProjection::Status::Complete
        || proofs.identities > UINT32_MAX - 3u
        || inventory_->cpuOwnerCount(3u + static_cast<std::uint32_t>(proofs.identities),
            budget_.maxRetainedOwners - native,
            retained))
        return "RHI-METAL-BINDLESS-ROOT-OWNER-CAPACITY";
    retained += native;
    return nullptr;
}

std::shared_ptr<const MetalRhiBindlessRootNativeClosure>
MetalRhiBindlessRootNativeClosure::prepareOriginal(std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    return prepareOriginal(
        std::move(root), std::move(inventory), device, budget, std::move(quota), diagnostic, {});
}

std::shared_ptr<const MetalRhiBindlessRootNativeClosure>
MetalRhiBindlessRootNativeClosure::prepareOriginal(std::shared_ptr<const Root> root,
    std::shared_ptr<const Sources> inventory,
    GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic,
    GeometryContents contents)
{
    diagnostic = OriginalBindlessRootNativeClosureContract::validateBudget(budget);
    if (diagnostic)
        return {};
    if ((diagnostic = OriginalBindlessRootNativeClosureContract::validateGeometryBackend(
             device.backend(), budget)))
        return {};
    if (budget.contractVersion != 1u && budget.contractVersion != 3u && budget.contractVersion != 9u
        && budget.contractVersion != 10u && budget.contractVersion != 11u
        && budget.contractVersion != 13u && budget.contractVersion != 14u
        && budget.contractVersion != 16u && budget.contractVersion != 21u) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-PROFILE";
        return {};
    }
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-QUOTA";
        return {};
    }
    if ((diagnostic = Root::validateOriginal(root, device))
        || (diagnostic = Sources::validateRegisteredLifetime(inventory, device)))
        return {};
    if (std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            root->originalPrepared())
        && (diagnostic = Root::validateOriginalGraphicsBufferRanges(root)))
        return {};
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
    const auto& plan = root->originalSnapshot()->plan();
    const bool depth = semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 6u)
        || OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 6u);
    if ((budget.contractVersion == 9u) != depth || (depth && !root->ordinaryDomains().empty())) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-DEPTH-PROFILE";
        return {};
    }
    const auto lowering = semantics::OriginalBindlessRootNativeClosureLowering::lowering(
        device.backend(), budget.contractVersion);
    if (device.backend() != Backend::Metal || !lowering.nativeCorrespondenceAvailable
        || lowering.diagnostic) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-BACKEND";
        return {};
    }
    if ((diagnostic = semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(
             device.backend(), root->sourceContractVersion())))
        return {};
    if (!root->matchesCanonicalSources(*inventory)
        || inventory->originalLeaseIndex(root) == SIZE_MAX) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-INVENTORY";
        return {};
    }
    if (core::SharedOwnerProjection::sameControlBlock(quota, inventory->originalMetadataQuota())) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-QUOTA-DOMAIN";
        return {};
    }
    const auto* prepared = std::get_if<std::shared_ptr<const BindlessPreparedComputeBinding>>(
        &root->originalPrepared());
    const auto* graphics = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
        &root->originalPrepared());
    if ((!prepared && !graphics) || (prepared && !*prepared) || (graphics && !*graphics)
        || root->originalSnapshot()->plan().execution != BindlessExecution::Native) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-PREPARED-PROFILE";
        return {};
    }
    if (budget.contractVersion == 16u)
        for (const auto& element : root->elements())
            if (element.declaration.write.type != DescriptorType::UniformBuffer
                && element.declaration.write.type != DescriptorType::ReadOnlyStorageBuffer
                && element.declaration.write.type != DescriptorType::StorageBuffer) {
                diagnostic = "RHI-METAL-BINDLESS-ROOT-ORDINARY-DEPTH-INDEXED-PROFILE";
                return {};
            }
    if ((diagnostic = rhi::OriginalBindlessInitializedShaderContract::validateInputs(*root,
             *inventory,
             device.backend(),
             budget.initializedShader,
             contents.shaderData,
             contents.shaderCount)))
        return {};
    if (OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget)
            != bool(contents.count)
        || (contents.count && !contents.data)
        || contents.count > budget.maxInitializedGeometryActors) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-GEOMETRY-SCHEMA";
        return {};
    }
    std::uint64_t initializedBytes = 0u;
    for (std::size_t i = 0u; i < contents.count; ++i) {
        const auto& actor = contents.data[i];
        if ((diagnostic = GeometryContent::validateOriginal(actor)))
            return {};
        if (inventory->originalLeaseIndex(actor->originalLease()) == SIZE_MAX) {
            diagnostic = "RHI-METAL-BINDLESS-ROOT-GEOMETRY-INVENTORY";
            return {};
        }
        for (std::size_t j = 0u; j < i; ++j)
            if (core::SharedOwnerProjection::sameOwner(
                    actor->originalLease(), contents.data[j]->originalLease())) {
                diagnostic = "RHI-METAL-BINDLESS-ROOT-GEOMETRY-DUPLICATE";
                return {};
            }
        for (std::size_t slot = 0u; slot < GeometryContent::exclusiveCpuQuotaSlots; ++slot)
            if (core::SharedOwnerProjection::sameControlBlock(
                    quota, actor->exclusiveCpuQuota(slot))) {
                diagnostic = "RHI-METAL-BINDLESS-ROOT-GEOMETRY-QUOTA-DOMAIN";
                return {};
            }
        if (actor->initializedBytes() > budget.maxInitializedGeometryBytes - initializedBytes) {
            diagnostic = "RHI-METAL-BINDLESS-ROOT-GEOMETRY-BYTE-CAPACITY";
            return {};
        }
        initializedBytes += actor->initializedBytes();
    }
    auto count = root->elements().size();
    if (root->originalSnapshot()->originalBufferSource(
            BindlessSnapshot::OriginalBufferRole::FaultReadback))
        ++count;
    if (count > budget.maxResourceEntries) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
        return {};
    }
    for (const auto& domain : root->ordinaryDomains()) {
        if (domain.source->frozenElements().size() > budget.maxResourceEntries - count) {
            diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
            return {};
        }
        count += domain.source->frozenElements().size();
    }
    if (contents.count > budget.maxResourceEntries - count) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<MetalRhiBindlessRootNativeClosure>(ConstructionKey{},
            std::move(root),
            std::move(inventory),
            budget,
            count + contents.count);
        result->pipeline_ = prepared
            ? metalRhiNativePayloadFor(*(*prepared)->owners()->desc().pipeline)
            : metalRhiNativePayloadFor(*(*graphics)->owners()->desc().pipeline);
        Group primary;
        primary.set = result->root_->originalSnapshot()->setIndex();
        if (prepared)
            primary.prepared = metalRhiPreparedComputePayloadFor(**prepared);
        else
            primary.graphics = metalRhiPreparedGraphicsPayloadFor(**graphics);
        if (!result->pipeline_ || !result->pipeline_->nativeRuntimeBacked
            || !result->pipeline_->nativeDevice || !result->pipeline_->pipelineState
            || (prepared
                && (!primary.prepared || !result->pipeline_->computeFunction
                    || !primary.prepared->sharedOwnerMatches(primary.prepared)
                    || !core::SharedOwnerProjection::sameOwner(
                        primary.prepared->pipeline, result->pipeline_)))
            || (graphics
                && (!primary.graphics || !result->pipeline_->vertexFunction
                    || !primary.graphics->sharedOwnerMatches(primary.graphics)
                    || !core::SharedOwnerProjection::sameOwner(
                        primary.graphics->pipeline, result->pipeline_)))) {
            diagnostic = "RHI-METAL-BINDLESS-ROOT-PREPARED-PAYLOAD";
            return {};
        }
        primary.frozenDescriptors =
            prepared ? primary.prepared->descriptors : primary.graphics->descriptors;
        if (graphics) {
            primary.frozenStages = primary.graphics->stages;
            primary.frozenLayouts = primary.graphics->layouts;
        }
        result->pipelineIdentity_ = MetalRhiNativeResourceObservation(
            result->pipeline_->nativeDevice, result->pipeline_->pipelineState);
        const std::array functions{result->pipeline_->computeFunction,
            result->pipeline_->vertexFunction,
            result->pipeline_->fragmentFunction};
        for (std::size_t i = 0u; i < functions.size(); ++i) {
            result->functionPresent_[i] = bool(functions[i]);
            result->functionIdentities_[i] =
                MetalRhiNativeResourceObservation(result->pipeline_->nativeDevice, functions[i]);
        }
        result->groups_.push_back(std::move(primary));
        core::pmr::String message;
        for (const auto& domain : result->root_->ordinaryDomains()) {
            const bool ordinaryImages = graphics
                && std::any_of(domain.source->originalObject()->layout().bindings.begin(),
                    domain.source->originalObject()->layout().bindings.end(),
                    [](const auto& binding) {
                        return binding.type == DescriptorType::SampledTexture;
                    });
            auto live = ordinaryImages
                ? (budget.contractVersion == 16u ? prepareMetalRhiOriginalOrdinaryDepthDescriptors(
                                                       result->root_, domain.set)
                                                 : prepareMetalRhiOriginalOrdinaryColorDescriptors(
                                                       result->root_, domain.set))
                : metalRhiNativePayloadFor(*domain.source->originalObject());
            Group group;
            group.set = domain.set;
            if (graphics) {
                for (const auto& binding : domain.source->originalObject()->layout().bindings)
                    if (binding.arrayCount != 1u || binding.dynamicOffset || binding.tableBacked
                        || binding.stages == ShaderStage::None
                        || (binding.stages & (ShaderStage::Vertex | ShaderStage::Fragment))
                            != binding.stages
                        || (binding.type != DescriptorType::UniformBuffer
                            && binding.type != DescriptorType::ReadOnlyStorageBuffer
                            && (binding.type != DescriptorType::StorageBuffer
                                || binding.stages != ShaderStage::Fragment)
                            && !(budget.contractVersion == 16u
                                && !semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::
                                    inspect(Backend::Metal, binding))
                            && !semantics::OriginalBindlessOrdinaryColorDomain::
                                acceptsGraphicsBinding(binding))) {
                        diagnostic = "RHI-METAL-BINDLESS-ROOT-ORDINARY-SCALAR-PROFILE";
                        return {};
                    }
                // The same sealed per-entry mappings, with zero argument-storage
                // capacity: ordinary scalar buffer/image/sampler domains cannot
                // create GPU descriptor arrays. Completed color uses only its
                // typed original exporter; ordinary nativePayload stays closed.
                group.graphics = prepareMetalRhiNativeGraphicsBindings(result->pipeline_,
                    live,
                    domain.set,
                    result->groups_.front().frozenLayouts,
                    {},
                    message);
                if (group.graphics) {
                    group.frozenDescriptors = group.graphics->descriptors;
                    group.frozenStages = group.graphics->stages;
                    group.frozenLayouts = group.graphics->layouts;
                }
            } else {
                group.prepared = prepareMetalRhiNativeScalarComputeBindings(
                    result->pipeline_, live, domain.set, message);
                if (group.prepared)
                    group.frozenDescriptors = group.prepared->descriptors;
            }
            if (!group.descriptors()) {
                diagnostic = "RHI-METAL-BINDLESS-ROOT-ORDINARY-PAYLOAD";
                return {};
            }
            result->groups_.push_back(std::move(group));
        }
        std::uint64_t shaderVisits = 0u;
        for (std::size_t index = 0u; index < count; ++index) {
            Entry entry;
            if ((diagnostic = result->captureEntry(index, entry)))
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
                diagnostic = "RHI-METAL-BINDLESS-ROOT-GEOMETRY-STATE";
                return {};
            }
            entry.initializedGeometryState.emplace(*state);
            result->entries_.push_back(std::move(entry));
        }
        if ((diagnostic = result->inspectInitializedGeometry({})))
            return {};
        if ((diagnostic = result->inspectOwners(result->nativeOwners_, result->retainedOwners_)))
            return {};
        if ((depth || budget.contractVersion == 13u || budget.contractVersion == 14u
                || budget.contractVersion == 16u)
            && std::none_of(
                result->entries_.begin(), result->entries_.end(), [](const auto& entry) {
                    return bool(entry.completedDepth);
                })) {
            diagnostic = "RHI-METAL-BINDLESS-ROOT-DEPTH-EMPTY";
            return {};
        }
        auto shared = shareOriginal(std::move(result));
        if ((diagnostic = shared->validate()))
            return {};
        return shared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-METAL-BINDLESS-ROOT-NATIVE-ALLOCATION";
    }
    return {};
}

const char* MetalRhiBindlessRootNativeClosure::validateOriginal(
    const std::shared_ptr<const MetalRhiBindlessRootNativeClosure>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER"
                                                          : self->validate();
}

const char* MetalRhiBindlessRootNativeClosure::validate() const noexcept
{
    return validatePhase({});
}

const char* MetalRhiBindlessRootNativeClosure::validateOriginalPending(
    const std::shared_ptr<const MetalRhiBindlessRootNativeClosure>& self,
    const std::shared_ptr<const OriginalCommandRecordingSeal>& seal) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self) || !seal
        || !seal->hasQuotaBoundSharedOwner(seal))
        return "RHI-METAL-BINDLESS-ROOT-PENDING-OWNER";
    return self->validatePhase(seal);
}

const char* MetalRhiBindlessRootNativeClosure::validatePhase(
    const std::shared_ptr<const OriginalCommandRecordingSeal>& seal) const noexcept
{
    if (const auto* id = Root::validateOriginal(root_))
        return id;
    if (std::holds_alternative<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
            root_->originalPrepared()))
        if (const auto* id = Root::validateOriginalGraphicsBufferRanges(root_))
            return id;
    if (const auto* id = OriginalBindlessRootNativeClosureContract::producedRootDiagnostic(
            *root_, budget_.contractVersion))
        return id;
    if (const auto* id = OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
            root_->originalSnapshot()->plan(), budget_.contractVersion))
        return id;
    if (const auto* id = OriginalBindlessRootNativeClosureContract::mixedRootDiagnostic(
            *root_, budget_.contractVersion))
        return id;
    if (const auto* id = Sources::validateOriginal(inventory_))
        return id;
    if (!root_->matchesCanonicalSources(*inventory_)
        || inventory_->originalLeaseIndex(root_) == SIZE_MAX)
        return "RHI-METAL-BINDLESS-ROOT-NATIVE-INVENTORY";
    const auto* prepared = std::get_if<std::shared_ptr<const BindlessPreparedComputeBinding>>(
        &root_->originalPrepared());
    const auto* graphics = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
        &root_->originalPrepared());
    if ((!prepared && !graphics) || (prepared && !*prepared) || (graphics && !*graphics)
        || groups_.size() != root_->ordinaryDomains().size() + 1u || !pipeline_
        || !core::SharedOwnerProjection::sameOwner(pipeline_,
            prepared ? metalRhiNativePayloadFor(*(*prepared)->owners()->desc().pipeline)
                     : metalRhiNativePayloadFor(*(*graphics)->owners()->desc().pipeline))
        || !pipelineIdentity_.matches(pipeline_->nativeDevice, pipeline_->pipelineState))
        return "RHI-METAL-BINDLESS-ROOT-NATIVE-PIPELINE";
    const std::array functions{
        pipeline_->computeFunction, pipeline_->vertexFunction, pipeline_->fragmentFunction};
    for (std::size_t i = 0u; i < functions.size(); ++i)
        if (functionPresent_[i] != bool(functions[i])
            || (functionPresent_[i]
                && !functionIdentities_[i].matches(pipeline_->nativeDevice, functions[i])))
            return "RHI-METAL-BINDLESS-ROOT-NATIVE-PIPELINE";
    for (std::size_t index = 0u; index < entries_.size(); ++index) {
        Entry current;
        const auto& frozen = entries_[index];
        if (const auto* id = frozen.initializedGeometry
                ? captureGeometryEntry(frozen.sourceIndex, current)
                : captureEntry(index, current))
            return id;
        if (frozen.sourceIndex != current.sourceIndex || frozen.groupIndex != current.groupIndex
            || frozen.resource != current.resource || frozen.generation != current.generation
            || frozen.kind != current.kind || frozen.parentGeneration != current.parentGeneration
            || !core::SharedOwnerProjection::sameOwner(frozen.buffer, current.buffer)
            || !core::SharedOwnerProjection::sameOwner(frozen.texture, current.texture)
            || !core::SharedOwnerProjection::sameOwner(frozen.heap, current.heap)
            || !core::SharedOwnerProjection::sameOwner(frozen.view, current.view)
            || !core::SharedOwnerProjection::sameOwner(frozen.sampler, current.sampler)
            || frozen.completedDepth.get() != current.completedDepth.get()
            || !core::SharedOwnerProjection::sameOwner(
                frozen.completedDepth, current.completedDepth)
            || !frozen.identity.matches(*current.nativeResource())
            || (current.texture && !frozen.parentIdentity.matches(current.texture->resource)))
            return "RHI-METAL-BINDLESS-ROOT-NATIVE-RESOURCE-IDENTITY";
    }
    for (std::size_t index = 0u; index < groups_.size(); ++index) {
        const auto& group = groups_[index];
        const auto set = index ? root_->ordinaryDomains()[index - 1u].set
                               : root_->originalSnapshot()->setIndex();
        if ((!group.prepared && !group.graphics) || (group.prepared && group.graphics)
            || !group.descriptors())
            return "RHI-METAL-BINDLESS-ROOT-NATIVE-GROUP";
        if (!core::SharedOwnerProjection::sameOwner(group.descriptors(),
                group.prepared ? group.prepared->descriptors : group.graphics->descriptors))
            return "RHI-METAL-BINDLESS-ROOT-NATIVE-DESCRIPTOR-IDENTITY";
        if (!index
            && (budget_.contractVersion == 10u || budget_.contractVersion == 11u
                || budget_.contractVersion == 13u || budget_.contractVersion == 14u)) {
            std::size_t textures = 0u;
            for (const auto& element : root_->elements())
                if (const auto* source =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &element.owner)) {
                    const auto* shape = findDescriptorSetBinding(
                        root_->originalObject()->layout(), element.declaration.write.binding);
                    if ((budget_.contractVersion == 13u || budget_.contractVersion == 14u) && shape
                        && shape->depthTexture)
                        continue;
                    ++textures;
                    if (std::none_of(group.descriptors()->originalTextureCellSources.begin(),
                            group.descriptors()->originalTextureCellSources.end(),
                            [&](const auto& frozen) {
                                return core::SharedOwnerProjection::sameOwner(frozen, *source);
                            }))
                        return "RHI-METAL-BINDLESS-ROOT-STORAGE-PUBLICATION";
                }
            if (textures != group.descriptors()->originalTextureCellSources.size())
                return "RHI-METAL-BINDLESS-ROOT-STORAGE-PUBLICATION";
        }
        for (const auto& source : group.descriptors()->originalCompletedTextureSources) {
            bool exact = false;
            if (!index && (prepared || graphics))
                for (const auto& element : root_->elements())
                    if (const auto* owner =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &element.owner))
                        exact = exact || core::SharedOwnerProjection::sameOwner(source, *owner);
            if (index && graphics)
                for (const auto& element :
                    root_->ordinaryDomains()[index - 1u].source->frozenElements())
                    if (const auto* owner =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &element.resourceOwner))
                        exact = exact || core::SharedOwnerProjection::sameOwner(source, *owner);
            if (!exact)
                return "RHI-METAL-BINDLESS-ROOT-COMPLETED-PREPARED-OWNER";
        }
        if (index && graphics) {
            std::size_t images = 0u;
            for (const auto& element :
                root_->ordinaryDomains()[index - 1u].source->frozenElements()) {
                if (element.declaration.write.type != DescriptorType::SampledTexture)
                    continue;
                const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.resourceOwner);
                if (!source || !*source)
                    return "RHI-METAL-BINDLESS-ROOT-ORDINARY-COLOR-SOURCE";
                const auto& parent = *(*source)->originalParentLease()->originalObject();
                const auto content = parent.observeOriginalContent();
                const auto* imageDiagnostic = budget_.contractVersion == 16u
                        && parent.desc().format == TextureFormat::D32Float
                    ? OriginalBindlessGraphicsDepthContentPlanContract::inspectDepthRead(
                          parent.desc(),
                          (*source)->originalObject()->desc(),
                          element.declaration.write.resourceState,
                          element.declaration.stages,
                          element.declaration.access)
                    : (!content ? "RHI-METAL-BINDLESS-ROOT-ORDINARY-COLOR-CONTENT"
                                : semantics::OriginalBindlessOrdinaryColorDomain::
                                      completedViewDiagnostic(parent.desc(),
                                          (*source)->originalObject()->desc(),
                                          content->controlVersion(),
                                          2u));
                if (imageDiagnostic
                    || std::none_of(group.descriptors()->originalCompletedTextureSources.begin(),
                        group.descriptors()->originalCompletedTextureSources.end(),
                        [&](const auto& retained) {
                            return core::SharedOwnerProjection::sameOwner(retained, *source);
                        }))
                    return "RHI-METAL-BINDLESS-ROOT-ORDINARY-COLOR-PUBLICATION";
                ++images;
            }
            if (images != group.descriptors()->originalCompletedTextureSources.size())
                return "RHI-METAL-BINDLESS-ROOT-ORDINARY-COLOR-COVERAGE";
        }
        std::size_t depthCount = 0u;
        for (const auto& entry : entries_)
            if (entry.completedDepth && entry.groupIndex == index)
                ++depthCount;
        if (depthCount != group.descriptors()->originalCompletedDepthSources.size()
            || (index && budget_.contractVersion != 16u
                && !group.descriptors()->originalCompletedDepthSources.empty()))
            return "RHI-METAL-BINDLESS-ROOT-DEPTH-COVERAGE";
        for (const auto& proof : group.descriptors()->originalCompletedDepthSources) {
            if (const auto* id = OriginalCompletedDepthTextureSource::validateOriginal(proof))
                return id;
            bool exact = false;
            for (const auto& entry : entries_)
                exact = exact
                    || (entry.groupIndex == index && entry.completedDepth.get() == proof.get()
                        && core::SharedOwnerProjection::sameOwner(entry.completedDepth, proof));
            if (!exact)
                return "RHI-METAL-BINDLESS-ROOT-DEPTH-PUBLICATION";
        }
        if (group.graphics)
            for (std::size_t i = 0u; i < 2u; ++i)
                if (!core::SharedOwnerProjection::sameOwner(
                        group.frozenStages[i], group.graphics->stages[i])
                    || !core::SharedOwnerProjection::sameOwner(
                        group.frozenLayouts[i], group.graphics->layouts[i]))
                    return "RHI-METAL-BINDLESS-ROOT-NATIVE-STAGE-IDENTITY";
        if (group.set != set || (group.prepared ? group.prepared->set : group.graphics->set) != set)
            return "RHI-METAL-BINDLESS-ROOT-NATIVE-SET";
        if (!core::SharedOwnerProjection::sameOwner(
                group.prepared ? group.prepared->pipeline : group.graphics->pipeline, pipeline_))
            return "RHI-METAL-BINDLESS-ROOT-NATIVE-PREPARED-PIPELINE";
        if (group.prepared
            && !core::SharedOwnerProjection::sameOwner(
                group.prepared->function, pipeline_->computeFunction))
            return "RHI-METAL-BINDLESS-ROOT-NATIVE-FUNCTION";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            core::pmr::String message;
            if (group.prepared
                    ? !validateMetalRhiNativePreparedComputeBindings(group.prepared, message)
                    : !validateMetalRhiNativeGraphicsBindings(group.graphics, message))
                return "RHI-METAL-BINDLESS-ROOT-PREPARED-CORRESPONDENCE";
        } catch (const std::bad_alloc&) {
            return "RHI-METAL-BINDLESS-ROOT-PREPARED-CORRESPONDENCE";
        } catch (const std::length_error&) {
            return "RHI-METAL-BINDLESS-ROOT-PREPARED-CORRESPONDENCE";
        }
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
        : "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER-REVISION";
}

const char* MetalRhiBindlessRootNativeClosure::storageExecutionDiagnostic() const noexcept
{
    if (const auto* id = validate())
        return id;
    return (budget_.contractVersion == 11u || budget_.contractVersion == 14u)
            && root_->ordinaryDomains().empty()
            && rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                root_->originalSnapshot()->plan(), budget_.contractVersion == 14u ? 11u : 9u)
            && root_->originalShaderEntryAccessVersion() == 1u && groups_.size() == 1u
            && groups_.front().prepared
            && !groups_.front().descriptors()->originalTextureCellSources.empty()
        ? nullptr
        : "RHI-METAL-ROOT-STORAGE-EXECUTION-PROFILE";
}

const char* MetalRhiBindlessRootNativeClosure::bufferExecutionDiagnostic() const noexcept
{
    for (const auto& entry : entries_)
        if (entry.kind != DescriptorResourceKind::Buffer)
            return "RHI-METAL-BINDLESS-ROOT-IMAGE-EXECUTION-UNAVAILABLE";
    return nullptr;
}

const char* MetalRhiBindlessRootNativeClosure::completedDepthExecutionDiagnostic() const noexcept
{
    if (budget_.contractVersion != 9u || !root_->ordinaryDomains().empty()
        || !OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
            root_->originalSnapshot()->plan(), 6u))
        return "RHI-METAL-BINDLESS-ROOT-DEPTH-EXECUTION-PROFILE";
    std::size_t sampled = 0u;
    for (const auto& entry : entries_) {
        if (entry.kind == DescriptorResourceKind::TextureView) {
            if (!entry.completedDepth)
                return "RHI-METAL-BINDLESS-ROOT-DEPTH-PROOF";
            ++sampled;
        }
        if (entry.sampler && entry.sampler->comparisonEnabled)
            return "RHI-METAL-BINDLESS-ROOT-DEPTH-COMPARISON-SAMPLER";
    }
    return sampled ? nullptr : "RHI-METAL-BINDLESS-ROOT-DEPTH-EMPTY";
}

const char* MetalRhiBindlessRootNativeClosure::completedColorExecutionDiagnostic(
    OriginalBindlessCompletedImageScope scope) const noexcept
{
    if (!OriginalBindlessCompletedImageDomain::acceptsPlan(root_->originalSnapshot()->plan(), scope)
        || groups_.empty()
        || groups_.front().descriptors()->originalCompletedTextureSources.empty())
        return "RHI-METAL-BINDLESS-ROOT-COMPLETED-COLOR-PROFILE";
    std::size_t sampled = 0u;
    for (const auto& element : root_->elements()) {
        const auto type = element.declaration.write.type;
        if (type == DescriptorType::SampledTexture) {
            const auto* source =
                std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                    &element.owner);
            if (!source || !*source)
                return "RHI-METAL-BINDLESS-ROOT-COMPLETED-COLOR-SOURCE";
            const auto& parent = *(*source)->originalParentLease()->originalObject();
            const auto& view = (*source)->originalObject()->desc();
            const auto content = parent.observeOriginalContent();
            if (!content || content->controlVersion() != 3u
                || parent.desc().format != TextureFormat::Rgba8Unorm
                || (view.dimension != TextureViewDimension::Texture2D
                    && view.dimension != TextureViewDimension::Texture2DArray)
                || view.baseMipLevel || view.baseArrayLayer
                || view.mipLevelCount != parent.desc().mipLevelCount
                || view.arrayLayerCount != parent.desc().arrayLayerCount)
                return "RHI-METAL-BINDLESS-ROOT-COMPLETED-COLOR-VIEW";
            ++sampled;
        } else if (type != DescriptorType::Sampler && type != DescriptorType::UniformBuffer
            && type != DescriptorType::ReadOnlyStorageBuffer
            && type != DescriptorType::StorageBuffer)
            return "RHI-METAL-BINDLESS-ROOT-COMPLETED-COLOR-TYPE";
    }
    return sampled ? nullptr : "RHI-METAL-BINDLESS-ROOT-COMPLETED-COLOR-EMPTY";
}

const char*
MetalRhiBindlessRootNativeClosure::ordinaryColorGraphicsExecutionDiagnostic() const noexcept
{
    if (const auto* id = validate())
        return id;
    if (budget_.contractVersion != 1u || root_->sourceContractVersion() != 2u
        || !std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            root_->originalPrepared())
        || groups_.empty()
        || !groups_.front().descriptors()->originalCompletedTextureSources.empty()
        || !OriginalBindlessCompletedImageDomain::acceptsPlan(root_->originalSnapshot()->plan(),
            OriginalBindlessCompletedImageScope::SampledGraphics1))
        return "RHI-METAL-BINDLESS-ROOT-ORDINARY-COLOR-GRAPHICS-PROFILE";
    // Keep the original indexed image domain separate. All images in this
    // profile belong to exact ordinary domains; indexed resources remain buffers.
    for (const auto& element : root_->elements())
        if (element.declaration.write.type != DescriptorType::UniformBuffer
            && element.declaration.write.type != DescriptorType::ReadOnlyStorageBuffer
            && element.declaration.write.type != DescriptorType::StorageBuffer)
            return "RHI-METAL-BINDLESS-ROOT-ORDINARY-COLOR-INDEXED-PROFILE";
    for (std::size_t i = 1u; i < groups_.size(); ++i)
        if (!groups_[i].descriptors()->originalCompletedTextureSources.empty())
            return nullptr;
    return "RHI-METAL-BINDLESS-ROOT-ORDINARY-COLOR-EMPTY";
}

const char*
MetalRhiBindlessRootNativeClosure::ordinaryDepthGraphicsExecutionDiagnostic() const noexcept
{
    if (const auto* id = validate())
        return id;
    if (budget_.contractVersion != 16u)
        return "RHI-METAL-BINDLESS-ROOT-ORDINARY-DEPTH-GRAPHICS-PROFILE";
    return OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthRootDiagnostic(
        *root_, budget_.contractVersion);
}
} // namespace arp::rhi_metal
