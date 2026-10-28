#pragma once

#include "rhi/frontend/ordinary_sampled_color.h"
#include "rhi/frontend/original_texture_upload_device.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::tests {

// Test assembly of the existing public upload Program. No private backend
// uploader, alternate executor, or fabricated completion is used here.
class OriginalTextureUploadFixture final {
public:
    static constexpr std::uint64_t metadataBytes = 2u * 1024u * 1024u;
    std::shared_ptr<const rhi::OriginalCommandTextureViewSource> view;
    std::shared_ptr<const rhi::OriginalTextureUploadPublication> publication;

    static void prepare(rhi::GpuDevice& device)
    {
        using namespace rhi;
        const char* id = nullptr;
        OriginalTextureUploadPreparationRequest request{
            {1u, true, 1u, 1u, 2048u}, {1u, true, 1u, 1024u, metadataBytes}};
        auto* extension = OriginalTextureUploadDeviceExtension::query(device, request, id);
        require(id);
        assert(extension);
        require(extension->prepareStorage(
            {1u, true, {1u, 2048u, 2048u, 1u, 2048u, 256u}, 1u, metadataBytes},
            core::makeShared<core::CpuMemoryQuota>(metadataBytes)));
    }

    OriginalTextureUploadFixture(rhi::GpuDevice& device,
        const rhi::TextureDesc& desc,
        core::ArrayView<std::uint8_t> pixels,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 13u> q;
        for (auto& quota : q)
            quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        assert(sourceQuota && sourceQuota->limitBytes() == metadataBytes);
        q[2] = sourceQuota;
        const char* id = nullptr;
        const auto nativeBytes = device.queryOriginalContentTextureAllocation(desc, id);
        require(id);
        assert(nativeBytes);
        core::UniquePtr<GpuTexture> texture;
        {
            core::CpuMemoryQuotaScope scope(q[0]);
            texture = device.createOriginalContentTexture(desc,
                {3u, true, 1u, 1u, pixels.size(), 1u, metadataBytes, 0u, nativeBytes},
                q[1],
                id);
        }
        require(id);
        assert(texture);
        auto* object = texture.get();
        auto lease = OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, metadataBytes}, q[2], id);
        require(id);
        TextureViewDesc vd;
        vd.format = desc.format;
        vd.usage = TextureUsage::Sampled;
        auto nativeView = device.createTextureView(*object, vd);
        assert(nativeView);
        auto viewLease = OriginalCommandTextureViewLease::prepareOriginal(
            nativeView, {1u, true, 2u, metadataBytes}, q[2], id);
        require(id);
        view = OriginalCommandTextureViewSource::prepareOriginal(viewLease,
            lease,
            {1u, true, OriginalCommandTextureViewSource::completeRetainedOwnerCount, metadataBytes},
            q[2],
            id);
        require(id);
        assert(view);
        OriginalOrdinarySampledColorRead::Snapshot observation;
        assert(OriginalOrdinarySampledColorRead::capture(*view, observation));
        UploadTextureDesc upload;
        upload.destinationTexture = object->id();
        upload.extent = desc.extent;
        upload.format = desc.format;
        upload.subresources = defaultTextureSubresourceRange(desc);
        upload.bytesPerRow = desc.extent.width * 4u;
        upload.rowsPerImage = desc.extent.height;
        upload.pixelData = pixels.data();
        upload.pixelDataSize = pixels.size();
        upload.mipLayouts = {{0u, desc.extent, upload.bytesPerRow, upload.rowsPerImage, 0u}};
        auto input = TextureUploadInputSnapshot::prepareOriginal(upload,
            {1u, true, 1u, 0u, pixels.size(), metadataBytes, metadataBytes, metadataBytes},
            q[3],
            q[3],
            q[2],
            id);
        require(id);
        auto source = OriginalCommandTextureUploadSource::prepareOriginal(
            input, lease, {1u, true, 5u, metadataBytes}, q[2], id);
        require(id);
        auto packed = TextureUploadPacking::prepareOriginal(
            input, desc, {1u, true, 1u, 2048u, metadataBytes, metadataBytes}, q[5], q[6], id);
        require(id);
        auto packing = OriginalCommandTextureUploadPackingSource::prepareOriginal(source,
            packed,
            {1u,
                true,
                1u,
                OriginalCommandTextureUploadPackingSource::retainedOwnerCount,
                pixels.size(),
                2048u,
                metadataBytes},
            q[7],
            id);
        require(id);
        ResourceBarrierDesc enter{object->id(),
            defaultTextureSubresourceRange(desc),
            ResourceState::Undefined,
            ResourceState::CopyDst,
            PipelineStage::TopOfPipe,
            PipelineStage::Transfer,
            ResourceAccess::None,
            ResourceAccess::TransferWrite};
        ResourceBarrierDesc leave{object->id(),
            defaultTextureSubresourceRange(desc),
            ResourceState::CopyDst,
            ResourceState::ShaderRead,
            PipelineStage::Transfer,
            PipelineStage::VertexShader | PipelineStage::FragmentShader
                | PipelineStage::ComputeShader,
            ResourceAccess::TransferWrite,
            ResourceAccess::ShaderRead};
        const std::array<CommandUsage, 6u> commands{{{K::TextureBarrier, 0u, 1u},
            {K::BeginTransferPass, 1u, 0u},
            {K::TextureUpload, 1u, 2u},
            {K::EndTransferPass, 3u, 0u},
            {K::TextureBarrier, 3u, 1u},
            {K::Finish, 4u, 0u}}};
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = commands.size();
        admission.maxReferences = 4u;
        admission.maxInputDescriptors = 3u;
        admission.maxGraphOwners = 12u;
        admission.maxMipMappings = 1u;
        admission.maxRetainedUploadBytes = pixels.size();
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] = 1u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Upload)] = 1u;
        admission.maxCpuBytes.fill(metadataBytes);
        const std::array<OriginalCommandResourceSources::Source, 2u> owners{lease, source};
        auto sources = OriginalCommandResourceSources::prepareOriginal(
            owners.data(), owners.size(), admission, q[2], id);
        require(id);
        const std::array<OriginalCommandUsage::Reference, 4u> references{{{0u}, {0u}, {1u}, {0u}}};
        auto usage = OriginalCommandUsage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            references.data(),
            references.size(),
            q[3],
            id);
        require(id);
        core::pmr::Vector<CommandVariableArguments::Input> fields;
        CommandRecordingBudget recording{1u, true, commands.size()};
        for (const auto& command : commands) {
            fields.push_back(
                {*CommandScalarArguments::capture(command.kind, CommandScalarArguments::None{})});
            require(recording.limits.appendArguments(fields.back()));
        }
        recording.limits.resourceReferences = references.size();
        auto arguments = CommandVariableArguments::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, static_cast<std::uint32_t>(fields.size()), 0u, 0u, 0u, metadataBytes},
            q[3],
            id);
        require(id);
        auto scalars = OriginalCommandScalars::prepareOriginal(usage, arguments, q[3], id);
        require(id);
        const std::array<OriginalCommandInputs::OriginalDescriptor, 3u> descriptors{
            {{0u, *ResourceDependencyArguments::capture(K::TextureBarrier, enter)},
                {2u, *TextureUploadOriginalInputArguments::captureOriginal(input)},
                {4u, *ResourceDependencyArguments::capture(K::TextureBarrier, leave)}}};
        auto inputs = OriginalCommandInputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), q[3], id);
        require(id);
        OriginalCommandStreamBudget stream;
        stream.contractVersion = 1u;
        stream.prototypeOptIn = true;
        stream.maxCommands = commands.size();
        stream.maxDescriptors = descriptors.size();
        stream.maxReferences = references.size();
        stream.maxTextureUploadMips = 1u;
        stream.maxTextureUploadPixelBytes = pixels.size();
        stream.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = metadataBytes;
        auto program = OriginalCommandStreamProgram::prepareRecordingBoundOriginal(
            inputs, stream, recording, q[10], id);
        require(id);
        assert(program);
        OriginalOrdinaryTextureUploadContract::Binding binding{2u, packing};
        OriginalTextureUploadPreparationRequest request{
            {1u, true, 1u, 1u, packed->packedBytes().size()},
            {1u,
                true,
                1u,
                OriginalTextureUploadPublication::requiredOwners(*program, 1u),
                metadataBytes}};
        auto* extension = OriginalTextureUploadDeviceExtension::query(device, request, id);
        require(id);
        core::pmr::String diagnostic;
        auto prepared =
            extension->prepareUploads(program, request, &binding, 1u, q[11], diagnostic);
        if (!prepared.encoder)
            std::fprintf(stderr, "upload fixture preparation: %s\n", diagnostic.c_str());
        assert(prepared.encoder && prepared.publications.size() == 1u);
        publication = prepared.publications[0];
        assert(publication->validate());
        assert(prepared.encoder->transition(*object, enter).success);
        auto transfer = prepared.encoder->beginTransferPass({});
        assert(transfer && transfer->uploadTexture(upload).success && transfer->end().success);
        transfer.reset();
        assert(prepared.encoder->transition(*object, leave).success);
        auto commandsOut = prepared.encoder->finish();
        assert(commandsOut);
        auto fence = device.createFence();
        assert(fence);
        const auto submitted = device.queue().submit(*commandsOut, fence.get());
        assert(submitted.success && submitted.hasAcceptedWork());
        fence->wait();
        assert(fence->isSignaled());
        require(OriginalTextureUploadPublication::validateOriginal(publication));
        require(OriginalOrdinarySampledColorRead::capture(*view, observation));
    }

private:
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "public upload fixture: %s\n", id);
        assert(!id);
    }
};

} // namespace arp::tests
