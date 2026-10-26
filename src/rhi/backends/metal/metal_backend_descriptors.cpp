#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/mutation_journal.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_original_buffer_binding.h"
#include "rhi/backends/metal/metal_original_texture_binding.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/descriptor_completed_texture.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/descriptor_state_copy.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace arp::rhi_metal {

namespace {

struct CompletedTexturePublication {
    rhi::DescriptorBindingWrite write;
    std::shared_ptr<const rhi::OriginalCommandTextureViewSource> source;
    std::optional<rhi::GpuTexture::ContentExpectation> content;
    MetalRhiNativeResourceObservation parentIdentity, viewIdentity;
    std::optional<rhi::GpuTexture::ContentRangeExpectation> cellIdentity;
    rhi::OriginalTextureCellDescriptorRequest cellRequest;
    std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource> depth;
    std::uint32_t colorPublicationVersion = 1u;
};

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

rhi::ValidationResult validateRegisteredResource(const MetalRhiRegisteredResource* resource,
    rhi::DescriptorType type,
    rhi::DescriptorResourceKind kind,
    bool textureViewDimensionConstrained = false,
    rhi::TextureViewDimension textureViewDimension = rhi::TextureViewDimension::Texture2D,
    bool depthTexture = false,
    const CompletedTexturePublication* completed = nullptr)
{
    if (!resource) {
        return {
            false, "RHI-DESCRIPTOR-RESOURCE-NOT-LIVE: Metal RHI descriptor resource is unknown"};
    }
    if (completed) {
        const bool cell = bool(completed->cellIdentity);
        if ((!cell && type != rhi::DescriptorType::SampledTexture)
            || kind != rhi::DescriptorResourceKind::TextureView
            || (unsigned(bool(completed->content)) + unsigned(cell)
                   + unsigned(bool(completed->depth)))
                != 1u)
            return {false, "RHI-METAL-COMPLETED-TEXTURE-TYPE"};
        const auto* diagnostic = completed->depth
            ? rhi::OriginalCompletedTextureDescriptorContract::validateSource(completed->depth, 4u)
            : cell
            ? rhi::OriginalTextureCellDescriptorContract::validateSource(completed->source,
                  *completed->cellIdentity,
                  completed->cellRequest.contractVersion)
            : rhi::OriginalCompletedTextureDescriptorContract::validateSource(
                  completed->source, *completed->content, completed->colorPublicationVersion);
        if (diagnostic)
            return {false, diagnostic};
        if (completed->depth
            && (completed->depth->originalViewSource().get() != completed->source.get()
                || !core::SharedOwnerProjection::sameOwner(
                    completed->depth->originalViewSource(), completed->source)))
            return {false, "RHI-METAL-COMPLETED-DEPTH-SOURCE-IDENTITY"};
        const auto& source = *completed->source;
        const auto& parent = *source.originalParentLease()->originalObject();
        const auto& view = *source.originalObject();
        const auto imagePayload = metalRhiNativePayloadFor(parent);
        const auto viewPayload = metalRhiNativePayloadFor(view);
        if (!imagePayload || !viewPayload || resource->textureViewWrapper.get() != &view
            || resource->parentTexture != parent.id() || !resource->parentTextureLive
            || !core::SharedOwnerProjection::sameOwner(resource->textureView, viewPayload)
            || !completed->parentIdentity.matches(imagePayload->resource)
            || !completed->viewIdentity.matches(viewPayload->resource))
            return {false, "RHI-METAL-COMPLETED-TEXTURE-NATIVE-IDENTITY"};
        const auto* nativeDiagnostic = completed->depth
            ? validateMetalRhiNativeCompletedDepthView(
                  *imagePayload, *viewPayload, parent.desc(), view.desc())
            : cell ? validateMetalRhiNativeTextureCellView(*imagePayload,
                         *viewPayload,
                         parent.desc(),
                         view.desc(),
                         rhi::OriginalTextureCellDescriptorContract::isProducedImageRead(
                             completed->cellRequest.contractVersion))
                   : validateMetalRhiNativeCompletedTextureView(
                         *imagePayload, *viewPayload, parent.desc(), view.desc());
        if (nativeDiagnostic)
            return {false, nativeDiagnostic};
    } else if (const auto* id = metalRhiOriginalTextureAccessDiagnostic(*resource))
        return {false, id};
    if (type == rhi::DescriptorType::StorageTexture && resource->immutableTextureRevision)
        return {false, "RHI-METAL-IMMUTABLE-TEXTURE-WRITE-FORBIDDEN"};
    if (type == rhi::DescriptorType::SampledTexture || type == rhi::DescriptorType::StorageTexture
        || type == rhi::DescriptorType::InputAttachment) {
        const auto parent = rhi::validateDescriptorTextureParent(resource->parentTextureLive);
        if (!parent.success)
            return parent;
    }
    switch (type) {
    case rhi::DescriptorType::UniformBuffer:
        if (kind != rhi::DescriptorResourceKind::Buffer
            || resource->kind != rhi::DescriptorResourceKind::Buffer
            || !hasResourceUsage(resource->bufferUsage, rhi::ResourceUsage::UniformBuffer)) {
            return {false, "Metal RHI descriptor buffer usage mismatch"};
        }
        break;
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        if (kind != rhi::DescriptorResourceKind::Buffer
            || resource->kind != rhi::DescriptorResourceKind::Buffer
            || !hasResourceUsage(resource->bufferUsage, rhi::ResourceUsage::StorageBuffer)) {
            return {false, "Metal RHI descriptor buffer usage mismatch"};
        }
        if (type == rhi::DescriptorType::StorageBuffer && resource->gpuWritesSealed)
            return {false, "RHI-BUFFER-GPU-WRITES-SEALED"};
        break;
    case rhi::DescriptorType::SampledTexture:
        if (kind != rhi::DescriptorResourceKind::TextureView
            || resource->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(resource->textureUsage, rhi::TextureUsage::Sampled)) {
            return {false, "Metal RHI descriptor texture usage mismatch"};
        }
        if (textureViewDimensionConstrained
            && resource->textureViewDimension != textureViewDimension) {
            return {false, "Metal RHI descriptor texture view dimension mismatch"};
        }
        return rhi::validateSampledDepthView(
            depthTexture, resource->textureFormat, resource->textureViewAspect);
    case rhi::DescriptorType::StorageTexture:
        if (kind != rhi::DescriptorResourceKind::TextureView
            || resource->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(resource->textureUsage, rhi::TextureUsage::Storage)) {
            return {false, "Metal RHI descriptor texture usage mismatch"};
        }
        if (textureViewDimensionConstrained
            && resource->textureViewDimension != textureViewDimension) {
            return {false, "Metal RHI descriptor texture view dimension mismatch"};
        }
        break;
    case rhi::DescriptorType::Sampler:
        if (kind != rhi::DescriptorResourceKind::Sampler
            || resource->kind != rhi::DescriptorResourceKind::Sampler) {
            return {false, "Metal RHI descriptor sampler mismatch"};
        }
        break;
    case rhi::DescriptorType::InputAttachment:
        if (kind != rhi::DescriptorResourceKind::TextureView
            || resource->kind != rhi::DescriptorResourceKind::TextureView
            || !hasTextureUsage(resource->textureUsage, rhi::TextureUsage::RenderAttachment)
            || rhi::isDepthTextureFormat(resource->textureFormat) != depthTexture) {
            return {
                false, "Metal RHI framebuffer-fetch input attachment resource identity mismatch"};
        }
        break;
    }
    return {true, ""};
}

class MetalRhiDescriptorSet final : public rhi::DescriptorSet {
    struct OriginalSynchronousPublication {
        struct Binding {
            rhi::OriginalCommandDescriptorSetElement element;
            rhi::OriginalCommandDescriptorDependencies::Owner owner;
            std::variant<MetalOriginalBufferBindingInspection::Observation,
                MetalOriginalTextureBindingInspection::Observation>
                native;
        };

        rhi::OriginalCommandDescriptorDependencies::PipelineOwner pipeline;
        std::shared_ptr<const rhi::DescriptorArena> arena;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
        std::array<std::uint64_t, 3u> quotaLimits{};
        core::pmr::Vector<Binding> bindings{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<rhi::DescriptorBindingWrite> writes{&core::defaultCpuMemoryResource()};
        std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> group;
        std::shared_ptr<MetalRhiNativePipelinePayload> nativePipeline;
        MetalRhiNativeResourceObservation nativePipelineIdentity;
        std::array<std::shared_ptr<void>, 3u> nativeShaderObjects;
        std::uint64_t revision = 0u;
        std::uint32_t set = 0u, contractVersion = 0u;
        bool committed = false;
    };

public:
    MetalRhiDescriptorSet(rhi::DescriptorSetAllocation allocation,
        rhi::DescriptorLayoutDesc layout,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        const core::LifetimeEpoch& generation)
        : allocation_(std::move(allocation)), layout_(std::move(layout)),
          registry_(std::move(registry)), generation_(generation.ticket())
    {}

    const rhi::DescriptorSetAllocation& allocation() const override { return allocation_; }

    const rhi::DescriptorLayoutDesc& layout() const override { return layout_; }

    bool hasOriginalGeneralContent() const noexcept override
    {
        return originalSynchronous_ && originalSynchronous_->committed;
    }

    std::uint32_t originalGeneralContentContractVersion() const noexcept override
    {
        return hasOriginalGeneralContent() ? originalSynchronous_->contractVersion : 0u;
    }

    const char* originalScopedContentDiagnostic() const noexcept override
    {
        if (!originalSynchronous_)
            return nullptr;
        return originalSynchronous_->committed ? originalSynchronousDiagnostic()
                                               : "RHI-SYNCHRONOUS-GRAPHICS-WRITE-UNCOMMITTED";
    }

    template <class Pipeline>
    const char* beginSynchronousWrite(const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetElement* elements,
        std::size_t count,
        const rhi::OriginalGeneralDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;
        using Source = rhi::OriginalGraphicsBindingInspectionContract::PipelineSource<compute>;
        using Dependencies = rhi::OriginalCommandDescriptorDependencies;
        if (request.contractVersion != (compute ? 4u : 3u) || request.lossGroupJoinVersion != 4u
            || !request.lossGroupJoinOptIn || originalSynchronous_ || !pipeline || !arena || !count
            || count != layout_.bindings.size() || count > request.maxBindings || !elements
            || !writes_.empty() || !completedTextures_.empty())
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-FRESH-COMPLETE-GROUP";
        if (const auto* id = rhi::OriginalGeneralDescriptorWriteContract::validate(request))
            return id;
        if (const auto* id =
                rhi::OriginalCommandResourceSources::validateRegisteredLifetime(sources, device))
            return id;
        const auto* slot = rhi::findDescriptorSetSlot(pipeline->originalLayoutOwner()->desc(), set);
        const auto* expected = slot ? rhi::findDescriptorSetLayout(
                                          pipeline->originalLayoutOwner()->desc(), slot->layoutName)
                                    : nullptr;
        if (!expected || !rhi::descriptorLayoutDefinitionMatches(layout_, *expected))
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-LAYOUT";
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            if (!quotas[i] || quotas[i]->limitBytes() != request.maxCpuBytes[i])
                return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-QUOTA";
            if (core::SharedOwnerProjection::overlapsOwner(
                    quotas[i], sources->originalMetadataQuota()))
                return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-QUOTA-DOMAIN";
        }
        std::size_t pipelineIndex = SIZE_MAX;
        for (std::size_t i = 0u; i < sources->sources().size(); ++i)
            if (const auto* source =
                    std::get_if<std::shared_ptr<const Source>>(&sources->sources()[i]);
                source && *source
                && core::SharedOwnerProjection::sameOwner((*source)->originalPipeline(), pipeline))
                pipelineIndex = i;
        if (pipelineIndex == SIZE_MAX)
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-PIPELINE-SOURCE";
        // Authenticate every binding and the complete physical bound before the
        // first candidate allocation. No new inventory is constructed or retained.
        std::uint64_t retained = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            for (std::size_t j = 0u; j < i; ++j)
                if (elements[i].write.binding == elements[j].write.binding)
                    return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-DUPLICATE";
            std::uint64_t bytes = 0u;
            if (elements[i].write.resourceKind == rhi::DescriptorResourceKind::Buffer) {
                MetalOriginalBufferBindingInspection::Observation observed;
                if (const auto* id =
                        MetalOriginalBufferBindingInspection::inspect({compute ? 2u : 1u, true},
                            device,
                            sources,
                            pipelineIndex,
                            set,
                            elements[i],
                            observed))
                    return id;
                bytes = observed.allocationBytes;
            } else {
                MetalOriginalTextureBindingInspection::Observation observed;
                if (const auto* id =
                        MetalOriginalTextureBindingInspection::inspect({compute ? 2u : 1u, true},
                            device,
                            sources,
                            pipelineIndex,
                            set,
                            elements[i],
                            observed))
                    return id;
                bytes = observed.allocationBytes;
            }
            // Conservative repeated-binding sum; queried bytes, never texel formulas.
            if (!bytes || bytes > request.maxRetainedAllocationBytes - retained)
                return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-ALLOCATION-BUDGET";
            retained += bytes;
        }
        try {
            core::CpuMemoryQuotaScope ownerScope(quotas[0]);
            auto candidate = core::makeShared<OriginalSynchronousPublication>();
            candidate->pipeline =
                std::get<std::shared_ptr<const Source>>(sources->sources()[pipelineIndex]);
            candidate->arena = arena;
            candidate->quotas = quotas;
            candidate->quotaLimits = request.maxCpuBytes;
            candidate->revision = contentRevision();
            candidate->set = set;
            candidate->contractVersion = request.contractVersion;
            candidate->nativePipeline = metalRhiNativePayloadFor(*pipeline);
            candidate->nativePipelineIdentity = MetalRhiNativeResourceObservation(
                candidate->nativePipeline->nativeDevice, candidate->nativePipeline->pipelineState);
            candidate->nativeShaderObjects = {candidate->nativePipeline->shaderLibrary,
                compute ? candidate->nativePipeline->computeFunction
                        : candidate->nativePipeline->vertexFunction,
                compute ? std::shared_ptr<void>{} : candidate->nativePipeline->fragmentFunction};
            {
                core::CpuMemoryQuotaScope bindingScope(quotas[1]);
                candidate->bindings.reserve(count);
            }
            {
                core::CpuMemoryQuotaScope groupScope(quotas[2]);
                candidate->writes.reserve(count);
                core::pmr::Vector<MetalRhiNativeDescriptorBindingInput> nativeInputs;
                nativeInputs.reserve(count);
                for (std::size_t i = 0u; i < count; ++i) {
                    const auto owner = Dependencies::capture(*sources, elements[i].sourceIndex);
                    if (!owner)
                        return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-RESOURCE-SOURCE";
                    candidate->bindings.push_back({elements[i], *owner, {}});
                    auto& binding = candidate->bindings.back();
                    MetalRhiNativeDescriptorBindingInput nativeInput;
                    nativeInput.binding = *findBinding(elements[i].write.binding);
                    nativeInput.write = elements[i].write;
                    if (elements[i].write.resourceKind == rhi::DescriptorResourceKind::Buffer) {
                        MetalOriginalBufferBindingInspection::Observation observed;
                        if (const auto* id = MetalOriginalBufferBindingInspection::inspect(
                                {compute ? 2u : 1u, true},
                                device,
                                sources,
                                pipelineIndex,
                                set,
                                elements[i],
                                observed))
                            return id;
                        nativeInput.buffer = observed.buffer;
                        binding.native = std::move(observed);
                    } else {
                        MetalOriginalTextureBindingInspection::Observation observed;
                        if (const auto* id = MetalOriginalTextureBindingInspection::inspect(
                                {compute ? 2u : 1u, true},
                                device,
                                sources,
                                pipelineIndex,
                                set,
                                elements[i],
                                observed))
                            return id;
                        nativeInput.textureView = observed.view;
                        binding.native = std::move(observed);
                    }
                    nativeInputs.push_back(std::move(nativeInput));
                    candidate->writes.push_back(elements[i].write);
                }
                candidate->group =
                    createMetalRhiNativeDescriptorGroupPayload(layout_, nativeInputs);
                if (!candidate->group || !candidate->group->nativeRuntimeBacked
                    || candidate->group->nativeRuntimeDiagnosticOnly
                    || candidate->group->nativeHandleExposedToUpperLayer
                    || candidate->group->bindings.size() != count
                    || !core::SharedOwnerProjection::sameOwner(
                        candidate->group->nativeDevice, candidate->nativePipeline->nativeDevice))
                    return "RHI-METAL-SYNCHRONOUS-WRITE-NATIVE-GROUP";
                for (std::size_t i = 0u; i < count; ++i)
                    if (const auto* buffer =
                            std::get_if<MetalOriginalBufferBindingInspection::Observation>(
                                &candidate->bindings[i].native)) {
                        auto& native = candidate->group->bindings[i];
                        native.bufferOffsetBytes = buffer->offsetBytes;
                        native.bufferOffsetAlignment = buffer->alignmentBytes;
                        native.bufferRequiredBytes = buffer->requiredBytes;
                        native.graphicsBufferRanges = buffer->graphicsRanges;
                    }
                if (const auto* id =
                        MetalRhiPreparedDescriptorIdentityValidator::capture(*candidate->group))
                    return id;
            }
            originalSynchronous_ = std::move(candidate);
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-CPU-BUDGET";
        }
    }

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
        return beginSynchronousWrite(
            device, arena, pipeline, sources, set, elements, count, request, quotas);
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
        return beginSynchronousWrite(
            device, arena, pipeline, sources, set, elements, count, request, quotas);
    }

    const char* finishOriginalGeneralWriteImpl() noexcept override
    {
        if (!originalSynchronous_ || originalSynchronous_->committed)
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-NO-CANDIDATE";
        if (const auto* id = originalSynchronousDiagnostic())
            return id;
        writes_.swap(originalSynchronous_->writes);
        originalSynchronous_->committed = true;
        return nullptr;
    }

    const char* cancelOriginalGeneralWriteImpl() noexcept override
    {
        if (!originalSynchronous_ || originalSynchronous_->committed)
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-NO-CANDIDATE";
        originalSynchronous_.reset();
        return nullptr;
    }

    std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>
    originalCompletedDepthPublication(
        std::uint32_t binding, std::uint32_t element) const noexcept override
    {
        const auto* publication = completedPublication(binding, element);
        return isCurrentAllocation() && publication ? publication->depth : nullptr;
    }

    bool belongsTo(const std::shared_ptr<MetalRhiResourceRegistry>& registry) const noexcept
    {
        return registry && core::SharedOwnerProjection::sameOwner(registry_, registry);
    }

    rhi::ValidationResult writeImpl(const rhi::DescriptorBindingWrite& write) override
    {
        if (originalSynchronous_)
            return {false, "RHI-SYNCHRONOUS-GRAPHICS-WRITE-IMMUTABLE"};
        if (completedPublication(write.binding, write.arrayElement))
            return {false, "RHI-METAL-COMPLETED-TEXTURE-ORIGINAL-PUBLICATION-REQUIRED"};
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Metal RHI descriptor set allocation is stale"};
        }
        const auto* binding = findBinding(write.binding);
        if (!binding || binding->type != write.type || !write.resource.isValid()) {
            return {false, "Metal RHI descriptor write does not match layout"};
        }
        if (write.physicalLowering.enabled) {
            return {false,
                "Metal RHI native framebuffer-fetch descriptor cannot consume a separate-pass "
                "lowering"};
        }
        if (write.arrayElement >= binding->arrayCount) {
            return {false, "Metal RHI descriptor array element is outside the layout range"};
        }
        if (binding->arrayCount == 1u && write.arrayElement != 0u) {
            return {false, "Metal RHI scalar descriptor binding requires array element zero"};
        }
        if (binding->tableBacked) {
            return {
                false, "Metal RHI direct descriptor lowering cannot consume table-backed bindings"};
        }
        const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
        const auto resourceValidation = validateRegisteredResource(resource ? &*resource : nullptr,
            write.type,
            write.resourceKind,
            binding->textureViewDimensionConstrained,
            binding->textureViewDimension,
            binding->depthTexture);
        if (!resourceValidation.success) {
            return resourceValidation;
        }
        core::pmr::Vector<rhi::DescriptorBindingWrite> candidateWrites(
            writes_, writes_.get_allocator());
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
        writes_.swap(candidateWrites);
        return {true, ""};
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentExpectation& content) override
    {
        if (originalSynchronous_)
            return {false, "RHI-SYNCHRONOUS-GRAPHICS-WRITE-IMMUTABLE"};
        return writeOriginalCompletedTextureImpl(write,
            source,
            content,
            {1u, true, 1u, 1u, rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount});
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentExpectation& content,
        const rhi::OriginalCompletedTextureDescriptorRequest& request) override
    {
        if (originalSynchronous_)
            return {false, "RHI-SYNCHRONOUS-GRAPHICS-WRITE-IMMUTABLE"};
        const auto* binding = findBinding(write.binding);
        if (!binding || binding->type != rhi::DescriptorType::SampledTexture || binding->tableBacked
            || write.arrayElement >= binding->arrayCount || !source)
            return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-ELEMENT"};
        const auto parent =
            metalRhiNativePayloadFor(*source->originalParentLease()->originalObject());
        const auto view = metalRhiNativePayloadFor(*source->originalObject());
        if (!parent || !view)
            return {false, "RHI-METAL-COMPLETED-TEXTURE-NATIVE-IDENTITY"};
        CompletedTexturePublication publication{write,
            source,
            content,
            MetalRhiNativeResourceObservation(parent->resource),
            MetalRhiNativeResourceObservation(view->resource)};
        publication.colorPublicationVersion = request.contractVersion;
        return publishOriginalTexture(write, publication);
    }

    rhi::ValidationResult writeOriginalCompletedTextureImpl(
        const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& depth) override
    {
        if (originalSynchronous_)
            return {false, "RHI-SYNCHRONOUS-GRAPHICS-WRITE-IMMUTABLE"};
        const auto& source = depth->originalViewSource();
        const auto parent =
            metalRhiNativePayloadFor(*source->originalParentLease()->originalObject());
        const auto view = metalRhiNativePayloadFor(*source->originalObject());
        if (!parent || !view)
            return {false, "RHI-METAL-COMPLETED-DEPTH-NATIVE-IDENTITY"};
        CompletedTexturePublication publication{write,
            source,
            {},
            MetalRhiNativeResourceObservation(parent->resource),
            MetalRhiNativeResourceObservation(view->resource)};
        publication.depth = depth;
        return publishOriginalTexture(write, publication);
    }

    rhi::ValidationResult writeOriginalTextureCellImpl(const rhi::DescriptorBindingWrite& write,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source,
        const rhi::GpuTexture::ContentRangeExpectation& identity,
        const rhi::OriginalTextureCellDescriptorRequest& request) override
    {
        if (originalSynchronous_)
            return {false, "RHI-SYNCHRONOUS-GRAPHICS-WRITE-IMMUTABLE"};
        const auto parent =
            metalRhiNativePayloadFor(*source->originalParentLease()->originalObject());
        const auto view = metalRhiNativePayloadFor(*source->originalObject());
        if (!parent || !view)
            return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-NATIVE-IDENTITY"};
        const CompletedTexturePublication publication{write,
            source,
            {},
            MetalRhiNativeResourceObservation(parent->resource),
            MetalRhiNativeResourceObservation(view->resource),
            identity,
            request};
        return publishOriginalTexture(write, publication);
    }

    rhi::ValidationResult publishOriginalTexture(
        const rhi::DescriptorBindingWrite& write, const CompletedTexturePublication& publication)
    {
        const auto* binding = findBinding(write.binding);
        if (!binding)
            return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-ELEMENT"};
        const auto& source = publication.source;
        if (publication.cellIdentity)
            if (const auto* id = rhi::OriginalTextureCellDescriptorContract::validateBinding(
                    *binding, write, publication.cellRequest.contractVersion))
                return {false, id};
        const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
        const auto validation = validateRegisteredResource(resource ? &*resource : nullptr,
            write.type,
            write.resourceKind,
            binding->textureViewDimensionConstrained,
            binding->textureViewDimension,
            binding->depthTexture,
            &publication);
        if (!validation.success)
            return validation;
        const auto sameElement = [&](const auto& candidate) {
            return candidate.write.binding == write.binding
                && candidate.write.arrayElement == write.arrayElement;
        };
        const auto prior =
            std::find_if(completedTextures_.begin(), completedTextures_.end(), sameElement);
        if (prior != completedTextures_.end()
            && (bool(prior->cellIdentity) != bool(publication.cellIdentity)
                || bool(prior->depth) != bool(publication.depth)
                || !core::SharedOwnerProjection::sameOwner(prior->source, source)))
            return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-SOURCE-REPLACEMENT"};
        try {
            core::pmr::Vector<CompletedTexturePublication> candidates(
                completedTextures_.get_allocator());
            candidates.reserve(
                completedTextures_.size() + (prior == completedTextures_.end() ? 1u : 0u));
            for (const auto& candidate : completedTextures_)
                if (!sameElement(candidate))
                    candidates.push_back(candidate);
            candidates.push_back(publication);
            auto writes =
                core::pmr::Vector<rhi::DescriptorBindingWrite>(writes_, writes_.get_allocator());
            const auto element =
                std::find_if(writes.begin(), writes.end(), [&](const auto& candidate) {
                    return candidate.binding == write.binding
                        && candidate.arrayElement == write.arrayElement;
                });
            if (element == writes.end())
                writes.push_back(write);
            else
                *element = write;
            writes_.swap(writes);
            completedTextures_.swap(candidates);
            return {true, {}};
        } catch (const std::bad_alloc&) {
            // Quota exhaustion must not allocate again while constructing
            // the failure diagnostic (the shared RHI short OOM label).
            return {false, "RHI-OOM"};
        }
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

    bool hasWrittenBindingElement(std::uint32_t binding, std::uint32_t arrayElement) const override
    {
        if (!isCurrentAllocation()) {
            return false;
        }
        return std::find_if(writes_.begin(), writes_.end(), [&](const auto& write) {
            return write.binding == binding && write.arrayElement == arrayElement;
        }) != writes_.end();
    }

    rhi::ValidationResult validateBinding(std::uint32_t binding) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Metal RHI descriptor set validation rejected "
                "after arena reset"};
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
            : rhi::ValidationResult{false, "Metal RHI descriptor binding is not written"};
    }

    rhi::ValidationResult validateBindingElement(
        std::uint32_t binding, std::uint32_t arrayElement) const override
    {
        if (originalSynchronous_) {
            if (!hasOriginalGeneralContent())
                return {false, "RHI-SYNCHRONOUS-GRAPHICS-WRITE-UNCOMMITTED"};
            if (const auto* id = originalSynchronousDiagnostic())
                return {false, id};
            return hasWrittenBindingElement(binding, arrayElement)
                ? rhi::ValidationResult{true, ""}
                : rhi::ValidationResult{false, "RHI-SYNCHRONOUS-GRAPHICS-WRITE-BINDING"};
        }

        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Metal RHI descriptor set validation rejected "
                "after arena reset"};
        }
        const auto* desc = findBinding(binding);
        for (const auto& write : writes_) {
            if (write.binding != binding || write.arrayElement != arrayElement)
                continue;
            const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
            return desc
                ? validateRegisteredResource(resource ? &*resource : nullptr,
                      write.type,
                      write.resourceKind,
                      desc->textureViewDimensionConstrained,
                      desc->textureViewDimension,
                      desc->depthTexture,
                      completedPublication(binding, arrayElement))
                : rhi::ValidationResult{false, "Metal RHI descriptor binding is not declared"};
        }
        return {false, "Metal RHI descriptor array element is not written"};
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

    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> nativePayload() const
    {
        if (originalSynchronous_)
            return {};
        if (!isCurrentAllocation() || !allBindingsWritten(writes_.size())) {
            return nullptr;
        }
        // Cold Publication/1 owns authentic content provenance. The ordinary
        // descriptor encoder has no original graph/seal/completion authority;
        // only the typed original exporter below may expose these images.
        if (!completedTextures_.empty())
            return nullptr;
        // DescriptorResourceLiveness/1: a set owns logical IDs, never a
        // native resource payload. Resolve on the recording path so a local
        // view may be released after the command has retained its exact
        // native payload for GPU completion.
        return createMetalRhiNativeDescriptorGroupPayload(layout_, nativeBindings(writes_));
    }

    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> originalOrdinaryGraphicsPayload(
        const rhi::OriginalCommandDescriptorSetSource& source, bool ordinaryDepth) const
    {
        if (originalSynchronous_)
            return {};
        using Domain = rhi::semantics::OriginalBindlessOrdinaryColorDomain;
        if (source.originalObject() != this || !isCurrentAllocation()
            || !allBindingsWritten(writes_.size()) || completedTextures_.empty())
            return {};
        std::size_t images = 0u;
        for (const auto& element : source.frozenElements()) {
            const auto& write = element.declaration.write;
            const auto* binding = findBinding(write.binding);
            if (!binding || !validateBindingElement(write.binding, write.arrayElement).success)
                return {};
            if (binding->type != rhi::DescriptorType::SampledTexture
                && binding->type != rhi::DescriptorType::Sampler)
                continue; // Original Root/selected-entry buffer validation is unchanged.
            const bool depth =
                ordinaryDepth && (binding->depthTexture || binding->comparisonSampler);
            if (depth ? rhi::semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                            rhi::Backend::Metal, *binding)
                        != nullptr
                      : !Domain::acceptsGraphicsBinding(*binding))
                return {};
            if (binding->type == rhi::DescriptorType::Sampler)
                continue;
            const auto* owner =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    &element.resourceOwner);
            const auto* publication = completedPublication(write.binding, write.arrayElement);
            if (!owner || !*owner || !publication || publication->cellIdentity
                || !core::SharedOwnerProjection::sameOwner(publication->source, *owner))
                return {};
            if (depth) {
                if (source.sourceContractVersion() != 6u || !publication->depth
                    || publication->content
                    || element.completedDepth.get() != publication->depth.get()
                    || !core::SharedOwnerProjection::sameOwner(
                        element.completedDepth, publication->depth)
                    || rhi::OriginalCompletedDepthTextureSource::validateOriginal(
                        publication->depth))
                    return {};
            } else if (!publication->content || publication->depth
                || Domain::completedViewDiagnostic(
                    (*owner)->originalParentLease()->originalObject()->desc(),
                    (*owner)->originalObject()->desc(),
                    publication->content->controlVersion(),
                    publication->colorPublicationVersion == 5u ? 2u : 0u))
                return {};
            ++images;
        }
        if (!images || images != completedTextures_.size())
            return {};
        auto payload = createMetalRhiNativeDescriptorGroupPayload(layout_, nativeBindings(writes_));
        if (payload)
            for (const auto& publication : completedTextures_) {
                payload->originalCompletedTextureSources.push_back(publication.source);
                if (publication.depth)
                    payload->originalCompletedDepthSources.push_back(publication.depth);
            }
        return payload;
    }

    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> originalCompletedImagePayload(
        const rhi::BindlessSnapshot& snapshot, rhi::OriginalBindlessCompletedImageScope scope) const
    {
        if (originalSynchronous_)
            return {};
        if (snapshot.descriptorSet() != this
            || !rhi::OriginalBindlessCompletedImageDomain::acceptsPlan(snapshot.plan(), scope)
            || !rhi::semantics::OriginalBindlessCompletedImageLowering::lowering(
                rhi::Backend::Metal, scope)
                .preparationAvailable
            || !isCurrentAllocation() || !allBindingsWritten(writes_.size())
            || completedTextures_.empty())
            return {};
        for (const auto& publication : completedTextures_)
            if (publication.cellIdentity || publication.depth
                || publication.colorPublicationVersion != 1u)
                return {};
        for (const auto& write : writes_) {
            const auto* binding = findBinding(write.binding);
            if (!binding
                || !rhi::OriginalBindlessCompletedImageDomain::acceptsStages(
                    scope, binding->type, binding->stages)
                || binding->type == rhi::DescriptorType::StorageTexture
                || binding->type == rhi::DescriptorType::InputAttachment
                || (binding->type == rhi::DescriptorType::SampledTexture
                    && !completedPublication(write.binding, write.arrayElement))
                || !validateBindingElement(write.binding, write.arrayElement).success)
                return {};
        }
        auto payload = createMetalRhiNativeDescriptorGroupPayload(layout_, nativeBindings(writes_));
        if (payload)
            for (const auto& publication : completedTextures_)
                payload->originalCompletedTextureSources.push_back(publication.source);
        return payload;
    }

    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> completedDepthPayload(
        const rhi::BindlessSnapshot& snapshot) const
    {
        if (originalSynchronous_)
            return {};
        const auto& plan = snapshot.plan();
        if (snapshot.descriptorSet() != this
            || (!rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 6u)
                && !rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 6u))
            || !isCurrentAllocation() || !allBindingsWritten(writes_.size())
            || completedTextures_.empty())
            return {};
        for (const auto& publication : completedTextures_)
            if (!publication.depth || publication.content || publication.cellIdentity)
                return {};
        std::size_t images = 0u;
        for (const auto& write : writes_) {
            const auto* binding = findBinding(write.binding);
            if (!binding || binding->stages != rhi::ShaderStage::Compute
                || (binding->type != rhi::DescriptorType::SampledTexture
                    && binding->type != rhi::DescriptorType::Sampler
                    && binding->type != rhi::DescriptorType::StorageBuffer
                    && binding->type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                || !validateBindingElement(write.binding, write.arrayElement).success)
                return {};
            if (binding->type == rhi::DescriptorType::SampledTexture) {
                const auto* publication = completedPublication(write.binding, write.arrayElement);
                if (!publication || !publication->depth || !binding->depthTexture)
                    return {};
                ++images;
            }
        }
        if (images != completedTextures_.size())
            return {};
        auto payload = createMetalRhiNativeDescriptorGroupPayload(layout_, nativeBindings(writes_));
        if (payload)
            for (const auto& publication : completedTextures_) {
                payload->originalCompletedTextureSources.push_back(publication.source);
                payload->originalCompletedDepthSources.push_back(publication.depth);
            }
        return payload;
    }

    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> imageCellPayload(
        const rhi::BindlessSnapshot& snapshot) const
    {
        if (originalSynchronous_)
            return {};
        if (snapshot.descriptorSet() != this
            || !rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                snapshot.plan())
            || !isCurrentAllocation() || !allBindingsWritten(writes_.size())
            || completedTextures_.empty())
            return {};
        bool storage = false;
        const bool produced =
            rhi::semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                snapshot.plan());
        for (const auto& write : writes_) {
            const auto* binding = findBinding(write.binding);
            if (!binding || binding->stages != rhi::ShaderStage::Compute
                || !validateBindingElement(write.binding, write.arrayElement).success)
                return {};
            if (binding->type == rhi::DescriptorType::SampledTexture
                || binding->type == rhi::DescriptorType::StorageTexture) {
                const auto* proof = completedPublication(write.binding, write.arrayElement);
                if (!proof
                    || rhi::semantics::OriginalBindlessPreparationLowering::imageProofDiagnostic(
                        snapshot.plan(), *binding, bool(proof->depth), bool(proof->cellIdentity)))
                    return {};
                if (produced
                    && !rhi::OriginalTextureCellDescriptorContract::isProducedImageRead(
                        proof->cellRequest.contractVersion))
                    return {};
                storage =
                    storage || produced || binding->type == rhi::DescriptorType::StorageTexture;
            }
        }
        if (!storage)
            return {};
        auto payload = createMetalRhiNativeDescriptorGroupPayload(layout_, nativeBindings(writes_));
        if (payload)
            for (const auto& publication : completedTextures_) {
                if (publication.cellIdentity)
                    payload->originalTextureCellSources.push_back(publication.source);
                if (publication.depth)
                    payload->originalCompletedDepthSources.push_back(publication.depth);
            }
        return payload;
    }

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

    const char* originalSynchronousDiagnostic() const noexcept
    {
        if (!originalSynchronous_ || !isCurrentAllocation())
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-RETIRED";
        const auto& publication = *originalSynchronous_;
        if (contentRevision() != publication.revision + std::uint64_t(publication.committed))
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-REVISION";
        if (const auto* id = originalResourceRegistration().diagnostic())
            return id;
        if (const auto* id = publication.arena->originalResourceRegistration().diagnostic())
            return id;
        for (std::size_t i = 0u; i < publication.quotas.size(); ++i)
            if (publication.quotas[i]->limitBytes() != publication.quotaLimits[i])
                return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-QUOTA";
        if (!publication.group || !publication.group->nativeRuntimeBacked
            || publication.group->nativeRuntimeDiagnosticOnly
            || publication.group->nativeHandleExposedToUpperLayer
            || publication.group->bindings.size() != publication.bindings.size()
            || !publication.group->originalCompletedTextureSources.empty()
            || !publication.group->originalCompletedDepthSources.empty()
            || !publication.group->originalTextureCellSources.empty())
            return "RHI-METAL-SYNCHRONOUS-WRITE-NATIVE-GROUP";
        if (const auto* id =
                MetalRhiPreparedDescriptorIdentityValidator::validate(*publication.group))
            return id;
        return std::visit(
            [&](const auto& source) -> const char* {
                using T = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-PIPELINE-SOURCE";
                else {
                    constexpr bool compute = std::is_same_v<T,
                        std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>;
                    if (const auto* id =
                            std::remove_const_t<typename T::element_type>::validateOriginal(source))
                        return id;
                    const auto native = metalRhiNativePayloadFor(*source->originalPipeline());
                    if (!core::SharedOwnerProjection::sameOwner(native, publication.nativePipeline)
                        || !core::SharedOwnerProjection::sameOwner(
                            native->nativeDevice, publication.group->nativeDevice))
                        return "RHI-METAL-SYNCHRONOUS-WRITE-NATIVE-PIPELINE";
                    if (!publication.nativePipelineIdentity.matches(
                            native->nativeDevice, native->pipelineState))
                        return "RHI-METAL-SYNCHRONOUS-WRITE-NATIVE-PIPELINE";
                    const std::array<std::shared_ptr<void>, 3u> shaderObjects{native->shaderLibrary,
                        compute ? native->computeFunction : native->vertexFunction,
                        compute ? std::shared_ptr<void>{} : native->fragmentFunction};
                    for (std::size_t i = 0u; i < shaderObjects.size(); ++i)
                        if (!core::SharedOwnerProjection::sameOwner(
                                shaderObjects[i], publication.nativeShaderObjects[i]))
                            return "RHI-METAL-SYNCHRONOUS-WRITE-NATIVE-PIPELINE";
                    for (std::size_t i = 0u; i < publication.bindings.size(); ++i) {
                        const auto& binding = publication.bindings[i];
                        const auto& n = publication.group->bindings[i];
                        const auto* desc = findBinding(binding.element.write.binding);
                        if (!desc || n.binding != desc->binding || n.type != desc->type
                            || n.stages != desc->stages || n.depthTexture != desc->depthTexture
                            || n.arrayCount != 1u || n.arrayElement
                            || n.dynamicOffsetIndex != UINT32_MAX
                            || n.resourceId != binding.element.write.resource
                            || n.resourceKind != binding.element.write.resourceKind || n.sampler)
                            return "RHI-METAL-SYNCHRONOUS-WRITE-NATIVE-BINDING";
                        if (const auto* saved =
                                std::get_if<MetalOriginalBufferBindingInspection::Observation>(
                                    &binding.native)) {
                            MetalOriginalBufferBindingInspection::Observation current;
                            if (const auto* id =
                                    MetalOriginalBufferBindingInspection::inspectRetained<compute>(
                                        source,
                                        publication.set,
                                        binding.element,
                                        binding.owner,
                                        current))
                                return id;
                            const auto resource = registry_
                                ? registry_->find(binding.element.write.resource)
                                : std::nullopt;
                            if (!resource
                                || !core::SharedOwnerProjection::sameOwner(
                                    resource->buffer, current.buffer)
                                || !core::SharedOwnerProjection::sameOwner(n.buffer, saved->buffer)
                                || n.textureView || n.argumentKind != MetalRhiArgumentKind::Buffer
                                || !saved->nativeIdentity.matches(current.buffer->resource)
                                || current.physicalSlot != saved->physicalSlot
                                || current.stages != saved->stages
                                || current.allocationBytes != saved->allocationBytes
                                || current.offsetBytes != n.bufferOffsetBytes
                                || current.requiredBytes != n.bufferRequiredBytes
                                || current.alignmentBytes != n.bufferOffsetAlignment)
                                return "RHI-METAL-SYNCHRONOUS-WRITE-BUFFER-IDENTITY";
                            if constexpr (compute) {
                                if (!current.computeFact.matches(saved->computeFact))
                                    return "RHI-METAL-SYNCHRONOUS-WRITE-BUFFER-FACTS";
                            } else {
                                for (std::size_t j = 0u; j < current.graphicsRanges.size(); ++j) {
                                    const auto& a = current.graphicsRanges[j];
                                    const auto& b = saved->graphicsRanges[j];
                                    const auto& c = n.graphicsBufferRanges[j];
                                    if (a.stage != b.stage || a.stage != c.stage
                                        || a.offset != b.offset || a.offset != c.offset
                                        || !a.fact.matches(b.fact) || !a.fact.matches(c.fact))
                                        return "RHI-METAL-SYNCHRONOUS-WRITE-BUFFER-FACTS";
                                }
                            }
                        } else {
                            const auto& savedTexture =
                                std::get<MetalOriginalTextureBindingInspection::Observation>(
                                    binding.native);
                            MetalOriginalTextureBindingInspection::Observation current;
                            if (const auto* id =
                                    MetalOriginalTextureBindingInspection::inspectRetained<compute>(
                                        source,
                                        publication.set,
                                        binding.element,
                                        binding.owner,
                                        current))
                                return id;
                            const auto resource = registry_
                                ? registry_->find(binding.element.write.resource)
                                : std::nullopt;
                            if (!resource
                                || !core::SharedOwnerProjection::sameOwner(
                                    resource->textureView, current.view)
                                || !core::SharedOwnerProjection::sameOwner(
                                    n.textureView, savedTexture.view)
                                || n.buffer || n.argumentKind != MetalRhiArgumentKind::Texture
                                || !savedTexture.parentIdentity.matches(current.parent->resource)
                                || !savedTexture.viewIdentity.matches(current.view->resource)
                                || current.physicalSlot != savedTexture.physicalSlot
                                || current.stages != savedTexture.stages
                                || current.allocationBytes != savedTexture.allocationBytes)
                                return "RHI-METAL-SYNCHRONOUS-WRITE-TEXTURE-IDENTITY";
                            for (std::size_t j = 0u; j < current.facts.size(); ++j)
                                if (!current.facts[j].matches(savedTexture.facts[j]))
                                    return "RHI-METAL-SYNCHRONOUS-WRITE-TEXTURE-FACTS";
                        }
                    }
                    return nullptr;
                }
            },
            publication.pipeline);
    }

    template <class Pipeline>
    const char* diagnoseSynchronousSource(const std::shared_ptr<const Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (!hasOriginalGeneralContent())
            return "RHI-SYNCHRONOUS-GRAPHICS-WRITE-UNCOMMITTED";
        if (const auto* id = originalSynchronousDiagnostic())
            return id;
        const auto& publication = *originalSynchronous_;
        const bool matches = std::visit(
            [&](const auto& source) {
                using T = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return false;
                else
                    return source
                        && core::SharedOwnerProjection::sameOwner(
                            pipeline, source->originalPipeline());
            },
            publication.pipeline);
        if (!matches || set != publication.set || !elements || count != publication.bindings.size())
            return "RHI-SYNCHRONOUS-GRAPHICS-SOURCE-PIPELINE-GROUP";
        if (const auto* id =
                rhi::OriginalCommandDescriptorDependencies::validateInventoryOwner(sources, quota))
            return id;
        for (const auto& owner : publication.quotas)
            if (core::SharedOwnerProjection::overlapsOwner(owner, quota))
                return "RHI-SYNCHRONOUS-GRAPHICS-SOURCE-QUOTA";
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& original = publication.bindings[i];
            const auto& frozen = elements[i];
            const auto& a = frozen.declaration;
            const auto& b = original.element;
            if (!frozen.observation.matchesObject(*this)
                || !rhi::descriptorBindingWriteMatches(a.write, b.write)
                || a.sourceIndex != b.sourceIndex
                || a.registrationGeneration != b.registrationGeneration
                || a.bufferAccessOffset != b.bufferAccessOffset
                || a.bufferAccessBytes != b.bufferAccessBytes || a.stages != b.stages
                || a.access != b.access || a.dynamicOffset != b.dynamicOffset
                || !rhi::OriginalCommandDescriptorDependencies::matches(
                    *sources, a.sourceIndex, original.owner))
                return "RHI-SYNCHRONOUS-GRAPHICS-SOURCE-BINDING";
        }
        return nullptr;
    }

    const char* diagnoseOriginalGeneralSourceImpl(
        const std::shared_ptr<const rhi::ComputePipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseSynchronousSource(pipeline, sources, set, elements, count, quota);
    }

    const char* diagnoseOriginalGeneralSourceImpl(
        const std::shared_ptr<const rhi::RenderPipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept override
    {
        return diagnoseSynchronousSource(pipeline, sources, set, elements, count, quota);
    }

    std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> originalSynchronousPayload(
        const rhi::OriginalCommandDescriptorSetSource& source) const noexcept
    {
        return source.originalObject() == this && hasOriginalGeneralContent()
                && !originalSynchronousDiagnostic()
            ? originalSynchronous_->group
            : nullptr;
    }

private:
    const CompletedTexturePublication* completedPublication(
        std::uint32_t binding, std::uint32_t element) const noexcept
    {
        const auto found = std::find_if(
            completedTextures_.begin(), completedTextures_.end(), [&](const auto& candidate) {
                return candidate.write.binding == binding
                    && candidate.write.arrayElement == element;
            });
        return found == completedTextures_.end() ? nullptr : &*found;
    }

    const rhi::DescriptorBindingDesc* findBinding(std::uint32_t binding) const
    {
        return findBindingInLayout(layout_, binding);
    }

    bool allBindingsWritten(std::size_t count) const
    {
        std::size_t required = 0;
        for (const auto& binding : layout_.bindings) {
            required += binding.arrayCount;
        }
        return count == required;
    }

    core::pmr::Vector<MetalRhiNativeDescriptorBindingInput> nativeBindings(
        const core::pmr::Vector<rhi::DescriptorBindingWrite>& writes) const
    {
        core::pmr::Vector<MetalRhiNativeDescriptorBindingInput> bindings{
            &core::defaultCpuMemoryResource()};
        bindings.reserve(writes.size());
        for (const auto& write : writes) {
            const auto* binding = findBinding(write.binding);
            const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
            if (!binding || !resource) {
                continue;
            }
            std::uint32_t dynamicOffsetIndex = 0;
            for (const auto& candidate : layout_.bindings) {
                if (candidate.dynamicOffset && candidate.binding < write.binding) {
                    ++dynamicOffsetIndex;
                }
            }
            bindings.push_back({
                *binding,
                write,
                write.arrayElement,
                binding->dynamicOffset ? dynamicOffsetIndex : UINT32_MAX,
                resource->buffer,
                resource->textureView,
                resource->sampler,
            });
        }
        return bindings;
    }

    std::shared_ptr<OriginalSynchronousPublication> originalSynchronous_;
    rhi::DescriptorSetAllocation allocation_;
    rhi::DescriptorLayoutDesc layout_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    core::LifetimeEpoch::Ticket generation_;
    core::pmr::Vector<rhi::DescriptorBindingWrite> writes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<CompletedTexturePublication> completedTextures_{
        &core::defaultCpuMemoryResource()};
};

class MetalRhiDescriptorTable final : public rhi::DescriptorTable {
public:
    explicit MetalRhiDescriptorTable(rhi::DescriptorTableAllocation allocation,
        rhi::DescriptorTableDesc desc,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        const core::LifetimeEpoch& generation)
        : allocation_(std::move(allocation)), desc_(std::move(desc)),
          registry_(std::move(registry)), generation_(generation.ticket()),
          descriptors_(desc_.capacity, &core::defaultCpuMemoryResource())
    {}

    const rhi::DescriptorTableAllocation& allocation() const override { return allocation_; }

    const rhi::DescriptorTableDesc& desc() const override { return desc_; }

    rhi::ValidationResult writeImpl(const rhi::DescriptorTableWrite& write) override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Metal RHI descriptor table allocation is stale"};
        }
        if (write.index >= desc_.capacity || write.type != desc_.type
            || !write.resource.isValid()) {
            return {false, "Metal RHI descriptor table write is invalid"};
        }
        const auto expectedKind = write.type == rhi::DescriptorType::Sampler
            ? rhi::DescriptorResourceKind::Sampler
            : (write.type == rhi::DescriptorType::UniformBuffer
                  || write.type == rhi::DescriptorType::StorageBuffer
                  || write.type == rhi::DescriptorType::ReadOnlyStorageBuffer)
            ? rhi::DescriptorResourceKind::Buffer
            : rhi::DescriptorResourceKind::TextureView;
        if (write.resourceKind != expectedKind) {
            return {false, "Metal RHI descriptor table resource kind mismatch"};
        }
        const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
        const auto resourceValidation = validateRegisteredResource(resource ? &*resource : nullptr,
            write.type,
            write.resourceKind,
            desc_.textureViewDimensionConstrained,
            desc_.textureViewDimension,
            desc_.depthTexture);
        if (!resourceValidation.success) {
            return resourceValidation;
        }
        core::pmr::Vector<Descriptor> candidateDescriptors(
            descriptors_, descriptors_.get_allocator());
        candidateDescriptors[write.index] = {
            write,
            true,
        };
        descriptors_.swap(candidateDescriptors);
        return {true, ""};
    }

    bool isCurrentAllocation() const override
    {
        return allocation_.success && generation_.isCurrent();
    }

    bool hasWrittenDescriptor(std::uint32_t index) const override
    {
        return isCurrentAllocation() && index < descriptors_.size() && descriptors_[index].written;
    }

    rhi::ValidationResult validateDescriptor(std::uint32_t index) const override
    {
        if (!isCurrentAllocation()) {
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: Metal RHI descriptor table validation rejected "
                "after arena reset"};
        }
        if (!hasWrittenDescriptor(index)) {
            return {false, "Metal RHI descriptor table entry is not written"};
        }
        const auto& write = descriptors_[index].write;
        const auto resource = registry_ ? registry_->find(write.resource) : std::nullopt;
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
            isCurrentAllocation() ? static_cast<std::uint32_t>(std::count_if(descriptors_.begin(),
                                        descriptors_.end(),
                                        [](const auto& descriptor) {
                                            return descriptor.written;
                                        }))
                                  : 0u,
            true,
            false,
        };
    }

    std::shared_ptr<MetalRhiNativeDescriptorTablePayload> nativePayload() const
    {
        return isCurrentAllocation() ? buildNativePayload(descriptors_) : nullptr;
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

private:
    struct Descriptor {
        rhi::DescriptorTableWrite write;
        bool written = false;
    };

    std::shared_ptr<MetalRhiNativeDescriptorTablePayload> buildNativePayload(
        const core::pmr::Vector<Descriptor>& descriptors) const
    {
        core::pmr::Vector<MetalRhiNativeDescriptorTableEntry> entries{
            &core::defaultCpuMemoryResource()};
        entries.reserve(descriptors.size());
        for (const auto& descriptor : descriptors) {
            if (!descriptor.written)
                continue;
            const auto resource =
                registry_ ? registry_->find(descriptor.write.resource) : std::nullopt;
            if (!resource) {
                return {};
            }
            entries.push_back({
                descriptor.write.index,
                descriptor.write.type,
                descriptor.write.resourceKind,
                resource->buffer,
                resource->textureView,
                resource->sampler,
            });
        }
        return createMetalRhiNativeDescriptorTablePayload(desc_, entries);
    }

    rhi::DescriptorTableAllocation allocation_;
    rhi::DescriptorTableDesc desc_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    core::LifetimeEpoch::Ticket generation_;
    core::pmr::Vector<Descriptor> descriptors_;
};

class MetalRhiDescriptorArena final : public rhi::DescriptorArena {
public:
    MetalRhiDescriptorArena(rhi::DescriptorArenaDesc desc,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        rhi::semantics::DirectObjectTelemetryLease telemetryLease,
        rhi::semantics::DirectObjectBudgetLease budgetLease)
        : desc_(std::move(desc)), registry_(std::move(registry)),
          telemetryLease_(std::move(telemetryLease)), budgetLease_(std::move(budgetLease))
    {}

    bool belongsTo(const std::shared_ptr<MetalRhiResourceRegistry>& registry) const noexcept
    {
        return registry && core::SharedOwnerProjection::sameOwner(registry_, registry);
    }

    rhi::DescriptorSetAllocation allocateSet(const rhi::DescriptorLayoutDesc& layout) override
    {
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        const auto validation = rhi::validateDescriptorLayout(layout);
        if (!validation.success) {
            return {false, validation.diagnostics, allocatedSets_, 0};
        }
        std::uint32_t descriptorCount = 0u;
        const auto count = rhi::countDescriptorElements(layout, descriptorCount);
        if (!count.success)
            return {false, count.diagnostics, 0u, 0u};
        if (allocatedSets_ >= desc_.maxSets || allocatedDescriptors_ > desc_.maxDescriptors
            || descriptorCount > desc_.maxDescriptors - allocatedDescriptors_) {
            return {false, "Metal RHI descriptor arena capacity exceeded", allocatedSets_, 0};
        }
        allocatedDescriptors_ += descriptorCount;
        return {true, "", allocatedSets_++, descriptorCount};
    }

    arp::core::UniquePtr<rhi::DescriptorSet> allocateDescriptorSet(
        const rhi::DescriptorSetDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<rhi::DescriptorSet> {
                auto allocation = allocateSet(copied.layout);
                return allocation.success
                    ? arp::core::makeUnique<MetalRhiDescriptorSet>(
                          allocation, std::move(copied.layout), registry_, generation_)
                    : nullptr;
            },
            allocatedSets_,
            allocatedTables_,
            allocatedDescriptors_);
    }

    rhi::DescriptorTableAllocation allocateTable(const rhi::DescriptorTableDesc& desc) override
    {
        if (!generation_.isActive()) {
            return {false, "RHI-DESCRIPTOR-GENERATION-EXHAUSTED", 0u, 0u};
        }
        if (desc.type == rhi::DescriptorType::InputAttachment) {
            return {false,
                "Metal RHI framebuffer-fetch input attachments cannot be table-backed",
                allocatedTables_,
                0};
        }
        if (desc.label.empty() || desc.capacity == 0 || desc.stages == rhi::ShaderStage::None) {
            return {false, "Metal RHI descriptor table desc is invalid", allocatedTables_, 0};
        }
        const auto shape = rhi::validateDescriptorTextureRequirement(
            desc.type, desc.textureViewDimensionConstrained, desc.depthTexture);
        if (!shape.success)
            return {false, shape.diagnostics, allocatedTables_, 0u};
        if (allocatedTables_ >= desc_.maxSets || allocatedDescriptors_ > desc_.maxDescriptors
            || desc.capacity > desc_.maxDescriptors - allocatedDescriptors_) {
            return {false,
                "Metal RHI descriptor arena table capacity exceeded",
                allocatedTables_,
                desc.capacity};
        }
        ++allocatedTables_;
        ++allocatedSets_;
        allocatedDescriptors_ += desc.capacity;
        return {true, "", allocatedTables_ - 1u, desc.capacity};
    }

    arp::core::UniquePtr<rhi::DescriptorTable> allocateDescriptorTable(
        const rhi::DescriptorTableDesc& desc) override
    {
        auto copied = arp::rhi::DescriptorStateCopy::copy(desc);
        return core::withMutationRollback(
            [&]() -> core::UniquePtr<rhi::DescriptorTable> {
                auto allocation = allocateTable(copied);
                return allocation.success
                    ? arp::core::makeUnique<MetalRhiDescriptorTable>(
                          allocation, std::move(copied), registry_, generation_)
                    : nullptr;
            },
            allocatedSets_,
            allocatedTables_,
            allocatedDescriptors_);
    }

    rhi::DescriptorArenaStats stats() const override
    {
        return {
            desc_.maxSets,
            desc_.maxDescriptors,
            allocatedSets_,
            allocatedTables_,
            allocatedDescriptors_,
        };
    }

    void reset() override
    {
        generation_.advance();
        allocatedSets_ = 0;
        allocatedTables_ = 0;
        allocatedDescriptors_ = 0;
    }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return generation_.ticket();
    }

private:
    rhi::DescriptorArenaDesc desc_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    rhi::semantics::DirectObjectTelemetryLease telemetryLease_;
    rhi::semantics::DirectObjectBudgetLease budgetLease_;
    core::LifetimeEpoch generation_;
    std::uint32_t allocatedSets_ = 0;
    std::uint32_t allocatedTables_ = 0;
    std::uint32_t allocatedDescriptors_ = 0;
};

} // namespace

arp::core::UniquePtr<rhi::DescriptorArena> createMetalRhiDescriptorArena(
    const rhi::DescriptorArenaDesc& desc,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    std::shared_ptr<rhi::semantics::DirectObjectTelemetryTracker> telemetry,
    rhi::semantics::DirectObjectBudgetLease budgetLease)
{
    return desc.maxSets == 0 || desc.maxDescriptors == 0
        ? nullptr
        : arp::core::makeUnique<MetalRhiDescriptorArena>(arp::rhi::DescriptorStateCopy::copy(desc),
              std::move(registry),
              telemetry ? telemetry->acquire(rhi::semantics::DirectObjectKind::DescriptorArena,
                              desc.maxDescriptors)
                        : rhi::semantics::DirectObjectTelemetryLease{},
              std::move(budgetLease));
}

std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> metalRhiOriginalSynchronousPayloadFor(
    const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& source) noexcept
{
    if (rhi::OriginalCommandDescriptorSetSource::validateOriginal(source))
        return {};
    const auto* concrete = dynamic_cast<const MetalRhiDescriptorSet*>(source->originalObject());
    return concrete ? concrete->originalSynchronousPayload(*source) : nullptr;
}

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> metalRhiNativePayloadFor(
    const rhi::DescriptorSet& descriptorSet)
{
    const auto* metalDescriptorSet = dynamic_cast<const MetalRhiDescriptorSet*>(&descriptorSet);
    return metalDescriptorSet ? metalDescriptorSet->nativePayload() : nullptr;
}

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload> prepareMetalOriginalImageCellDescriptors(
    const rhi::BindlessSnapshot& snapshot)
{
    const auto* set = dynamic_cast<const MetalRhiDescriptorSet*>(snapshot.descriptorSet());
    return set ? set->imageCellPayload(snapshot) : nullptr;
}

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalCompletedImageDescriptors(
    const rhi::BindlessSnapshot& snapshot, rhi::OriginalBindlessCompletedImageScope scope)
{
    const auto* set = snapshot.descriptorSet();
    const auto* concrete = dynamic_cast<const MetalRhiDescriptorSet*>(set);
    return concrete ? concrete->originalCompletedImagePayload(snapshot, scope) : nullptr;
}

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalCompletedDepthDescriptors(const rhi::BindlessSnapshot& snapshot)
{
    const auto* set = dynamic_cast<const MetalRhiDescriptorSet*>(snapshot.descriptorSet());
    return set ? set->completedDepthPayload(snapshot) : nullptr;
}

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalOrdinaryColorDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root, std::uint32_t set)
{
    using Root = rhi::OriginalCommandBindlessRootSource;
    if (Root::validateOriginal(root) || root->sourceContractVersion() != 2u
        || !std::holds_alternative<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
            root->originalPrepared())
        || !rhi::OriginalBindlessCompletedImageDomain::acceptsPlan(root->originalSnapshot()->plan(),
            rhi::OriginalBindlessCompletedImageScope::SampledGraphics1))
        return {};
    for (const auto& element : root->elements())
        if (element.declaration.write.type != rhi::DescriptorType::UniformBuffer
            && element.declaration.write.type != rhi::DescriptorType::ReadOnlyStorageBuffer
            && element.declaration.write.type != rhi::DescriptorType::StorageBuffer)
            return {};
    for (const auto& domain : root->ordinaryDomains()) {
        if (domain.set != set)
            continue;
        const auto* concrete =
            dynamic_cast<const MetalRhiDescriptorSet*>(domain.source->originalObject());
        return concrete ? concrete->originalOrdinaryGraphicsPayload(*domain.source, false)
                        : nullptr;
    }
    return {};
}

std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>
prepareMetalRhiOriginalOrdinaryDepthDescriptors(
    const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root, std::uint32_t set)
{
    using namespace rhi;
    using Root = OriginalCommandBindlessRootSource;
    if (Root::validateOriginal(root)
        || OriginalBindlessRootNativeClosureContract::ordinaryGraphicsDepthRootDiagnostic(
            *root, 16u)
        || !semantics::OriginalBindlessRootNativeClosureLowering::lowering(Backend::Metal, 16u)
            .nativeCorrespondenceAvailable)
        return {};
    for (const auto& element : root->elements())
        if (element.declaration.write.type != DescriptorType::UniformBuffer
            && element.declaration.write.type != DescriptorType::ReadOnlyStorageBuffer
            && element.declaration.write.type != DescriptorType::StorageBuffer)
            return {};
    const auto& graphics =
        std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(root->originalPrepared());
    const auto* selected =
        graphics->owners()->desc().pipeline->bindlessShaderModule(ShaderStage::Fragment, 1u);
    for (const auto& domain : root->ordinaryDomains()) {
        if (domain.set != set)
            continue;
        for (const auto& element : domain.source->frozenElements()) {
            const auto* shape = findBindingInLayout(
                domain.source->originalObject()->layout(), element.declaration.write.binding);
            if (!shape)
                return {};
            if (!shape->depthTexture && !shape->comparisonSampler)
                continue;
            BindlessOrdinaryOpaqueBinding binding;
            if (Root::originalOrdinaryOpaqueBinding(
                    root, selected->entry, set, shape->binding, binding)
                || binding.selectedStage != ShaderStage::Fragment
                || binding.sourceIndex != element.declaration.sourceIndex
                || binding.resource != element.declaration.write.resource)
                return {};
        }
        const auto* concrete =
            dynamic_cast<const MetalRhiDescriptorSet*>(domain.source->originalObject());
        return concrete ? concrete->originalOrdinaryGraphicsPayload(*domain.source, true) : nullptr;
    }
    return {};
}

bool metalRhiDescriptorRegistryMatches(const rhi::DescriptorSet& set,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry) noexcept
{
    const auto* concrete = dynamic_cast<const MetalRhiDescriptorSet*>(&set);
    return concrete && concrete->belongsTo(registry);
}

bool metalRhiDescriptorRegistryMatches(const rhi::DescriptorArena& arena,
    const std::shared_ptr<MetalRhiResourceRegistry>& registry) noexcept
{
    const auto* concrete = dynamic_cast<const MetalRhiDescriptorArena*>(&arena);
    return concrete && concrete->belongsTo(registry);
}

std::shared_ptr<MetalRhiNativeDescriptorTablePayload> metalRhiNativePayloadFor(
    const rhi::DescriptorTable& descriptorTable)
{
    const auto* metalDescriptorTable =
        dynamic_cast<const MetalRhiDescriptorTable*>(&descriptorTable);
    return metalDescriptorTable ? metalDescriptorTable->nativePayload() : nullptr;
}

MetalRhiDescriptorPayloadInspection inspectMetalRhiDescriptorPayload(
    const rhi::DescriptorSet& descriptorSet)
{
    MetalRhiDescriptorPayloadInspection inspection;
    const auto payload = metalRhiNativePayloadFor(descriptorSet);
    if (!payload) {
        return inspection;
    }
    inspection.backendPayloadPresent = true;
    inspection.argumentBindingsBacked = !payload->bindings.empty();
    inspection.nativeRuntimeBacked = payload->nativeRuntimeBacked;
    inspection.nativeRuntimeDiagnosticOnly = payload->nativeRuntimeDiagnosticOnly;
    inspection.nativeHandleExposedToUpperLayer = payload->nativeHandleExposedToUpperLayer;
    inspection.bindingCount = static_cast<std::uint32_t>(payload->bindings.size());
    for (const auto& binding : payload->bindings) {
        switch (binding.argumentKind) {
        case MetalRhiArgumentKind::Buffer:
            ++inspection.bufferArgumentCount;
            break;
        case MetalRhiArgumentKind::Texture:
            ++inspection.textureArgumentCount;
            break;
        case MetalRhiArgumentKind::Sampler:
            ++inspection.samplerArgumentCount;
            break;
        }
        switch (binding.type) {
        case rhi::DescriptorType::UniformBuffer:
            inspection.uniformBufferBound = true;
            break;
        case rhi::DescriptorType::StorageBuffer:
        case rhi::DescriptorType::ReadOnlyStorageBuffer:
            inspection.storageBufferBound = true;
            break;
        case rhi::DescriptorType::SampledTexture:
            inspection.sampledTextureBound = true;
            break;
        case rhi::DescriptorType::StorageTexture:
            inspection.storageTextureBound = true;
            break;
        case rhi::DescriptorType::Sampler:
            inspection.samplerBound = true;
            break;
        case rhi::DescriptorType::InputAttachment:
            inspection.framebufferFetchBound = true;
            break;
        }
    }
    return inspection;
}

} // namespace arp::rhi_metal
