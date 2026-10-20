#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/transfer_upload_generation_validation.h"
#include "rhi/frontend/transfer_upload_packing.h"
#include "texture_upload_packed_copy_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class TextureUploadPackingFixture final {
    using Plan = rhi::TextureUploadPacking;
    using Snapshot = rhi::TextureUploadInputSnapshot;
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>, 5>;
    static constexpr std::uint64_t cpuBytes = 65536u;
    static constexpr std::size_t pixelCount = 160u;

    static Quotas quotas()
    {
        Quotas result;
        for (auto& q : result)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        return result;
    }

    static rhi::TextureDesc destination()
    {
        return {"packing destination comparison only",
            {8u, 8u, 1u},
            rhi::TextureFormat::Rgba8Unorm,
            rhi::TextureUsage::CopyDst,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::CopyDst,
            4u,
            4u,
            1u,
            false};
    }

    static rhi::UploadTextureDesc input(const void* pixels)
    {
        rhi::UploadTextureDesc d;
        d.label = "immutable two mip packing";
        d.destinationTexture = rhi::allocateGpuResourceId();
        d.extent = {3u, 2u, 1u};
        d.destinationOffset = {1u, 1u, 0u};
        d.format = rhi::TextureFormat::Rgba8Unorm;
        d.bytesPerRow = 16u;
        d.rowsPerImage = 3u;
        d.mipLevel = 1u;
        d.arrayLayer = 1u;
        d.subresources = {1u, 2u, 1u, 2u, rhi::TextureAspect::Color};
        d.pixelData = pixels;
        d.pixelDataSize = pixelCount;
        d.frameSerial = 53u;
        d.resourceGeneration = 59u;
        d.mipLayouts.push_back({1u, d.extent, 16u, 3u, 8u});
        d.mipLayouts.push_back({2u, {1u, 1u, 1u}, 8u, 2u, 112u});
        return d;
    }

    static auto capture(const rhi::UploadTextureDesc& d, const Quotas& q, const char*& diagnostic)
    {
        return Snapshot::prepareOriginal(d,
            {1u, true, 4u, 128u, 4096u, cpuBytes, cpuBytes, cpuBytes},
            q[0],
            q[1],
            q[2],
            diagnostic);
    }

    static auto prepare(const std::shared_ptr<const Snapshot>& s,
        const rhi::TextureDesc& d,
        const Quotas& q,
        const rhi::TextureUploadPackingBudget& budget,
        const char*& diagnostic)
    {
        return Plan::prepareOriginal(s, d, budget, q[3], q[4], diagnostic);
    }

    static void verifyRows(const std::shared_ptr<const Plan>& original)
    {
        const auto& plan = *original;
        TextureUploadPackedCopyFixture::verify(original);
        // Independent byte oracle: every packed byte is either the original
        // addressed texel/block byte or the explicitly defined zero padding.
        for (const auto& m : plan.mipLayouts()) {
            assert(m.packedOffset % rhi::kTextureUploadRowPitchAlignment == 0u);
            assert(m.packedBytesPerRow % rhi::kTextureUploadRowPitchAlignment == 0u);
            assert(m.packedSize
                == static_cast<std::uint64_t>(m.packedBytesPerRow) * m.rowCount * m.sliceCount);
            for (std::uint32_t slice = 0u; slice < m.sliceCount; ++slice)
                for (std::uint32_t row = 0u; row < m.rowCount; ++row) {
                    const auto source = m.sourceOffset
                        + (static_cast<std::uint64_t>(slice) * m.sourceRowsPerImage + row)
                            * m.sourceBytesPerRow;
                    const auto packed = m.packedOffset
                        + (static_cast<std::uint64_t>(slice) * m.rowCount + row)
                            * m.packedBytesPerRow;
                    for (std::uint32_t byte = 0u; byte < m.packedBytesPerRow; ++byte)
                        assert(plan.packedBytes()[packed + byte]
                            == (byte < m.rowBytes ? plan.inputOwner()->pixelBytes()[source + byte]
                                                  : 0u));
                }
        }
    }

public:
    // Shared exact authoring/packing fixture for subsequent content ownership.
    static rhi::UploadTextureDesc contentInput(const void* bytes)
    {
        auto d = input(bytes);
        d.destinationTexture = {41u};
        return d;
    }

    static rhi::TextureDesc contentDestination() { return destination(); }

    static std::shared_ptr<const Plan> prepareContentPacking(const rhi::UploadTextureDesc& d,
        const rhi::TextureDesc& t,
        const Quotas& q,
        const char*& diagnostic)
    {
        const auto s = capture(d, q, diagnostic);
        if (!s)
            return {};
        return prepare(s, t, q, {1u, true, 4u, 4096u, cpuBytes, cpuBytes}, diagnostic);
    }

    static void verify()
    {
        using namespace rhi;
        assert(blocksForExtent(UINT32_MAX, 4u) == 1073741824u
            && blocksForExtent(UINT32_MAX, 1u) == UINT32_MAX && !blocksForExtent(1u, 0u));
        auto q = quotas();
        std::array<std::uint8_t, pixelCount> pixels{};
        for (std::size_t i = 0u; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::uint8_t>(i + 1u);
        auto d = input(pixels.data());
        auto t = destination();
        const char* diagnostic = nullptr;
        auto s = capture(d, q, diagnostic);
        assert(s);
        TextureUploadPackingBudget budget;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-TEXTURE-UPLOAD-PACKING-VERSION");
        budget.contractVersion = 1u;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-TEXTURE-UPLOAD-PACKING-OPT-IN");
        budget.prototypeOptIn = true;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-TEXTURE-UPLOAD-PACKING-BUDGET");
        budget = {1u, true, 2u, 1536u, cpuBytes, cpuBytes};
        const auto factory = [&] {
            return prepare(s, t, q, budget, diagnostic);
        };
        std::array<std::uint64_t, 2> peaks{};
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            CpuQuotaFactoryFixture admission(q[3u + i]);
            peaks[i] = admission.measure(factory);
            admission.verify(peaks[i], factory);
            assert(!q[3]->liveBytes() && !q[4]->liveBytes());
            {
                core::CpuMemoryQuotaLease exhausted(q[3u + i], cpuBytes);
                assert(exhausted.charged() && !factory());
            }
            assert(!q[3]->liveBytes() && !q[4]->liveBytes());
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && diagnostic && std::string_view(diagnostic) == id
                && (!q[3] || !q[3]->liveBytes()) && (!q[4] || !q[4]->liveBytes()));
        };
        --budget.maxPackedBytes;
        reject("RHI-TEXTURE-UPLOAD-PACKING-CAPACITY");
        ++budget.maxPackedBytes;
        --budget.maxMipLayouts;
        reject("RHI-TEXTURE-UPLOAD-PACKING-CAPACITY");
        ++budget.maxMipLayouts;
        --budget.maxCpuMetadataBytes;
        reject("RHI-TEXTURE-UPLOAD-PACKING-QUOTA");
        ++budget.maxCpuMetadataBytes;
        --budget.maxCpuPackedBytes;
        reject("RHI-TEXTURE-UPLOAD-PACKING-QUOTA");
        ++budget.maxCpuPackedBytes;
        for (const auto index : {3u, 4u}) {
            auto original = q[index];
            q[index].reset();
            reject("RHI-TEXTURE-UPLOAD-PACKING-QUOTA");
            q[index] = original;
        }
        budget.maxMipLayouts = UINT32_MAX;
        reject("RHI-TEXTURE-UPLOAD-PACKING-ALLOCATION");
        budget.maxMipLayouts = 2u;
        auto original = s;
        s = std::shared_ptr<const Snapshot>(q[0], s.get());
        reject("RHI-TEXTURE-UPLOAD-PACKING-ORIGINAL-INPUT");
        s = original;
        original.reset();
        auto plan = factory();
        assert(plan && !diagnostic && plan->hasQuotaBoundSharedOwner(plan));
        auto forged = std::shared_ptr<const Plan>(q[3], plan.get());
        assert(!plan->hasQuotaBoundSharedOwner(forged));
        forged.reset();
        assert(plan->mipLayouts().size() == 2u && plan->packedBytes().size() == 1536u
            && plan->inputOwner().get() == s.get());
        const auto& first = plan->mipLayouts()[0];
        const auto& second = plan->mipLayouts()[1];
        assert(first.sourceSize == 96u && first.rowBytes == 12u && first.rowCount == 2u
            && first.sliceCount == 2u && first.packedSize == 1024u);
        assert(second.sourceSize == 32u && second.rowBytes == 4u && second.rowCount == 1u
            && second.packedOffset == 1024u && second.packedSize == 512u);
        assert(first.range.baseMipLevel == 1u && second.range.baseMipLevel == 2u
            && first.range.baseArrayLayer == 1u && first.origin == d.destinationOffset);
        verifyRows(plan);
        auto canonical = d;
        auto copy = pixels;
        canonical.pixelData = copy.data();
        s.reset();
        d.label.clear();
        d.mipLayouts.clear();
        pixels.fill(0u);
        assert(
            plan->matches(canonical, t) && plan->inputOwner()->pixelBytes().size() == pixelCount);
        {
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(plan->matches(canonical, t));
            verifyRows(plan);
            semantics::TextureUploadLayoutValidation::Mapper mapper(
                *plan->inputOwner(), plan->destinationLayout());
            Plan::Mip mip;
            assert(mapper(0u, mip) && mapper(1u, mip) && mapper.packedBytes() == 1536u);
            ++canonical.resourceGeneration;
            assert(!plan->matches(canonical, t));
            --canonical.resourceGeneration;
            ++canonical.mipLayouts[1].bufferOffsetBytes;
            assert(!plan->matches(canonical, t));
            --canonical.mipLayouts[1].bufferOffsetBytes;
            const auto saved = Plan::Destination::capture(t);
            const std::array<void (*)(TextureDesc&), 11> changes{{[](auto& texture) {
                                                                      ++texture.extent.width;
                                                                  },
                [](auto& texture) {
                    ++texture.extent.height;
                },
                [](auto& texture) {
                    ++texture.extent.depth;
                },
                [](auto& texture) {
                    texture.format = TextureFormat::R8Unorm;
                },
                [](auto& texture) {
                    texture.usage = TextureUsage::Sampled;
                },
                [](auto& texture) {
                    texture.memory = MemoryUsage::CpuToGpu;
                },
                [](auto& texture) {
                    texture.initialState = ResourceState::CopySrc;
                },
                [](auto& texture) {
                    ++texture.mipLevelCount;
                },
                [](auto& texture) {
                    ++texture.arrayLayerCount;
                },
                [](auto& texture) {
                    ++texture.sampleCount;
                },
                [](auto& texture) {
                    texture.cubeCompatible = true;
                }}};
            for (const auto change : changes) {
                change(t);
                assert(!plan->matches(canonical, t));
                saved.write(t);
            }
            assert(std::string_view(plan->executionDiagnostic())
                == "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        plan.reset();
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        verifyInvalid(canonical, t, q);
        verifyShapes(q);
        verifyGenerationGate(canonical, t, q);
        std::printf(
            "BINDLESS_RHI_TEXTURE_UPLOAD_PACKING_OK %llu %llu mips2 arrays3d bc-edge pixels160 packed1536 zero-padding gates-closed allocation0\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }

    static void prepareActualLoss()
    {
        lossQuotas_ = quotas();
        std::array<std::uint8_t, pixelCount> bytes{};
        const char* diagnostic = nullptr;
        auto s = capture(input(bytes.data()), lossQuotas_, diagnostic);
        lossPlan_ = prepare(
            s, destination(), lossQuotas_, {1u, true, 2u, 1536u, cpuBytes, cpuBytes}, diagnostic);
        assert(lossPlan_);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost() && lossPlan_);
        verifyRows(lossPlan_);
        assert(std::string_view(lossPlan_->executionDiagnostic())
            == "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN");
        assert(std::string_view(rhi::semantics::TextureUploadGenerationValidation::admitOriginal(
                   lossPlan_, rhi::Backend::WebGPU, {1u, true}))
            == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
        lossPlan_.reset();
        for (auto& quota : lossQuotas_) {
            assert(!quota->liveBytes());
            quota.reset();
        }
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_PACKING_LOSS_OK cpu-only immutable original-owner exact-retire");
    }

private:
    static void verifyGenerationGate(
        rhi::UploadTextureDesc d, const rhi::TextureDesc& t, const Quotas& q)
    {
        using Validation = rhi::semantics::TextureUploadGenerationValidation;
        const char* diagnostic = nullptr;
        for (const auto generation :
            {std::uint64_t{0u}, std::uint64_t{59u}, std::uint64_t{UINT64_MAX}}) {
            d.resourceGeneration = generation;
            auto s = capture(d, q, diagnostic);
            auto plan = prepare(s, t, q, {1u, true, 2u, 1536u, cpuBytes, cpuBytes}, diagnostic);
            assert(plan);
            auto forged = std::shared_ptr<const Plan>(q[3], plan.get());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(std::string_view(Validation::admitOriginal(plan, rhi::Backend::WebGPU, {}))
                == "RHI-TEXTURE-UPLOAD-GENERATION-VERSION");
            assert(
                std::string_view(Validation::admitOriginal(plan, rhi::Backend::WebGPU, {1u, false}))
                == "RHI-TEXTURE-UPLOAD-GENERATION-OPT-IN");
            assert(std::string_view(Validation::admitOriginal({}, rhi::Backend::WebGPU, {1u, true}))
                == "RHI-TEXTURE-UPLOAD-GENERATION-ORIGINAL-PACKING");
            assert(std::string_view(
                       Validation::admitOriginal(forged, rhi::Backend::WebGPU, {1u, true}))
                == "RHI-TEXTURE-UPLOAD-GENERATION-ORIGINAL-PACKING");
            assert(std::string_view(Validation::admitOriginal(
                       plan, static_cast<rhi::Backend>(UINT32_MAX), {1u, true}))
                == "RHI-TEXTURE-UPLOAD-GENERATION-BACKEND-UNKNOWN");
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU}) {
                const auto lowering = Validation::lowering(backend);
                assert(!lowering.executable
                    && lowering.validatorOnly == (backend == rhi::Backend::Null));
                assert(std::string_view(Validation::admitOriginal(plan, backend, {1u, true}))
                    == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
                const rhi::CanonicalConsumerImageContentContract::Revision structural{
                    1u, {43u}, d.destinationTexture, generation};
                assert(structural.valid() == (generation != 0u));
                assert(!rhi::CanonicalConsumerImageContentContract::lowering(backend).executable);
                assert(plan->matches(d, t));
            }
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TEXTURE_UPLOAD_GENERATION_GATE_OK backends4 zero-nonzero-max content-version structural-revision-unproven allocation0");
    }

    static void verifyInvalid(
        const rhi::UploadTextureDesc& source, const rhi::TextureDesc& destination, const Quotas& q)
    {
        auto d = source;
        auto t = destination;
        const char* diagnostic = nullptr;
        const auto reject = [&](const char* id) {
            auto s = capture(d, q, diagnostic);
            assert(s);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!prepare(s, t, q, {1u, true, 4u, 4096u, cpuBytes, cpuBytes}, diagnostic)
                && std::string_view(diagnostic) == id);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations
                && !q[3]->liveBytes() && !q[4]->liveBytes());
            d = source;
            t = destination;
        };
        d.destinationTexture = {};
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-INPUT");
        d.backendOwnedStaging = false;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-INPUT");
        d.stagingIntent = static_cast<rhi::StagingIntent>(UINT32_MAX);
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-INPUT");
        d.pixelDataSize = 0u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-INPUT");
        d.format = rhi::TextureFormat::Unknown;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-FORMAT");
        d.format = static_cast<rhi::TextureFormat>(UINT32_MAX);
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-FORMAT");
        d.format = rhi::TextureFormat::D32Float;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-ASPECT");
        d.subresources.aspect = static_cast<rhi::TextureAspect>(UINT32_MAX);
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-ASPECT");
        t.format = rhi::TextureFormat::Bgra8Unorm;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-FORMAT-MISMATCH");
        t.extent.width = 0u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-DESTINATION");
        t.sampleCount = 4u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-DESTINATION");
        t.usage = rhi::TextureUsage::Sampled;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-DESTINATION");
        t.extent.depth = 4u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-DIMENSION");
        t.cubeCompatible = true;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-DIMENSION");
        t.mipLevelCount = 5u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-DESTINATION-MIPS");
        d.mipLevel = 0u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-INDEPENDENT-RANGE");
        d.arrayLayer = 0u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-INDEPENDENT-RANGE");
        d.subresources.mipLevelCount = 1u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-RANGE");
        d.subresources.arrayLayerCount = UINT32_MAX;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-RANGE");
        d.mipLayouts.clear();
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-RANGE");
        ++d.bytesPerRow;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-FIRST-MIP");
        std::swap(d.mipLayouts[0], d.mipLayouts[1]);
        d.extent = d.mipLayouts[0].extent;
        d.bytesPerRow = d.mipLayouts[0].bytesPerRow;
        d.rowsPerImage = d.mipLayouts[0].rowsPerImage;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-MIP-ORDER");
        d.mipLayouts[1].mipLevel = 1u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-MIP-ORDER");
        d.mipLayouts[1].extent = {0u, 1u, 1u};
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-REGION");
        d.destinationOffset.width = 2u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-REGION");
        d.destinationOffset.depth = 1u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-REGION");
        d.mipLayouts[1].bytesPerRow = 3u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-PITCH");
        d.mipLayouts[1].rowsPerImage = 0u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-PITCH");
        d.mipLayouts[1].bufferOffsetBytes = 113u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-PITCH");
        d.mipLayouts[1].bufferOffsetBytes = UINT64_MAX - 3u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-RANGE");
        d.mipLayouts[1].bufferOffsetBytes = 100u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-OVERLAP");
        d.pixelDataSize = 143u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-RANGE");
        d.mipLayouts[1].rowsPerImage = UINT32_MAX;
        d.mipLayouts[1].bytesPerRow = UINT32_MAX - 3u;
        d.subresources.arrayLayerCount = 3u;
        reject("RHI-TEXTURE-UPLOAD-LAYOUT-OVERFLOW");
        for (const auto& quota : q)
            assert(!quota->liveBytes());
    }

    static void verifyShapes(const Quotas& q)
    {
        using namespace rhi;
        std::array<std::uint8_t, 512u> pixels{};
        pixels.fill(0xadu);
        const char* diagnostic = nullptr;
        UploadTextureDesc d;
        d.destinationTexture = allocateGpuResourceId();
        d.pixelData = pixels.data();
        d.pixelDataSize = pixels.size();
        d.format = TextureFormat::Rgba8Unorm;
        TextureDesc t{"explicit3d",
            {8u, 8u, 4u},
            d.format,
            TextureUsage::CopyDst,
            MemoryUsage::GpuOnly,
            ResourceState::CopyDst,
            4u,
            1u};
        d.extent = {2u, 2u, 2u};
        d.destinationOffset = {1u, 1u, 0u};
        d.mipLevel = 1u;
        d.subresources = {1u, 1u, 0u, 1u, TextureAspect::Color};
        d.bytesPerRow = 12u;
        d.rowsPerImage = 3u;
        const auto check = [&](std::uint64_t rowBytes, std::uint32_t rows, std::uint32_t slices) {
            auto s = capture(d, q, diagnostic);
            assert(s);
            auto plan = prepare(s, t, q, {1u, true, 1u, 4096u, cpuBytes, cpuBytes}, diagnostic);
            assert(plan && !diagnostic);
            const auto& m = plan->mipLayouts()[0];
            assert(m.rowBytes == rowBytes && m.rowCount == rows && m.sliceCount == slices);
            verifyRows(plan);
        };
        check(8u, 2u, 2u);
        t.extent.depth = 8u;
        d.destinationOffset.depth = 1u;
        check(8u, 2u, 2u);
        d.mipLevel = 0u;
        d.subresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
        d.extent = {6u, 6u, 1u};
        d.destinationOffset = {4u, 0u, 0u};
        d.rowsPerImage = 3u;
        t.extent = {10u, 6u, 1u};
        t.mipLevelCount = 1u;
        for (const auto format : {TextureFormat::Bc1Unorm,
                 TextureFormat::Bc3Unorm,
                 TextureFormat::Bc7Unorm,
                 TextureFormat::Bc7UnormSrgb}) {
            d.format = t.format = format;
            d.bytesPerRow = bytesPerBlock(format) * 3u;
            check(bytesPerBlock(format) * 2u, 2u, 1u);
        }
        d.destinationOffset.width = 0u;
        auto s = capture(d, q, diagnostic);
        assert(!prepare(s, t, q, {1u, true, 1u, 4096u, cpuBytes, cpuBytes}, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-UPLOAD-LAYOUT-BLOCK-REGION");
        s.reset();
        d.destinationOffset.width = 1u;
        s = capture(d, q, diagnostic);
        assert(!prepare(s, t, q, {1u, true, 1u, 4096u, cpuBytes, cpuBytes}, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-UPLOAD-LAYOUT-BLOCK-REGION");
        s.reset();
        d.extent = {1u, 1u, 1u};
        d.destinationOffset = {0u, 0u, 0u};
        d.rowsPerImage = 1u;
        t.extent = {8u, 8u, 1u};
        t.arrayLayerCount = 6u;
        t.cubeCompatible = true;
        for (const auto format : {TextureFormat::Bgra8Unorm,
                 TextureFormat::Rgba8Unorm,
                 TextureFormat::Rgba8UnormSrgb,
                 TextureFormat::R8Unorm,
                 TextureFormat::Rgba16Float,
                 TextureFormat::Rg16Float,
                 TextureFormat::R32Float}) {
            d.format = t.format = format;
            d.bytesPerRow = static_cast<std::uint32_t>(bytesPerPixel(format));
            check(bytesPerPixel(format), 1u, 1u);
        }
        for (const auto& quota : q)
            assert(!quota->liveBytes());
    }

    inline static Quotas lossQuotas_;
    inline static std::shared_ptr<const Plan> lossPlan_;
};
} // namespace arp::test
