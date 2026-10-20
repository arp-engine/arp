#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"
#include "rhi/frontend/transfer_readback_input.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class TextureReadbackLayoutFixture final {
    using Plan = rhi::TextureReadbackLayout;
    using Input = rhi::TextureReadbackInputSnapshot;
    using Mip = Plan::Mip;
    static constexpr std::uint64_t cpuBytes = 65536u;

public:
    static rhi::TextureDesc texture()
    {
        rhi::TextureDesc t;
        t.extent = {8u, 8u, 1u};
        t.format = rhi::TextureFormat::Rgba8Unorm;
        t.usage = rhi::TextureUsage::CopySrc;
        t.memory = rhi::MemoryUsage::GpuOnly;
        t.initialState = rhi::ResourceState::CopySrc;
        t.mipLevelCount = 3u;
        t.arrayLayerCount = 4u;
        t.sampleCount = 1u;
        return t;
    }

    static rhi::ReadbackTextureDesc descriptor()
    {
        rhi::ReadbackTextureDesc d;
        d.label = "original readback layout 原始";
        d.sourceResource = {17u};
        d.destinationReadbackBuffer = {23u};
        d.sizeBytes = 5120u;
        d.sourceSubresources = {1u, 2u, 1u, 2u, rhi::TextureAspect::Color};
        d.extent = {4u, 4u, 1u};
        d.bytesPerRow = 256u;
        d.rowsPerImage = 5u;
        d.destinationOffsetBytes = 256u;
        d.mipLayouts.push_back({1u, d.extent, 256u, 5u, 256u});
        d.mipLayouts.push_back({2u, {2u, 2u, 1u}, 256u, 3u, 3328u});
        return d;
    }

    class Context final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;

        Context()
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        }

        auto capture(const rhi::ReadbackTextureDesc& d, const char*& diagnostic)
        {
            return Input::prepareOriginal(
                d, {1u, true, 4u, 128u, cpuBytes, cpuBytes}, quotas[0], quotas[1], diagnostic);
        }

        auto prepare(const rhi::ReadbackTextureDesc& d,
            const rhi::TextureDesc& t,
            const char*& diagnostic,
            std::uint64_t maxBytes = 5120u)
        {
            auto original = capture(d, diagnostic);
            assert(original);
            return Plan::prepareOriginal(
                std::move(original), t, {1u, true, 4u, maxBytes, cpuBytes}, quotas[2], diagnostic);
        }

        void retired() const
        {
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

public:
    static const auto& mipMutations()
    {
        static const std::array<void (*)(Mip&), 21> changes{{[](auto& m) {
                                                                 ++m.range.baseMipLevel;
                                                             },
            [](auto& m) {
                ++m.range.mipLevelCount;
            },
            [](auto& m) {
                ++m.range.baseArrayLayer;
            },
            [](auto& m) {
                ++m.range.arrayLayerCount;
            },
            [](auto& m) {
                m.range.aspect = rhi::TextureAspect::Depth;
            },
            [](auto& m) {
                ++m.extent.width;
            },
            [](auto& m) {
                ++m.extent.height;
            },
            [](auto& m) {
                ++m.extent.depth;
            },
            [](auto& m) {
                ++m.physicalOrigin.width;
            },
            [](auto& m) {
                ++m.physicalOrigin.height;
            },
            [](auto& m) {
                ++m.physicalOrigin.depth;
            },
            [](auto& m) {
                ++m.physicalExtent.width;
            },
            [](auto& m) {
                ++m.physicalExtent.height;
            },
            [](auto& m) {
                ++m.physicalExtent.depth;
            },
            [](auto& m) {
                ++m.bytesPerRow;
            },
            [](auto& m) {
                ++m.rowsPerImage;
            },
            [](auto& m) {
                ++m.rowCount;
            },
            [](auto& m) {
                ++m.sliceCount;
            },
            [](auto& m) {
                ++m.rowBytes;
            },
            [](auto& m) {
                ++m.bufferOffsetBytes;
            },
            [](auto& m) {
                ++m.footprintBytes;
            }}};
        return changes;
    }

    static void verify()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        Context c;
        auto d = descriptor();
        auto t = texture();
        const char* diagnostic = nullptr;
        TextureReadbackLayoutBudget b;
        assert(std::string_view(Plan::validateBudget(b)) == "RHI-TEXTURE-READBACK-LAYOUT-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Plan::validateBudget(b)) == "RHI-TEXTURE-READBACK-LAYOUT-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Plan::validateBudget(b)) == "RHI-TEXTURE-READBACK-LAYOUT-BUDGET");
        b = {1u, true, 2u, d.sizeBytes, cpuBytes};
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto outcome = Plan::lowering(backend);
            assert(outcome.cpuMetadataImplemented
                && outcome.validatorOnly == (backend == Backend::Null)
                && std::string_view(outcome.diagnostic)
                    == "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN");
        }
        assert(!Plan::lowering(static_cast<Backend>(UINT32_MAX)).cpuMetadataImplemented);
        auto original = c.capture(d, diagnostic);
        assert(original);
        const auto factory = [&] {
            return Plan::prepareOriginal(original, t, b, c.quotas[2], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(c.quotas[2]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(c.quotas[2], cpuBytes);
            assert(full.charged() && !factory());
        }
        assert(!c.quotas[2]->liveBytes());
        const auto reject = [&](const char* id) {
            assert(!factory() && diagnostic && std::string_view(diagnostic) == id
                && !c.quotas[2]->liveBytes());
        };
        --b.maxMipCopies;
        reject("RHI-TEXTURE-READBACK-LAYOUT-CAPACITY");
        ++b.maxMipCopies;
        --b.maxReadbackBytes;
        reject("RHI-TEXTURE-READBACK-LAYOUT-CAPACITY");
        ++b.maxReadbackBytes;
        --b.maxCpuMetadataBytes;
        reject("RHI-TEXTURE-READBACK-LAYOUT-QUOTA");
        ++b.maxCpuMetadataBytes;
        b.maxMipCopies = UINT32_MAX;
        reject("RHI-TEXTURE-READBACK-LAYOUT-ALLOCATION");
        b.maxMipCopies = 2u;
        auto saved = original;
        original = std::shared_ptr<const Input>(c.quotas[0], saved.get());
        reject("RHI-TEXTURE-READBACK-LAYOUT-ORIGINAL-INPUT");
        original = std::move(saved);
        assert(!Plan::prepareOriginal(original, t, b, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-LAYOUT-QUOTA");
        auto plan = factory();
        assert(plan && plan->matches(d, t) && plan->mipCopies().size() == 2u);
        const auto& first = plan->mipCopies()[0];
        const auto& second = plan->mipCopies()[1];
        assert(first.range == TextureSubresourceRange({1u, 1u, 1u, 2u, TextureAspect::Color})
            && first.extent == Extent3D({4u, 4u, 1u})
            && first.physicalOrigin == Extent3D({0u, 0u, 1u})
            && first.physicalExtent == Extent3D({4u, 4u, 2u}) && first.bufferOffsetBytes == 256u
            && first.bytesPerRow == 256u && first.rowsPerImage == 5u && first.rowBytes == 16u
            && first.rowCount == 4u && first.sliceCount == 2u && first.footprintBytes == 2560u);
        assert(second.range.baseMipLevel == 2u && second.physicalExtent == Extent3D({2u, 2u, 2u})
            && second.bufferOffsetBytes == 3328u && second.rowBytes == 8u && second.rowCount == 2u
            && second.rowsPerImage == 3u && second.footprintBytes == 1536u);
        auto canonical = d;
        d.label.clear();
        d.label.shrink_to_fit();
        d.mipLayouts.clear();
        d.mipLayouts.shrink_to_fit();
        original.reset();
        assert(plan->matches(canonical, t));
        const std::array<void (*)(TextureDesc&), 11> mutations{{[](auto& a) {
                                                                    ++a.extent.width;
                                                                },
            [](auto& a) {
                ++a.extent.height;
            },
            [](auto& a) {
                ++a.extent.depth;
            },
            [](auto& a) {
                a.format = TextureFormat::R8Unorm;
            },
            [](auto& a) {
                a.usage = TextureUsage::CopyDst;
            },
            [](auto& a) {
                a.memory = MemoryUsage::GpuToCpu;
            },
            [](auto& a) {
                a.initialState = ResourceState::Undefined;
            },
            [](auto& a) {
                ++a.mipLevelCount;
            },
            [](auto& a) {
                ++a.arrayLayerCount;
            },
            [](auto& a) {
                ++a.sampleCount;
            },
            [](auto& a) {
                a.cubeCompatible = true;
            }}};

        {
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            core::CpuMemoryQuotaScope scope(c.quotas[2]);
            core::CpuMemoryQuotaLease full(c.quotas[2], cpuBytes - c.quotas[2]->liveBytes());
            assert(full.charged());
            for (const auto mutate : mutations) {
                const auto layout = TextureResourceLayout::capture(t);
                mutate(t);
                assert(!plan->matches(canonical, t));
                layout.write(t);
            }
            for (std::size_t i = 0u; i < plan->mipCopies().size(); ++i) {
                assert(!Plan::validateOriginalMip(plan, i, plan->mipCopies()[i]));
                for (const auto mutate : mipMutations()) {
                    auto changed = plan->mipCopies()[i];
                    mutate(changed);
                    assert(std::string_view(Plan::validateOriginalMip(plan, i, changed))
                        == "RHI-TEXTURE-READBACK-LAYOUT-MIP-MISMATCH");
                }
            }
            assert(Plan::validateOriginalMip(plan, 2u, first));
            auto forged = std::shared_ptr<const Plan>(c.quotas[0], plan.get());
            assert(Plan::validateOriginalMip(forged, 0u, first));
            semantics::TextureReadbackLayoutValidation::Mapper mapper(
                *plan->inputOwner(), plan->sourceLayout());
            Mip output = first;
            assert(!mapper(1u, output) && output.matches(first));
            assert(std::string_view(plan->executionDiagnostic())
                    == "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"
                && WebGpuOriginalCommandInputs::family(CommandUsageKind::TextureReadback)
                    == WebGpuOriginalCommandInputs::Family::TextureReadback);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        plan.reset();
        c.retired();
        verifyNegatives();
        verifyGeometry();
        std::printf(
            "BINDLESS_RHI_TEXTURE_READBACK_LAYOUT_OK %llu mips2 arrays3d bc-edge absolute-offset padding bounded caller-drop mapping21 source11 allocation0 native-closed\n",
            static_cast<unsigned long long>(peak));
    }

    static void prepareActualLoss()
    {
        lossContext_ = core::makeUnique<Context>();
        const char* diagnostic = nullptr;
        lossPlan_ = lossContext_->prepare(descriptor(), texture(), diagnostic);
        assert(lossPlan_);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost() && lossPlan_->matches(descriptor(), texture()));
        assert(std::string_view(lossPlan_->executionDiagnostic())
            == "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN");
        lossPlan_.reset();
        lossContext_->retired();
        lossContext_.reset();
        std::puts(
            "BINDLESS_RHI_TEXTURE_READBACK_LAYOUT_LOSS_OK cpu-only original-input exact-retire native-closed");
    }

private:
    static void verifyNegatives()
    {
        using namespace rhi;
        Context c;
        const auto initial = descriptor();
        const auto source = texture();
        auto d = initial;
        auto t = source;
        const char* diagnostic = nullptr;
        const auto reject = [&](const char* id, std::uint64_t maxBytes = 5120u) {
            auto snapshot = c.capture(d, diagnostic);
            assert(snapshot);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Plan::prepareOriginal(
                snapshot, t, {1u, true, 4u, maxBytes, cpuBytes}, c.quotas[2], diagnostic));
            if (std::string_view(diagnostic) != id)
                std::printf("READBACK_LAYOUT_REJECT expected=%s actual=%s\n", id, diagnostic);
            assert(std::string_view(diagnostic) == id
                && core::defaultCpuAllocator().stats().allocationCount == allocations
                && !c.quotas[2]->liveBytes());
            snapshot.reset();
            c.retired();
            d = initial;
            t = source;
        };
        d.sourceResource = {};
        reject("RHI-TEXTURE-READBACK-LAYOUT-INPUT");
        d.destinationReadbackBuffer = d.sourceResource;
        reject("RHI-TEXTURE-READBACK-LAYOUT-INPUT");
        d.sizeBytes = 0u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-INPUT");
        d.backendOwnedStaging = false;
        reject("RHI-TEXTURE-READBACK-LAYOUT-INPUT");
        d.stagingIntent = StagingIntent::Upload;
        reject("RHI-TEXTURE-READBACK-LAYOUT-INPUT");
        d.extent.width = 0u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-LEGACY-EXTENT");
        d.sourceSubresources.aspect = TextureAspect::Depth;
        reject("RHI-TEXTURE-READBACK-LAYOUT-FORMAT-ASPECT");
        t.format = TextureFormat::Unknown;
        reject("RHI-TEXTURE-READBACK-LAYOUT-FORMAT-ASPECT");
        t.format = TextureFormat::D32Float;
        reject("RHI-TEXTURE-READBACK-LAYOUT-FORMAT-ASPECT");
        t.usage = TextureUsage::CopyDst;
        reject("RHI-TEXTURE-READBACK-LAYOUT-SOURCE");
        t.extent.width = 0u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-SOURCE");
        t.sampleCount = 2u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-SOURCE");
        t.mipLevelCount = 99u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-SOURCE-MIPS");
        t.cubeCompatible = true;
        reject("RHI-TEXTURE-READBACK-LAYOUT-DIMENSION");
        d.sourceSubresources.baseArrayLayer = 3u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-RANGE");
        d.sourceSubresources.mipLevelCount = 1u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-RANGE");
        d.mipLayouts[0].bufferOffsetBytes = 512u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-FIRST-MIP");
        d.mipLayouts[0].extent.width = 3u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-FIRST-MIP");
        d.mipLayouts[1].mipLevel = 1u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-MIP-ORDER");
        d.mipLayouts[1].extent.width = 3u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-REGION");
        d.mipLayouts[1].bytesPerRow = 255u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-PITCH-RANGE");
        d.mipLayouts[1].rowsPerImage = 1u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-PITCH-RANGE");
        d.mipLayouts[1].bufferOffsetBytes = 3329u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-PITCH-RANGE");
        d.mipLayouts[1].bufferOffsetBytes = 2560u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-OVERLAP");
        d.mipLayouts.clear();
        d.sourceSubresources.mipLevelCount = 1u;
        d.sizeBytes = 2320u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-FOOTPRINT");
        d.mipLayouts.clear();
        d.sourceSubresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
        d.extent = {1u, 1u, 2u};
        d.bytesPerRow = UINT32_MAX - 255u;
        d.rowsPerImage = UINT32_MAX;
        d.destinationOffsetBytes = 0u;
        d.sizeBytes = UINT64_MAX;
        t.extent = {1u, 1u, 2u};
        t.mipLevelCount = 1u;
        t.arrayLayerCount = 1u;
        reject("RHI-TEXTURE-READBACK-LAYOUT-OVERFLOW", UINT64_MAX);
    }

public:
    static void verifyGeometry(void (*inspect)(const std::shared_ptr<const Plan>&) = nullptr)
    {
        using namespace rhi;
        Context c;
        auto t = texture();
        auto d = descriptor();
        const char* diagnostic = nullptr;
        t.extent = {4u, 4u, 4u};
        t.mipLevelCount = 2u;
        t.arrayLayerCount = 1u;
        d.sourceSubresources = {0u, 2u, 0u, 1u, TextureAspect::Color};
        d.extent = {2u, 2u, 2u};
        d.rowsPerImage = 3u;
        d.destinationOffsetBytes = 128u;
        d.sizeBytes = 2304u;
        d.mipLayouts = {{0u, d.extent, 256u, 3u, 128u}, {1u, {1u, 1u, 1u}, 256u, 2u, 1792u}};
        auto volume = c.prepare(d, t, diagnostic);
        assert(volume && volume->mipCopies()[0].physicalExtent == Extent3D({2u, 2u, 2u})
            && volume->mipCopies()[0].footprintBytes == 1536u
            && volume->mipCopies()[1].footprintBytes == 512u);
        if (inspect)
            inspect(volume);
        volume.reset();
        c.retired();
        for (const auto format : {TextureFormat::Bc1Unorm,
                 TextureFormat::Bc3Unorm,
                 TextureFormat::Bc7Unorm,
                 TextureFormat::Bc7UnormSrgb}) {
            t.extent = {10u, 6u, 1u};
            t.mipLevelCount = 1u;
            t.arrayLayerCount = 3u;
            t.format = format;
            d.sourceSubresources = {0u, 1u, 1u, 2u, TextureAspect::Color};
            d.extent = t.extent;
            d.rowsPerImage = 2u;
            d.destinationOffsetBytes = 0u;
            d.sizeBytes = 1024u;
            d.mipLayouts.clear();
            auto bc = c.prepare(d, t, diagnostic);
            assert(bc && bc->mipCopies()[0].physicalOrigin == Extent3D({0u, 0u, 1u})
                && bc->mipCopies()[0].physicalExtent == Extent3D({12u, 8u, 2u})
                && bc->mipCopies()[0].rowBytes == 3u * bytesPerBlock(format));
            if (inspect)
                inspect(bc);
            bc.reset();
            c.retired();
            d.extent.width = 6u;
            assert(!c.prepare(d, t, diagnostic)
                && std::string_view(diagnostic) == "RHI-TEXTURE-READBACK-LAYOUT-BLOCK-EDGE");
            c.retired();
        }
        t = texture();
        t.arrayLayerCount = 6u;
        t.cubeCompatible = true;
        d = descriptor();
        d.sourceSubresources.baseArrayLayer = 2u;
        d.sourceSubresources.arrayLayerCount = 3u;
        d.mipLayouts[1].bufferOffsetBytes = 4352u;
        d.sizeBytes = 6656u;
        auto cube = c.prepare(d, t, diagnostic, d.sizeBytes);
        assert(cube && cube->mipCopies()[0].sliceCount == 3u
            && cube->mipCopies()[1].physicalOrigin.depth == 2u);
        if (inspect)
            inspect(cube);
        cube.reset();
        c.retired();
    }

private:
    static inline core::UniquePtr<Context> lossContext_;
    static inline std::shared_ptr<const Plan> lossPlan_;
};
} // namespace arp::test
