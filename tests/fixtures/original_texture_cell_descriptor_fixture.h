#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_device.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalTextureCellDescriptorFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Texture cell descriptor: %s\n", id);
        assert(!id);
    }

public:
    using NativeExportCheck = void (*)(const rhi::DescriptorSet&);

    struct DevicePublication {
        NativeExportCheck exportCheck = nullptr;
        std::shared_ptr<rhi::DescriptorArena> arena;
        std::array<core::UniquePtr<rhi::DescriptorSet>, 2u> sets;
        std::array<std::weak_ptr<const rhi::OriginalCommandTextureViewSource>, 2u> retained;

        void validate() const
        {
            for (const auto& set : sets) {
                assert(set && set->validateBindingElement(0u, 0u).success);
                if (exportCheck)
                    exportCheck(*set);
            }
        }

        ~DevicePublication()
        {
            for (auto& set : sets)
                set.reset();
            for (const auto& owner : retained)
                assert(owner.expired());
        }
    };

    static core::UniquePtr<DevicePublication> prepareDevice(rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandTextureLease>& parent,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
        NativeExportCheck exportCheck,
        std::uint32_t baseMipLevel = 1u)
    {
        using namespace rhi;
        auto result = core::makeUnique<DevicePublication>();
        result->exportCheck = exportCheck;
        auto metadata = parent->originalMetadataQuota();
        const char* id = nullptr;
        {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            result->arena = core::shareUnique(
                device.createOriginalDescriptorArena({"cell descriptor metadata", 2u, 2u}));
        }
        assert(result->arena);
        const auto& texture = *parent->originalObject();
        const auto identity =
            texture.observeOriginalContent({baseMipLevel, 1u, 0u, 1u, TextureAspect::Color});
        assert(identity);
        const auto wrongRange =
            texture.observeOriginalContent({0u, 1u, 0u, 1u, TextureAspect::Color});
        assert(wrongRange);
        for (unsigned index = 0u; index != 2u; ++index) {
            const bool storage = index == 0u;
            TextureViewDesc vd;
            vd.format = texture.desc().format;
            vd.baseMipLevel = baseMipLevel;
            vd.usage = storage ? TextureUsage::Storage : TextureUsage::Sampled;
            core::UniquePtr<GpuTextureView> view;
            {
                core::CpuMemoryQuotaScope allocation(resourceQuota);
                view = device.createTextureView(texture, vd);
            }
            assert(view);
            auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                view, {1u, true, 2u, metadata->limitBytes()}, metadata, id);
            require(id);
            auto source = OriginalCommandTextureViewSource::prepareOriginal(lease,
                parent,
                {1u,
                    true,
                    OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                    metadata->limitBytes()},
                metadata,
                id);
            require(id);
            result->retained[index] = source;
            DescriptorLayoutDesc layout;
            layout.name = "single mip original cell";
            DescriptorBindingDesc binding{0u,
                storage ? DescriptorType::StorageTexture : DescriptorType::SampledTexture,
                ShaderStage::Compute,
                1u};
            binding.storageTextureFormat = TextureFormat::Rgba16Float;
            binding.textureViewDimensionConstrained = true;
            layout.bindings.push_back(binding);
            {
                core::CpuMemoryQuotaScope allocation(resourceQuota);
                result->sets[index] =
                    device.allocateOriginalDescriptorSet(result->arena, {layout.name, layout});
            }
            auto& set = *result->sets[index];
            DescriptorBindingWrite write;
            write.type = binding.type;
            write.resource = source->resource();
            write.resourceKind = DescriptorResourceKind::TextureView;
            write.resourceState = storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
            OriginalTextureCellDescriptorRequest request{
                device.backend() == Backend::WebGPU ? 2u : 1u,
                true,
                1u,
                1u,
                OriginalCommandTextureViewSource::completeRetainedOwnerCount};
            const auto revision = set.contentRevision();
            assert(!set.write(write).success);
            assert(
                !set.writeOriginalTextureCell(device, write, source, *wrongRange, request).success);
            auto wrong = request;
            wrong.contractVersion = request.contractVersion == 1u ? 2u : 1u;
            assert(!set.writeOriginalTextureCell(device, write, source, *identity, wrong).success);
            wrong = request;
            --wrong.maxRetainedSourceOwners;
            assert(!set.writeOriginalTextureCell(device, write, source, *identity, wrong).success);
            auto forged =
                std::shared_ptr<const OriginalCommandTextureViewSource>(metadata, source.get());
            assert(
                !set.writeOriginalTextureCell(device, write, forged, *identity, request).success);
            forged.reset();
            assert(set.contentRevision() == revision && !set.hasWrittenBinding(0u));
            auto admission = core::makeShared<core::CpuMemoryQuota>(65536u);
            if (device.backend() != Backend::Vulkan) {
                core::CpuMemoryQuotaScope scope(admission);
                core::CpuMemoryQuotaLease pressure(admission, admission->limitBytes());
                assert(pressure.charged());
                const auto failed =
                    set.writeOriginalTextureCell(device, write, source, *identity, request);
                assert(!failed.success && failed.diagnostics == "RHI-OOM");
                assert(set.contentRevision() == revision && !set.hasWrittenBinding(0u));
            }
            {
                core::CpuMemoryQuotaScope scope(admission);
                const auto published =
                    set.writeOriginalTextureCell(device, write, source, *identity, request);
                if (!published.success)
                    std::fprintf(stderr, "Cell publication: %s\n", published.diagnostics.c_str());
                assert(published.success);
            }
            assert(set.contentRevision() == revision + 1u
                && set.validateBindingElement(0u, 0u).success);
            auto replacement = OriginalCommandTextureViewSource::prepareOriginal(
                lease, parent, {1u, true, 5u, metadata->limitBytes()}, metadata, id);
            require(id);
            assert(!set.writeOriginalTextureCell(device, write, replacement, *identity, request)
                    .success);
            assert(set.contentRevision() == revision + 1u);
            source.reset();
            lease.reset();
            assert(!result->retained[index].expired());
            // Cold publication is content-neutral and never grants initialization.
            require(texture.validateOriginalContent(*identity));
            assert(
                identity->state() == common::OriginalTextureContentControl::State::Uninitialized);
        }
        result->validate();
        return result;
    }

    static void verifyContract()
    {
        using namespace rhi;
        using C = OriginalTextureCellDescriptorContract;
        for (auto version : {1u, 2u, 3u, 4u}) {
            OriginalTextureCellDescriptorRequest request{version,
                true,
                1u,
                1u,
                OriginalCommandTextureViewSource::completeRetainedOwnerCount};
            assert(!C::validateIntent(request));
            for (unsigned field = 0u; field < 5u; ++field) {
                auto bad = request;
                if (field == 0u)
                    bad.contractVersion = 0u;
                if (field == 1u)
                    bad.prototypeOptIn = false;
                if (field == 2u)
                    --bad.maxNativeImageViews;
                if (field == 3u)
                    --bad.maxNativeWrites;
                if (field == 4u)
                    --bad.maxRetainedSourceOwners;
                assert(C::validateIntent(bad));
            }
            for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto d = C::lowering(backend, version);
                assert(
                    !d.shaderExecutionAvailable && d.validatorOnly == (backend == Backend::Null));
                assert(d.publicationAvailable
                    == (version == 1u || version == 3u
                            ? backend == Backend::Vulkan || backend == Backend::Metal
                            : backend == Backend::WebGPU));
                assert(bool(d.diagnostic) != d.publicationAvailable);
            }
        }
        assert(!C::lowering(static_cast<Backend>(99u), 1u).publicationAvailable);
        assert(!C::lowering(Backend::Vulkan, 5u).publicationAvailable);
        for (bool storage : {false, true}) {
            DescriptorBindingDesc binding;
            binding.binding = 1u;
            binding.stages = ShaderStage::Compute;
            binding.type =
                storage ? DescriptorType::StorageTexture : DescriptorType::SampledTexture;
            binding.storageTextureFormat = TextureFormat::Rgba16Float;
            DescriptorBindingWrite write;
            write.binding = 1u;
            write.type = binding.type;
            write.resource = GpuResourceId{1u};
            write.resourceKind = DescriptorResourceKind::TextureView;
            write.resourceState = storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
            assert(!C::validateBinding(binding, write));
            for (unsigned field = 0u; field < 11u; ++field) {
                auto b = binding;
                auto w = write;
                if (field == 0u)
                    b.stages = ShaderStage::Fragment;
                if (field == 1u)
                    b.stages = ShaderStage::Compute | ShaderStage::Vertex;
                if (field == 2u)
                    b.dynamicOffset = true;
                if (field == 3u)
                    b.tableBacked = true;
                if (field == 4u)
                    b.depthTexture = true;
                if (field == 5u)
                    w.resourceState = ResourceState::CopyDst;
                if (field == 6u)
                    ++w.arrayElement;
                if (field == 7u)
                    w.resourceKind = DescriptorResourceKind::Buffer;
                if (field == 8u)
                    w.physicalLowering.enabled = true;
                if (field == 9u) {
                    b.textureViewDimensionConstrained = true;
                    b.textureViewDimension = TextureViewDimension::Texture2DArray;
                }
                if (field == 10u)
                    ++w.binding;
                assert(C::validateBinding(b, w));
            }
            if (storage) {
                binding.storageTextureAccess = StorageTextureAccess::ReadWrite;
                assert(C::validateBinding(binding, write));
                binding.storageTextureAccess = StorageTextureAccess::WriteOnly;
                binding.storageTextureFormat = TextureFormat::R32Float;
                assert(C::validateBinding(binding, write));
            }
        }
        std::puts(
            "BINDLESS_RHI_TEXTURE_CELL_DESCRIPTOR12_CONTRACT_OK identity-only no-completion compute-only four-backend");
    }
};
} // namespace arp::test
