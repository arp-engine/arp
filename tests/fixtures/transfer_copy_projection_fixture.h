#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/transfer_copy.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class TransferCopyProjectionFixture final {
    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verify()
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const char* diagnostic = nullptr;
        rhi::BufferToTextureCopyDesc desc;
        desc.sourceBuffer = rhi::allocateGpuResourceId();
        desc.destinationTexture = rhi::allocateGpuResourceId();
        desc.textureSubresources = {2u, 3u, 1u, 2u, rhi::TextureAspect::Color};
        desc.extent = {16u, 16u, 1u};
        desc.bytesPerRow = 512u;
        desc.rowsPerImage = 16u;
        desc.bufferOffsetBytes = 128u;
        desc.mipLayouts = {{2u, {16u, 16u, 1u}, 512u, 16u, 128u},
            {3u, {8u, 8u, 1u}, 256u, 8u, 32768u},
            {4u, {4u, 4u, 1u}, 256u, 4u, 40960u}};
        const auto freeze = [&](const auto& input) {
            return rhi::TransferCopyInputSnapshot::prepareOriginal(
                input, {1u, true, 3u, 65536u}, quota, diagnostic);
        };
        auto full = freeze(desc);
        assert(full);
        auto badOrder = desc;
        badOrder.mipLayouts[1].mipLevel = 4u;
        auto order = freeze(badOrder);
        assert(order);
        auto badFirst = desc;
        badFirst.mipLayouts[0].bytesPerRow = 256u;
        auto first = freeze(badFirst);
        assert(first);
        rhi::TextureToTextureCopyDesc texture;
        texture.sourceTexture = desc.destinationTexture;
        texture.destinationTexture = rhi::allocateGpuResourceId();
        texture.sourceSubresources = {3u, 2u, 0u, 1u, rhi::TextureAspect::Color};
        texture.destinationSubresources = {5u, 2u, 0u, 1u, rhi::TextureAspect::Color};
        texture.extent = {8u, 8u, 1u};
        texture.mipLayouts = {{3u, {8u, 8u, 1u}}, {4u, {4u, 4u, 1u}}};
        auto tex = freeze(texture);
        assert(tex);
        rhi::TextureToBufferCopyDesc read;
        read.sourceTexture = desc.destinationTexture;
        read.destinationBuffer = desc.sourceBuffer;
        read.textureSubresources = desc.textureSubresources;
        read.extent = desc.extent;
        read.bytesPerRow = desc.bytesPerRow;
        read.rowsPerImage = desc.rowsPerImage;
        read.bufferOffsetBytes = desc.bufferOffsetBytes;
        for (const auto& mip : desc.mipLayouts)
            read.mipLayouts.push_back({mip.mipLevel,
                mip.extent,
                mip.bytesPerRow,
                mip.rowsPerImage,
                mip.bufferOffsetBytes});
        auto readFull = freeze(read);
        assert(readFull);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                std::size_t count = 0u;
                assert(!rhi::TransferCopyProjection::visit<rhi::BufferToTextureCopyDesc>(
                    full->scalarFields(),
                    full.get(),
                    [&](const auto& copy) noexcept -> const char* {
                        const auto& authored = desc.mipLayouts[count++];
                        assert(copy.sourceBuffer == desc.sourceBuffer
                            && copy.destinationTexture == desc.destinationTexture
                            && copy.mipLayouts.empty());
                        assert(copy.textureSubresources.baseMipLevel == authored.mipLevel
                            && copy.textureSubresources.mipLevelCount == 1u);
                        assert(copy.textureSubresources.baseArrayLayer == 1u
                            && copy.textureSubresources.arrayLayerCount == 2u);
                        assert(copy.extent == authored.extent
                            && copy.bytesPerRow == authored.bytesPerRow
                            && copy.rowsPerImage == authored.rowsPerImage
                            && copy.bufferOffsetBytes == authored.bufferOffsetBytes);
                        return nullptr;
                    }));
                assert(count == 3u);
                count = 0u;
                assert(!rhi::TransferCopyProjection::visit<rhi::TextureToBufferCopyDesc>(
                    readFull->scalarFields(),
                    readFull.get(),
                    [&](const auto& copy) noexcept -> const char* {
                        const auto& authored = read.mipLayouts[count++];
                        assert(copy.sourceTexture == read.sourceTexture
                            && copy.destinationBuffer == read.destinationBuffer
                            && copy.mipLayouts.empty());
                        assert(copy.textureSubresources.baseMipLevel == authored.mipLevel
                            && copy.bufferOffsetBytes == authored.bufferOffsetBytes);
                        assert(copy.extent == authored.extent
                            && copy.bytesPerRow == authored.bytesPerRow
                            && copy.rowsPerImage == authored.rowsPerImage);
                        return nullptr;
                    }));
                assert(count == 3u);
                count = 0u;
                assert(!rhi::TransferCopyProjection::visit<rhi::TextureToTextureCopyDesc>(
                    tex->scalarFields(), tex.get(), [&](const auto& copy) noexcept -> const char* {
                        assert(copy.sourceSubresources.baseMipLevel == 3u + count
                            && copy.destinationSubresources.baseMipLevel == 5u + count);
                        assert(copy.sourceSubresources.mipLevelCount == 1u
                            && copy.destinationSubresources.mipLevelCount == 1u
                            && copy.mipLayouts.empty());
                        assert(copy.extent == texture.mipLayouts[count].extent);
                        ++count;
                        return nullptr;
                    }));
                assert(count == 2u);
                const auto nothing = [](const auto&) noexcept -> const char* {
                    return nullptr;
                };
                is(rhi::TransferCopyProjection::visit<rhi::BufferToTextureCopyDesc>(
                       full->scalarFields(), nullptr, nothing),
                    "RHI-COPY-PROJECTION-MIP-COUNT");
                is(rhi::TransferCopyProjection::visit<rhi::BufferToTextureCopyDesc>(
                       tex->scalarFields(), full.get(), nothing),
                    "RHI-COPY-PROJECTION-SCALAR-BINDING");
                is(rhi::TransferCopyProjection::visit<rhi::TextureToTextureCopyDesc>(
                       full->scalarFields(), full.get(), nothing),
                    "RHI-COPY-PROJECTION-KIND");
                is(rhi::TransferCopyProjection::visit<rhi::BufferToTextureCopyDesc>(
                       order->scalarFields(), order.get(), nothing),
                    "RHI-COPY-PROJECTION-MIP-ORDER");
                is(rhi::TransferCopyProjection::visit<rhi::BufferToTextureCopyDesc>(
                       first->scalarFields(), first.get(), nothing),
                    "RHI-COPY-PROJECTION-FIRST-MIP");
                count = 0u;
                is(rhi::TransferCopyProjection::visit<rhi::BufferToTextureCopyDesc>(
                       full->scalarFields(),
                       full.get(),
                       [&](const auto&) noexcept -> const char* {
                           ++count;
                           return "TEST-COPY-PROJECTION-REFUSE";
                       }),
                    "TEST-COPY-PROJECTION-REFUSE");
                assert(count == 1u);
                return nullptr;
            }));
        std::puts(
            "BINDLESS_RHI_COPY_PROJECTION_CONTRACT_OK exact-buffer-texture-mips3 texture-pair-mips2 source-destination-relative-mip pitch-offset-layer-preserved missing-order-first-kind-binding-refuse visitor-failure-preserved hot-allocation0 no-native-authority");
    }
};
} // namespace arp::test
