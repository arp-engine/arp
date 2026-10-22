#pragma once

#include "core/algorithm/ordered_span_cursor.h"
#include "core/ownership/shared_allocation_projection.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_physical_groups.h"
#include "rhi/backends/webgpu/webgpu_readback_preparation.h"
#include "rhi/backends/webgpu/webgpu_transfer_copy_validation.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/command_recording_order.h"
#include "rhi/frontend/transfer_copy.h"

// region Webgpu generic command projection
namespace arp::rhi_webgpu {

// Ownership-only prerequisite. No command recording or completion authority.
using WebGpuCommandReferenceDomain = rhi::CommandReferenceDomain;
enum class WebGpuCommandReferenceSource : std::uint32_t {
    Transfer,
    Resource,
    Pipeline,
    PhysicalGroups,
    OriginalBufferUpload,
    OriginalTextureUpload,
    OriginalTextureReadback,
    OriginalTextureReadbackState
};

struct WebGpuCommandReference {
    WebGpuCommandReferenceDomain domain = WebGpuCommandReferenceDomain::Buffer;
    WebGpuCommandReferenceSource source = WebGpuCommandReferenceSource::Transfer;
    rhi::GpuResourceId resource;
    std::uint32_t sourceIndex = 0u;
};

struct WebGpuGenericCommandProjectionBudget {
    static constexpr auto domainCount =
        static_cast<std::size_t>(WebGpuCommandReferenceDomain::Count);
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxTransferProjections = 0u, maxResourceProjections = 0u;
    std::uint32_t maxPipelines = 0u, maxPhysicalGroupProjections = 0u;
    std::uint32_t maxReferences = 0u, maxAllocationReferences = 0u;
    std::array<std::uint32_t, domainCount> maxDomainReferences{};
    std::uint64_t maxCpuMetadataBytes = 0u, maxRetainedAllocationBytes = 0u;
    std::uint32_t maxOriginalUploadSources = 0u;
    std::uint64_t maxOriginalUploadBytes = 0u;
    std::uint32_t maxOriginalTextureUploadSources = 0u;
    std::uint64_t maxTexturePackedBytes = 0u, maxTextureContentBytes = 0u,
                  maxTextureDestinationTexelBytes = 0u;
    std::uint32_t maxOriginalReadbackPairs = 0u, maxReadbackMipCopies = 0u;
    std::uint64_t maxReadbackRetainedContentBytes = 0u, maxReadbackUnpublishedResultBytes = 0u;
    std::uint32_t maxReadbackStateReferences = 0u;
};

class WebGpuGenericCommandProjection final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuGenericCommandProjection;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto originalReadbackLowering(rhi::Backend backend) noexcept
    {
        return WebGpuOriginalTextureReadbackPairProjection::lowering(backend);
    }

    struct Sources {
        std::shared_ptr<const WebGpuOrdinaryTransferRetention> transfer;
        std::shared_ptr<const WebGpuOrdinaryResourceProjection> resources;
        const std::shared_ptr<const WebGpuOrdinaryPipelineObservation>* pipelines = nullptr;
        std::size_t pipelineCount = 0u;
        const std::shared_ptr<const WebGpuOrdinaryPhysicalGroups>* groups = nullptr;
        std::size_t groupCount = 0u;
        const std::shared_ptr<const WebGpuOriginalBufferUploadSource>* uploads = nullptr;
        std::size_t uploadCount = 0u;
        const std::shared_ptr<const WebGpuOriginalTextureUploadSource>* textureUploads = nullptr;
        std::size_t textureUploadCount = 0u;
        const std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection>* readbacks =
            nullptr;
        std::size_t readbackCount = 0u;
    };

    WebGpuGenericCommandProjection(
        ConstructionKey, Sources&& sources, WebGpuGenericCommandProjectionBudget budget)
        : transfer_(std::move(sources.transfer)), resources_(std::move(sources.resources)),
          pipelines_(budget.maxPipelines), groups_(budget.maxPhysicalGroupProjections),
          uploads_(budget.maxOriginalUploadSources),
          textureUploads_(budget.maxOriginalTextureUploadSources),
          readbacks_(budget.maxOriginalReadbackPairs), references_(budget.maxReferences),
          budget_(budget)
    {}

    static const char* validateBudget(const WebGpuGenericCommandProjectionBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-COMMAND-PROJECTION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-COMMAND-PROJECTION-OPT-IN";
        if (!budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-COMMAND-PROJECTION-BUDGET";
        return nullptr;
    }

    static core::UniquePtr<WebGpuGenericCommandProjection> prepare(Sources&& sources,
        const WebGpuCommandReference* references,
        std::size_t count,
        const WebGpuGenericCommandProjectionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-QUOTA";
            return {};
        }
        if ((sources.transfer && !budget.maxTransferProjections)
            || (sources.resources && !budget.maxResourceProjections)
            || (sources.pipelineCount && !sources.pipelines)
            || (sources.groupCount && !sources.groups)
            || sources.pipelineCount > budget.maxPipelines
            || sources.groupCount > budget.maxPhysicalGroupProjections
            || (sources.uploadCount && !sources.uploads)
            || sources.uploadCount > budget.maxOriginalUploadSources
            || (sources.textureUploadCount && !sources.textureUploads)
            || sources.textureUploadCount > budget.maxOriginalTextureUploadSources
            || (sources.readbackCount && !sources.readbacks)
            || sources.readbackCount > budget.maxOriginalReadbackPairs || (count && !references)
            || count > budget.maxReferences) {
            diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY";
            return {};
        }
        if ((diagnostic = validateReadbacks(sources.readbacks, sources.readbackCount, budget)))
            return {};
        // Count/domain rejection precedes allocation or consumption of sources.
        std::array<std::uint32_t, WebGpuGenericCommandProjectionBudget::domainCount> counts{};
        for (std::size_t i = 0u; i < count; ++i) {
            const auto domain = static_cast<std::size_t>(references[i].domain);
            if (domain >= counts.size()) {
                diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN";
                return {};
            }
            if (domain >= static_cast<std::size_t>(WebGpuCommandReferenceDomain::BindlessRoot)
                && !(references[i].domain == WebGpuCommandReferenceDomain::Upload
                    && ((budget.maxOriginalUploadSources && budget.maxOriginalUploadBytes)
                        || (budget.maxOriginalTextureUploadSources && budget.maxTexturePackedBytes
                            && budget.maxTextureContentBytes
                            && budget.maxTextureDestinationTexelBytes)))
                && !(references[i].domain == WebGpuCommandReferenceDomain::Readback
                    && sources.readbackCount == 1u
                    && references[i].source
                        == WebGpuCommandReferenceSource::OriginalTextureReadback)) {
                diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN";
                return {};
            }
            if (counts[domain] == budget.maxDomainReferences[domain]) {
                diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-CAPACITY";
                return {};
            }
            ++counts[domain];
        }
        if ((diagnostic = validateReadbackReferences(sources.readbacks,
                 sources.readbackCount,
                 references,
                 count,
                 budget.maxReadbackStateReferences)))
            return {};
        diagnostic = validateUploads(
            sources.transfer, sources.uploads, sources.uploadCount, budget.maxOriginalUploadBytes);
        if (diagnostic)
            return {};
        diagnostic =
            validateTextureUploads(sources.textureUploads, sources.textureUploadCount, budget);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuGenericCommandProjection>(
                ConstructionKey{}, std::move(sources), budget);
            for (std::size_t i = 0u; i < sources.pipelineCount; ++i)
                result->pipelines_.append(sources.pipelines[i]);
            for (std::size_t i = 0u; i < sources.groupCount; ++i)
                result->groups_.append(sources.groups[i]);
            for (std::size_t i = 0u; i < sources.uploadCount; ++i)
                result->uploads_.append(sources.uploads[i]);
            for (std::size_t i = 0u; i < sources.textureUploadCount; ++i)
                result->textureUploads_.append(sources.textureUploads[i]);
            for (std::size_t i = 0u; i < sources.readbackCount; ++i)
                result->readbacks_.append(sources.readbacks[i]);
            for (std::size_t i = 0u; i < count; ++i)
                result->references_.append(references[i]);
            diagnostic = result->validateSources();
            if (diagnostic)
                return {};
            for (const auto& reference : result->references_.values())
                if (!result->resolve(reference)) {
                    diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED";
                    return {};
                }
            std::size_t allocationCount = 0u;
            const auto add = [&](std::size_t n) {
                if (n > budget.maxAllocationReferences - allocationCount)
                    return false;
                allocationCount += n;
                return true;
            };
            if ((result->transfer_ && !add(result->transfer_->resources().size()))
                || (result->resources_ && !add(result->resources_->resources().size()))
                || !add(result->uploads_.values().size())) {
                diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT";
                return {};
            }
            for (const auto& source : result->textureUploads_.values()) {
                (void)source;
                if (!add(1u) || !add(1u)) {
                    diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT";
                    return {};
                }
            }
            for (const auto& pair : result->readbacks_.values())
                if (!add(pair->allocationRecordCount())) {
                    diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT";
                    return {};
                }
            for (const auto& group : result->groups_.values())
                for (const auto& set : group->observedSets())
                    if (!add(set.observation->resources().resources().size())) {
                        diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT";
                        return {};
                    }
            const auto inventory = core::SharedAllocationProjection::inspect(allocationCount,
                budget.maxAllocationReferences,
                budget.maxRetainedAllocationBytes,
                [&](std::size_t index) {
                    return result->allocationAt(index);
                });
            using Status = core::SharedAllocationProjection::Status;
            if (inventory.status != Status::Complete) {
                diagnostic = inventory.status == Status::ByteCapacity
                    ? "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-BYTES"
                    : "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-ALIAS";
                return {};
            }
            result->allocationBytes_ = inventory.bytes;
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION";
            return {};
        }
    }

    // Include the original shared control block in the same cold quota and
    // diagnostic boundary as the ownership graph; no partial graph publication.
    static std::shared_ptr<const WebGpuGenericCommandProjection> prepareOriginalShared(
        Sources&& sources,
        const WebGpuCommandReference* references,
        std::size_t count,
        const WebGpuGenericCommandProjectionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        try {
            return shareOriginal(prepare(
                std::move(sources), references, count, budget, std::move(quota), diagnostic));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept { return validateSources(); }

    const char* validateReference(
        std::size_t index, WebGpuCommandReferenceDomain domain, const void* original) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (index >= references_.values().size() || references_.values()[index].domain != domain
            || !original || resolve(references_.values()[index]) != original)
            return "RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED";
        return nullptr;
    }

    const auto& references() const noexcept { return references_.values(); }

    const auto& transfer() const noexcept { return transfer_; }

    const auto& resources() const noexcept { return resources_; }

    const auto& pipelines() const noexcept { return pipelines_.values(); }

    const auto& physicalGroups() const noexcept { return groups_.values(); }

    const auto& originalUploads() const noexcept { return uploads_.values(); }

    const auto& originalTextureUploads() const noexcept { return textureUploads_.values(); }

    const auto& originalReadbacks() const noexcept { return readbacks_.values(); }

    const auto& budget() const noexcept { return budget_; }

    std::uint64_t retainedAllocationBytes() const noexcept { return allocationBytes_; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return webGpuGenericCompletionResourceGraphDiagnostic;
    }

private:
    static const char* validateReadbacks(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection>* pairs,
        std::size_t count,
        const WebGpuGenericCommandProjectionBudget& budget) noexcept
    {
        if (count > 1u)
            return "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-MULTI-PAIR-UNPROVEN";
        if (!count)
            return nullptr;
        const auto& pair = pairs[0];
        if (!pair || !pair->hasQuotaBoundSharedOwner(pair))
            return "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-OWNER";
        if (const auto* id = pair->validate())
            return id;
        if (pair->layout()->mipCopies().size() > budget.maxReadbackMipCopies
            || pair->destination()->source()->retainedContentBytes()
                > budget.maxReadbackRetainedContentBytes
            || pair->destination()->unpublishedResultCapacityBytes()
                > budget.maxReadbackUnpublishedResultBytes)
            return "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-CAPACITY";
        return nullptr;
    }

    static const char* validateReadbackReferences(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection>* pairs,
        std::size_t count,
        const WebGpuCommandReference* references,
        std::size_t referenceCount,
        std::uint32_t maxStateReferences) noexcept
    {
        using Source = WebGpuCommandReferenceSource;
        using Domain = WebGpuCommandReferenceDomain;
        std::size_t paired = 0u, stateReferences = 0u;
        for (std::size_t i = 0u; i < referenceCount; ++i) {
            const auto& reference = references[i];
            if (reference.source == Source::OriginalTextureReadbackState) {
                if (stateReferences == maxStateReferences)
                    return "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-STATE-CAPACITY";
                if (count != 1u || reference.domain != Domain::Texture
                    || reference.sourceIndex != 0u
                    || reference.resource != pairs[0]->destination()->source()->resource().id)
                    return "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-STATE-REFERENCE";
                ++stateReferences;
                continue;
            }
            if (reference.source != Source::OriginalTextureReadback)
                continue;
            if (count != 1u || paired || reference.domain != Domain::Texture
                || reference.sourceIndex != 0u
                || reference.resource != pairs[0]->destination()->source()->resource().id
                || i + 1u >= referenceCount)
                return "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES";
            const auto& result = references[++i];
            if (result.source != Source::OriginalTextureReadback
                || result.domain != Domain::Readback || result.sourceIndex != 0u
                || result.resource != pairs[0]->destination()->resourceId())
                return "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES";
            ++paired;
        }
        return paired == count ? nullptr : "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES";
    }

    static const char* validateUploads(
        const std::shared_ptr<const WebGpuOrdinaryTransferRetention>& transfer,
        const std::shared_ptr<const WebGpuOriginalBufferUploadSource>* uploads,
        std::size_t count,
        std::uint64_t maxBytes) noexcept
    {
        if (count && (!transfer || !transfer->hasQuotaBoundSharedOwner(transfer)))
            return "RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-TRANSFER";
        std::uint64_t bytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& source = uploads[i];
            if (!source || !source->hasQuotaBoundSharedOwner(source))
                return "RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-OWNER";
            if (const auto* id = source->validate())
                return id;
            const auto& original = source->resource();
            const auto* joined = transfer->find(original.id);
            if (!joined || !joined->matches() || joined->texture
                || joined->buffer.get() != original.buffer.get()
                || !core::SharedOwnerProjection::sameOwner(joined->buffer, original.buffer)
                || !core::SharedOwnerProjection::sameOwner(
                    joined->allocationOwner(), original.allocationOwner())
                || joined->allocationBytes != source->sizeBytes())
                return "RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-TRANSFER";
            for (std::size_t j = 0u; j < i; ++j)
                if (uploads[j]->resource().id == original.id)
                    return "RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-DUPLICATE";
            if (source->sizeBytes() > maxBytes - bytes)
                return "RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-BYTES";
            bytes += source->sizeBytes();
        }
        return nullptr;
    }

    static const char* validateTextureUploads(
        const std::shared_ptr<const WebGpuOriginalTextureUploadSource>* sources,
        std::size_t count,
        const WebGpuGenericCommandProjectionBudget& budget) noexcept
    {
        std::uint64_t packed = 0u, content = 0u, texel = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& source = sources[i];
            if (!source || !source->hasQuotaBoundSharedOwner(source))
                return "RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-UPLOAD-OWNER";
            if (const auto* id = source->validate())
                return id;
            for (std::size_t j = 0u; j < i; ++j)
                if (sources[j]->resource().id == source->resource().id
                    || sources[j]->destination()->resource().id
                        == source->destination()->resource().id)
                    return "RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-UPLOAD-DUPLICATE";
            const auto nextPacked = source->bufferSource()->sizeBytes(),
                       nextContent = source->contentSource()->retainedBytes(),
                       nextTexel = source->destination()->resource().allocationBytes;
            if (nextPacked > budget.maxTexturePackedBytes - packed)
                return "RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-PACKED-BYTES";
            if (nextContent > budget.maxTextureContentBytes - content)
                return "RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-CONTENT-BYTES";
            if (nextTexel > budget.maxTextureDestinationTexelBytes - texel)
                return "RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-DESTINATION-BYTES";
            packed += nextPacked;
            content += nextContent;
            texel += nextTexel;
        }
        return nullptr;
    }

    const char* validateSources() const noexcept
    {
        if ((transfer_ && !transfer_->hasQuotaBoundSharedOwner(transfer_))
            || (resources_ && !resources_->hasQuotaBoundSharedOwner(resources_)))
            return "RHI-WEBGPU-COMMAND-PROJECTION-RESOURCE-OWNER";
        if (webGpuNativeDeviceLost()
            && (transfer_ || resources_ || !pipelines_.values().empty() || !groups_.values().empty()
                || !textureUploads_.values().empty() || !readbacks_.values().empty()))
            return "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST";
        if ((transfer_ && !transfer_->matches()) || (resources_ && !resources_->matches()))
            return "RHI-WEBGPU-COMMAND-PROJECTION-IDENTITY-CHANGED";
        if (const auto* id = validateUploads(transfer_,
                uploads_.values().data(),
                uploads_.values().size(),
                budget_.maxOriginalUploadBytes))
            return id;
        if (const auto* id = validateTextureUploads(
                textureUploads_.values().data(), textureUploads_.values().size(), budget_))
            return id;
        if (const auto* id =
                validateReadbacks(readbacks_.values().data(), readbacks_.values().size(), budget_))
            return id;
        void* device = nullptr;
        const auto sameDevice = [&](void* next) {
            if (!next)
                return false;
            if (!device)
                device = next;
            return device == next;
        };
        if (transfer_)
            for (const auto& resource : transfer_->resources()) {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
                if (!sameDevice(resource.buffer ? resource.buffer->ownerDevice
                                                : resource.texture->ownerDevice))
                    return "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE";
#else
                (void)resource;
                return "RHI-WEBGPU-COMMAND-PROJECTION-NATIVE-UNAVAILABLE";
#endif
            }
        for (const auto& source : textureUploads_.values()) {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!sameDevice(source->resource().buffer->ownerDevice)
                || !sameDevice(source->destination()->resource().texture->ownerDevice))
                return "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE";
#else
            (void)source;
            return "RHI-WEBGPU-COMMAND-PROJECTION-NATIVE-UNAVAILABLE";
#endif
        }
        for (const auto& pair : readbacks_.values()) {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!sameDevice(pair->destination()->nativeResource()->ownerDevice))
                return "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE";
#else
            (void)pair;
            return "RHI-WEBGPU-COMMAND-PROJECTION-NATIVE-UNAVAILABLE";
#endif
        }
        if (resources_)
            for (const auto& resource : resources_->resources())
                if (!sameDevice(resource.nativeDevice()))
                    return "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE";
        for (const auto& pipeline : pipelines_.values()) {
            if (!pipeline || !pipeline->hasQuotaBoundSharedOwner(pipeline))
                return "RHI-WEBGPU-COMMAND-PROJECTION-PIPELINE-OWNER";
            if (const auto* id = pipeline->validate())
                return id;
            if (!sameDevice(pipeline->contract().nativeDevice()))
                return "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE";
        }
        for (const auto& group : groups_.values()) {
            if (!group || !group->hasQuotaBoundSharedOwner(group))
                return "RHI-WEBGPU-COMMAND-PROJECTION-GROUP-OWNER";
            if (const auto* id = group->validate())
                return id;
            if (!sameDevice(group->pipeline()->contract().nativeDevice()))
                return "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE";
        }
        return nullptr;
    }

    const void* resolve(const WebGpuCommandReference& reference) const noexcept
    {
        using Domain = WebGpuCommandReferenceDomain;
        using Source = WebGpuCommandReferenceSource;
        if (reference.source == Source::OriginalTextureReadback
            || reference.source == Source::OriginalTextureReadbackState) {
            if (reference.sourceIndex >= readbacks_.values().size())
                return nullptr;
            const auto& pair = readbacks_.values()[reference.sourceIndex];
            if (reference.domain == Domain::Texture
                && reference.resource == pair->destination()->source()->resource().id)
                return pair->destination()->source()->resource().texture.get();
            if (reference.source == Source::OriginalTextureReadbackState)
                return nullptr;
            if (reference.domain == Domain::Readback
                && reference.resource == pair->destination()->resourceId())
                return pair.get();
            return nullptr;
        }
        if (reference.source == Source::OriginalTextureUpload) {
            if (reference.sourceIndex >= textureUploads_.values().size())
                return nullptr;
            const auto& source = textureUploads_.values()[reference.sourceIndex];
            if (reference.domain == Domain::Buffer && reference.resource == source->resource().id)
                return source->resource().buffer.get();
            if (reference.resource != source->destination()->resource().id)
                return nullptr;
            if (reference.domain == Domain::Texture)
                return source->destination()->resource().texture.get();
            if (reference.domain == Domain::Upload)
                return source.get();
            return nullptr;
        }
        if (reference.source == Source::OriginalBufferUpload) {
            if (reference.domain != Domain::Upload || !reference.resource.isValid()
                || reference.sourceIndex >= uploads_.values().size())
                return nullptr;
            const auto& source = uploads_.values()[reference.sourceIndex];
            return reference.resource == source->resource().id ? source.get() : nullptr;
        }
        if (reference.source == Source::Pipeline) {
            if (reference.domain != Domain::Pipeline || reference.resource.value
                || reference.sourceIndex >= pipelines_.values().size())
                return nullptr;
            const auto& pipeline = pipelines_.values()[reference.sourceIndex];
            return pipeline->compute() ? static_cast<const void*>(pipeline->compute().get())
                                       : pipeline->graphics().get();
        }
        if (reference.source == Source::PhysicalGroups) {
            if (reference.domain != Domain::Descriptor || reference.resource.value
                || reference.sourceIndex >= groups_.values().size())
                return nullptr;
            return groups_.values()[reference.sourceIndex].get();
        }
        if (reference.sourceIndex || !reference.resource.value)
            return nullptr;
        if (reference.source == Source::Transfer) {
            const auto* resource = transfer_ ? transfer_->find(reference.resource) : nullptr;
            if (!resource)
                return nullptr;
            if (reference.domain == Domain::Buffer)
                return resource->buffer.get();
            if (reference.domain == Domain::Texture)
                return resource->texture.get();
        } else if (reference.source == Source::Resource) {
            const auto* resource = resources_ ? resources_->find(reference.resource) : nullptr;
            if (!resource)
                return nullptr;
            if (reference.domain == Domain::Buffer)
                return resource->backing().buffer.get();
            if (reference.domain == Domain::TextureView)
                return resource->view().get();
            if (reference.domain == Domain::Sampler)
                return resource->sampler().get();
        }
        return nullptr;
    }

    static core::SharedAllocationProjection::Record allocation(
        const WebGpuTransferResourceObservation& resource) noexcept
    {
        return resource.allocationRecord();
    }

    core::SharedAllocationProjection::Record allocationAt(std::size_t index) const noexcept
    {
        if (transfer_) {
            if (index < transfer_->resources().size())
                return allocation(transfer_->resources()[index]);
            index -= transfer_->resources().size();
        }
        if (resources_) {
            if (index < resources_->resources().size())
                return allocation(resources_->resources()[index].backing());
            index -= resources_->resources().size();
        }
        if (index < uploads_.values().size())
            return allocation(uploads_.values()[index]->resource());
        index -= uploads_.values().size();
        for (const auto& source : textureUploads_.values()) {
            if (index == 0u)
                return allocation(source->resource());
            if (index == 1u)
                return allocation(source->destination()->resource());
            index -= 2u;
        }
        for (const auto& pair : readbacks_.values()) {
            if (index < pair->allocationRecordCount())
                return pair->allocationRecordAt(index);
            index -= pair->allocationRecordCount();
        }
        for (const auto& group : groups_.values())
            for (const auto& set : group->observedSets()) {
                const auto& resources = set.observation->resources().resources();
                if (index < resources.size())
                    return allocation(resources[index].backing());
                index -= resources.size();
            }
        return {};
    }

    const std::shared_ptr<const WebGpuOrdinaryTransferRetention> transfer_;
    const std::shared_ptr<const WebGpuOrdinaryResourceProjection> resources_;
    core::BoundedSequence<std::shared_ptr<const WebGpuOrdinaryPipelineObservation>> pipelines_;
    core::BoundedSequence<std::shared_ptr<const WebGpuOrdinaryPhysicalGroups>> groups_;
    core::BoundedSequence<std::shared_ptr<const WebGpuOriginalBufferUploadSource>> uploads_;
    core::BoundedSequence<std::shared_ptr<const WebGpuOriginalTextureUploadSource>> textureUploads_;
    core::BoundedSequence<std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection>>
        readbacks_;
    core::BoundedSequence<WebGpuCommandReference> references_;
    const WebGpuGenericCommandProjectionBudget budget_;
    std::uint64_t allocationBytes_ = 0u;
};

// endregion Webgpu generic command projection

// region Webgpu generic command usage

struct WebGpuGenericCommandUsageBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxReferenceUsages = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// Metadata-only. Consuming this plan never bypasses native admission gates.
class WebGpuGenericCommandUsage final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuGenericCommandUsage;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct RawReference {
        rhi::CommandReferenceDomain domain;
        const void* original = nullptr;
    };

    WebGpuGenericCommandUsage(ConstructionKey,
        std::shared_ptr<const WebGpuGenericCommandProjection> projection,
        WebGpuGenericCommandUsageBudget budget)
        : projection_(std::move(projection)), commands_(budget.maxCommands), budget_(budget)
    {}

    static const char* validateBudget(const WebGpuGenericCommandUsageBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-COMMAND-USAGE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-COMMAND-USAGE-OPT-IN";
        if (!budget.maxCommands || !budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-COMMAND-USAGE-BUDGET";
        return nullptr;
    }

    static core::UniquePtr<WebGpuGenericCommandUsage> prepare(
        std::shared_ptr<const WebGpuGenericCommandProjection> projection,
        const rhi::CommandUsage* commands,
        std::size_t count,
        const WebGpuGenericCommandUsageBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-COMMAND-USAGE-QUOTA";
            return {};
        }
        if (!projection || !projection->hasQuotaBoundSharedOwner(projection)) {
            diagnostic = "RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PROJECTION";
            return {};
        }
        diagnostic = projection->validate();
        if (diagnostic)
            return {};
        if (!commands || !count || count > budget.maxCommands
            || projection->references().size() > budget.maxReferenceUsages) {
            diagnostic = "RHI-WEBGPU-COMMAND-USAGE-CAPACITY";
            return {};
        }
        const rhi::CommandUsageSequenceContract::Diagnostics sequenceDiagnostics{
            "RHI-WEBGPU-COMMAND-USAGE-CAPACITY",
            "RHI-WEBGPU-COMMAND-USAGE-FINISH",
            "RHI-WEBGPU-COMMAND-USAGE-SPAN",
            "RHI-WEBGPU-COMMAND-USAGE-SIGNATURE",
            "RHI-WEBGPU-COMMAND-USAGE-REFERENCE-TAIL"};
        diagnostic = rhi::CommandUsageSequenceContract::validate(
            commands,
            count,
            projection->references().size(),
            [&](std::size_t index) {
                return projection->references()[index].domain;
            },
            sequenceDiagnostics);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuGenericCommandUsage>(
                ConstructionKey{}, std::move(projection), budget);
            for (std::size_t i = 0u; i < count; ++i)
                result->commands_.append(commands[i]);
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-COMMAND-USAGE-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-COMMAND-USAGE-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept
    {
        if (!projection_->hasQuotaBoundSharedOwner(projection_))
            return "RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PROJECTION";
        return projection_->validate();
    }

    static std::shared_ptr<const WebGpuGenericCommandUsage> prepareOriginal(
        std::shared_ptr<const WebGpuGenericCommandProjection> projection,
        const rhi::CommandUsage* commands,
        std::size_t count,
        const WebGpuGenericCommandUsageBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        try {
            return shareOriginal(prepare(
                std::move(projection), commands, count, budget, std::move(quota), diagnostic));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-COMMAND-USAGE-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-COMMAND-USAGE-ALLOCATION";
            return {};
        }
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

    const auto& commands() const noexcept { return commands_.values(); }

    const auto& projection() const noexcept { return projection_; }

    const auto& budget() const noexcept { return budget_; }

    class Consumption final {
    public:
        explicit Consumption(std::shared_ptr<const WebGpuGenericCommandUsage> original)
            : original_(std::move(original)),
              cursor_(original_ ? original_->commands().size() : 0u,
                  original_ ? original_->projection_->references().size() : 0u)
        {}

        const char* validate() const noexcept
        {
            if (!original_ || !original_->hasQuotaBoundSharedOwner(original_))
                return "RHI-WEBGPU-COMMAND-USAGE-ORIGINAL-PLAN";
            return original_->validate();
        }

        core::OrderedSpanCursor::Ticket preflight(rhi::CommandUsageKind kind,
            const RawReference* references,
            std::size_t count,
            const char*& diagnostic) const noexcept
        {
            diagnostic = validate();
            if (diagnostic)
                return {};
            if (cursor_.nextRecord() >= original_->commands().size()
                || original_->commands()[cursor_.nextRecord()].kind != kind) {
                diagnostic = "RHI-WEBGPU-COMMAND-USAGE-ORDER";
                return {};
            }
            const auto& command = original_->commands()[cursor_.nextRecord()];
            if (count != command.referenceCount || (count && !references)) {
                diagnostic = "RHI-WEBGPU-COMMAND-USAGE-REFERENCE-COUNT";
                return {};
            }
            const auto ticket =
                cursor_.preflight({command.firstReference, count}, [&](std::size_t index) {
                    const auto& reference = references[index - command.firstReference];
                    diagnostic = original_->projection_->validateReference(
                        index, reference.domain, reference.original);
                    return !diagnostic;
                });
            if (!ticket && !diagnostic)
                diagnostic = "RHI-WEBGPU-COMMAND-USAGE-CLOSED";
            return ticket;
        }

        bool commit(const core::OrderedSpanCursor::Ticket& ticket, const char*& diagnostic) noexcept
        {
            diagnostic = validate();
            if (diagnostic) {
                cursor_.abort();
                return false;
            }
            if (!cursor_.commit(ticket)) {
                diagnostic = "RHI-WEBGPU-COMMAND-USAGE-TICKET";
                return false;
            }
            return true;
        }

        void abort() noexcept { cursor_.abort(); }

        bool complete() const noexcept { return !validate() && cursor_.complete(); }

        std::size_t consumedCommands() const noexcept { return cursor_.nextRecord(); }

        std::size_t consumedReferences() const noexcept { return cursor_.nextElement(); }

        const char* executionDiagnostic() const noexcept
        {
            if (const auto* id = validate())
                return id;
            return webGpuGenericRecordingUsageDiagnostic;
        }

    private:
        const std::shared_ptr<const WebGpuGenericCommandUsage> original_;
        core::OrderedSpanCursor cursor_;
    };

private:
    const std::shared_ptr<const WebGpuGenericCommandProjection> projection_;
    core::BoundedSequence<rhi::CommandUsage> commands_;
    const WebGpuGenericCommandUsageBudget budget_;
};

// endregion Webgpu generic command usage

// region Webgpu frozen command scalars

struct WebGpuFrozenCommandScalarsBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxDebugDepth = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// Whole original stream scalar/variable binding. It retains the complete usage
// graph and cannot issue recorded-command proof or authorize native opening.
class WebGpuFrozenCommandScalars final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuFrozenCommandScalars;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuFrozenCommandScalars(ConstructionKey,
        std::shared_ptr<const WebGpuGenericCommandUsage> usage,
        std::shared_ptr<const rhi::CommandVariableArguments> arguments,
        WebGpuFrozenCommandScalarsBudget budget)
        : usage_(std::move(usage)), arguments_(std::move(arguments)), budget_(budget)
    {}

    static const char* validateBudget(const WebGpuFrozenCommandScalarsBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-WEBGPU-COMMAND-SCALARS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-WEBGPU-COMMAND-SCALARS-OPT-IN";
        if (!b.maxCommands || !b.maxCpuMetadataBytes)
            return "RHI-WEBGPU-COMMAND-SCALARS-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuFrozenCommandScalars> prepareOriginal(
        std::shared_ptr<const WebGpuGenericCommandUsage> usage,
        std::shared_ptr<const rhi::CommandVariableArguments> arguments,
        const WebGpuFrozenCommandScalarsBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-COMMAND-SCALARS-QUOTA";
            return {};
        }
        if ((diagnostic = validateSources(usage, arguments, budget)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<WebGpuFrozenCommandScalars>(
                ConstructionKey{}, std::move(usage), std::move(arguments), budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-COMMAND-SCALARS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-COMMAND-SCALARS-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept { return validateSources(usage_, arguments_, budget_); }

    const auto& arguments() const noexcept { return arguments_; }

    const auto& usage() const noexcept { return usage_; }

    const char* compareRaw(std::size_t command,
        const rhi::CommandVariableArguments::Input& input,
        const WebGpuGenericCommandUsage::RawReference* references,
        std::size_t count) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (!arguments_->matches(command, input))
            return "RHI-WEBGPU-COMMAND-SCALARS-INPUT";
        const auto& entry = usage_->commands()[command];
        if (count != entry.referenceCount || (count && !references))
            return "RHI-WEBGPU-COMMAND-SCALARS-REFERENCE";
        for (std::size_t i = 0u; i < count; ++i)
            if (const auto* id = usage_->projection()->validateReference(
                    entry.firstReference + i, references[i].domain, references[i].original))
                return id;
        return nullptr;
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

private:
    static const char* validateSources(
        const std::shared_ptr<const WebGpuGenericCommandUsage>& usage,
        const std::shared_ptr<const rhi::CommandVariableArguments>& arguments,
        const WebGpuFrozenCommandScalarsBudget& budget) noexcept
    {
        if (!usage || !usage->hasQuotaBoundSharedOwner(usage))
            return "RHI-WEBGPU-COMMAND-SCALARS-ORIGINAL-USAGE";
        if (!arguments || !arguments->hasQuotaBoundSharedOwner(arguments))
            return "RHI-WEBGPU-COMMAND-SCALARS-ORIGINAL-ARGUMENTS";
        if (const auto* id = usage->validate())
            return id;
        const auto& commands = usage->commands();
        if (commands.size() != arguments->records().size() || commands.size() > budget.maxCommands)
            return "RHI-WEBGPU-COMMAND-SCALARS-COUNT";
        if (const auto* id = rhi::semantics::CommandRecordingOrder::validate(
                commands.data(), commands.size(), budget.maxDebugDepth))
            return id;
        const auto& graph = *usage->projection();
        const std::shared_ptr<const WebGpuOrdinaryPipelineObservation>* pipeline = nullptr;
        using K = rhi::CommandUsageKind;
        for (std::size_t i = 0u; i < commands.size(); ++i) {
            const auto& command = commands[i];
            const auto& scalar = arguments->records()[i].scalar;
            if (!arguments->matchesUsageKind(i, command.kind))
                return "RHI-WEBGPU-COMMAND-SCALARS-ORDER";
            if (command.kind == K::BeginRenderPass || command.kind == K::BeginComputePass
                || command.kind == K::BeginTransferPass || command.kind == K::EndRenderPass
                || command.kind == K::EndComputePass || command.kind == K::EndTransferPass)
                pipeline = nullptr;
            if (command.kind == K::BindGraphicsPipeline || command.kind == K::BindComputePipeline) {
                const auto& reference = graph.references()[command.firstReference];
                if (reference.source != WebGpuCommandReferenceSource::Pipeline
                    || reference.sourceIndex >= graph.pipelines().size())
                    return "RHI-WEBGPU-COMMAND-SCALARS-PIPELINE";
                pipeline = &graph.pipelines()[reference.sourceIndex];
                if (static_cast<bool>((*pipeline)->compute())
                    != (command.kind == K::BindComputePipeline))
                    return "RHI-WEBGPU-COMMAND-SCALARS-PIPELINE";
            }
            if (command.kind == K::BindRenderDescriptor
                || command.kind == K::BindComputeDescriptor) {
                const auto* binding = scalar.get<rhi::CommandScalarArguments::DescriptorBinding>();
                const auto& reference = graph.references()[command.firstReference];
                if (!binding || reference.source != WebGpuCommandReferenceSource::PhysicalGroups
                    || reference.sourceIndex >= graph.physicalGroups().size())
                    return "RHI-WEBGPU-COMMAND-SCALARS-DESCRIPTOR";
                const auto& groups = *graph.physicalGroups()[reference.sourceIndex];
                if (!pipeline
                    || !core::SharedOwnerProjection::sameOwner(groups.pipeline(), *pipeline))
                    return "RHI-WEBGPU-COMMAND-SCALARS-PIPELINE";
                const WebGpuOrdinaryDescriptorObservation* original = nullptr;
                for (const auto& set : groups.observedSets())
                    if (set.logicalSet == binding->set)
                        original = set.observation.get();
                if (!original)
                    return "RHI-WEBGPU-COMMAND-SCALARS-DESCRIPTOR";
                if (arguments->records()[i].offsetCount != original->offsets().size())
                    return "RHI-WEBGPU-COMMAND-SCALARS-OFFSETS";
                for (std::size_t j = 0u; j < original->offsets().size(); ++j)
                    if (arguments->offsets(i)[j] != original->offsets()[j])
                        return "RHI-WEBGPU-COMMAND-SCALARS-OFFSETS";
            }
        }
        return nullptr;
    }

    const std::shared_ptr<const WebGpuGenericCommandUsage> usage_;
    const std::shared_ptr<const rhi::CommandVariableArguments> arguments_;
    const WebGpuFrozenCommandScalarsBudget budget_;
};

// endregion Webgpu frozen command scalars

// region Webgpu frozen render pass binding

struct WebGpuFrozenRenderPassBindingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPassRecords = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class WebGpuFrozenRenderPassBinding final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuFrozenRenderPassBinding;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct OriginalPass {
        std::size_t commandIndex = 0u;
        std::shared_ptr<const rhi::RenderPassArguments> arguments;
    };

    WebGpuFrozenRenderPassBinding(ConstructionKey,
        std::shared_ptr<const WebGpuGenericCommandUsage> usage,
        const WebGpuFrozenRenderPassBindingBudget& budget)
        : usage_(std::move(usage)), passes_(budget.maxPassRecords)
    {}

    static const char* validateBudget(const WebGpuFrozenRenderPassBindingBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-WEBGPU-PASS-ARGUMENTS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-WEBGPU-PASS-ARGUMENTS-OPT-IN";
        if (!b.maxCpuMetadataBytes)
            return "RHI-WEBGPU-PASS-ARGUMENTS-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuFrozenRenderPassBinding> prepareOriginal(
        std::shared_ptr<const WebGpuGenericCommandUsage> usage,
        const OriginalPass* passes,
        std::size_t count,
        const WebGpuFrozenRenderPassBindingBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-PASS-ARGUMENTS-QUOTA";
            return {};
        }
        if ((diagnostic = validateOriginalUsage(usage)))
            return {};
        if ((count && !passes) || count > budget.maxPassRecords) {
            diagnostic = "RHI-WEBGPU-PASS-ARGUMENTS-CAPACITY";
            return {};
        }
        std::size_t record = 0u;
        for (std::size_t i = 0u; i < usage->commands().size(); ++i) {
            if (usage->commands()[i].kind != rhi::CommandUsageKind::BeginRenderPass)
                continue;
            if (record == count || passes[record].commandIndex != i) {
                diagnostic = "RHI-WEBGPU-PASS-ARGUMENTS-ORDER";
                return {};
            }
            if ((diagnostic = validatePass(*usage, passes[record++])))
                return {};
        }
        if (record != count) {
            diagnostic = "RHI-WEBGPU-PASS-ARGUMENTS-ORDER";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuFrozenRenderPassBinding>(
                ConstructionKey{}, std::move(usage), budget);
            for (std::size_t i = 0u; i < count; ++i)
                result->passes_.append(passes[i]);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-PASS-ARGUMENTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-PASS-ARGUMENTS-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateOriginalUsage(usage_))
            return id;
        for (const auto& pass : passes_.values())
            if (const auto* id = validatePass(*usage_, pass))
                return id;
        return nullptr;
    }

    const char* compareRaw(std::size_t commandIndex,
        const rhi::RenderPassDesc& input,
        const WebGpuGenericCommandUsage::RawReference* references,
        std::size_t count) const noexcept
    {
        if (const auto* id = validate())
            return id;
        for (const auto& pass : passes_.values()) {
            if (pass.commandIndex != commandIndex)
                continue;
            if (!pass.arguments->matches(input))
                return "RHI-WEBGPU-PASS-ARGUMENTS-INPUT";
            const auto& command = usage_->commands()[commandIndex];
            if (count != command.referenceCount || (count && !references))
                return "RHI-WEBGPU-PASS-ARGUMENTS-REFERENCE";
            for (std::size_t i = 0u; i < count; ++i)
                if (const auto* id = usage_->projection()->validateReference(
                        command.firstReference + i, references[i].domain, references[i].original))
                    return id;
            return nullptr;
        }
        return "RHI-WEBGPU-PASS-ARGUMENTS-ORDER";
    }

    const auto& passes() const noexcept { return passes_.values(); }

    const auto& usage() const noexcept { return usage_; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

private:
    static const char* validateOriginalUsage(
        const std::shared_ptr<const WebGpuGenericCommandUsage>& usage) noexcept
    {
        if (!usage || !usage->hasQuotaBoundSharedOwner(usage))
            return "RHI-WEBGPU-PASS-ARGUMENTS-ORIGINAL-USAGE";
        return usage->validate();
    }

    static const char* validatePass(
        const WebGpuGenericCommandUsage& usage, const OriginalPass& pass) noexcept
    {
        if (!pass.arguments || !pass.arguments->hasQuotaBoundSharedOwner(pass.arguments))
            return "RHI-WEBGPU-PASS-ARGUMENTS-ORIGINAL-SNAPSHOT";
        const auto& command = usage.commands()[pass.commandIndex];
        const auto& graph = *usage.projection();
        const auto& source = pass.arguments->desc();
        const auto& resources = graph.resources();
        const auto view = [&](std::size_t operand,
                              rhi::GpuResourceId target,
                              rhi::TextureFormat format,
                              bool depth) -> const char* {
            const auto& reference = graph.references()[command.firstReference + operand];
            if (reference.domain != rhi::CommandReferenceDomain::TextureView
                || reference.source != WebGpuCommandReferenceSource::Resource
                || reference.resource != target || !resources)
                return "RHI-WEBGPU-PASS-ARGUMENTS-REFERENCE";
            const auto* observed = resources->find(target);
            if (!observed || !observed->view() || !observed->backing().texture
                || (observed->viewDesc().usage & rhi::TextureUsage::RenderAttachment)
                    == rhi::TextureUsage::None
                || (observed->backing().textureDesc.usage & rhi::TextureUsage::RenderAttachment)
                    == rhi::TextureUsage::None
                || observed->viewDesc().aspect
                    != (depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color)
                || rhi::isDepthTextureFormat(observed->viewDesc().format) != depth
                || (format != rhi::TextureFormat::Unknown && format != observed->viewDesc().format))
                return "RHI-WEBGPU-PASS-ARGUMENTS-VIEW";
            return graph.validateReference(command.firstReference + operand,
                rhi::CommandReferenceDomain::TextureView,
                observed->view().get());
        };
        return rhi::RenderPassReferenceSequenceContract::validate(
            source, command.referenceCount, view, "RHI-WEBGPU-PASS-ARGUMENTS-REFERENCE");
    }

    const std::shared_ptr<const WebGpuGenericCommandUsage> usage_;
    core::BoundedSequence<OriginalPass> passes_;
};

// endregion Webgpu frozen render pass binding

// region Webgpu frozen transfer arguments

struct WebGpuFrozenTransferArgumentsBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class WebGpuFrozenTransferArguments final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuFrozenTransferArguments;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuFrozenTransferArguments(ConstructionKey,
        std::shared_ptr<const WebGpuGenericCommandUsage> usage,
        const WebGpuFrozenTransferArgumentsBudget& budget)
        : usage_(std::move(usage)), arguments_(budget.maxCommands)
    {}

    static const char* validateBudget(const WebGpuFrozenTransferArgumentsBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-WEBGPU-TRANSFER-ARGUMENTS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-WEBGPU-TRANSFER-ARGUMENTS-OPT-IN";
        if (!b.maxCommands || !b.maxCpuMetadataBytes)
            return "RHI-WEBGPU-TRANSFER-ARGUMENTS-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuFrozenTransferArguments> prepareOriginal(
        std::shared_ptr<const WebGpuGenericCommandUsage> usage,
        const rhi::TransferCopyArguments* arguments,
        std::size_t count,
        const WebGpuFrozenTransferArgumentsBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-QUOTA";
            return {};
        }
        if (!usage || !usage->hasQuotaBoundSharedOwner(usage)) {
            diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-USAGE";
            return {};
        }
        if ((diagnostic = usage->validate()))
            return {};
        const auto& root = usage->projection()->transfer();
        if (!root || !root->hasQuotaBoundSharedOwner(root)) {
            diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-TRANSFER";
            return {};
        }
        if (!arguments || !count || count > budget.maxCommands
            || count != usage->commands().size()) {
            diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-CAPACITY";
            return {};
        }
        WebGpuGenericCommandUsage::Consumption consumption(usage);
        for (std::size_t i = 0u; i < count; ++i) {
            if ((diagnostic = validateArgument(*root, arguments[i])))
                return {};
            std::array<WebGpuGenericCommandUsage::RawReference, 2> raw{};
            const auto references = rawReferences(*root, arguments[i], raw);
            const auto ticket =
                consumption.preflight(arguments[i].kind(), raw.data(), references, diagnostic);
            if (!ticket || !consumption.commit(ticket, diagnostic))
                return {};
        }
        if (!consumption.complete()) {
            diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-INCOMPLETE";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuFrozenTransferArguments>(
                ConstructionKey{}, std::move(usage), budget);
            for (std::size_t i = 0u; i < count; ++i)
                result->arguments_.append(arguments[i]);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-ALLOCATION";
            return {};
        }
    }

    const char* validateAgainst(const WebGpuOrdinaryTransferRetention& root) const noexcept
    {
        if (!usage_->hasQuotaBoundSharedOwner(usage_))
            return "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-USAGE";
        if (const auto* id = usage_->validate())
            return id;
        if (!root.matches())
            return "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED";
        WebGpuGenericCommandUsage::Consumption consumption(usage_);
        for (const auto& a : arguments_.values()) {
            if (const auto* id = validateArgument(root, a))
                return id;
            std::array<WebGpuGenericCommandUsage::RawReference, 2> raw{};
            const auto references = rawReferences(root, a, raw);
            const char* diagnostic = nullptr;
            const auto ticket = consumption.preflight(a.kind(), raw.data(), references, diagnostic);
            if (!ticket || !consumption.commit(ticket, diagnostic))
                return diagnostic;
        }
        return consumption.complete() ? nullptr : "RHI-WEBGPU-TRANSFER-ARGUMENTS-INCOMPLETE";
    }

    const auto& arguments() const noexcept { return arguments_.values(); }

    const auto& transfer() const noexcept { return usage_->projection()->transfer(); }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validateAgainst(*transfer()))
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

    class Consumption final {
    public:
        explicit Consumption(std::shared_ptr<const WebGpuFrozenTransferArguments> original)
            : original_(std::move(original)), usage_(original_ ? original_->usage_ : nullptr)
        {}

        const char* validate() const noexcept
        {
            if (!original_ || !original_->hasQuotaBoundSharedOwner(original_))
                return "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-PLAN";
            return usage_.validate();
        }

        template <class Desc>
        core::OrderedSpanCursor::Ticket preflight(const WebGpuOrdinaryTransferRetention& root,
            const Desc& incoming,
            Desc& frozen,
            const char*& diagnostic) noexcept
        {
            diagnostic = validate();
            if (diagnostic || !root.matches()) {
                if (!diagnostic)
                    diagnostic = "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED";
                usage_.abort();
                return {};
            }
            const auto index = usage_.consumedCommands();
            if (index >= original_->arguments().size()
                || !original_->arguments()[index].matches(incoming)) {
                diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-MISMATCH";
                return {};
            }
            const auto& a = original_->arguments()[index];
            diagnostic = validateArgument(root, a);
            if (diagnostic)
                return {};
            std::array<WebGpuGenericCommandUsage::RawReference, 2> raw{};
            const auto count = rawReferences(root, a, raw);
            const auto ticket = usage_.preflight(a.kind(), raw.data(), count, diagnostic);
            if (ticket && !a.write(frozen)) {
                diagnostic = "RHI-WEBGPU-TRANSFER-ARGUMENTS-MISMATCH";
                return {};
            }
            return ticket;
        }

        core::OrderedSpanCursor::Ticket preflightFinish(const char*& diagnostic) noexcept
        {
            diagnostic = validate();
            if (diagnostic) {
                usage_.abort();
                return {};
            }
            return usage_.preflight(rhi::CommandUsageKind::Finish, nullptr, 0u, diagnostic);
        }

        bool commit(const core::OrderedSpanCursor::Ticket& ticket, const char*& diagnostic) noexcept
        {
            diagnostic = validate();
            if (diagnostic) {
                usage_.abort();
                return false;
            }
            return usage_.commit(ticket, diagnostic);
        }

        void abort() noexcept { usage_.abort(); }

        bool complete() const noexcept { return !validate() && usage_.complete(); }

        std::size_t consumedCommands() const noexcept { return usage_.consumedCommands(); }

    private:
        const std::shared_ptr<const WebGpuFrozenTransferArguments> original_;
        WebGpuGenericCommandUsage::Consumption usage_;
    };

private:
    static std::size_t rawReferences(const WebGpuOrdinaryTransferRetention& root,
        const rhi::TransferCopyArguments& a,
        std::array<WebGpuGenericCommandUsage::RawReference, 2>& raw) noexcept
    {
        if (a.kind() == rhi::CommandUsageKind::Finish)
            return 0u;
        const auto* s = root.find(a.source());
        const auto* d = root.find(a.destination());
        using Domain = rhi::CommandReferenceDomain;
        if (s)
            raw[0] = {s->buffer ? Domain::Buffer : Domain::Texture,
                s->buffer ? static_cast<const void*>(s->buffer.get()) : s->texture.get()};
        if (d)
            raw[1] = {d->buffer ? Domain::Buffer : Domain::Texture,
                d->buffer ? static_cast<const void*>(d->buffer.get()) : d->texture.get()};
        return 2u;
    }

    static const char* validateArgument(
        const WebGpuOrdinaryTransferRetention& root, const rhi::TransferCopyArguments& a) noexcept
    {
        return WebGpuTransferCopyValidation::validate(root, a);
    }

    const std::shared_ptr<const WebGpuGenericCommandUsage> usage_;
    core::BoundedSequence<rhi::TransferCopyArguments> arguments_;
};
} // namespace arp::rhi_webgpu

// endregion Webgpu frozen transfer arguments
