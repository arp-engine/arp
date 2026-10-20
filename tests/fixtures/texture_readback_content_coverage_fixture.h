#pragma once
#include "rhi/frontend/transfer_readback_coverage.h"
#include "texture_readback_layout_fixture.h"
#include "texture_upload_packing_fixture.h"

namespace arp::test {
class TextureReadbackContentCoverageFixture final {
    using Coverage = rhi::semantics::TextureReadbackContentCoverage;
    static constexpr std::uint64_t cpuBytes = 65536u;
    static constexpr Coverage::Budget budget{1u, true, 4u, 4u};

public:
    class Context final {
    public:
        TextureReadbackLayoutFixture::Context readContext;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> uploadQuotas;
        std::array<std::uint8_t, 160> pixels{};
        rhi::TextureDesc texture = TextureReadbackLayoutFixture::texture();
        rhi::ReadbackTextureDesc read = TextureReadbackLayoutFixture::descriptor();
        rhi::UploadTextureDesc upload;
        std::shared_ptr<const rhi::TextureReadbackLayout> readback;
        std::shared_ptr<const rhi::TextureUploadPacking> packing;

        Context()
        {
            for (auto& q : uploadQuotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            for (std::size_t i = 0u; i < pixels.size(); ++i)
                pixels[i] = static_cast<std::uint8_t>(i + 1u);
            texture.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
            texture.initialState = rhi::ResourceState::CopyDst;
            upload = TextureUploadPackingFixture::contentInput(pixels.data());
            upload.destinationTexture = rhi::allocateGpuResourceId();
            read.sourceResource = upload.destinationTexture;
            read.destinationReadbackBuffer = rhi::allocateGpuResourceId();
            upload.destinationOffset = {0u, 0u, 0u};
            upload.extent = {4u, 4u, 1u};
            upload.bytesPerRow = 16u;
            upload.rowsPerImage = 4u;
            upload.mipLayouts = {
                {1u, upload.extent, 16u, 4u, 0u}, {2u, {2u, 2u, 1u}, 8u, 2u, 128u}};
        }

        void freeze()
        {
            readback.reset();
            packing.reset();
            const char* diagnostic = nullptr;
            readback = readContext.prepare(read, texture, diagnostic, read.sizeBytes);
            assert(readback);
            packing = TextureUploadPackingFixture::prepareContentPacking(
                upload, texture, uploadQuotas, diagnostic);
            assert(packing);
        }

        void retire()
        {
            readback.reset();
            packing.reset();
            readContext.retired();
            for (const auto& q : uploadQuotas)
                assert(!q->liveBytes());
        }
    };

    static void verify()
    {
        Context c;
        c.freeze();
        auto b = budget;
        const auto reject = [&](const char* expected) {
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(
                std::string_view(Coverage::validateOriginal(c.readback, c.packing, b)) == expected
                && core::defaultCpuAllocator().stats().allocationCount == before);
        };
        b.contractVersion = 0u;
        reject("RHI-TEXTURE-READBACK-COVERAGE-VERSION");
        b.contractVersion = 1u;
        b.prototypeOptIn = false;
        reject("RHI-TEXTURE-READBACK-COVERAGE-OPT-IN");
        b.prototypeOptIn = true;
        b.maxReadbackMips = 0u;
        reject("RHI-TEXTURE-READBACK-COVERAGE-BUDGET");
        b.maxReadbackMips = 1u;
        reject("RHI-TEXTURE-READBACK-COVERAGE-CAPACITY");
        b.maxReadbackMips = 4u;
        b.maxUploadMips = 1u;
        reject("RHI-TEXTURE-READBACK-COVERAGE-CAPACITY");
        b.maxUploadMips = 4u;
        auto readback = c.readback;
        c.readback = std::shared_ptr<const rhi::TextureReadbackLayout>(
            c.readContext.quotas[0], readback.get());
        reject("RHI-TEXTURE-READBACK-COVERAGE-READBACK-OWNER");
        c.readback = std::move(readback);
        auto packing = c.packing;
        c.packing =
            std::shared_ptr<const rhi::TextureUploadPacking>(c.uploadQuotas[0], packing.get());
        reject("RHI-TEXTURE-READBACK-COVERAGE-UPLOAD-OWNER");
        c.packing = std::move(packing);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto outcome = Coverage::lowering(backend);
            assert(outcome.metadataEquivalent && !outcome.nativeExecution
                && std::string_view(outcome.diagnostic)
                    == "RHI-TEXTURE-READBACK-COVERAGE-EXECUTION-UNPROVEN");
        }
        assert(!Coverage::lowering(static_cast<rhi::Backend>(99)).metadataEquivalent);
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Coverage::validateOriginal(c.readback, c.packing, {1u, true, 2u, 2u}));
        for (const auto& q : c.uploadQuotas) {
            core::CpuMemoryQuotaLease full(q, cpuBytes - q->liveBytes());
            assert(full.charged() && !Coverage::validateOriginal(c.readback, c.packing, budget));
        }
        for (const auto& q : c.readContext.quotas) {
            core::CpuMemoryQuotaLease full(q, cpuBytes - q->liveBytes());
            assert(full.charged() && !Coverage::validateOriginal(c.readback, c.packing, budget));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        std::pmr::set_default_resource(previous);
        c.read.label.clear();
        c.read.mipLayouts.clear();
        c.read.mipLayouts.shrink_to_fit();
        c.upload.label.clear();
        c.upload.mipLayouts.clear();
        c.upload.mipLayouts.shrink_to_fit();
        c.pixels.fill(0u);
        assert(!Coverage::validateOriginal(c.readback, c.packing, budget)
            && c.packing->inputOwner()->pixelBytes()[0] == 1u);
        c.retire();
        verifyRegions();
        verifyVolume();
        verifyBlocks();
        verifyMalformed();
        std::puts(
            "BINDLESS_RHI_READBACK_COVERAGE_OK original-owners descriptor-id mips2 arrays3d bc-logical no-zero-union bounded caller-drop allocation0 native-closed");
    }

    static void prepareActualLoss()
    {
        loss_ = core::makeUnique<Context>();
        loss_->freeze();
        assert(!Coverage::validateOriginal(loss_->readback, loss_->packing, budget));
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost()
            && !Coverage::validateOriginal(loss_->readback, loss_->packing, budget));
        loss_->retire();
        loss_.reset();
        std::puts(
            "BINDLESS_RHI_READBACK_COVERAGE_LOSS_OK cpu-only unchanged-regions exact-retire native-closed");
    }

private:
    static void verifyRegions()
    {
        for (unsigned kind = 0u; kind < 9u; ++kind) {
            Context c;
            if (kind == 0u) {
                c.read.sourceSubresources.baseArrayLayer = 2u;
                c.read.sourceSubresources.arrayLayerCount = 1u;
                c.read.extent = {3u, 2u, 1u};
                c.read.mipLayouts[0].extent = c.read.extent;
                c.read.mipLayouts[1].extent = {1u, 1u, 1u};
            }
            if (kind == 1u || kind == 2u) {
                c.upload.destinationOffset =
                    kind == 1u ? rhi::Extent3D{1u, 0u, 0u} : rhi::Extent3D{0u, 1u, 0u};
                if (kind == 1u) {
                    c.upload.extent.width = 3u;
                    c.upload.mipLayouts[1].extent.width = 1u;
                } else {
                    c.upload.extent.height = 3u;
                    c.upload.mipLayouts[1].extent.height = 1u;
                }
                c.upload.mipLayouts[0].extent = c.upload.extent;
            }
            if (kind == 3u || kind == 4u) {
                c.read.sourceSubresources.baseArrayLayer = kind == 3u ? 0u : 3u;
                c.read.sourceSubresources.arrayLayerCount = 1u;
            }
            if (kind == 5u) {
                c.read.sourceSubresources = {0u, 1u, 1u, 1u, rhi::TextureAspect::Color};
                c.read.mipLayouts.clear();
            }
            if (kind == 6u) {
                c.upload.subresources.mipLevelCount = 1u;
                c.upload.mipLayouts.resize(1u);
                c.read.sourceSubresources = {2u, 1u, 1u, 1u, rhi::TextureAspect::Color};
                c.read.extent = {2u, 2u, 1u};
                c.read.mipLayouts.clear();
            }
            if (kind == 7u)
                c.read.sourceResource = rhi::allocateGpuResourceId();
            c.freeze();
            if (kind == 8u) {
                auto different = c.texture;
                different.format = rhi::TextureFormat::R8Unorm;
                const char* diagnostic = nullptr;
                auto read = c.readContext.prepare(c.read, different, diagnostic);
                assert(read);
                c.readback = std::move(read);
            }
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            const auto* diagnostic = Coverage::validateOriginal(c.readback, c.packing, budget);
            if (!kind)
                assert(!diagnostic);
            else
                assert(std::string_view(diagnostic)
                    == (kind < 5u        ? "RHI-TEXTURE-READBACK-COVERAGE-REGION"
                            : kind < 7u  ? "RHI-TEXTURE-READBACK-COVERAGE-MIP-MISSING"
                            : kind == 7u ? "RHI-TEXTURE-READBACK-COVERAGE-SOURCE-ID"
                                         : "RHI-TEXTURE-READBACK-COVERAGE-SOURCE-LAYOUT"));
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
            c.retire();
        }
    }

    static void verifyVolume()
    {
        for (const bool nonzeroOrigin : {false, true}) {
            Context c;
            c.texture.extent = {4u, 4u, 4u};
            c.texture.mipLevelCount = 2u;
            c.texture.arrayLayerCount = 1u;
            c.upload.mipLevel = 0u;
            c.upload.arrayLayer = 0u;
            c.upload.subresources = {0u, 2u, 0u, 1u, rhi::TextureAspect::Color};
            c.upload.extent = {2u, 2u, 2u};
            c.upload.bytesPerRow = 8u;
            c.upload.rowsPerImage = 2u;
            c.upload.destinationOffset = {0u, 0u, nonzeroOrigin ? 1u : 0u};
            c.upload.mipLayouts = {
                {0u, c.upload.extent, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, 32u}};
            c.read.sourceSubresources = c.upload.subresources;
            c.read.extent = c.upload.extent;
            c.read.bytesPerRow = 256u;
            c.read.rowsPerImage = 2u;
            c.read.destinationOffsetBytes = 0u;
            c.read.sizeBytes = 1536u;
            c.read.mipLayouts = {
                {0u, c.read.extent, 256u, 2u, 0u}, {1u, {1u, 1u, 1u}, 256u, 1u, 1280u}};
            c.freeze();
            const auto* diagnostic = Coverage::validateOriginal(c.readback, c.packing, budget);
            assert(nonzeroOrigin ? diagnostic
                        && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-COVERAGE-REGION"
                                 : !diagnostic);
            c.retire();
        }
    }

    static void verifyBlocks()
    {
        for (const auto format : {rhi::TextureFormat::Bc1Unorm,
                 rhi::TextureFormat::Bc3Unorm,
                 rhi::TextureFormat::Bc7Unorm,
                 rhi::TextureFormat::Bc7UnormSrgb})
            for (const bool partial : {false, true}) {
                Context c;
                c.texture.extent = {10u, 6u, 1u};
                c.texture.mipLevelCount = 1u;
                c.texture.arrayLayerCount = 1u;
                c.texture.format = format;
                c.upload.format = format;
                c.upload.mipLevel = 0u;
                c.upload.arrayLayer = 0u;
                c.upload.subresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
                c.upload.extent = partial ? rhi::Extent3D{8u, 4u, 1u} : c.texture.extent;
                c.upload.bytesPerRow =
                    static_cast<std::uint32_t>((partial ? 2u : 3u) * rhi::bytesPerBlock(format));
                c.upload.rowsPerImage = partial ? 1u : 2u;
                c.upload.mipLayouts.clear();
                c.read.sourceSubresources = c.upload.subresources;
                c.read.extent = c.texture.extent;
                c.read.destinationOffsetBytes = 0u;
                c.read.sizeBytes = 512u;
                c.read.bytesPerRow = 256u;
                c.read.rowsPerImage = 2u;
                c.read.mipLayouts.clear();
                c.freeze();
                assert(c.readback->mipCopies()[0].physicalExtent == rhi::Extent3D({12u, 8u, 1u}));
                const auto* diagnostic = Coverage::validateOriginal(c.readback, c.packing, budget);
                assert(partial ? diagnostic
                            && std::string_view(diagnostic)
                                == "RHI-TEXTURE-READBACK-COVERAGE-REGION"
                               : !diagnostic);
                c.retire();
            }
    }

    static void verifyMalformed()
    {
        Context c;
        const char* diagnostic = nullptr;
        c.upload.mipLayouts[1].mipLevel = 1u;
        auto snapshot = rhi::TextureUploadInputSnapshot::prepareOriginal(c.upload,
            {1u, true, 4u, 128u, 4096u, cpuBytes, cpuBytes, cpuBytes},
            c.uploadQuotas[0],
            c.uploadQuotas[1],
            c.uploadQuotas[2],
            diagnostic);
        assert(snapshot);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!rhi::TextureUploadPacking::prepareOriginal(snapshot,
                   c.texture,
                   {1u, true, 4u, 4096u, cpuBytes, cpuBytes},
                   c.uploadQuotas[3],
                   c.uploadQuotas[4],
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-UPLOAD-LAYOUT-MIP-ORDER"
            && core::defaultCpuAllocator().stats().allocationCount == before);
        snapshot.reset();
        c.read.sourceSubresources.baseArrayLayer = UINT32_MAX;
        c.read.sourceSubresources.arrayLayerCount = 2u;
        auto read = c.readContext.capture(c.read, diagnostic);
        assert(read);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!rhi::TextureReadbackLayout::prepareOriginal(read,
                   c.texture,
                   {1u, true, 4u, c.read.sizeBytes, cpuBytes},
                   c.readContext.quotas[2],
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-LAYOUT-RANGE"
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        read.reset();
        c.retire();
    }

    static inline core::UniquePtr<Context> loss_;
};
} // namespace arp::test
