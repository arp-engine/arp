#pragma once
#include "rhi/frontend/bindless_material_resources.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace arp::rhi {
// TerrainMaterial/1. Six authored layers (albedo/normal/roughness/height),
// two independent 2DArray weight pages and one 2D macro texture. Frame lighting,
// weather, foam history and page-table bounds remain explicit inputs.
struct alignas(16) BindlessTerrainMaterial240 {
    std::array<std::array<std::uint32_t, 4u>, 6u> layers{};
    std::array<std::uint32_t, 4u> routing{};    // page A/B, macro, sampler
    std::array<std::uint32_t, 4u> domains{};    // images, arrays, samplers, ABI
    std::array<std::uint32_t, 4u> generation{}; // snapshot low/high, pages low/high
    std::array<float, 4u> texParams{};
    std::array<float, 4u> weightParams{};
    std::array<float, 4u> layerRough012{};
    std::array<float, 4u> layerRough345{};
    std::array<float, 4u> brushParams{};
    std::array<float, 4u> brushColor{};
};

struct alignas(16) BindlessTerrainPage32 {
    std::array<float, 4u> worldBounds{};     // minX, maxX, minZ, maxZ
    std::array<std::uint32_t, 4u> routing{}; // layer, pages low/high, reserved zero
};

static_assert(std::is_trivially_copyable_v<BindlessTerrainMaterial240>);
static_assert(sizeof(BindlessTerrainMaterial240) == 240u);
static_assert(alignof(BindlessTerrainMaterial240) == 16u);
static_assert(offsetof(BindlessTerrainMaterial240, routing) == 96u);
static_assert(offsetof(BindlessTerrainMaterial240, domains) == 112u);
static_assert(offsetof(BindlessTerrainMaterial240, generation) == 128u);
static_assert(offsetof(BindlessTerrainMaterial240, texParams) == 144u);
static_assert(offsetof(BindlessTerrainMaterial240, weightParams) == 160u);
static_assert(offsetof(BindlessTerrainMaterial240, layerRough012) == 176u);
static_assert(offsetof(BindlessTerrainMaterial240, layerRough345) == 192u);
static_assert(offsetof(BindlessTerrainMaterial240, brushParams) == 208u);
static_assert(offsetof(BindlessTerrainMaterial240, brushColor) == 224u);
static_assert(sizeof(BindlessTerrainPage32) == 32u && alignof(BindlessTerrainPage32) == 16u);
static_assert(offsetof(BindlessTerrainPage32, routing) == 16u);

class BindlessTerrainMaterialContract final {
public:
    static constexpr std::uint32_t version = 1u;

    struct Values {
        // No heuristic defaults: copy the actual TerrainSplatParams lanes.
        std::array<float, 4u> texParams{}, weightParams{}, layerRough012{}, layerRough345{},
            brushParams{}, brushColor{};
        std::uint64_t pageGeneration = 0u;
    };

    struct Resources {
        std::array<std::array<BindlessResourceHandle, 4u>, 6u> layers{};
        std::array<BindlessResourceHandle, 2u> weightPages{};
        BindlessResourceHandle macro, sampler;
    };

    static const char* validateValues(const Values& values) noexcept
    {
        for (const auto* lane : {&values.texParams,
                 &values.weightParams,
                 &values.layerRough012,
                 &values.layerRough345,
                 &values.brushParams,
                 &values.brushColor})
            for (const auto value : *lane)
                if (!std::isfinite(value))
                    return "RHI-BINDLESS-TERRAIN-MATERIAL-NONFINITE";
        for (const auto scale : values.texParams)
            if (scale <= 0.f)
                return "RHI-BINDLESS-TERRAIN-MATERIAL-SCALE";
        if (!values.pageGeneration || values.weightParams[0u] <= 0.f
            || values.weightParams[2u] <= 0.f || values.brushParams[1u] < 0.f
            || values.brushParams[3u] < 0.f || values.brushColor[3u] < 0.f)
            return "RHI-BINDLESS-TERRAIN-MATERIAL-PARAMETERS";
        for (std::size_t i = 0u; i < 6u; ++i) {
            const auto roughness = i < 4u ? values.layerRough012[i] : values.layerRough345[i - 4u];
            if (roughness < 0.f || roughness > 1.f)
                return "RHI-BINDLESS-TERRAIN-MATERIAL-ROUGHNESS";
        }
        return nullptr;
    }

    // Domain: 0 = 2D images, 1 = 2DArray pages, 2 = independent sampler.
    // resolve consumes real candidate snapshot handles in the production overload.
    template <class Resolve>
    static const char* encode(const Values& values,
        const Resources& resources,
        std::uint64_t generation,
        Resolve&& resolve,
        BindlessTerrainMaterial240& output) noexcept
    {
        output = {};
        if (const auto* id = validateValues(values))
            return id;
        const std::array ids{
            resources.macro.tableId, resources.weightPages[0u].tableId, resources.sampler.tableId};
        if (!generation || !ids[0u] || !ids[1u] || !ids[2u] || ids[0u] == ids[1u]
            || ids[0u] == ids[2u] || ids[1u] == ids[2u])
            return "RHI-BINDLESS-TERRAIN-MATERIAL-TABLES";
        const auto valid = [&](BindlessResourceHandle value, std::uint32_t domain) noexcept {
            return value.generation == generation && value.tableId == ids[domain]
                && resolve(value, domain);
        };
        BindlessTerrainMaterial240 result;
        for (std::size_t layer = 0u; layer < 6u; ++layer)
            for (std::size_t role = 0u; role < 4u; ++role) {
                if (!valid(resources.layers[layer][role], 0u))
                    return "RHI-BINDLESS-TERRAIN-MATERIAL-LAYER-HANDLE";
                result.layers[layer][role] = resources.layers[layer][role].index;
            }
        if (!valid(resources.macro, 0u) || !valid(resources.weightPages[0u], 1u)
            || !valid(resources.weightPages[1u], 1u) || !valid(resources.sampler, 2u))
            return "RHI-BINDLESS-TERRAIN-MATERIAL-RESOURCE-HANDLE";
        result.routing = {resources.weightPages[0u].index,
            resources.weightPages[1u].index,
            resources.macro.index,
            resources.sampler.index};
        result.domains = {ids[0u], ids[1u], ids[2u], version};
        result.generation = {static_cast<std::uint32_t>(generation),
            static_cast<std::uint32_t>(generation >> 32u),
            static_cast<std::uint32_t>(values.pageGeneration),
            static_cast<std::uint32_t>(values.pageGeneration >> 32u)};
        result.texParams = values.texParams;
        result.weightParams = values.weightParams;
        result.layerRough012 = values.layerRough012;
        result.layerRough345 = values.layerRough345;
        result.brushParams = values.brushParams;
        result.brushColor = values.brushColor;
        output = result;
        return nullptr;
    }

    static const char* encodeCandidate(const BindlessSnapshot& snapshot,
        const BindlessShaderArtifact& artifact,
        const Values& values,
        const Resources& resources,
        BindlessTerrainMaterial240& output) noexcept
    {
        output = {};
        using Domain = BindlessMaterialResourceContract;
        if (!Domain::candidate(snapshot, artifact)
            || !Domain::sampledDomain(artifact,
                resources.macro.tableId,
                resources.sampler.tableId,
                TextureViewDimension::Texture2D)
            || !Domain::sampledDomain(artifact,
                resources.weightPages[0u].tableId,
                resources.sampler.tableId,
                TextureViewDimension::Texture2DArray))
            return "RHI-BINDLESS-TERRAIN-MATERIAL-SNAPSHOT-DOMAIN";
        return encode(
            values,
            resources,
            snapshot.generation(),
            [&](BindlessResourceHandle value, std::uint32_t) noexcept {
                return Domain::handle(snapshot, value);
            },
            output);
    }

    template <class Resolve>
    static const char* validateRecord(const BindlessTerrainMaterial240& record,
        std::uint64_t generation,
        Resolve&& resolve) noexcept
    {
        if (record.domains[3u] != version || record.generation[0u] != std::uint32_t(generation)
            || record.generation[1u] != std::uint32_t(generation >> 32u))
            return "RHI-BINDLESS-TERRAIN-MATERIAL-RECORD-GENERATION";
        Values values;
        values.texParams = record.texParams;
        values.weightParams = record.weightParams;
        values.layerRough012 = record.layerRough012;
        values.layerRough345 = record.layerRough345;
        values.brushParams = record.brushParams;
        values.brushColor = record.brushColor;
        values.pageGeneration =
            std::uint64_t(record.generation[2u]) | (std::uint64_t(record.generation[3u]) << 32u);
        Resources resources;
        for (std::size_t layer = 0u; layer < resources.layers.size(); ++layer)
            for (std::size_t role = 0u; role < resources.layers[layer].size(); ++role)
                resources.layers[layer][role] = {
                    generation, record.domains[0u], record.layers[layer][role]};
        resources.weightPages = {{{generation, record.domains[1u], record.routing[0u]},
            {generation, record.domains[1u], record.routing[1u]}}};
        resources.macro = {generation, record.domains[0u], record.routing[2u]};
        resources.sampler = {generation, record.domains[2u], record.routing[3u]};
        BindlessTerrainMaterial240 checked;
        return encode(values, resources, generation, std::forward<Resolve>(resolve), checked);
    }

    static const char* encode(const BindlessSnapshot& snapshot,
        const BindlessShaderArtifact& artifact,
        const Values& values,
        const Resources& resources,
        BindlessTerrainMaterial240& output) noexcept
    {
        output = {};
        if (!snapshot.sealed() || !snapshot.validate().success)
            return "RHI-BINDLESS-TERRAIN-MATERIAL-SNAPSHOT";
        return encodeCandidate(snapshot, artifact, values, resources, output);
    }

    static const char* encodePage(std::array<float, 4u> bounds,
        std::uint32_t layer,
        std::uint32_t actualLayersA,
        std::uint32_t actualLayersB,
        std::uint64_t generation,
        BindlessTerrainPage32& output) noexcept
    {
        output = {};
        for (const auto value : bounds)
            if (!std::isfinite(value))
                return "RHI-BINDLESS-TERRAIN-PAGE-BOUNDS";
        if (!(bounds[1u] > bounds[0u]) || !(bounds[3u] > bounds[2u])
            || !std::isfinite(bounds[1u] - bounds[0u]) || !std::isfinite(bounds[3u] - bounds[2u]))
            return "RHI-BINDLESS-TERRAIN-PAGE-BOUNDS";
        if (!generation || layer >= actualLayersA || layer >= actualLayersB)
            return "RHI-BINDLESS-TERRAIN-PAGE-LAYER-GENERATION";
        output.worldBounds = bounds;
        output.routing = {layer,
            static_cast<std::uint32_t>(generation),
            static_cast<std::uint32_t>(generation >> 32u),
            0u};
        return nullptr;
    }
};
} // namespace arp::rhi
