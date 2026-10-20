#include "renderer/terrain_gpu_rhi_uploader.h"

#include <array>
#include <cstring>
#include <limits>

namespace arp::renderer {
namespace {

struct TerrainTilePageTableGpuRecord final {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    std::uint32_t weightArrayLayer = 0u;
    std::uint32_t resourceGenerationLow = 0u;
    std::uint32_t resourceGenerationHigh = 0u;
    std::uint32_t reserved0 = 0u;
};

static_assert(sizeof(TerrainTilePageTableGpuRecord) == 32u,
    "TerrainTilePageTableEntry Slang ABI must remain 32 bytes");

struct TerrainPageTableParamsGpu final {
    std::uint32_t entryCount = 0u;
    std::uint32_t reserved0 = 0u;
    std::uint32_t reserved1 = 0u;
    std::uint32_t reserved2 = 0u;
};

static_assert(sizeof(TerrainPageTableParamsGpu) == 16u,
    "TerrainPageTableParams Slang ABI must remain 16 bytes");

core::pmr::String text(const char* value)
{
    return core::pmr::String(value, &core::defaultCpuMemoryResource());
}

} // namespace

TerrainGpuRhiUploaderResult TerrainGpuRhiUploader::failure(
    TerrainGpuPageBackendStatus status, const char* message) const
{
    TerrainGpuRhiUploaderResult result;
    result.status = status;
    result.diagnostics = text(message);
    return result;
}

bool TerrainGpuRhiUploader::backendCanExecute(core::pmr::String& diagnostics) const
{
    if (device_ == nullptr) {
        diagnostics = text("TerrainAsset GPU uploader has no RHI device");
        return false;
    }
    switch (device_->backend()) {
    case rhi::Backend::Null:
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
        return true;
    case rhi::Backend::WebGPU:
        // WebGPU now uses the same explicit texture-array, transfer-upload,
        // and page-table path as Vulkan. The RHI binding/layout validation is
        // exercised by the Web target build and runtime smoke, so do not
        // fail closed before the actual upload commands are recorded.
        return true;
    case rhi::Backend::WebGL:
        diagnostics = text("TERRAIN-GPU-WEBGL1-EXECUTION-UNAVAILABLE");
        return false;
    }
    diagnostics = text("TERRAIN-GPU-BACKEND-DIAGNOSTIC-FAILURE: unknown RHI backend");
    return false;
}

TerrainGpuRhiUploaderResult TerrainGpuRhiUploader::create(
    rhi::GpuDevice& device, const TerrainGpuRhiUploaderDesc& desc)
{
    clear();
    if (!desc.valid()) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset GPU uploader requires explicit samplesPerEdge and maxTilePages");
    }
    device_ = &device;
    desc_ = desc;
    core::pmr::String diagnostic;
    if (!backendCanExecute(diagnostic)) {
        const auto rejectedStatus = device.backend() == rhi::Backend::WebGPU
            ? TerrainGpuPageBackendStatus::Degraded
            : TerrainGpuPageBackendStatus::DiagnosticFailure;
        auto result = failure(rejectedStatus, diagnostic.c_str());
        clear();
        return result;
    }

    const auto makeTexture = [&](const char* suffix, rhi::TextureFormat format) {
        rhi::TextureDesc texture;
        texture.label = desc_.label + suffix;
        texture.extent = {desc_.samplesPerEdge, desc_.samplesPerEdge, 1u};
        texture.format = format;
        texture.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        texture.memory = rhi::MemoryUsage::GpuOnly;
        texture.initialState = rhi::ResourceState::Undefined;
        texture.arrayLayerCount = desc_.maxTilePages;
        return device_->createTexture(texture);
    };
    heightPages_ = makeTexture("HeightPages", rhi::TextureFormat::R32Float);
    weightPageA_ = makeTexture("WeightPageA", rhi::TextureFormat::Rgba8Unorm);
    weightPageB_ = makeTexture("WeightPageB", rhi::TextureFormat::Rgba8Unorm);
    if (!heightPages_ || !weightPageA_ || !weightPageB_) {
        clear();
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset GPU texture-array creation failed");
    }

    const auto makeView = [&](rhi::GpuTexture& texture, const char* suffix) {
        rhi::TextureViewDesc view;
        view.label = desc_.label + suffix;
        view.format = texture.desc().format;
        view.usage = rhi::TextureUsage::Sampled;
        view.dimension = rhi::TextureViewDimension::Texture2DArray;
        view.mipLevelCount = 1u;
        view.arrayLayerCount = desc_.maxTilePages;
        return device_->createTextureView(texture, view);
    };
    heightPagesView_ = makeView(*heightPages_, "HeightPagesView");
    weightPageAView_ = makeView(*weightPageA_, "WeightPageAView");
    weightPageBView_ = makeView(*weightPageB_, "WeightPageBView");
    if (!heightPagesView_ || !weightPageAView_ || !weightPageBView_) {
        clear();
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset GPU texture-array view creation failed");
    }

    // The PBR descriptor intentionally addresses each complete texture array.
    // Vulkan therefore requires a declared layout for every layer, including
    // unleased layers that the page table never publishes. These layers remain
    // unreferenced and contain no payload; an upload transitions its explicit
    // layer back through CopyDst before that page may enter the table.
    auto layoutEncoder = device_->createCommandEncoder({desc_.label + "PageArrayInitialLayout"});
    if (!layoutEncoder) {
        clear();
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset GPU texture-array initial layout command encoder creation failed");
    }
    const auto transitionFullArray = [&](rhi::GpuTexture& texture) {
        const auto range = rhi::defaultTextureSubresourceRange(texture.desc());
        return layoutEncoder->transition(
            texture, range, texture.subresourceState(range), rhi::ResourceState::ShaderRead);
    };
    if (!transitionFullArray(*heightPages_).success || !transitionFullArray(*weightPageA_).success
        || !transitionFullArray(*weightPageB_).success) {
        clear();
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset GPU texture-array initial layout transition failed");
    }
    auto layoutCommands = layoutEncoder->finish();
    if (!layoutCommands || !device_->queue().submit(*layoutCommands).success) {
        clear();
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset GPU texture-array initial layout submission failed");
    }

    status_ = device.backend() == rhi::Backend::Null ? TerrainGpuPageBackendStatus::NullValidation
                                                     : TerrainGpuPageBackendStatus::Native;
    TerrainGpuRhiUploaderResult result;
    result.success = true;
    result.status = status_;
    return result;
}

TerrainGpuRhiUploaderResult TerrainGpuRhiUploader::upload(
    const TerrainGpuTilePayload& tile, std::uint32_t arrayLayer, std::uint64_t submissionSerial)
{
    if (!ready() || !tile.valid() || tile.samplesPerEdge != desc_.samplesPerEdge
        || arrayLayer >= desc_.maxTilePages || submissionSerial == 0u) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload violates the declared page layout or submission serial contract");
    }
    core::pmr::String diagnostic;
    if (!backendCanExecute(diagnostic)) {
        return failure(TerrainGpuPageBackendStatus::Degraded, diagnostic.c_str());
    }
    auto encoder = device_->createCommandEncoder({desc_.label + "TileUpload"});
    if (!encoder) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload command encoder creation failed");
    }
    const rhi::TextureSubresourceRange range{0u, 1u, arrayLayer, 1u, rhi::TextureAspect::Color};
    const auto transition = [&](rhi::GpuTexture& texture) {
        return encoder->transition(
            texture, range, texture.subresourceState(range), rhi::ResourceState::CopyDst);
    };
    if (!transition(*heightPages_).success || !transition(*weightPageA_).success
        || !transition(*weightPageB_).success) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload could not transition page textures to CopyDst");
    }
    auto transfer = encoder->beginTransferPass({desc_.label + "TileUploadTransfer"});
    if (!transfer) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload transfer pass creation failed");
    }
    const auto uploadTexture = [&](rhi::GpuTexture& texture,
                                   const char* suffix,
                                   rhi::TextureFormat format,
                                   const void* bytes,
                                   std::size_t byteCount) {
        const auto tightRowBytes = static_cast<std::size_t>(desc_.samplesPerEdge) * 4u;
        const auto alignedRowPitch =
            core::alignCpuAllocationOffsetUp(tightRowBytes, rhi::kTextureUploadRowPitchAlignment);
        if (!alignedRowPitch.success
            || alignedRowPitch.alignedValue > std::numeric_limits<std::uint32_t>::max()
            || byteCount != tightRowBytes * desc_.samplesPerEdge) {
            return rhi::CommandValidationResult{
                false,
                "TerrainAsset tile upload cannot pack the declared texture row layout",
                0u,
            };
        }
        const auto rowPitch = static_cast<std::uint32_t>(alignedRowPitch.alignedValue);
        core::pmr::Vector<std::uint8_t> stagingBytes{&core::defaultCpuMemoryResource()};
        stagingBytes.resize(static_cast<std::size_t>(rowPitch) * desc_.samplesPerEdge, 0u);
        const auto* source = static_cast<const std::uint8_t*>(bytes);
        for (std::uint32_t row = 0u; row < desc_.samplesPerEdge; ++row) {
            std::memcpy(stagingBytes.data() + static_cast<std::size_t>(row) * rowPitch,
                source + static_cast<std::size_t>(row) * tightRowBytes,
                tightRowBytes);
        }
        rhi::UploadTextureDesc request;
        request.label = desc_.label + suffix;
        request.destinationTexture = texture.id();
        request.extent = {desc_.samplesPerEdge, desc_.samplesPerEdge, 1u};
        request.format = format;
        request.bytesPerRow = rowPitch;
        request.rowsPerImage = desc_.samplesPerEdge;
        request.arrayLayer = arrayLayer;
        request.subresources = range;
        request.pixelData = stagingBytes.data();
        request.pixelDataSize = static_cast<std::uint32_t>(stagingBytes.size());
        request.backendOwnedStaging = true;
        request.frameSerial = submissionSerial;
        request.resourceGeneration = tile.resourceGeneration;
        return transfer->uploadTexture(request);
    };
    const auto heightUploaded = uploadTexture(*heightPages_,
        "HeightUpload",
        rhi::TextureFormat::R32Float,
        tile.heights,
        tile.heightCount * sizeof(float));
    const auto pageAUploaded = heightUploaded.success ? uploadTexture(*weightPageA_,
                                                            "WeightPageAUpload",
                                                            rhi::TextureFormat::Rgba8Unorm,
                                                            tile.weightPageA,
                                                            tile.weightPageAByteCount)
                                                      : heightUploaded;
    const auto pageBUploaded = pageAUploaded.success ? uploadTexture(*weightPageB_,
                                                           "WeightPageBUpload",
                                                           rhi::TextureFormat::Rgba8Unorm,
                                                           tile.weightPageB,
                                                           tile.weightPageBByteCount)
                                                     : pageAUploaded;
    if (!pageBUploaded.success) {
        TerrainGpuRhiUploaderResult result = failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload transfer recording failed: ");
        result.diagnostics += pageBUploaded.diagnostics;
        return result;
    }
    const auto transferEnded = transfer->end();
    if (!transferEnded.success) {
        TerrainGpuRhiUploaderResult result = failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload transfer pass finalization failed: ");
        result.diagnostics += transferEnded.diagnostics;
        return result;
    }
    const auto transitionToRead = [&](rhi::GpuTexture& texture) {
        return encoder->transition(
            texture, range, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
    };
    if (!transitionToRead(*heightPages_).success || !transitionToRead(*weightPageA_).success
        || !transitionToRead(*weightPageB_).success) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload could not transition page textures to ShaderRead");
    }
    auto commands = encoder->finish();
    if (!commands || !device_->queue().submit(*commands).success) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset tile upload submission failed");
    }
    TerrainGpuRhiUploaderResult result;
    result.success = true;
    result.status = status_;
    result.pages = {
        heightPages_->id().value, weightPageA_->id().value, weightPageB_->id().value, arrayLayer};
    return result;
}

TerrainGpuRhiUploaderResult TerrainGpuRhiUploader::writePageTable(
    const core::pmr::Vector<TerrainGpuPageTableEntry>& entries, std::uint64_t submissionSerial)
{
    if (!ready() || submissionSerial == 0u || entries.size() > desc_.maxTilePages) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset page-table write violates capacity or submission serial contract");
    }
    core::pmr::Vector<TerrainTilePageTableGpuRecord> records{&core::defaultCpuMemoryResource()};
    records.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry.worldBounds.valid() || !entry.pages.valid()
            || entry.pages.heightPage != heightPages_->id().value
            || entry.pages.weightPageA != weightPageA_->id().value
            || entry.pages.weightPageB != weightPageB_->id().value
            || entry.pages.weightArrayLayer >= desc_.maxTilePages) {
            return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
                "TerrainAsset page table contains a foreign page or invalid bounds");
        }
        records.push_back({entry.worldBounds.minX,
            entry.worldBounds.maxX,
            entry.worldBounds.minZ,
            entry.worldBounds.maxZ,
            entry.pages.weightArrayLayer,
            static_cast<std::uint32_t>(entry.resourceGeneration),
            static_cast<std::uint32_t>(entry.resourceGeneration >> 32u),
            0u});
    }
    // Capacity includes the published snapshot, every in-flight retired
    // snapshot, and this candidate. Reject before resource allocation so an
    // unavailable descriptor generation cannot be mistaken for publication.
    if (activePageTable_.generation != 0u
        && retiredPageTables_.size() + 2u > desc_.maxPageTableSnapshots) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset page-table snapshot capacity is exhausted; wait for completed submissions");
    }
    if (activePageTable_.generation != 0u && !retiredPageTables_.canRetire()) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset page-table retirement capacity is exhausted");
    }

    // An empty snapshot is represented by entryCount == 0. Do not issue a
    // zero-byte write with a null data pointer: no shader may address stale
    // records once the count is published, and several RHI backends reject
    // that invalid host-write request by contract.
    OwnedPageTableSnapshot candidate;
    candidate.generation = nextPageTableGeneration_;
    candidate.entries = device_->createBuffer({
        desc_.label + "PageTableGeneration",
        static_cast<std::uint64_t>(desc_.maxTilePages) * sizeof(TerrainTilePageTableGpuRecord),
        rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead,
    });
    candidate.params = device_->createBuffer({
        desc_.label + "PageTableParamsGeneration",
        sizeof(TerrainPageTableParamsGpu),
        rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead,
    });
    if (!candidate.entries || !candidate.params) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset page-table snapshot allocation failed");
    }
    rhi::CommandValidationResult tableWritten;
    tableWritten.success = true;
    if (!records.empty()) {
        tableWritten = device_->writeBuffer(*candidate.entries,
            records.data(),
            records.size() * sizeof(TerrainTilePageTableGpuRecord));
    }
    const TerrainPageTableParamsGpu params{static_cast<std::uint32_t>(records.size()), 0u, 0u, 0u};
    rhi::CommandValidationResult paramsWritten = tableWritten;
    if (tableWritten.success) {
        paramsWritten = device_->writeBuffer(*candidate.params, &params, sizeof(params));
    }
    if (!paramsWritten.success) {
        return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
            "TerrainAsset page-table buffer write failed");
    }
    if (activePageTable_.generation != 0u) {
        if (!retiredPageTables_.retire(submissionSerial, std::move(activePageTable_))) {
            return failure(TerrainGpuPageBackendStatus::DiagnosticFailure,
                "TerrainAsset page-table retirement capacity is exhausted");
        }
    }
    activePageTable_ = std::move(candidate);
    ++nextPageTableGeneration_;
    TerrainGpuRhiUploaderResult result;
    result.success = true;
    result.status = status_;
    result.pageTableGeneration = activePageTable_.generation;
    return result;
}

TerrainGpuRhiUploader::PageTableSnapshot TerrainGpuRhiUploader::pageTableSnapshot() const noexcept
{
    return {
        activePageTable_.generation, activePageTable_.entries.get(), activePageTable_.params.get()};
}

void TerrainGpuRhiUploader::clear()
{
    heightPagesView_.reset();
    weightPageAView_.reset();
    weightPageBView_.reset();
    activePageTable_ = {};
    (void)retiredPageTables_.reapAll();
    nextPageTableGeneration_ = 1u;
    heightPages_.reset();
    weightPageA_.reset();
    weightPageB_.reset();
    device_ = nullptr;
    status_ = TerrainGpuPageBackendStatus::Uninitialized;
}

} // namespace arp::renderer
