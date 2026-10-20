#include "rhi/backends/metal/metal_closed_consumer_images.h"

#include "core/monotonic_id_sequence.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/gpu_extensions.h"

namespace arp::rhi_metal {
namespace {
core::MonotonicIdSequence immutableImageRevisions;
}

MetalRhiClosedConsumerImages::MetalRhiClosedConsumerImages(ConstructionKey,
    std::shared_ptr<const Geometry> geometry,
    Parents parents,
    Views views,
    Contents contents,
    Observations observations,
    Markers markers,
    Budget budget,
    std::uint64_t bytes,
    core::LifetimeEpoch::Ticket issuer)
    : geometry_(std::move(geometry)), parents_(std::move(parents)), views_(std::move(views)),
      contents_(std::move(contents)), observations_(std::move(observations)),
      markers_(std::move(markers)), budget_(budget), actualBytes_(bytes), issuer_(std::move(issuer))
{
    owners_.capture([&](auto&& visit) {
        projectOwners(visit);
    });
}

const char* MetalRhiClosedConsumerImages::validate() const noexcept
{
    if (!issuer_.isCurrent())
        return "RHI-METAL-EXCLUSIVE-IMAGES-ISSUER-RETIRED";
    if (!geometry_ || !geometry_->sharedOwnerMatches(geometry_))
        return "RHI-METAL-EXCLUSIVE-IMAGES-ORIGINAL-OWNER";
    if (const auto* id = geometry_->validate())
        return id;
    if (budget_.values.contractVersion != Contract::version || !budget_.values.prototypeOptIn
        || budget_.values.maxCommands < Contract::commandUnits || !budget_.maxTextureBytes
        || !budget_.maxChildOwners || !actualBytes_ || actualBytes_ > budget_.maxTextureBytes
        || owners_.size() > budget_.maxChildOwners)
        return "RHI-METAL-EXCLUSIVE-IMAGES-BUDGET";
    std::uint64_t textureBytes = 0u, sourceBytes = 0u;
    for (std::size_t i = 0u; i < parents_.size(); ++i) {
        if (!parents_[i] || !views_[i] || !contents_[i] || !markers_[i]
            || !contents_[i]->sharedOwnerMatches(contents_[i])
            || !markers_[i]->sharedOwnerMatches(markers_[i]))
            return "RHI-METAL-EXCLUSIVE-IMAGES-ORIGINAL-OWNER";
        if (const auto* id = observations_[i].validate())
            return id;
        const auto& observation = observations_[i];
        const auto& content = *contents_[i];
        const auto& marker = *markers_[i];
        if (observation.parent().textureWrapper.get() != parents_[i].get()
            || observation.view().textureViewWrapper.get() != views_[i].get()
            || !core::SharedOwnerProjection::sameOwner(
                observation.parent().texture->immutableRevision(), markers_[i])
            || !core::SharedOwnerProjection::sameOwner(
                observation.view().textureView->immutableRevision(), markers_[i])
            || marker.range() != content.image().range)
            return "RHI-METAL-EXCLUSIVE-IMAGES-ORIGINAL-REVISION";
        if (const auto* id = content.validate(observation.values(), marker.revision()))
            return id;
        const auto& desc = parents_[i]->desc();
        if (desc.usage
                != (rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst
                    | rhi::TextureUsage::CopySrc)
            || desc.memory != rhi::MemoryUsage::GpuOnly
            || desc.initialState != rhi::ResourceState::ShaderRead || desc.cubeCompatible
            || observation.parent().texture->aliasHeap
            || observation.parent().texture->resource.storageMode
                != MetalRhiNativeStorageMode::Private)
            return "RHI-METAL-EXCLUSIVE-IMAGES-STORAGE";
        const auto reported = parents_[i]->allocation().estimatedSizeBytes;
        if (!reported || reported > budget_.maxTextureBytes - textureBytes)
            return "RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-BUDGET";
        textureBytes += reported;
        if (content.bytes().size() > budget_.values.maxSourceBytes - sourceBytes
            || content.bytes().size() > budget_.values.maxSnapshotBytes - sourceBytes)
            return "RHI-METAL-EXCLUSIVE-IMAGES-SNAPSHOT-BUDGET";
        sourceBytes += content.bytes().size();
    }
    if (textureBytes != actualBytes_ || !owners_.matches([&](auto&& visit) {
            projectOwners(visit);
        }))
        return "RHI-METAL-EXCLUSIVE-IMAGES-ORIGINAL-OWNER";
    return nullptr;
}

std::shared_ptr<const MetalRhiClosedConsumerImages> MetalRhiClosedConsumerImages::prepare(
    std::shared_ptr<const Geometry> geometry,
    rhi::GpuDevice& device,
    const Contract::Inputs& inputs,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](const char* id) -> std::shared_ptr<const MetalRhiClosedConsumerImages> {
        diagnostic = id;
        return {};
    };
    const auto decision = Contract::lowering(device.backend());
    if (!decision.prototypeCold)
        return fail(decision.diagnosticId);
    Contract::Facts facts;
    if (const auto* id = Contract::verify(inputs, budget.values, facts))
        return fail(id);
    if (!geometry || !geometry->sharedOwnerMatches(geometry))
        return fail("RHI-METAL-EXCLUSIVE-IMAGES-ORIGINAL-OWNER");
    if (const auto* id = geometry->validate())
        return fail(id);
    const auto& source = geometry->pipeline()->source();
    const auto& registry = source->index()->registry();
    if (!registry || registry != metalRhiIssuingResourceRegistry(device))
        return fail("RHI-METAL-EXCLUSIVE-IMAGES-ISSUER-MISMATCH");
    const auto* issuer = static_cast<const rhi::BindlessCrossExtension*>(
        device.queryInterface(rhi::kExtensionBindlessCross));
    if (!issuer || !issuer->lifetime_.ticket().isCurrent())
        return fail("RHI-METAL-EXCLUSIVE-IMAGES-ISSUER-RETIRED");
    if (!budget.maxTextureBytes || !budget.maxChildOwners || !source->quota()
        || !source->quota()->limitBytes())
        return fail("RHI-METAL-EXCLUSIVE-IMAGES-BUDGET");
    const auto required = 1u + geometry->childOwnerCount() + 3u
        + Contract::imageCount
            * (4u + 2u * MetalRhiResourceOwnerProjection::independentResourceOwnerCount);
    if (required > budget.maxChildOwners)
        return fail("RHI-METAL-EXCLUSIVE-IMAGES-CHILD-OWNER-BUDGET");
    // Reuse the existing descriptor size estimator only after proving that all
    // per-mip and aggregate arithmetic fits. The private budget is also bound
    // by the original device's explicit reported resource ledger capacity.
    if (budget.maxTextureBytes > device.memoryStats().budgetCapacityBytes)
        return fail("RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-BUDGET");
    std::uint64_t estimated = 0u;
    for (const auto& input : inputs) {
        if (budget.maxTextureBytes > UINT64_MAX / input.mipCount
            || std::uint64_t{input.parentExtent.width} * input.parentExtent.height
                > budget.maxTextureBytes / rhi::bytesPerPixel(input.format))
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-BUDGET");
        const auto size = estimateMetalRhiTextureBytes(Contract::parentDescriptor(input));
        if (!size || size > budget.maxTextureBytes - estimated)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-BUDGET");
        estimated += size;
    }
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        Parents parents;
        Views views;
        Contents contents;
        Observations observations;
        Markers markers;
        std::array<Content::Revision, Contract::imageCount> revisions;
        std::uint64_t actual = 0u;
        for (std::size_t i = 0u; i < inputs.size(); ++i) {
            const auto& input = inputs[i];
            parents[i] = core::shareUnique(device.createTexture(Contract::parentDescriptor(input)));
            if (!parents[i])
                return fail("RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-CREATE-FAILED");
            const auto bytes = parents[i]->allocation().estimatedSizeBytes;
            if (!bytes || bytes > budget.maxTextureBytes - actual)
                return fail("RHI-METAL-EXCLUSIVE-IMAGES-TEXTURE-BUDGET");
            actual += bytes;
            views[i] = core::shareUnique(
                device.createTextureView(*parents[i], Contract::viewDescriptor(input)));
            if (!views[i])
                return fail("RHI-METAL-EXCLUSIVE-IMAGES-VIEW-CREATE-FAILED");
            rhi::BindlessTextureInventoryResource image;
            if (const auto* id =
                    rhi::BindlessTextureInventoryResource::capture(*parents[i], *views[i], image))
                return fail(id);
            contents[i] = core::makeShared<Content>(Content::Input{image,
                                                        input.extent,
                                                        input.bytesPerRow,
                                                        input.rowsPerImage,
                                                        input.bytes,
                                                        input.byteCount},
                Content::Budget{Contract::version, true, facts.bytes[i], facts.bytes[i]});
            if (const auto* id = contents[i]->diagnostic())
                return fail(id);
            if (const auto* id = MetalRhiTextureInventoryObservation::capture(registry,
                    geometry->pipeline()->native()->nativeDevice,
                    views[i]->id(),
                    observations[i]))
                return fail(id);
            if (observations[i].parent().texture->immutableRevision()
                || observations[i].view().textureView->immutableRevision()
                || observations[i].parent().texture->aliasHeap)
                return fail("RHI-METAL-EXCLUSIVE-IMAGES-FRESH-STORAGE-REQUIRED");
            const auto revision = immutableImageRevisions.tryAcquire();
            if (!revision)
                return fail("RHI-METAL-EXCLUSIVE-IMAGES-REVISION-EXHAUSTED");
            revisions[i] = {Content::version, image.view, image.parent, revision};
        }
        auto encoder = device.createCommandEncoder({});
        if (!encoder)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-COMMAND-CREATE-FAILED");
        const auto barrier = [&](std::size_t i, bool upload) {
            rhi::ResourceBarrierDesc desc;
            desc.resource = parents[i]->id();
            desc.textureSubresources = contents[i]->image().range;
            desc.before = upload ? rhi::ResourceState::ShaderRead : rhi::ResourceState::CopyDst;
            desc.after = upload ? rhi::ResourceState::CopyDst : rhi::ResourceState::ShaderRead;
            desc.sourceStages =
                upload ? rhi::PipelineStage::FragmentShader : rhi::PipelineStage::Transfer;
            desc.sourceAccess =
                upload ? rhi::ResourceAccess::ShaderRead : rhi::ResourceAccess::TransferWrite;
            desc.destinationStages =
                upload ? rhi::PipelineStage::Transfer : rhi::PipelineStage::FragmentShader;
            desc.destinationAccess =
                upload ? rhi::ResourceAccess::TransferWrite : rhi::ResourceAccess::ShaderRead;
            return encoder->transition(*parents[i], desc).success;
        };
        for (std::size_t i = 0u; i < inputs.size(); ++i)
            if (!barrier(i, true))
                return fail("RHI-METAL-EXCLUSIVE-IMAGES-UPLOAD-DEPENDENCY");
        auto pass = encoder->beginTransferPass({});
        if (!pass)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-TRANSFER-BEGIN");
        for (std::size_t i = 0u; i < inputs.size(); ++i) {
            rhi::UploadTextureDesc desc;
            if (const auto* id =
                    contents[i]->describeUpload(revisions[i], encoder->recordingIdentity(), desc))
                return fail(id);
            const auto uploaded = pass->uploadTexture(desc);
            if (!uploaded.success) {
                diagnostic = uploaded.diagnostics;
                return {};
            }
        }
        if (!pass->end().success)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-TRANSFER-END");
        for (std::size_t i = 0u; i < inputs.size(); ++i)
            if (!barrier(i, false))
                return fail("RHI-METAL-EXCLUSIVE-IMAGES-READ-DEPENDENCY");
        auto command = encoder->finish();
        if (!command || command->commandCount() != Contract::commandUnits)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-COMMAND-FINISH");
        const auto native = metalRhiNativePayloadFor(*command);
        if (!native || !native->stats() || !native->stats()->nativeRuntimeBacked
            || native->stats()->nativeRuntimeDiagnosticOnly
            || !native->stats()->nativeBlitCopyEncoded)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-NATIVE-COMMAND-REQUIRED");
        if (!device.queue().submit(*command).success)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-SUBMIT-FAILED");
        if (!waitMetalRhiNativeQueueIdle(diagnostic) || !native->readbackWasSubmitted()
            || native->readbackCompletionStatus() != MetalReadbackCompletion::Completed)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-NATIVE-COMPLETION-REQUIRED");
        // No resource IDs/owners have escaped this method. The only admitted
        // writer is already completed before issuing any immutable marker.
        for (std::size_t i = 0u; i < inputs.size(); ++i) {
            markers[i] = core::makeShared<Marker>(
                Marker::ConstructionKey{}, revisions[i], contents[i]->image().range);
            observations[i].parent().texture->immutableRevision_ = markers[i];
            observations[i].view().textureView->immutableRevision_ = markers[i];
        }
        auto root = core::makeShared<MetalRhiClosedConsumerImages>(ConstructionKey{},
            geometry,
            std::move(parents),
            std::move(views),
            std::move(contents),
            std::move(observations),
            std::move(markers),
            budget,
            actual,
            issuer->lifetime_.ticket());
        if (root->childOwnerCount() != required)
            return fail("RHI-METAL-EXCLUSIVE-IMAGES-OWNER-PROJECTION");
        if (const auto* id = root->validate())
            return fail(id);
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-EXCLUSIVE-IMAGES-ALLOCATION-FAILED");
    }
}
} // namespace arp::rhi_metal
