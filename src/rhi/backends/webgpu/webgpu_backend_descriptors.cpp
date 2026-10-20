#include "core/cpu_allocator.h"
#include "core/lifetime_epoch.h"
#include "core/mutation_journal.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_completed_texture_publication.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_ordinary_descriptor_observation.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_state_copy.h"
#include "rhi/frontend/descriptor_table_publication.h"
#include "rhi/frontend/descriptor_whole_buffer_binding.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace arp::rhi_webgpu {

namespace {

bool hasResourceUsage(rhi::ResourceUsage usage, rhi::ResourceUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

const rhi::DescriptorBindingDesc* findBindingInLayout(
    const rhi::DescriptorLayoutDesc& layout, std::uint32_t binding)
{
    for (const auto& candidate : layout.bindings) {
        if (candidate.binding == binding) {
            return &candidate;
        }
    }
    return nullptr;
}

rhi::ValidationResult validateRegisteredResource(const WebGpuRhiDescriptorResourceFacts* resource,
    rhi::DescriptorType type,
    rhi::DescriptorResourceKind kind,
    bool textureViewDimensionConstrained = false,
    rhi::TextureViewDimension textureViewDimension = rhi::TextureViewDimension::Texture2D,
    bool depthTexture = false)
{
    if (!resource) {
        return {
            false, "RHI-DESCRIPTOR-RESOURCE-NOT-LIVE: WebGPU RHI descriptor resource is unknown"};
    }
    // Descriptor arenas only accept the physical resource type emitted by an
    // explicit code-level lowering ticket. They never infer that ticket from
    // a descriptor name or resource kind.
    if (type == rhi::DescriptorType::SampledTexture
        || type == rhi::DescriptorType::StorageTexture) {
        if (resource->textureContentExclusive)
            return {false, "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE"};
        const auto parent = rhi::validateDescriptorTextureParent(resource->parentTextureLive);
        if (!parent.success)
            return parent;
    }
    switch (type) {
    case rhi::DescriptorType::UniformBuffer:
        if (kind != rhi::DescriptorResourceKind::Buffer
            || resource->kind != rhi::DescriptorResourceKind::Buffer
            || !hasResourceUsage(resource->bufferUsage, rhi::ResourceUsage::UniformBuffer)) {
            return {false, "WebGPU RHI descriptor buffer usage mismatch"};
        }
        break;
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        if (kind != rhi::DescriptorResourceKind::Buffer
            || resource->kind != rhi::DescriptorResourceKind::Buffer
            || !hasResourceUsage(resource->bufferUsage, rhi::ResourceUsage::StorageBuffer)) {
            return {false, "WebGPU RHI descriptor buffer usage mismatch"};
        }
        if (type == rhi::DescriptorType::StorageBuffer && resource->gpuWritesSealed)
            return {false, "RHI-BUFFER-GPU-WRITES-SEALED"};
        break;
    case rhi::DescriptorType::SampledTexture:
        if (kind != rhi::DescriptorResourceKind::TextureView
            || resource->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(resource->textureUsage, rhi::TextureUsage::Sampled)) {
            return {false, "WebGPU RHI descriptor texture usage mismatch"};
        }
        if (textureViewDimensionConstrained
            && resource->textureViewDimension != textureViewDimension) {
            return {false, "WebGPU RHI descriptor texture view dimension mismatch"};
        }
        return rhi::validateSampledDepthView(
            depthTexture, resource->textureFormat, resource->textureViewAspect);
    case rhi::DescriptorType::StorageTexture:
        if (kind != rhi::DescriptorResourceKind::TextureView
            || resource->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(resource->textureUsage, rhi::TextureUsage::Storage)) {
            return {false, "WebGPU RHI descriptor texture usage mismatch"};
        }
        if (textureViewDimensionConstrained
            && resource->textureViewDimension != textureViewDimension) {
            return {false, "WebGPU RHI descriptor texture view dimension mismatch"};
        }
        break;
    case rhi::DescriptorType::Sampler:
        if (kind != rhi::DescriptorResourceKind::Sampler
            || resource->kind != rhi::DescriptorResourceKind::Sampler) {
            return {false, "WebGPU RHI descriptor sampler mismatch"};
        }
        break;
    case rhi::DescriptorType::InputAttachment:
        return {false, "WebGPU RHI logical InputAttachment requires a physical lowering ticket"};
    }
    return {true, ""};
}

} // namespace

class WebGpuRhiDescriptorSet final : public rhi::DescriptorSet {
public:
    WebGpuRhiDescriptorSet(rhi::DescriptorSetAllocation allocation,
        rhi::DescriptorLayoutDesc layout,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
        const core::LifetimeEpoch& generation)
        : allocation_(std::move(allocation)), layout_(std::move(layout)),
          registry_(std::move(registry)), nativePayloadPolicy_(nativePayloadPolicy),
          generation_(generation.ticket())
    {
        // Canonicalize the backend-owned copy so dynamic offsets are always
        // ordered by binding number, independent of source declaration order.
        std::sort(
            layout_.bindings.begin(), layout_.bindings.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.binding < rhs.binding;
            });
    }

    const rhi::DescriptorSetAllocation& allocation() const override { return allocation_; }

    const rhi::DescriptorLayoutDesc& layout() const override { return layout_; }

    std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>
    originalCompletedDepthPublication(
        std::uint32_t binding, std::uint32_t element) const noexcept override
    {
        if (!isCurrentAllocation() || !nativePayload_)
            return {};
        for (const auto& publication : nativePayload_->completedTexturePublications)
            if (publication.write.binding == binding && publication.write.arrayElement == element)
                return publication.depthContent;
        return {};
    }

    rhi::ValidationResult writeImpl(const rhi::DescriptorBindingWrite& write) override
    {
        return publishWrite(write, nullptr);
    }

    rhi::ValidationResult writeOriginalBufferRangeImpl(const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& source) override
    {
        const auto native = source ? webGpuNativePayloadFor(*source->originalObject()) : nullptr;
        const auto registered = registry_ ? registry_->find(write.resource) : std::nullopt;
        if (!native || !registered || !registered->buffer
            || !core::SharedOwnerProjection::sameOwner(native, registered->buffer)
            || !webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost())
            return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-NATIVE-IDENTITY"};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto limits = webGpuNativeDeviceLimits();
        rhi::DescriptorBufferBindingRangeContract::Resolved range;
        if (!native->buffer || !native->ownerDevice
            || wgpuBufferGetSize(native->buffer) != source->layout().sizeBytes)
            return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-NATIVE-IDENTITY"};
        if (const auto* id =
                rhi::OriginalDescriptorBufferBindingContract::validateFiniteInterval(write.type,
                    write.bufferBindingRange,
                    native->sizeBytes,
                    0u,
                    1u,
                    {1u,
                        limits.uniformBufferOffsetAlignment,
                        limits.storageBufferOffsetAlignment,
                        limits.maxUniformBufferBindingSize,
                        limits.maxStorageBufferBindingSize},
                    range))
            return {false, id};
        return publishWrite(write, &source);
#else
        return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-NATIVE-UNAVAILABLE"};
#endif
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentExpectation& content) override
    {
        return writeOriginalCompletedTextureImpl(write,
            source,
            content,
            {2u, true, 1u, 1u, rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount});
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentExpectation& content,
        const rhi::OriginalCompletedTextureDescriptorRequest& request) override
    {
        WebGpuCompletedTexturePublication publication{write, source, content};
        publication.colorPublicationVersion = request.contractVersion;
        try {
            return publishWrite(write, nullptr, &publication);
        } catch (const std::bad_alloc&) {
            // Keep the error itself allocation-free on wasm32 libc++, whose
            // inline string storage is smaller than the native host's storage.
            // The exhausted-admission browser fixture guards this boundary.
            return {false, "RHI-OOM"};
        }
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& content) override
    {
        const WebGpuCompletedTexturePublication publication{
            write, content->originalViewSource(), {}, content};
        try {
            return publishWrite(write, nullptr, &publication);
        } catch (const std::bad_alloc&) {
            return {false, "RHI-OOM"};
        }
    }

    rhi::ValidationResult writeOriginalTextureCellImpl(const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentRangeExpectation& identity,
        const rhi::OriginalTextureCellDescriptorRequest& request) override
    {
        const WebGpuCompletedTexturePublication publication{
            write, source, {}, {}, identity, request};
        try {
            return publishWrite(write, nullptr, &publication);
        } catch (const std::bad_alloc&) {
            return {false, "RHI-OOM"};
        }
    }

    rhi::ValidationResult publishWrite(const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>* rangeSource,
        const WebGpuCompletedTexturePublication* completed = nullptr)
    {
        for (const auto& prior : completedTextures_)
            if (prior.cellIdentity && prior.write.binding == write.binding
                && prior.write.arrayElement == write.arrayElement
                && (!completed || !completed->cellIdentity))
                return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-ORIGINAL-PUBLICATION-REQUIRED"};
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: WebGPU RHI descriptor set write rejected after "
                "descriptor arena reset"};
        }
        const auto* binding = findBinding(write.binding);
        if (!binding || binding->type != write.type || !write.resource.isValid()) {
            return {false, "WebGPU RHI descriptor write does not match layout"};
        }
        if (write.arrayElement >= binding->arrayCount) {
            return {false, "WebGPU RHI descriptor array element is outside the layout range"};
        }
        const auto loweringValidation = rhi::validateDescriptorWriteLowering(write);
        if (!loweringValidation.success) {
            return loweringValidation;
        }
        const auto physicalType = rhi::physicalDescriptorType(write);
        if (write.type == rhi::DescriptorType::InputAttachment && !write.physicalLowering.enabled) {
            return {
                false, "WebGPU RHI InputAttachment requires an explicit sampled-texture lowering"};
        }
        const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
        auto facts = resource ? std::optional{resource->descriptorFacts()} : std::nullopt;
        if (completed) {
            const WebGpuNativeDescriptorBindingPayload candidate{*binding,
                write,
                write.arrayElement,
                resource ? resource->buffer : nullptr,
                resource ? resource->textureView : nullptr,
                resource ? resource->sampler : nullptr};
            if (const auto* id = validateWebGpuOriginalTexturePublication(*completed, candidate))
                return {false, id};
            if (!facts || !facts->textureContentExclusive)
                return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PROVENANCE"};
            // Only the authenticated completed publication reaches shared shape
            // checks. Ordinary descriptor writes retain the exclusive-use gate.
            facts->textureContentExclusive = false;
        }
        const auto resourceValidation = validateRegisteredResource(facts ? &*facts : nullptr,
            physicalType,
            write.resourceKind,
            binding->textureViewDimensionConstrained,
            binding->textureViewDimension,
            binding->depthTexture);
        if (!resourceValidation.success) {
            return resourceValidation;
        }
        if (physicalType == rhi::DescriptorType::Sampler) {
            const bool comparisonSampler = resource->samplerDesc.compareOp != rhi::CompareOp::Never;
            if (comparisonSampler != binding->comparisonSampler) {
                return {false, "WebGPU RHI descriptor sampler comparison state mismatch"};
            }
            if (const auto* id = rhi::validateNonFilteringSamplerBinding(
                    binding->sampledTextureFilterable, resource->samplerDesc))
                return {false, id};
        }
        core::pmr::Vector<rhi::DescriptorBindingWrite> candidateWrites(
            writes_, writes_.get_allocator());
        core::pmr::Vector<WebGpuNativeDescriptorBindingPayload> candidateBindings(
            nativeBindingPayloads_, nativeBindingPayloads_.get_allocator());
        core::pmr::Vector<WebGpuOriginalBufferRangePublication> candidateRanges(
            originalRanges_, originalRanges_.get_allocator());
        core::pmr::Vector<WebGpuCompletedTexturePublication> candidateCompleted(
            completedTextures_, completedTextures_.get_allocator());
        const auto completedFound = std::find_if(
            candidateCompleted.begin(), candidateCompleted.end(), [&](const auto& value) {
                return value.write.binding == write.binding
                    && value.write.arrayElement == write.arrayElement;
            });
        if (completed) {
            if (completedFound != candidateCompleted.end()
                && (completedFound->cellIdentity || completed->cellIdentity)
                && (bool(completedFound->cellIdentity) != bool(completed->cellIdentity)
                    || !core::SharedOwnerProjection::sameOwner(
                        completedFound->source, completed->source)))
                return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-SOURCE-REPLACEMENT"};
            if (completedFound == candidateCompleted.end())
                candidateCompleted.push_back(*completed);
            else
                *completedFound = *completed;
        } else if (completedFound != candidateCompleted.end())
            candidateCompleted.erase(completedFound);
        const auto rangeFound =
            std::find_if(candidateRanges.begin(), candidateRanges.end(), [&](const auto& range) {
                return range.write.binding == write.binding
                    && range.write.arrayElement == write.arrayElement;
            });
        if (rangeSource) {
            const WebGpuOriginalBufferRangePublication range{write, *rangeSource};
            if (rangeFound == candidateRanges.end())
                candidateRanges.push_back(range);
            else
                *rangeFound = range;
        } else if (rangeFound != candidateRanges.end())
            candidateRanges.erase(rangeFound);
        const auto found = std::find_if(
            candidateWrites.begin(), candidateWrites.end(), [&](const auto& candidate) {
                return candidate.binding == write.binding
                    && candidate.arrayElement == write.arrayElement;
            });
        if (found == candidateWrites.end()) {
            candidateWrites.push_back(write);
        } else {
            *found = write;
        }
        // Descriptor sets may outlive FrameGraph transient resources. Capture
        // the native payloads at write time so a later registry unregister does
        // not make an otherwise valid descriptor group impossible to update.
        const WebGpuNativeDescriptorBindingPayload nativeBinding{
            *binding,
            write,
            write.arrayElement,
            resource ? resource->buffer : nullptr,
            resource ? resource->textureView : nullptr,
            resource ? resource->sampler : nullptr,
        };
        const auto nativeFound = std::find_if(
            candidateBindings.begin(), candidateBindings.end(), [&](const auto& candidate) {
                return candidate.binding.binding == write.binding
                    && candidate.arrayElement == write.arrayElement;
            });
        if (nativeFound == candidateBindings.end()) {
            candidateBindings.push_back(nativeBinding);
        } else {
            *nativeFound = nativeBinding;
        }
        auto candidatePayload = nativePayload_;
        if (nativePayloadPolicy_.enabled && allBindingsWritten(candidateWrites.size())) {
            const bool cells = std::any_of(
                candidateCompleted.begin(), candidateCompleted.end(), [](const auto& value) {
                    return bool(value.cellIdentity);
                });
            auto nativePayload = cells
                ? createWebGpuNativeOriginalTextureCellDescriptorPayload(
                      layout_, candidateBindings, candidateRanges, candidateCompleted)
                : !candidateCompleted.empty()
                ? createWebGpuNativeOriginalCompletedTextureDescriptorPayload(
                      layout_, candidateBindings, candidateRanges, candidateCompleted)
                : candidateRanges.empty()
                ? createWebGpuNativeDescriptorGroupPayload(layout_, candidateBindings)
                : createWebGpuNativeOriginalRangeDescriptorPayload(
                      layout_, candidateBindings, candidateRanges);
            if (nativePayloadPolicy_.required && !nativePayload) {
                return {false, "RHI-DESCRIPTOR-PAYLOAD-CREATION-FAILED: WebGPU set"};
            }
            candidatePayload = std::move(nativePayload);
        }
        writes_.swap(candidateWrites);
        nativeBindingPayloads_.swap(candidateBindings);
        originalRanges_.swap(candidateRanges);
        completedTextures_.swap(candidateCompleted);
        nativePayload_.swap(candidatePayload);
        return {true, ""};
    }

    bool isCurrentAllocation() const override
    {
        return allocation_.success && generation_.isCurrent();
    }

    bool hasWrittenBinding(std::uint32_t binding) const override
    {
        if (!isCurrentAllocation()) {
            return false;
        }
        return std::find_if(writes_.begin(), writes_.end(), [&](const auto& write) {
            return write.binding == binding;
        }) != writes_.end();
    }

    rhi::ValidationResult validateBinding(std::uint32_t binding) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: WebGPU RHI descriptor set validation rejected "
                "after descriptor arena reset"};
        }
        bool written = false;
        for (const auto& write : writes_) {
            if (write.binding != binding)
                continue;
            written = true;
            const auto validation = validateBindingElement(binding, write.arrayElement);
            if (!validation.success)
                return validation;
        }
        return written
            ? rhi::ValidationResult{true, ""}
            : rhi::ValidationResult{false, "WebGPU RHI descriptor binding is not written"};
    }

    bool hasWrittenBindingElement(std::uint32_t binding, std::uint32_t arrayElement) const override
    {
        if (!isCurrentAllocation()) {
            return false;
        }
        return std::find_if(writes_.begin(), writes_.end(), [&](const auto& write) {
            return write.binding == binding && write.arrayElement == arrayElement;
        }) != writes_.end();
    }

    rhi::ValidationResult validateBindingElement(
        std::uint32_t binding, std::uint32_t arrayElement) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: WebGPU RHI descriptor set validation rejected "
                "after descriptor arena reset"};
        }
        const auto* desc = findBinding(binding);
        for (const auto& write : writes_) {
            if (write.binding != binding || write.arrayElement != arrayElement)
                continue;
            const auto completed = std::find_if(
                completedTextures_.begin(), completedTextures_.end(), [&](const auto& value) {
                    return value.write.binding == binding
                        && value.write.arrayElement == arrayElement;
                });
            if (completed != completedTextures_.end()) {
                const auto payload = std::find_if(nativeBindingPayloads_.begin(),
                    nativeBindingPayloads_.end(),
                    [&](const auto& value) {
                        return value.write.binding == binding
                            && value.write.arrayElement == arrayElement;
                    });
                if (payload == nativeBindingPayloads_.end())
                    return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PROVENANCE"};
                if (const auto* id = validateWebGpuOriginalTexturePublication(*completed, *payload))
                    return {false, id};
                return {true, {}};
            }
            const auto resource =
                registry_ ? registry_->findDescriptorFacts(write.resource) : std::nullopt;
            return desc
                ? validateRegisteredResource(resource ? &*resource : nullptr,
                      rhi::physicalDescriptorType(write),
                      write.resourceKind,
                      desc->textureViewDimensionConstrained,
                      desc->textureViewDimension,
                      desc->depthTexture)
                : rhi::ValidationResult{false, "WebGPU RHI descriptor binding is not declared"};
        }
        return {false, "WebGPU RHI descriptor array element is not written"};
    }

    rhi::DescriptorSetStats stats() const override
    {
        return {
            static_cast<std::uint32_t>(layout_.bindings.size()),
            isCurrentAllocation() ? static_cast<std::uint32_t>(writes_.size()) : 0u,
            true,
            false,
        };
    }

    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> nativePayload() const
    {
        for (const auto& publication : completedTextures_)
            if (publication.cellIdentity || publication.colorPublicationVersion == 6u)
                return nullptr;
        return isCurrentAllocation() ? nativePayload_ : nullptr;
    }

    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> imageCellPayload(
        const rhi::BindlessSnapshot& snapshot) const
    {
        if (snapshot.descriptorSet() != this
            || !rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                snapshot.plan())
            || !isCurrentAllocation() || !nativePayload_ || completedTextures_.empty())
            return {};
        bool storage = false;
        const bool produced =
            rhi::semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                snapshot.plan());
        for (const auto& binding : layout_.bindings) {
            if (binding.stages != rhi::ShaderStage::Compute
                || !validateBindingElement(binding.binding, 0u).success)
                return {};
            if (binding.type == rhi::DescriptorType::SampledTexture
                || binding.type == rhi::DescriptorType::StorageTexture) {
                const auto proof = std::find_if(
                    completedTextures_.begin(), completedTextures_.end(), [&](const auto& p) {
                        return p.write.binding == binding.binding;
                    });
                if (proof == completedTextures_.end()
                    || rhi::semantics::OriginalBindlessPreparationLowering::imageProofDiagnostic(
                        snapshot.plan(),
                        binding,
                        bool(proof->depthContent),
                        bool(proof->cellIdentity)))
                    return {};
                if (produced
                    && !rhi::OriginalTextureCellDescriptorContract::isProducedImageRead(
                        proof->cellRequest.contractVersion))
                    return {};
                storage =
                    storage || produced || binding.type == rhi::DescriptorType::StorageTexture;
            }
        }
        return storage ? nativePayload_ : nullptr;
    }

    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> originalOrdinaryGraphicsPayload(
        const rhi::OriginalCommandDescriptorSetSource& source, bool depthScope) const
    {
        using Domain = rhi::semantics::OriginalBindlessOrdinaryColorDomain;
        if (source.originalObject() != this || !isCurrentAllocation() || !nativePayload_
            || !nativePayload_->sharedOwnerMatches(nativePayload_)
            || source.frozenElements().size() != writes_.size()
            || nativePayload_->bindings.size() != writes_.size())
            return {};
        std::size_t images = 0u;
        for (const auto& element : source.frozenElements()) {
            const auto& write = element.declaration.write;
            const auto* binding = findBinding(write.binding);
            if (!binding || !validateBindingElement(write.binding, write.arrayElement).success)
                return {};
            if (binding->type == rhi::DescriptorType::UniformBuffer
                || binding->type == rhi::DescriptorType::ReadOnlyStorageBuffer
                || binding->type == rhi::DescriptorType::StorageBuffer)
                continue;
            const bool depthBinding = binding->depthTexture || binding->comparisonSampler;
            if (depthScope && depthBinding
                    ? rhi::semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                          rhi::Backend::WebGPU, *binding)
                        != nullptr
                    : !Domain::acceptsGraphicsBinding(*binding))
                return {};
            if (binding->type == rhi::DescriptorType::Sampler)
                continue;
            const auto* owner =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    &element.resourceOwner);
            const auto proof = std::find_if(
                completedTextures_.begin(), completedTextures_.end(), [&](const auto& p) {
                    return rhi::descriptorBindingWriteMatches(p.write, write);
                });
            if (!owner || !*owner || proof == completedTextures_.end() || proof->cellIdentity
                || !core::SharedOwnerProjection::sameOwner(proof->source, *owner))
                return {};
            if (depthScope && binding->depthTexture) {
                if (!proof->depthContent || proof->content
                    || proof->depthContent.get() != element.completedDepth.get()
                    || !core::SharedOwnerProjection::sameOwner(
                        proof->depthContent, element.completedDepth))
                    return {};
            } else if (!proof->content || proof->depthContent
                || proof->colorPublicationVersion != 6u
                || Domain::completedViewDiagnostic(
                    (*owner)->originalParentLease()->originalObject()->desc(),
                    (*owner)->originalObject()->desc(),
                    proof->content->controlVersion(),
                    3u))
                return {};
            ++images;
        }
        return images == completedTextures_.size() ? nativePayload_ : nullptr;
    }

    const auto& registry() const noexcept { return registry_; }

    bool originalLossFactoryMatches(
        const std::shared_ptr<WebGpuRhiResourceRegistry>& registry) const noexcept
    {
        return nativePayloadPolicy_.enabled && nativePayloadPolicy_.required
            && isCurrentAllocation() && registry
            && core::SharedOwnerProjection::sameOwner(registry_, registry);
    }

    const auto& recordedWrites() const noexcept { return writes_; }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_;
    }

    std::optional<rhi::DescriptorBindingWrite> successfulBindingWrite(
        std::uint32_t binding, std::uint32_t element) const noexcept override
    {
        if (!isCurrentAllocation())
            return {};
        const auto found = std::find_if(writes_.begin(), writes_.end(), [&](const auto& write) {
            return write.binding == binding && write.arrayElement == element;
        });
        return found != writes_.end() ? std::optional<rhi::DescriptorBindingWrite>(*found)
                                      : std::nullopt;
    }

    bool hasOriginalFiniteContent() const noexcept override
    {
        return nativePayload_ && nativePayload_->containsOriginalFiniteWrite();
    }

    bool hasOriginalWholeContent() const noexcept override
    {
        return nativePayload_ && nativePayload_->containsOriginalWholeWrite();
    }

    bool hasOriginalGeneralContent() const noexcept override
    {
        return nativePayload_ && nativePayload_->containsOriginalGeneralWrite();
    }

    std::uint32_t originalGeneralContentContractVersion() const noexcept override
    {
        return nativePayload_ ? nativePayload_->generalContentContractVersion() : 0u;
    }

    bool originalGeneralWriteScopeObserved(std::uint32_t participant) const noexcept override
    {
        return finiteCandidate_ && finiteCandidate_->containsOriginalGeneralWrite()
            && finiteCandidate_->finiteScopeObserved(participant);
    }

    bool originalGeneralWriteScopesDrained() const noexcept override
    {
        return finiteCandidate_ && finiteCandidate_->containsOriginalGeneralWrite()
            && finiteCandidate_->finiteScopesDrained();
    }

    const char* originalScopedContentDiagnostic() const noexcept override
    {
        if (finiteCandidate_ && finiteCandidate_->containsOriginalGeneralWrite())
            return "RHI-ORIGINAL-GENERAL-WRITE-PENDING";
        if (hasOriginalGeneralContent())
            return nativePayload_->generalContentDiagnostic();
        if (finiteCandidate_ && finiteCandidate_->containsOriginalWholeWrite())
            return "RHI-ORIGINAL-WHOLE-WRITE-PENDING";
        return hasOriginalWholeContent() ? nativePayload_->wholeContentDiagnostic() : nullptr;
    }

    bool originalWholeWriteScopeObserved(std::uint32_t participant) const noexcept override
    {
        return finiteCandidate_ && finiteCandidate_->containsOriginalWholeWrite()
            && finiteCandidate_->finiteScopeObserved(participant);
    }

    bool originalWholeWriteScopesDrained() const noexcept override
    {
        return finiteCandidate_ && finiteCandidate_->containsOriginalWholeWrite()
            && finiteCandidate_->finiteScopesDrained();
    }

    bool originalFiniteWriteScopeObserved(std::uint32_t participant) const noexcept override
    {
        return finiteCandidate_ && finiteCandidate_->containsOriginalFiniteWrite()
            && finiteCandidate_->finiteScopeObserved(participant);
    }

    bool originalFiniteWriteScopesDrained() const noexcept override
    {
        return finiteCandidate_ && finiteCandidate_->containsOriginalFiniteWrite()
            && finiteCandidate_->finiteScopesDrained();
    }

private:
    template <class Pipeline>
    const char* diagnoseGeneralSource(const std::shared_ptr<const Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& resources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (finiteCandidate_)
            return "RHI-ORIGINAL-GENERAL-WRITE-PENDING";
        if (!nativePayload_)
            return "RHI-ORIGINAL-GENERAL-SOURCE-PAYLOAD-MISSING";
        return nativePayload_->diagnoseOriginalGeneralSource(
            pipeline, resources, set, elements, count, quota);
    }

    const char* diagnoseOriginalGeneralSourceImpl(
        const std::shared_ptr<const rhi::ComputePipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& resources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseGeneralSource(pipeline, resources, set, elements, count, quota);
    }

    const char* diagnoseOriginalGeneralSourceImpl(
        const std::shared_ptr<const rhi::RenderPipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& resources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseGeneralSource(pipeline, resources, set, elements, count, quota);
    }

    template <bool targetMemory = false, class Pipeline>
    const char* diagnoseFiniteSource(const std::shared_ptr<const Pipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (finiteCandidate_)
            return "RHI-ORIGINAL-FINITE-WRITE-PENDING";
        if (!nativePayload_)
            return "RHI-ORIGINAL-FINITE-SOURCE-PAYLOAD-MISSING";
        if constexpr (targetMemory)
            return nativePayload_->diagnoseOriginalFiniteTargetMemorySource(
                pipeline, set, writes, count, quota);
        else
            return nativePayload_->diagnoseOriginalFiniteSource(
                pipeline, set, writes, count, quota);
    }

    const char* diagnoseOriginalFiniteSourceImpl(
        const std::shared_ptr<const rhi::ComputePipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseFiniteSource(pipeline, set, writes, count, quota);
    }

    const char* diagnoseOriginalFiniteSourceImpl(
        const std::shared_ptr<const rhi::RenderPipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseFiniteSource(pipeline, set, writes, count, quota);
    }

    const char* diagnoseOriginalFiniteTargetMemorySourceImpl(
        const std::shared_ptr<const rhi::ComputePipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseFiniteSource<true>(pipeline, set, writes, count, quota);
    }

    const char* diagnoseOriginalFiniteTargetMemorySourceImpl(
        const std::shared_ptr<const rhi::RenderPipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseFiniteSource<true>(pipeline, set, writes, count, quota);
    }

    const char* ordinaryWriteAdmissionDiagnostic() const noexcept override
    {
        if (finiteCandidate_)
            return "RHI-ORIGINAL-FINITE-WRITE-PENDING";
        if (hasOriginalFiniteContent())
            return "RHI-ORIGINAL-FINITE-WRITE-ORDINARY-CLOSED";
        if (hasOriginalGeneralContent())
            return "RHI-ORIGINAL-GENERAL-WRITE-ORDINARY-CLOSED";
        return hasOriginalWholeContent() ? "RHI-ORIGINAL-WHOLE-WRITE-ORDINARY-CLOSED" : nullptr;
    }

    const char* finishOriginalFiniteWriteImpl() noexcept override { return finishScoped(false); }

    const char* finishOriginalWholeWriteImpl() noexcept override { return finishScoped(true); }

    const char* finishOriginalGeneralWriteImpl() noexcept override
    {
        return finishScoped(false, true);
    }

    const char* finishScoped(bool whole, bool general = false) noexcept
    {
        if (finiteCandidate_
            && (finiteCandidate_->containsOriginalWholeWrite() != whole
                || finiteCandidate_->containsOriginalGeneralWrite() != general))
            return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
        if (!finiteCandidate_)
            return "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED";
        if (const auto* id = finiteCandidate_->finiteCandidateDiagnostic())
            return id;
        // Native evidence and immutable originals precede the allocation-free
        // swap. Only the final L2 boundary can advance the content revision.
        writes_.swap(finiteCandidate_->finiteCandidateWrites());
        nativeBindingPayloads_.clear();
        finiteCandidate_->commitOriginalFinite();
        nativePayload_.swap(finiteCandidate_);
        finiteCandidate_.reset();
        return nullptr;
    }

    const char* cancelOriginalFiniteWriteImpl() noexcept override { return cancelScoped(false); }

    const char* cancelOriginalWholeWriteImpl() noexcept override { return cancelScoped(true); }

    const char* cancelOriginalGeneralWriteImpl() noexcept override
    {
        return cancelScoped(false, true);
    }

    const char* cancelScoped(bool whole, bool general = false) noexcept
    {
        if (finiteCandidate_
            && (finiteCandidate_->containsOriginalWholeWrite() != whole
                || finiteCandidate_->containsOriginalGeneralWrite() != general))
            return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
        if (!finiteCandidate_)
            return "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED";
        if (!finiteCandidate_->finiteScopesDrained())
            return "RHI-ORIGINAL-FINITE-WRITE-PENDING";
        finiteCandidate_.reset();
        return nullptr;
    }

    template <class Pipeline, class Request>
    const char* beginFinite(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const Request& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (finiteCandidate_)
            return "RHI-ORIGINAL-FINITE-WRITE-PENDING";
        const char* diagnostic = nullptr;
        if (hasOriginalWholeContent() || hasOriginalGeneralContent()
            || (hasOriginalFiniteContent()
                && nativePayload_->containsOriginalFiniteTargetMemoryWrite()
                    != std::is_same_v<Request, rhi::OriginalFiniteTargetMemoryWriteRequest>))
            return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
        auto candidate = WebGpuNativeDescriptorGroupPayload::prepareOriginalFinite(
            device, *this, arena, pipeline, set, writes, count, request, quotas, diagnostic);
        if (candidate)
            finiteCandidate_ = std::move(candidate);
        return diagnostic;
    }

    const char* beginOriginalFiniteWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const rhi::OriginalFiniteDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginFinite(device, arena, pipeline, set, writes, count, request, quotas);
    }

    const char* beginOriginalFiniteWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::RenderPipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const rhi::OriginalFiniteDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginFinite(device, arena, pipeline, set, writes, count, request, quotas);
    }

    const char* beginOriginalFiniteTargetMemoryWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const rhi::OriginalFiniteTargetMemoryWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginFinite(device, arena, pipeline, set, writes, count, request, quotas);
    }

    const char* beginOriginalFiniteTargetMemoryWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::RenderPipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const rhi::OriginalFiniteTargetMemoryWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginFinite(device, arena, pipeline, set, writes, count, request, quotas);
    }

    template <class Pipeline>
    const char* beginWhole(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalWholeDescriptorBufferWrite* writes,
        std::size_t count,
        const rhi::OriginalWholeDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (finiteCandidate_)
            return "RHI-ORIGINAL-WHOLE-WRITE-PENDING";
        if (hasOriginalFiniteContent() || hasOriginalGeneralContent())
            return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
        const char* diagnostic = nullptr;
        auto candidate = WebGpuNativeDescriptorGroupPayload::prepareOriginalWhole(
            device, *this, arena, pipeline, set, writes, count, request, quotas, diagnostic);
        if (candidate)
            finiteCandidate_ = std::move(candidate);
        return diagnostic;
    }

    const char* beginOriginalWholeWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalWholeDescriptorBufferWrite* writes,
        std::size_t count,
        const rhi::OriginalWholeDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginWhole(device, arena, pipeline, set, writes, count, request, quotas);
    }

    const char* beginOriginalWholeWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::RenderPipeline>& pipeline,
        std::uint32_t set,
        const rhi::OriginalWholeDescriptorBufferWrite* writes,
        std::size_t count,
        const rhi::OriginalWholeDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginWhole(device, arena, pipeline, set, writes, count, request, quotas);
    }

private:
    const char* beginOriginalGeneralWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement* elements,
        std::size_t count,
        const rhi::OriginalGeneralDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        if (finiteCandidate_)
            return "RHI-ORIGINAL-GENERAL-WRITE-PENDING";
        if (hasOriginalWholeContent() || hasOriginalFiniteContent())
            return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
        const char* diagnostic = nullptr;
        auto candidate = WebGpuNativeDescriptorGroupPayload::prepareOriginalGeneral(device,
            *this,
            arena,
            pipeline,
            sources,
            set,
            elements,
            count,
            request,
            quotas,
            diagnostic);
        if (candidate)
            finiteCandidate_ = std::move(candidate);
        return diagnostic;
    }

    const char* beginOriginalGeneralWriteImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::RenderPipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement* elements,
        std::size_t count,
        const rhi::OriginalGeneralDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        if (finiteCandidate_)
            return "RHI-ORIGINAL-GENERAL-WRITE-PENDING";
        if (hasOriginalWholeContent() || hasOriginalFiniteContent())
            return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
        const char* diagnostic = nullptr;
        auto candidate = WebGpuNativeDescriptorGroupPayload::prepareOriginalGeneral(device,
            *this,
            arena,
            pipeline,
            sources,
            set,
            elements,
            count,
            request,
            quotas,
            diagnostic);
        if (candidate)
            finiteCandidate_ = std::move(candidate);
        return diagnostic;
    }

private:
    const rhi::DescriptorBindingDesc* findBinding(std::uint32_t binding) const
    {
        return findBindingInLayout(layout_, binding);
    }

    bool allBindingsWritten(std::size_t count) const
    {
        std::size_t expected = 0u;
        for (const auto& binding : layout_.bindings) {
            expected += binding.arrayCount;
        }
        return count == expected;
    }

    rhi::DescriptorSetAllocation allocation_;
    rhi::DescriptorLayoutDesc layout_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy_;
    core::LifetimeEpoch::Ticket generation_;
    core::pmr::Vector<rhi::DescriptorBindingWrite> writes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuNativeDescriptorBindingPayload> nativeBindingPayloads_{
        &core::defaultCpuMemoryResource()};
    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> finiteCandidate_;
    core::pmr::Vector<WebGpuOriginalBufferRangePublication> originalRanges_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuCompletedTexturePublication> completedTextures_{
        &core::defaultCpuMemoryResource()};
    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> nativePayload_;
};

class WebGpuRhiDescriptorTable final : public rhi::DescriptorTable {
public:
    WebGpuRhiDescriptorTable(rhi::DescriptorTableAllocation allocation,
        rhi::DescriptorTableDesc desc,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        WebGpuRhiNativePayloadPolicy nativePayloadPolicy,
        const core::LifetimeEpoch& generation)
        : allocation_(std::move(allocation)), desc_(std::move(desc)),
          registry_(std::move(registry)), nativePayloadPolicy_(nativePayloadPolicy),
          generation_(generation.ticket()),
          descriptors_(desc_.capacity, &core::defaultCpuMemoryResource())
    {}

    const rhi::DescriptorTableAllocation& allocation() const override { return allocation_; }

    const rhi::DescriptorTableDesc& desc() const override { return desc_; }

    rhi::ValidationResult writeImpl(const rhi::DescriptorTableWrite& write) override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: WebGPU RHI descriptor table write rejected after "
                "descriptor arena reset"};
        }
        if (write.index >= desc_.capacity || write.type != desc_.type
            || !write.resource.isValid()) {
            return {false, "WebGPU RHI descriptor table write is invalid"};
        }
        const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
        const auto facts = resource ? std::optional{resource->descriptorFacts()} : std::nullopt;
        const auto resourceValidation = validateRegisteredResource(facts ? &*facts : nullptr,
            write.type,
            write.resourceKind,
            desc_.textureViewDimensionConstrained,
            desc_.textureViewDimension,
            desc_.depthTexture);
        if (!resourceValidation.success) {
            return resourceValidation;
        }
        core::pmr::Vector<std::uint32_t> candidateWritten(
            writtenDescriptors_, writtenDescriptors_.get_allocator());
        core::pmr::Vector<Descriptor> candidateDescriptors(
            descriptors_, descriptors_.get_allocator());
        if (std::find(candidateWritten.begin(), candidateWritten.end(), write.index)
            == candidateWritten.end()) {
            candidateWritten.push_back(write.index);
        }
        candidateDescriptors[write.index] = {
            write,
            resource ? resource->buffer : nullptr,
            resource ? resource->textureView : nullptr,
            resource ? resource->sampler : nullptr,
            true,
        };
        auto candidatePayload = nativePayload_;
        if (nativePayloadPolicy_.enabled && candidateWritten.size() == desc_.capacity) {
            candidatePayload = buildNativePayload(candidateDescriptors);
            if (nativePayloadPolicy_.required && !candidatePayload)
                return {false, "RHI-DESCRIPTOR-PAYLOAD-CREATION-FAILED: WebGPU table"};
        }
        descriptors_.swap(candidateDescriptors);
        writtenDescriptors_.swap(candidateWritten);
        nativePayload_.swap(candidatePayload);
        return {true, ""};
    }

    bool isCurrentAllocation() const override
    {
        return allocation_.success && generation_.isCurrent();
    }

    bool hasWrittenDescriptor(std::uint32_t index) const override
    {
        if (!isCurrentAllocation()) {
            return false;
        }
        return std::find(writtenDescriptors_.begin(), writtenDescriptors_.end(), index)
            != writtenDescriptors_.end();
    }

    rhi::ValidationResult validateDescriptor(std::uint32_t index) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: WebGPU RHI descriptor table validation rejected "
                "after descriptor arena reset"};
        }
        if (!hasWrittenDescriptor(index)) {
            return {false, "WebGPU RHI descriptor table slot is not written"};
        }
        const auto& write = descriptors_[index].write;
        const auto resource =
            registry_ ? registry_->findDescriptorFacts(write.resource) : std::nullopt;
        return validateRegisteredResource(resource ? &*resource : nullptr,
            write.type,
            write.resourceKind,
            desc_.textureViewDimensionConstrained,
            desc_.textureViewDimension,
            desc_.depthTexture);
    }

    rhi::DescriptorTableStats stats() const override
    {
        return {
            desc_.capacity,
            isCurrentAllocation() ? static_cast<std::uint32_t>(writtenDescriptors_.size()) : 0u,
            true,
            false,
        };
    }

    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> nativePayload() const
    {
        return isCurrentAllocation() ? nativePayload_ : nullptr;
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_;
    }

    std::optional<rhi::DescriptorTableWrite> successfulDescriptorWrite(
        std::uint32_t index) const noexcept override
    {
        return hasWrittenDescriptor(index)
            ? std::optional<rhi::DescriptorTableWrite>(descriptors_[index].write)
            : std::nullopt;
    }

    bool originalDepthTablePublicationScopesDrained() const noexcept override
    {
        return depthCandidate_ && depthCandidate_->finiteScopesDrained();
    }

    bool hasOriginalDepthTableContent() const noexcept override
    {
        return nativePayload_ && nativePayload_->containsOriginalDepthTableWrite();
    }

    const char* originalDepthTableContentDiagnostic() const noexcept override
    {
        if (depthCandidate_)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-PENDING";
        return hasOriginalDepthTableContent() ? nativePayload_->depthContentDiagnostic() : nullptr;
    }

    const char* originalDepthTablePipelineDiagnostic(
        const rhi::ComputePipeline& pipeline) const noexcept override
    {
        return depthPipelineDiagnostic(pipeline);
    }

    const char* originalDepthTablePipelineDiagnostic(
        const rhi::RenderPipeline& pipeline) const noexcept override
    {
        return depthPipelineDiagnostic(pipeline);
    }

private:
    template <class Pipeline>
    const char* depthPipelineDiagnostic(const Pipeline& pipeline) const noexcept
    {
        if (const auto* id = originalDepthTableContentDiagnostic())
            return id;
        return hasOriginalDepthTableContent() ? nativePayload_->depthPipelineDiagnostic(pipeline)
                                              : "RHI-ORIGINAL-TABLE-BINDING-PAYLOAD-UNAVAILABLE";
    }

    const char* ordinaryWriteAdmissionDiagnostic() const noexcept override
    {
        if (depthCandidate_)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-PENDING";
        return hasOriginalDepthTableContent() ? "RHI-ORIGINAL-TABLE-PUBLICATION-ORDINARY-CLOSED"
                                              : nullptr;
    }

    template <class Pipeline>
    const char* beginDepth(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        const rhi::OriginalDepthTablePublicationWrite* writes,
        std::size_t count,
        const rhi::OriginalDepthTablePublicationRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (depthCandidate_)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-PENDING";
        const char* id = nullptr;
        auto candidate = WebGpuNativeDescriptorGroupPayload::prepareOriginalDepthTable(
            device, *this, arena, pipeline, writes, count, request, quotas, id);
        if (candidate)
            depthCandidate_ = std::move(candidate);
        return id;
    }

    const char* beginOriginalDepthTablePublicationImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        const rhi::OriginalDepthTablePublicationWrite* writes,
        std::size_t count,
        const rhi::OriginalDepthTablePublicationRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginDepth(device, arena, pipeline, writes, count, request, quotas);
    }

    const char* beginOriginalDepthTablePublicationImpl(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<rhi::RenderPipeline>& pipeline,
        const rhi::OriginalDepthTablePublicationWrite* writes,
        std::size_t count,
        const rhi::OriginalDepthTablePublicationRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) override
    {
        return beginDepth(device, arena, pipeline, writes, count, request, quotas);
    }

    const char* finishOriginalDepthTablePublicationImpl() noexcept override
    {
        if (!depthCandidate_)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-UNPREPARED";
        if (const auto* id = depthCandidate_->finiteCandidateDiagnostic())
            return id;
        const auto& writes = depthCandidate_->depthCandidateWrites();
        if (writes.size() != descriptors_.size())
            return "RHI-ORIGINAL-TABLE-WRITE-COVERAGE";
        // All checks precede this allocation-free publication. Both storage
        // arrays and the native candidate were allocated in cold preparation.
        for (std::size_t i = 0u; i < writes.size(); ++i)
            descriptors_[i] = {
                writes[i].write, nullptr, depthCandidate_->bindings[i].textureView, nullptr, true};
        writtenDescriptors_.swap(depthCandidate_->depthCandidateIndices());
        depthCandidate_->commitOriginalFinite();
        nativePayload_.swap(depthCandidate_);
        depthCandidate_.reset();
        return nullptr;
    }

    const char* cancelOriginalDepthTablePublicationImpl() noexcept override
    {
        if (!depthCandidate_)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-UNPREPARED";
        if (!depthCandidate_->finiteScopesDrained())
            return "RHI-ORIGINAL-TABLE-PUBLICATION-CALLBACK-DRAIN";
        depthCandidate_.reset();
        return nullptr;
    }

    struct Descriptor {
        rhi::DescriptorTableWrite write;
        std::shared_ptr<WebGpuNativeBufferPayload> buffer;
        std::shared_ptr<WebGpuNativeTextureViewPayload> textureView;
        std::shared_ptr<WebGpuNativeSamplerPayload> sampler;
        bool written = false;
    };

    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> buildNativePayload(
        const core::pmr::Vector<Descriptor>& descriptors) const
    {
        rhi::DescriptorLayoutDesc layout;
        layout.name = "WebGPU descriptor table lowering";
        rhi::DescriptorBindingDesc binding;
        binding.binding = 0u;
        binding.type = desc_.type;
        binding.stages = desc_.stages;
        binding.arrayCount = desc_.capacity;
        binding.tableBacked = true;
        binding.textureViewDimensionConstrained = desc_.textureViewDimensionConstrained;
        binding.textureViewDimension = desc_.textureViewDimension;
        binding.depthTexture = desc_.depthTexture;
        layout.bindings.push_back(binding);

        core::pmr::Vector<WebGpuNativeDescriptorBindingPayload> nativeBindings{
            &core::defaultCpuMemoryResource()};
        for (const auto& descriptor : descriptors) {
            if (!descriptor.written) {
                continue;
            }
            rhi::DescriptorBindingWrite bindingWrite;
            bindingWrite.binding = 0u;
            bindingWrite.type = descriptor.write.type;
            bindingWrite.resource = descriptor.write.resource;
            bindingWrite.resourceKind = descriptor.write.resourceKind;
            bindingWrite.arrayElement = descriptor.write.index;
            nativeBindings.push_back({
                binding,
                bindingWrite,
                descriptor.write.index,
                descriptor.buffer,
                descriptor.textureView,
                descriptor.sampler,
            });
        }
        auto payload = createWebGpuNativeDescriptorGroupPayload(layout, nativeBindings);
        if (payload) {
            payload->tableBacked = true;
        }
        return payload;
    }

    rhi::DescriptorTableAllocation allocation_;
    rhi::DescriptorTableDesc desc_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy_;
    core::LifetimeEpoch::Ticket generation_;
    core::pmr::Vector<std::uint32_t> writtenDescriptors_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Descriptor> descriptors_{&core::defaultCpuMemoryResource()};
    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> nativePayload_;
    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> depthCandidate_;
};

namespace {

class WebGpuRhiDescriptorArena final : public rhi::DescriptorArena {
public:
    bool originalLossFactoryMatches(
        const std::shared_ptr<WebGpuRhiResourceRegistry>& registry) const noexcept
    {
        return nativePayloadPolicy_.enabled && nativePayloadPolicy_.required
            && generation_.isActive() && registry
            && core::SharedOwnerProjection::sameOwner(registry_, registry);
    }

    WebGpuRhiDescriptorArena(rhi::DescriptorArenaDesc desc,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        WebGpuRhiNativePayloadPolicy nativePayloadPolicy)
        : desc_(std::move(desc)), registry_(std::move(registry)),
          nativePayloadPolicy_(nativePayloadPolicy)
    {
        stats_.maxSets = desc_.maxSets;
        stats_.maxDescriptors = desc_.maxDescriptors;
    }

    rhi::DescriptorSetAllocation allocateSet(const rhi::DescriptorLayoutDesc& layout) override
    {
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        const auto validation = rhi::validateDescriptorLayout(layout);
        if (!validation.success) {
            return {false, validation.diagnostics, stats_.allocatedSets, 0};
        }
        if (stats_.allocatedSets >= desc_.maxSets) {
            return {false, "WebGPU RHI descriptor set capacity exceeded", stats_.allocatedSets, 0};
        }
        std::uint32_t capacity = 0u;
        const auto count = rhi::countDescriptorElements(layout, capacity);
        if (!count.success)
            return {false, count.diagnostics, stats_.allocatedSets, 0u};
        if (stats_.allocatedDescriptors > desc_.maxDescriptors
            || capacity > desc_.maxDescriptors - stats_.allocatedDescriptors) {
            return {
                false, "WebGPU RHI descriptor capacity exceeded", stats_.allocatedSets, capacity};
        }
        const auto setIndex = stats_.allocatedSets++;
        stats_.allocatedDescriptors += capacity;
        return {true, "", setIndex, capacity};
    }

    arp::core::UniquePtr<rhi::DescriptorSet> allocateDescriptorSet(
        const rhi::DescriptorSetDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<rhi::DescriptorSet> {
                auto allocation = allocateSet(copied.layout);
                return allocation.success
                    ? arp::core::makeUnique<WebGpuRhiDescriptorSet>(allocation,
                          std::move(copied.layout),
                          registry_,
                          nativePayloadPolicy_,
                          generation_)
                    : nullptr;
            },
            stats_);
    }

    rhi::DescriptorTableAllocation allocateTable(const rhi::DescriptorTableDesc& desc) override
    {
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        if (desc.label.empty() || desc.capacity == 0 || desc.stages == rhi::ShaderStage::None) {
            return {
                false, "WebGPU RHI descriptor table desc is invalid", stats_.allocatedTables, 0};
        }
        const auto shape = rhi::validateDescriptorTextureRequirement(
            desc.type, desc.textureViewDimensionConstrained, desc.depthTexture);
        if (!shape.success)
            return {false, shape.diagnostics, stats_.allocatedTables, 0u};
        if (desc.type == rhi::DescriptorType::InputAttachment) {
            return {false,
                "WebGPU RHI InputAttachment descriptor tables have no executable lowering",
                stats_.allocatedTables,
                0};
        }
        if (stats_.allocatedSets >= desc_.maxSets) {
            return {false,
                "WebGPU RHI descriptor table capacity is invalid",
                stats_.allocatedTables,
                desc.capacity};
        }
        if (stats_.allocatedDescriptors > desc_.maxDescriptors
            || desc.capacity > desc_.maxDescriptors - stats_.allocatedDescriptors) {
            return {false,
                "WebGPU RHI descriptor capacity exceeded",
                stats_.allocatedTables,
                desc.capacity};
        }
        const auto tableIndex = stats_.allocatedTables++;
        ++stats_.allocatedSets;
        stats_.allocatedDescriptors += desc.capacity;
        return {true, "", tableIndex, desc.capacity};
    }

    arp::core::UniquePtr<rhi::DescriptorTable> allocateDescriptorTable(
        const rhi::DescriptorTableDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<rhi::DescriptorTable> {
                auto allocation = allocateTable(copied);
                return allocation.success
                    ? arp::core::makeUnique<WebGpuRhiDescriptorTable>(allocation,
                          std::move(copied),
                          registry_,
                          nativePayloadPolicy_,
                          generation_)
                    : nullptr;
            },
            stats_);
    }

    rhi::DescriptorArenaStats stats() const override { return stats_; }

    void reset() override
    {
        stats_.allocatedSets = 0;
        stats_.allocatedTables = 0;
        stats_.allocatedDescriptors = 0;
        generation_.advance();
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_.ticket();
    }

private:
    rhi::DescriptorArenaDesc desc_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy_;
    core::LifetimeEpoch generation_;
    rhi::DescriptorArenaStats stats_;
};

} // namespace

const char* validateWebGpuOriginalDescriptorLossSource(const rhi::DescriptorArena& arena,
    const std::shared_ptr<WebGpuRhiResourceRegistry>& registry) noexcept
{
    const auto* source = dynamic_cast<const WebGpuRhiDescriptorArena*>(&arena);
    return source && source->originalLossFactoryMatches(registry)
        ? nullptr
        : "RHI-WEBGPU-ROOT-DESCRIPTOR-LOSS-ARENA-FACTORY";
}

const char* validateWebGpuOriginalDescriptorLossSource(const rhi::DescriptorSet& set,
    const std::shared_ptr<WebGpuRhiResourceRegistry>& registry) noexcept
{
    const auto* source = dynamic_cast<const WebGpuRhiDescriptorSet*>(&set);
    return source && source->originalLossFactoryMatches(registry)
        ? nullptr
        : "RHI-WEBGPU-ROOT-DESCRIPTOR-LOSS-SET-FACTORY";
}

core::UniquePtr<WebGpuOrdinaryDescriptorObservation> WebGpuOrdinaryDescriptorObservation::prepare(
    const rhi::DescriptorSet& descriptor,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    const std::uint32_t* offsets,
    std::uint32_t offsetCount,
    const WebGpuOrdinaryDescriptorBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const WebGpuOrdinaryResourceBudget& resourceBudget,
    std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-QUOTA";
        return {};
    }
    if (const auto* id = WebGpuOrdinaryResourceProjection::validateBudget(resourceBudget)) {
        diagnostic = id;
        return {};
    }
    if (!resourceQuota
        || resourceQuota->limitBytes() != resourceBudget.retention.maxCpuMetadataBytes) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return {};
    }
    const auto* source = dynamic_cast<const WebGpuRhiDescriptorSet*>(&descriptor);
    if (!source || !registry
        || !core::SharedOwnerProjection::sameOwner(registry, source->registry())) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-REGISTRY";
        return {};
    }
    if (!source->isCurrentAllocation()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-STALE";
        return {};
    }
    if (source->hasOriginalFiniteContent()) {
        diagnostic = "RHI-ORIGINAL-FINITE-WRITE-ORDINARY-CLOSED";
        return {};
    }
    const auto payload = source->nativePayload();
    if (!payload || !payload->sharedOwnerMatches(payload) || payload->tableBacked) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD";
        return {};
    }
    const auto& layout = descriptor.layout().bindings;
    if (layout.size() > budget.maxBindings || payload->bindings.size() > budget.maxWrites
        || offsetCount > budget.maxDynamicOffsets) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-CAPACITY";
        return {};
    }
    if (offsetCount != rhi::dynamicOffsetBindingCount(descriptor.layout())
        || (offsetCount && !offsets)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-OFFSET-COUNT";
        return {};
    }
    if (!rhi::semantics::DescriptorWholeBufferBinding::preservesRanges(offsets, offsetCount)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-OFFSET-RANGE-UNPROVEN";
        return {};
    }
    for (const auto& binding : layout)
        if (binding.tableBacked || binding.arrayCount != 1u) {
            diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-ARRAY-TABLE-UNSUPPORTED";
            return {};
        }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<WebGpuOrdinaryDescriptorObservation>(
            ConstructionKey{}, descriptor, registry, payload);
        result->layout_.reserve(budget.maxBindings);
        result->bindings_.reserve(budget.maxWrites);
        result->offsetBindings_.reserve(budget.maxDynamicOffsets);
        result->offsets_.reserve(budget.maxDynamicOffsets);
        result->layout_.assign(layout.begin(), layout.end());
        result->bindings_.assign(payload->bindings.begin(), payload->bindings.end());
        for (const auto& binding : layout)
            if (binding.dynamicOffset)
                result->offsetBindings_.push_back(binding.binding);
        if (offsetCount)
            result->offsets_.assign(offsets, offsets + offsetCount);
        core::BoundedSequence<rhi::GpuResourceId> ids(resourceBudget.maxResources);
        for (const auto& binding : result->bindings_) {
            if (ids.contains(binding.write.resource))
                continue;
            if (!ids.append(binding.write.resource)) {
                diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-CAPACITY";
                return {};
            }
        }
        result->resources_ = WebGpuOrdinaryResourceProjection::prepare(*registry,
            ids.values().data(),
            ids.values().size(),
            resourceBudget,
            std::move(resourceQuota),
            diagnostic);
        if (!result->resources_)
            return {};
        diagnostic = result->validate();
        if (diagnostic)
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PREPARATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PREPARATION-ALLOCATION";
        return {};
    }
}

const char* WebGpuOrdinaryDescriptorObservation::validate() const noexcept
{
    const auto* descriptor = descriptor_.get();
    if (!descriptor)
        return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-EXPIRED";
    if (!descriptor->isCurrentAllocation())
        return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-STALE";
    if (descriptor->contentRevision() != revision_)
        return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-REVISION";
    const auto* source = dynamic_cast<const WebGpuRhiDescriptorSet*>(descriptor);
    if (!source || !core::SharedOwnerProjection::sameOwner(registry_, source->registry()))
        return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-REGISTRY";
    if (!core::SharedOwnerProjection::sameOwner(payload_, source->nativePayload())
        || !payload_->sharedOwnerMatches(payload_) || payload_->tableBacked
        || payload_->shaderPackageHash != packageHash_
        || payload_->containsLoweredInputAttachment != loweredInput_
        || payload_->dynamicOffsetCount != offsets_.size()
        || payload_->dynamicOffsetBindings != offsetBindings_
        || payload_->bindings.size() != bindings_.size())
        return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD";
    const auto& currentLayout = descriptor->layout().bindings;
    if (currentLayout.size() != layout_.size() || bindings_.size() != layout_.size()
        || source->recordedWrites().size() != bindings_.size())
        return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-LAYOUT";
    for (std::size_t index = 0u; index < layout_.size(); ++index) {
        const auto& binding = layout_[index];
        if (binding.stages != currentLayout[index].stages
            || !rhi::descriptorBindingLayoutMatches(binding, currentLayout[index]))
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-LAYOUT";
        std::size_t found = 0u;
        for (const auto& written : bindings_)
            if (written.binding.binding == binding.binding)
                ++found;
        if (found != 1u)
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-LAYOUT";
    }
    for (std::size_t index = 0u; index < bindings_.size(); ++index) {
        const auto& frozen = bindings_[index];
        const auto& current = payload_->bindings[index];
        const auto* layoutBinding =
            rhi::findDescriptorSetBinding(descriptor->layout(), frozen.binding.binding);
        if (!layoutBinding || frozen.binding.stages != layoutBinding->stages
            || !rhi::descriptorBindingLayoutMatches(frozen.binding, *layoutBinding)
            || frozen.arrayElement != 0u || frozen.write.arrayElement != 0u
            || frozen.write.binding != frozen.binding.binding
            || frozen.write.type != frozen.binding.type
            || frozen.binding.stages != current.binding.stages
            || !rhi::descriptorBindingLayoutMatches(frozen.binding, current.binding)
            || !rhi::descriptorBindingWriteMatches(frozen.write, current.write)
            || frozen.arrayElement != current.arrayElement
            || !core::SharedOwnerProjection::sameOwner(frozen.buffer, current.buffer)
            || !core::SharedOwnerProjection::sameOwner(frozen.textureView, current.textureView)
            || !core::SharedOwnerProjection::sameOwner(frozen.sampler, current.sampler))
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD";
        const auto written = std::find_if(source->recordedWrites().begin(),
            source->recordedWrites().end(),
            [&](const auto& write) {
                return write.binding == frozen.write.binding
                    && write.arrayElement == frozen.write.arrayElement;
            });
        if (written == source->recordedWrites().end()
            || !rhi::descriptorBindingWriteMatches(frozen.write, *written))
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-PAYLOAD";
        const auto* resource = resources_->find(frozen.write.resource);
        if (!resource
            || !core::SharedOwnerProjection::sameOwner(frozen.buffer, resource->backing().buffer)
            || !core::SharedOwnerProjection::sameOwner(frozen.textureView, resource->view())
            || !core::SharedOwnerProjection::sameOwner(frozen.sampler, resource->sampler()))
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-RESOURCE-OWNER";
        if (resource->backing().texture)
            if (const auto* id =
                    resource->backing().texture->contentAccessPolicy().ordinaryAccessDiagnostic())
                return id;
        if (frozen.textureView && frozen.textureView->parentTexture)
            if (const auto* id = frozen.textureView->parentTexture->contentAccessPolicy()
                    .ordinaryAccessDiagnostic())
                return id;
        if (rhi::physicalDescriptorType(frozen.write) == rhi::DescriptorType::StorageBuffer
            && resource->backing().writeSeal
            && resource->backing().writeSeal->writableDescriptorSealed())
            return "RHI-BUFFER-GPU-WRITES-SEALED";
    }
    return registry_->validateOrdinaryResources(
        resources_->resources().data(), resources_->resources().size());
}

arp::core::UniquePtr<rhi::DescriptorArena> createWebGpuRhiDescriptorArena(
    const rhi::DescriptorArenaDesc& desc,
    std::shared_ptr<WebGpuRhiResourceRegistry> registry,
    WebGpuRhiNativePayloadPolicy nativePayloadPolicy)
{
    if (desc.maxSets == 0 || desc.maxDescriptors == 0) {
        return nullptr;
    }
    return arp::core::makeUnique<WebGpuRhiDescriptorArena>(
        arp::rhi::DescriptorStateCopy::copy(desc), std::move(registry), nativePayloadPolicy);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareWebGpuOriginalImageCellDescriptors(
    const rhi::BindlessSnapshot& snapshot)
{
    const auto* set = dynamic_cast<const WebGpuRhiDescriptorSet*>(snapshot.descriptorSet());
    return set ? set->imageCellPayload(snapshot) : nullptr;
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
prepareWebGpuOriginalOrdinaryGraphicsDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
    std::uint32_t logicalSet,
    std::uint32_t closureVersion)
{
    using Contract = rhi::OriginalBindlessRootNativeClosureContract;
    if (!Contract::canonicalGraphicsVersion(closureVersion)
        || rhi::OriginalCommandBindlessRootSource::validateOriginal(root)
        || Contract::canonicalGraphicsColorRootDiagnostic(*root, closureVersion)
        || Contract::canonicalGraphicsDepthRootDiagnostic(*root, closureVersion))
        return {};
    for (const auto& domain : root->ordinaryDomains()) {
        if (domain.set != logicalSet)
            continue;
        const auto* set =
            dynamic_cast<const WebGpuRhiDescriptorSet*>(domain.source->originalObject());
        return set ? set->originalOrdinaryGraphicsPayload(*domain.source,
                         closureVersion == Contract::canonicalGraphicsDepthVersion
                             || closureVersion == Contract::canonicalGraphicsDepthExecutionVersion)
                   : nullptr;
    }
    return {};
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> prepareWebGpuOriginalOrdinaryColorDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
    std::uint32_t logicalSet,
    std::uint32_t closureVersion)
{
    using Contract = rhi::OriginalBindlessRootNativeClosureContract;
    return closureVersion == Contract::canonicalGraphicsColorVersion
            || closureVersion == Contract::canonicalGraphicsColorExecutionVersion
        ? prepareWebGpuOriginalOrdinaryGraphicsDescriptors(root, logicalSet, closureVersion)
        : nullptr;
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuNativePayloadFor(
    const rhi::DescriptorSet& descriptorSet)
{
    const auto* webGpuDescriptorSet = dynamic_cast<const WebGpuRhiDescriptorSet*>(&descriptorSet);
    return webGpuDescriptorSet ? webGpuDescriptorSet->nativePayload() : nullptr;
}

const char* observeWebGpuDescriptorResource(const rhi::DescriptorSet& descriptor,
    rhi::GpuResourceId id,
    WebGpuOrdinaryResourceObservation& output) noexcept
{
    const auto* source = dynamic_cast<const WebGpuRhiDescriptorSet*>(&descriptor);
    if (!source || !source->isCurrentAllocation() || !source->registry())
        return "RHI-WEBGPU-BINDLESS-PREPARED-RESOURCE-REGISTRY";
    return source->registry()->observeOrdinaryResources(&id, 1u, &output);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuNativePayloadFor(
    const rhi::DescriptorTable& descriptorTable)
{
    const auto* webGpuTable = dynamic_cast<const WebGpuRhiDescriptorTable*>(&descriptorTable);
    return webGpuTable ? webGpuTable->nativePayload() : nullptr;
}

} // namespace arp::rhi_webgpu
