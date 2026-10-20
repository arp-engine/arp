#pragma once
#include "rhi/frontend/bindless_scene_content.h"
#include "rhi/frontend/command_geometry_content.h"
#include "rhi/gpu_extensions.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Shared family bytes and original lease factories. Null checks value/owner
// composition only. Platform callers explicitly request real public immutable
// initialization and pass the retained actors to their original closure.
class BindlessSceneBufferDataFixture {
    using Snapshot = rhi::BindlessSnapshot;
    using Artifact = rhi::BindlessShaderArtifact;
    using Bytes = core::pmr::Vector<std::uint8_t>;

    template <class T> static Bytes bytes(const T& value)
    {
        const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
        return {first, first + sizeof(value)};
    }

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Scene buffers: %s\n", id);
        assert(!id);
    }

    std::shared_ptr<const rhi::OriginalCommandBufferLease> create(rhi::GpuDevice& device,
        const rhi::BufferDesc& desc,
        rhi::DescriptorType type,
        const Bytes& contents,
        const rhi::OriginalCommandResourceLeaseBudget& leaseBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& leaseQuota) const
    {
        using namespace rhi;
        assert(desc.sizeBytes == contents.size());
        const char* id = nullptr;
        if (initializeNative && type != DescriptorType::StorageBuffer) {
            using C = OriginalHostGeometryContentContract;
            auto actor = initializeContent(device,
                desc,
                contents,
                C::Staging::ShaderContentIntent{1u, true, type, contents.size()},
                leaseBudget,
                leaseQuota);
            state->shaderActors.push_back(actor);
            return actor->originalLease();
        }
        // Null bytes are never passed off as a native initialized actor.
        // Writable outputs have no immutable-content authority on any backend.
        assert(type == DescriptorType::StorageBuffer
            || (!initializeNative && device.backend() == Backend::Null));
        auto buffer = device.createBuffer(desc);
        assert(buffer);
        if (type != DescriptorType::StorageBuffer)
            assert(device.writeBuffer(*buffer, contents.data(), contents.size()).success);
        auto lease =
            OriginalCommandBufferLease::prepareOriginal(buffer, leaseBudget, leaseQuota, id);
        require(id);
        assert(lease && !buffer);
        return lease;
    }

public:
    static constexpr bool originalBufferFactories = true;
    static constexpr std::uint64_t resultBytes =
        2u * 2u * rhi::BindlessPbrGeometryContract::numericLanesPerPixel * sizeof(std::uint32_t);
    bool initializeNative = false;
    rhi::OriginalBindlessSceneContentInputs::Family family =
        rhi::OriginalBindlessSceneContentInputs::Family::Mesh;
    const Artifact* artifact = nullptr;

    struct State {
        core::pmr::Vector<std::shared_ptr<const rhi::OriginalInitializedGeometrySource>>
            shaderActors;
        std::uint32_t tableFactoryCalls = 0u, ordinaryFactoryCalls = 0u;
        std::uint64_t observedGeneration = 0u;
    };

    std::shared_ptr<State> state = core::makeShared<State>();

    // One public factory for shader and geometry contents. The definition
    // determines the contract version; allocation size comes from the device.
    static std::shared_ptr<const rhi::OriginalInitializedGeometrySource> initializeContent(
        rhi::GpuDevice& device,
        const rhi::BufferDesc& desc,
        const Bytes& contents,
        const rhi::OriginalHostGeometryContentContract::Definition& definition,
        const rhi::OriginalCommandResourceLeaseBudget& leaseBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& leaseQuota)
    {
        using namespace rhi;
        using C = OriginalHostGeometryContentContract;
        constexpr std::uint64_t cpu = 65536u;
        assert(desc.sizeBytes == contents.size());
        const char* id = nullptr;
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        const auto version =
            std::holds_alternative<C::Staging::ShaderContentIntent>(definition) ? 2u : 1u;
        const auto allocation =
            extension->queryOriginalInitializedBufferAllocation(device, desc, version, id);
        require(id);
        assert(allocation >= desc.sizeBytes);
        C::Quotas quotas{core::makeShared<core::CpuMemoryQuota>(cpu),
            core::makeShared<core::CpuMemoryQuota>(contents.size()),
            core::makeShared<core::CpuMemoryQuota>(cpu),
            leaseQuota};
        const C::Budget budget{version,
            true,
            1u,
            C::retainedOwnerBound,
            allocation,
            cpu,
            contents.size(),
            cpu,
            leaseBudget.maxCpuMetadataBytes,
            definition};
        auto actor = extension->prepareOriginalInitializedBuffer(
            device, {desc, contents.data(), contents.size(), budget, quotas}, id);
        require(id);
        assert(actor && actor->initializedBytes() == contents.size());
        return actor;
    }

    bool terrain() const
    {
        assert(artifact);
        return family == rhi::OriginalBindlessSceneContentInputs::Family::Terrain;
    }

    Bytes tableBytes(const Snapshot& snapshot, std::uint32_t table) const
    {
        using namespace rhi;
        assert(artifact && !snapshot.sealed());
        state->observedGeneration = snapshot.generation();
        if (table == (terrain() ? 7u : 4u))
            return Bytes(resultBytes, 0u);
        if (!terrain()) {
            assert(table == 3u);
            std::array<BindlessPbrMaterial96, 2u> materials;
            for (std::uint32_t m = 0u; m < materials.size(); ++m) {
                BindlessPbrMaterialContract::Values values;
                values.baseColorAlpha =
                    m ? std::array{0.25f, 0.5f, 0.75f, 1.f} : std::array{0.75f, 0.5f, 0.25f, 1.f};
                values.roughness = m ? 0.75f : 0.25f;
                BindlessPbrMaterialContract::Resources resources;
                for (std::uint32_t role = 0u; role < 5u; ++role)
                    resources.images[role] = snapshot.handle(1u, m * 5u + role);
                resources.sampler = snapshot.handle(2u, m);
                if (initializeNative)
                    require(BindlessPbrMaterialContract::encodeCandidate(
                        snapshot, *artifact, values, resources, materials[m]));
                else {
                    // Null owns authentic handles but has Validator execution,
                    // so it cannot issue a Native/Emulated candidate grant.
                    assert(snapshot.plan().execution == BindlessExecution::Validator);
                    assert(BindlessMaterialResourceContract::sampledDomain(
                        *artifact, 1u, 2u, TextureViewDimension::Texture2D));
                    require(BindlessPbrMaterialContract::encode(
                        values,
                        resources,
                        snapshot.generation(),
                        [&](auto handle, bool sampler) noexcept {
                            return handle.tableId == (sampler ? 2u : 1u)
                                && BindlessMaterialResourceContract::handle(snapshot, handle);
                        },
                        materials[m]));
                }
                require(BindlessPbrMaterialContract::validateRecord(
                    materials[m], snapshot.generation(), [&](auto handle, bool) noexcept {
                        return BindlessMaterialResourceContract::handle(snapshot, handle);
                    }));
                assert(BindlessPbrMaterialContract::validateRecord(
                    materials[m], snapshot.generation() + 1u, [](auto, bool) noexcept {
                        return true;
                    }));
            }
            return bytes(materials);
        }
        if (table == 4u) {
            BindlessTerrainMaterialContract::Values values;
            values.texParams = {1.f, 2.f, 3.f, 4.f};
            values.weightParams = {1.f, 0.25f, 1.f, 0.f};
            values.layerRough012 = {0.2f, 0.3f, 0.4f, 0.5f};
            values.layerRough345 = {0.6f, 0.7f, 0.f, 0.f};
            values.pageGeneration = snapshot.generation();
            BindlessTerrainMaterialContract::Resources resources;
            for (std::uint32_t layer = 0u; layer < 6u; ++layer)
                for (std::uint32_t role = 0u; role < 4u; ++role)
                    resources.layers[layer][role] = snapshot.handle(1u, layer * 4u + role);
            resources.macro = snapshot.handle(1u, 24u);
            resources.weightPages = {snapshot.handle(2u, 0u), snapshot.handle(2u, 1u)};
            resources.sampler = snapshot.handle(3u, 0u);
            BindlessTerrainMaterial240 material;
            if (initializeNative)
                require(BindlessTerrainMaterialContract::encodeCandidate(
                    snapshot, *artifact, values, resources, material));
            else {
                assert(snapshot.plan().execution == BindlessExecution::Validator);
                assert(BindlessMaterialResourceContract::sampledDomain(
                    *artifact, 1u, 3u, TextureViewDimension::Texture2D));
                assert(BindlessMaterialResourceContract::sampledDomain(
                    *artifact, 2u, 3u, TextureViewDimension::Texture2DArray));
                require(BindlessTerrainMaterialContract::encode(
                    values,
                    resources,
                    snapshot.generation(),
                    [&](auto handle, std::uint32_t domain) noexcept {
                        return domain < 3u && handle.tableId == domain + 1u
                            && BindlessMaterialResourceContract::handle(snapshot, handle);
                    },
                    material));
            }
            return bytes(material);
        }
        if (table == 5u) {
            BindlessTerrainPage32 page;
            require(BindlessTerrainMaterialContract::encodePage(
                {-2.f, 2.f, -2.f, 2.f}, 1u, 2u, 2u, snapshot.generation(), page));
            return bytes(page);
        }
        assert(table == 6u);
        return bytes(std::array<std::uint32_t, 4u>{0u, 0u, 0u, 0u});
    }

    static Bytes ordinaryBytes(std::uint32_t set, std::uint32_t binding)
    {
        using namespace rhi;
        assert(set == 0u && binding < 4u);
        const std::array<float, 16u> identity{
            1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
        if (binding == 0u) {
            BindlessPbrGeometryFrame400 frame;
            frame.renderViewProjection = frame.currentViewProjection =
                frame.previousViewProjection = frame.shadowViewProjection = identity;
            frame.outputExtent = {2u, 2u, BindlessPbrGeometryContract::numericLanesPerPixel, 1u};
            require(BindlessPbrGeometryContract::validateFrame(frame, 2u, 2u, resultBytes));
            return bytes(frame);
        }
        if (binding == 1u)
            return bytes(std::array<float, 16u>{
                0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
        if (binding == 2u) {
            BindlessPbrTemporalPalette32864 palettes;
            for (auto* palette : {&palettes.current, &palettes.previous}) {
                palette->matrices[0u] = identity;
                palette->jointCount = 1u;
                palette->worldTranslationAndScale = {0.f, 0.f, 0.f, 1.f};
                palette->worldRotation = {0.f, 0.f, 0.f, 1.f};
            }
            require(BindlessPbrGeometryContract::validatePalette(palettes));
            return bytes(palettes);
        }
        BindlessTerrainGeometryParameters64 parameters;
        parameters.pages = {0u, 1u, 1u, 0u};
        require(BindlessTerrainGeometryContract::validateParameters(parameters, 1u, 1u, 1u, 4u));
        return bytes(parameters);
    }

    void configureBuffer(rhi::BufferDesc& desc, const rhi::BindlessTableMapping& table) const
    {
        const bool writable = table.tableId == (terrain() ? 7u : 4u);
        desc.sizeBytes = writable ? resultBytes
            : !terrain()          ? 2u * sizeof(rhi::BindlessPbrMaterial96)
            : table.tableId == 4u ? sizeof(rhi::BindlessTerrainMaterial240)
            : table.tableId == 5u ? sizeof(rhi::BindlessTerrainPage32)
                                  : 4u * sizeof(std::uint32_t);
        desc.memory = writable ? rhi::MemoryUsage::GpuOnly : rhi::MemoryUsage::CpuToGpu;
        if (writable)
            desc.usage = desc.usage | rhi::ResourceUsage::CopySrc;
    }

    void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding) const
    {
        desc.sizeBytes = ordinaryBytes(set, binding.binding).size();
        desc.memory = rhi::MemoryUsage::CpuToGpu;
    }

    auto createOriginalTableBuffer(rhi::GpuDevice& device,
        const rhi::BufferDesc& desc,
        const rhi::BindlessTableMapping& table,
        std::uint32_t slot,
        const Snapshot& snapshot,
        const rhi::OriginalCommandResourceLeaseBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const
    {
        assert(slot == 0u);
        ++state->tableFactoryCalls;
        const auto* binding =
            rhi::findDescriptorSetBinding(artifact->physicalLayout, table.firstBinding);
        assert(binding);
        return create(
            device, desc, binding->type, tableBytes(snapshot, table.tableId), budget, quota);
    }

    auto createOriginalOrdinaryBuffer(rhi::GpuDevice& device,
        const rhi::BufferDesc& desc,
        std::uint32_t set,
        const rhi::DescriptorBindingDesc& binding,
        const Snapshot&,
        const rhi::OriginalCommandResourceLeaseBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const
    {
        ++state->ordinaryFactoryCalls;
        return create(
            device, desc, binding.type, ordinaryBytes(set, binding.binding), budget, quota);
    }
};
} // namespace arp::test
