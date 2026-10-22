#pragma once
#include "rhi/frontend/bindless_material_resources.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace arp::renderer {
using rhi::BindlessMaterialResourceContract;
using rhi::BindlessResourceHandle;
using rhi::BindlessShaderArtifact;
using rhi::BindlessSnapshot;
using rhi::TextureViewDimension;

// PbrMaterial/1 is a prototype renderer resource-addressing ABI, independent of frame,
// lighting, debug, temporal and geometry data. It does not issue execution or
// publication authority. The immutable table and its snapshot must be retained
// together through the original command lifecycle.
struct alignas(16) BindlessPbrMaterial96 {
    std::array<float, 4u> baseColorAlpha{};
    // metallic, roughness, occlusion strength, emissive strength
    std::array<float, 4u> factors{};
    // alpha cutoff, leaf translucency, dielectric F0, leaf HSV saturation
    std::array<float, 4u> coverageLeaf{};
    // base color, normal, metallic-roughness, emissive
    std::array<std::uint32_t, 4u> textures{};
    // occlusion, independent sampler, flags, ABI version
    std::array<std::uint32_t, 4u> routing{};
    // actual snapshot generation low/high, image table ID, sampler table ID
    std::array<std::uint32_t, 4u> generation{};
};

static_assert(std::is_trivially_copyable_v<BindlessPbrMaterial96>);
static_assert(sizeof(BindlessPbrMaterial96) == 96u && alignof(BindlessPbrMaterial96) == 16u);
static_assert(offsetof(BindlessPbrMaterial96, factors) == 16u);
static_assert(offsetof(BindlessPbrMaterial96, coverageLeaf) == 32u);
static_assert(offsetof(BindlessPbrMaterial96, textures) == 48u);
static_assert(offsetof(BindlessPbrMaterial96, routing) == 64u);
static_assert(offsetof(BindlessPbrMaterial96, generation) == 80u);

class BindlessPbrMaterialContract final {
public:
    enum Flag : std::uint32_t {
        AlphaMask = 1u,
        TwoSidedLighting = 2u,
        ReceivesShadow = 4u,
        AuthoredLeaf = 8u
    };

    static constexpr std::uint32_t version = 1u, validFlags = 15u;

    struct Values {
        std::array<float, 4u> baseColorAlpha{1.f, 1.f, 1.f, 1.f};
        float metallic = 0.f, roughness = 1.f, occlusionStrength = 1.f, emissiveStrength = 0.f;
        float alphaCutoff = 0.5f, leafTranslucency = 0.f, dielectricF0 = 0.04f,
              leafSaturation = 1.f;
        std::uint32_t flags = ReceivesShadow;
    };

    struct Resources {
        // Five independent image handles followed by the independent sampler.
        std::array<BindlessResourceHandle, 5u> images{};
        BindlessResourceHandle sampler;
    };

    static const char* validateValues(const Values& values) noexcept
    {
        const auto unit = [](float value) noexcept {
            return std::isfinite(value) && value >= 0.f && value <= 1.f;
        };
        for (std::size_t i = 0u; i < 3u; ++i)
            if (!std::isfinite(values.baseColorAlpha[i]) || values.baseColorAlpha[i] < 0.f)
                return "RHI-BINDLESS-PBR-MATERIAL-COLOR";
        if (!unit(values.baseColorAlpha[3u]) || !unit(values.metallic) || !unit(values.roughness)
            || !unit(values.occlusionStrength) || !unit(values.alphaCutoff)
            || !std::isfinite(values.emissiveStrength) || values.emissiveStrength < 0.f
            || !std::isfinite(values.leafTranslucency) || values.leafTranslucency < 0.f
            || !unit(values.dielectricF0) || !std::isfinite(values.leafSaturation)
            || values.leafSaturation < 0.f)
            return "RHI-BINDLESS-PBR-MATERIAL-FACTORS";
        if (values.flags & ~validFlags)
            return "RHI-BINDLESS-PBR-MATERIAL-FLAGS";
        // The existing authored-leaf lane is alpha-masked and two-sided.
        // Other combinations need an explicit family; never guess a remapping.
        if ((values.flags & AuthoredLeaf)
            && (values.flags & (AlphaMask | TwoSidedLighting)) != (AlphaMask | TwoSidedLighting))
            return "RHI-BINDLESS-PBR-MATERIAL-LEAF-PROFILE";
        return nullptr;
    }

    // Pure bounded encoding. resolve must inspect the actual candidate snapshot,
    // not table capacity or a caller-authored capacity/owner projection.
    template <class Resolve>
    static const char* encode(const Values& values,
        const Resources& resources,
        std::uint64_t generation,
        Resolve&& resolve,
        BindlessPbrMaterial96& output) noexcept
    {
        output = {};
        if (const auto* id = validateValues(values))
            return id;
        if (!generation || !resources.images[0u].tableId || !resources.sampler.tableId
            || resources.images[0u].tableId == resources.sampler.tableId)
            return "RHI-BINDLESS-PBR-MATERIAL-RESOURCE-TABLES";
        for (const auto& handle : resources.images)
            if (handle.generation != generation || handle.tableId != resources.images[0u].tableId
                || !resolve(handle, false))
                return "RHI-BINDLESS-PBR-MATERIAL-IMAGE-HANDLE";
        if (resources.sampler.generation != generation || !resolve(resources.sampler, true))
            return "RHI-BINDLESS-PBR-MATERIAL-SAMPLER-HANDLE";
        BindlessPbrMaterial96 record;
        record.baseColorAlpha = values.baseColorAlpha;
        record.factors = {
            values.metallic, values.roughness, values.occlusionStrength, values.emissiveStrength};
        record.coverageLeaf = {values.alphaCutoff,
            values.leafTranslucency,
            values.dielectricF0,
            values.leafSaturation};
        for (std::size_t i = 0u; i < 4u; ++i)
            record.textures[i] = resources.images[i].index;
        record.routing = {
            resources.images[4u].index, resources.sampler.index, values.flags, version};
        record.generation = {static_cast<std::uint32_t>(generation),
            static_cast<std::uint32_t>(generation >> 32u),
            resources.images[0u].tableId,
            resources.sampler.tableId};
        output = record;
        return nullptr;
    }

    static const char* encodeCandidate(const BindlessSnapshot& snapshot,
        const BindlessShaderArtifact& artifact,
        const Values& values,
        const Resources& resources,
        BindlessPbrMaterial96& output) noexcept
    {
        output = {};
        if (!BindlessMaterialResourceContract::candidate(snapshot, artifact))
            return "RHI-BINDLESS-PBR-MATERIAL-SNAPSHOT";
        if (!BindlessMaterialResourceContract::sampledDomain(artifact,
                resources.images[0u].tableId,
                resources.sampler.tableId,
                TextureViewDimension::Texture2D))
            return "RHI-BINDLESS-PBR-MATERIAL-RESOURCE-DOMAIN";
        return encode(
            values,
            resources,
            snapshot.generation(),
            [&](const BindlessResourceHandle& handle, bool) noexcept {
                return BindlessMaterialResourceContract::handle(snapshot, handle);
            },
            output);
    }

    // Recheck bytes obtained from an authenticated initialized-buffer actor.
    // Decode into the same encoder so cold admission has exactly the writer's
    // value, handle, generation and independent sampler rules.
    template <class Resolve>
    static const char* validateRecord(
        const BindlessPbrMaterial96& record, std::uint64_t generation, Resolve&& resolve) noexcept
    {
        if (record.routing[3u] != version || record.generation[0u] != std::uint32_t(generation)
            || record.generation[1u] != std::uint32_t(generation >> 32u))
            return "RHI-BINDLESS-PBR-MATERIAL-RECORD-GENERATION";
        Values values;
        values.baseColorAlpha = record.baseColorAlpha;
        values.metallic = record.factors[0u];
        values.roughness = record.factors[1u];
        values.occlusionStrength = record.factors[2u];
        values.emissiveStrength = record.factors[3u];
        values.alphaCutoff = record.coverageLeaf[0u];
        values.leafTranslucency = record.coverageLeaf[1u];
        values.dielectricF0 = record.coverageLeaf[2u];
        values.leafSaturation = record.coverageLeaf[3u];
        values.flags = record.routing[2u];
        Resources resources;
        for (std::size_t i = 0u; i < resources.images.size(); ++i)
            resources.images[i] = {generation,
                record.generation[2u],
                i < 4u ? record.textures[i] : record.routing[0u]};
        resources.sampler = {generation, record.generation[3u], record.routing[1u]};
        BindlessPbrMaterial96 checked;
        return encode(values, resources, generation, std::forward<Resolve>(resolve), checked);
    }

    static const char* encode(const BindlessSnapshot& snapshot,
        const BindlessShaderArtifact& artifact,
        const Values& values,
        const Resources& resources,
        BindlessPbrMaterial96& output) noexcept
    {
        output = {};
        if (!snapshot.sealed() || !snapshot.validate().success)
            return "RHI-BINDLESS-PBR-MATERIAL-SNAPSHOT";
        return encodeCandidate(snapshot, artifact, values, resources, output);
    }
};
} // namespace arp::renderer
