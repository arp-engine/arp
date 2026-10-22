#pragma once
#include "bindless_geometry_numeric_contract.h"
#include "renderer/bindless/bindless_pbr_material.h"
#include "renderer/bindless/bindless_terrain_geometry.h"
#include "renderer/bindless/bindless_terrain_material.h"
#include "rhi/frontend/bindless_initialized_graphics.h"
#include "rhi/frontend/command_geometry_content.h"
#include "rhi/frontend/command_recording.h"

#include <cstring>

namespace arp::test {
using namespace renderer;
using namespace rhi;

// SceneContent/1 is a cold check over original native initialized actors. It
// retains no borrowed data and issues no command, seal or completion authority.
struct BindlessSceneConformanceInputs {
    enum class Family : std::uint32_t { Mesh, Skin, Grass, Canopy, Terrain };
    std::uint32_t contractVersion = 0u;
    bool optIn = false;
    Family family = Family::Mesh;
    std::uint64_t maxDescriptorVisits = 0u, maxRecordVisits = 0u, maxCommandVisits = 0u;

    bool empty() const noexcept
    {
        return !contractVersion && !optIn && family == Family::Mesh && !maxDescriptorVisits
            && !maxRecordVisits && !maxCommandVisits;
    }

    const char* diagnostic() const noexcept
    {
        if (empty())
            return nullptr;
        if (contractVersion != 1u)
            return "RHI-BINDLESS-SCENE-CONTENT-VERSION";
        if (!optIn)
            return "RHI-BINDLESS-SCENE-CONTENT-OPT-IN";
        if (family > Family::Terrain)
            return "RHI-BINDLESS-SCENE-CONTENT-FAMILY";
        return maxDescriptorVisits && maxRecordVisits && maxCommandVisits
            ? nullptr
            : "RHI-BINDLESS-SCENE-CONTENT-BUDGET";
    }
};

class BindlessSceneConformanceContract final {
public:
    using Inputs = BindlessSceneConformanceInputs;
    using Family = Inputs::Family;

    // SceneContent/1 augments the original direct-graphics execution profile.
    // Immutable shader bytes and geometry must be retained by the same closure.
    // WebGPU's explicit attachment extension supplies HDR and depth custody;
    // the historical empty scene keeps its original RGBA8-only scope.
    struct EntrySelection {
        bool recognized = false;
        Family family = Family::Mesh;
        std::uint32_t pass = 0u;
        const char* diagnostic = nullptr;
    };

    // The family belongs to the immutable pipeline's actual entry, not a label
    // supplied alongside its resources. Reuse the same pairing at both public
    // admission and the backend's original-byte inspection.
    static EntrySelection selectEntries(core::StringView vertex, core::StringView fragment) noexcept
    {
        constexpr std::array<core::StringView, 15u> vertices{"meshColorVertex",
            "meshDepthVertex",
            "meshShadowVertex",
            "skinColorVertex",
            "skinDepthVertex",
            "skinShadowVertex",
            "grassColorVertex",
            "grassDepthVertex",
            "grassShadowVertex",
            "canopyColorVertex",
            "canopyDepthVertex",
            "canopyShadowVertex",
            "terrainColorVertex",
            "terrainDepthVertex",
            "terrainShadowVertex"};
        constexpr std::array<core::StringView, 6u> fragments{"familyColorMain",
            "familyDepthMain",
            "familyShadowMain",
            "terrainColorMain",
            "terrainDepthMain",
            "terrainShadowMain"};
        for (std::size_t i = 0u; i < vertices.size(); ++i) {
            if (vertices[i] != vertex)
                continue;
            EntrySelection result{
                true, static_cast<Family>(i / 3u), static_cast<std::uint32_t>(i % 3u), nullptr};
            if (fragment != fragments[(i >= 12u ? 3u : 0u) + result.pass])
                result.diagnostic = "RHI-BINDLESS-SCENE-CONTENT-ENTRY-FAMILY";
            return result;
        }
        for (const auto entry : fragments)
            if (fragment == entry)
                return {false, Family::Mesh, 0u, "RHI-BINDLESS-SCENE-CONTENT-ENTRY-FAMILY"};
        return {};
    }

    static const char* validateRootInput(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
        const Inputs& input) noexcept
    {
        if (const auto* id = input.diagnostic())
            return id;
        if (const auto* id = OriginalCommandBindlessRootSource::validateOriginal(root))
            return id;
        const auto* prepared = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            &root->originalPrepared());
        if (!prepared)
            return input.empty() ? nullptr : "RHI-BINDLESS-SCENE-CONTENT-PIPELINE";
        if (!*prepared || !(*prepared)->owners() || !(*prepared)->owners()->desc().pipeline)
            return "RHI-BINDLESS-SCENE-CONTENT-PIPELINE";
        const auto& pipeline = *(*prepared)->owners()->desc().pipeline;
        const auto& package = root->authorization()->package();
        const auto* vertexPointer = pipeline.bindlessShaderModule(ShaderStage::Vertex, 1u);
        const auto* fragmentPointer = pipeline.bindlessShaderModule(ShaderStage::Fragment, 1u);
        const BindlessPackageModule* vertex = nullptr;
        const BindlessPackageModule* fragment = nullptr;
        for (const auto& module : package.modules()) {
            if (&module == vertexPointer)
                vertex = &module;
            if (&module == fragmentPointer)
                fragment = &module;
        }
        if (!vertex || !fragment)
            return "RHI-BINDLESS-SCENE-CONTENT-ENTRY-OWNER";
        const auto selection = selectEntries(vertex->entry, fragment->entry);
        if (selection.diagnostic)
            return selection.diagnostic;
        if (!selection.recognized)
            return input.empty() ? nullptr : "RHI-BINDLESS-SCENE-CONTENT-ENTRY-FAMILY";
        if (input.empty())
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-INPUTS-REQUIRED";
        return input.family == selection.family ? nullptr
                                                : "RHI-BINDLESS-SCENE-CONTENT-ENTRY-FAMILY";
    }

    template <class Content> struct InitializedView {
        std::shared_ptr<const OriginalCommandBindlessRootSource> root;
        std::shared_ptr<const OriginalCommandResourceSources> inventory;
        const std::shared_ptr<const OriginalInitializedGeometrySource>* actors = nullptr;
        std::size_t count = 0u;

        const auto& originalRoot() const noexcept { return root; }

        const auto& originalInventory() const noexcept { return inventory; }

        std::shared_ptr<const Content> initializedShader(std::size_t source) const noexcept
        {
            if (source >= inventory->sources().size())
                return {};
            const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &inventory->sources()[source]);
            if (!lease)
                return {};
            for (std::size_t i = 0u; i < count; ++i)
                if (actors[i]
                    && core::SharedOwnerProjection::sameOwner(actors[i]->originalLease(), *lease))
                    return std::dynamic_pointer_cast<const Content>(actors[i]);
            return {};
        }
    };

    template <class Content>
    static const char* inspectInitialized(
        const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalInitializedGeometrySource>* actors,
        std::size_t count,
        const Inputs& input) noexcept
    {
        if (!inventory || (count && !actors))
            return "SCENE-CONFORMANCE-INPUTS";
        InitializedView<Content> view{root, inventory, actors, count};
        // Borrowed validation view; it creates no RHI lease, seal or authority.
        const std::shared_ptr<const InitializedView<Content>> borrowed(
            std::shared_ptr<const void>{}, &view);
        return inspectNative(borrowed, stream, input);
    }

    template <class Closure>
    static const char* inspectNative(const std::shared_ptr<const Closure>& closure,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const Inputs& input) noexcept
    {
        if (const auto* id = input.diagnostic())
            return id;
        if (!closure)
            return "SCENE-CONFORMANCE-VIEW-MISSING";
        if (const auto* id = validateRootInput(closure->originalRoot(), input))
            return id;
        if (input.empty())
            return nullptr;
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(stream))
            return id;
        const auto& root = *closure->originalRoot();
        const auto& snapshot = *root.originalSnapshot();
        const auto& inventory = closure->originalInventory();
        if (!core::SharedOwnerProjection::sameOwner(inventory,
                stream->originalInputs()->originalScalars()->originalUsage()->originalSources())
            || !std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                root.originalPrepared())
            || !snapshot.sealed())
            return "RHI-BINDLESS-SCENE-CONTENT-ROOT";
        using Owner = std::decay_t<decltype(closure->initializedShader(std::size_t{}))>;
        using Content = typename Owner::element_type;

        struct Bytes {
            const std::uint8_t* data = nullptr;
            std::size_t size = 0u;
        };

        std::uint64_t descriptors = 0u, records = 0u;
        const auto charge = [](std::uint64_t& used, std::uint64_t limit) noexcept {
            if (used == limit)
                return false;
            ++used;
            return true;
        };
        const auto readDeclaration = [&](const auto& declaration,
                                         Bytes& output) noexcept -> const char* {
            if (declaration.dynamicOffset || declaration.sourceIndex >= inventory->sources().size())
                return "RHI-BINDLESS-SCENE-CONTENT-BUFFER-SOURCE";
            const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &inventory->sources()[declaration.sourceIndex]);
            if (!lease)
                return "RHI-BINDLESS-SCENE-CONTENT-BUFFER-SOURCE";
            const auto& actor = closure->initializedShader(declaration.sourceIndex);
            return Content::visitInitializedShader(actor,
                *lease,
                declaration.write.type,
                [&](const void* data, std::size_t size) noexcept -> const char* {
                    DescriptorBufferBindingRangeContract::Resolved range;
                    if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                            declaration.write.bufferBindingRange, size, 0u, range))
                        return id;
                    if (!core::containsUnsignedRange(declaration.bufferAccessOffset,
                            declaration.bufferAccessBytes,
                            range.offsetBytes,
                            range.sizeBytes))
                        return "RHI-BINDLESS-SCENE-CONTENT-BUFFER-RANGE";
                    output = {static_cast<const std::uint8_t*>(data) + range.offsetBytes,
                        static_cast<std::size_t>(range.sizeBytes)};
                    return nullptr;
                });
        };
        const auto ordinary =
            [&](std::uint32_t binding, DescriptorType type, Bytes& output) noexcept -> const char* {
            bool found = false;
            for (const auto& domain : root.ordinaryDomains()) {
                if (domain.set != 0u)
                    continue;
                for (const auto& element : domain.source->frozenElements()) {
                    if (!charge(descriptors, input.maxDescriptorVisits))
                        return "RHI-BINDLESS-SCENE-CONTENT-DESCRIPTOR-CAPACITY";
                    const auto& declaration = element.declaration;
                    if (declaration.write.binding != binding || declaration.write.arrayElement)
                        continue;
                    if (found || declaration.write.type != type)
                        return "RHI-BINDLESS-SCENE-CONTENT-ORDINARY-BINDING";
                    found = true;
                    if (const auto* id = readDeclaration(declaration, output))
                        return id;
                }
            }
            return found ? nullptr : "RHI-BINDLESS-SCENE-CONTENT-ORDINARY-MISSING";
        };
        const auto& artifact = root.authorization()->package().artifact();
        const auto selectTable =
            [&](std::uint32_t tableId, std::uint32_t slot, auto&& visitor) noexcept -> const char* {
            const BindlessTableMapping* selected = nullptr;
            for (const auto& mapping : artifact.tables) {
                if (!charge(descriptors, input.maxDescriptorVisits))
                    return "RHI-BINDLESS-SCENE-CONTENT-DESCRIPTOR-CAPACITY";
                if (mapping.tableId == tableId) {
                    if (selected)
                        return "RHI-BINDLESS-SCENE-CONTENT-TABLE-DUPLICATE";
                    selected = &mapping;
                }
            }
            if (!selected || slot >= snapshot.activeCount(tableId))
                return "RHI-BINDLESS-SCENE-CONTENT-TABLE-MISSING";
            bool found = false;
            for (const auto& element : root.elements()) {
                if (!charge(descriptors, input.maxDescriptorVisits))
                    return "RHI-BINDLESS-SCENE-CONTENT-DESCRIPTOR-CAPACITY";
                const auto& declaration = element.declaration;
                const auto binding = artifact.execution == BindlessExecution::Native
                    ? selected->firstBinding
                    : selected->firstBinding + slot;
                const auto arrayElement =
                    artifact.execution == BindlessExecution::Native ? slot : 0u;
                if (declaration.write.binding != binding
                    || declaration.write.arrayElement != arrayElement)
                    continue;
                if (found)
                    return "RHI-BINDLESS-SCENE-CONTENT-TABLE-BINDING";
                found = true;
                if (const auto* id = visitor(element))
                    return id;
            }
            return found ? nullptr : "RHI-BINDLESS-SCENE-CONTENT-TABLE-MISSING";
        };
        const auto table = [&](std::uint32_t tableId,
                               std::uint32_t slot,
                               DescriptorType type,
                               Bytes& output) noexcept -> const char* {
            return selectTable(tableId, slot, [&](const auto& element) noexcept -> const char* {
                if (element.declaration.write.type != type)
                    return "RHI-BINDLESS-SCENE-CONTENT-TABLE-TYPE";
                return readDeclaration(element.declaration, output);
            });
        };
        const auto arrayLayers = [&](std::uint32_t tableId,
                                     std::uint32_t slot,
                                     std::uint32_t& output) noexcept -> const char* {
            return selectTable(tableId, slot, [&](const auto& element) noexcept -> const char* {
                const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.owner);
                if (!source || !*source
                    || element.declaration.write.type != DescriptorType::SampledTexture
                    || (*source)->originalObject()->desc().dimension
                        != TextureViewDimension::Texture2DArray)
                    return "RHI-BINDLESS-SCENE-CONTENT-PAGE-TEXTURE";
                output = (*source)->originalObject()->desc().arrayLayerCount;
                return output ? nullptr : "RHI-BINDLESS-SCENE-CONTENT-PAGE-LAYERS";
            });
        };
        Bytes frameBytes;
        if (const auto* id = ordinary(0u, DescriptorType::UniformBuffer, frameBytes))
            return id;
        BindlessPbrGeometryFrame400 frame;
        if (frameBytes.size != sizeof(frame))
            return "RHI-BINDLESS-SCENE-CONTENT-FRAME-SIZE";
        std::memcpy(&frame, frameBytes.data, sizeof(frame));
        const bool terrain = input.family == Family::Terrain;
        const auto materialId = terrain ? 4u : 3u;
        const auto resultsId = terrain ? 7u : 4u;
        const auto resultElements = snapshot.bufferElementCount(snapshot.handle(resultsId, 0u));
        if (const auto* id = BindlessGeometryNumericContract::validateFrame(frame,
                frame.outputExtent[0u],
                frame.outputExtent[1u],
                std::uint64_t(resultElements) * 4u))
            return id;
        const bool instanced = input.family == Family::Grass || input.family == Family::Canopy;
        std::uint32_t instances = 0u;
        if (instanced) {
            Bytes bytes;
            if (const auto* id = ordinary(1u, DescriptorType::ReadOnlyStorageBuffer, bytes))
                return id;
            if (!bytes.size || bytes.size % 64u || bytes.size / 64u > UINT32_MAX)
                return "RHI-BINDLESS-SCENE-CONTENT-INSTANCE-SIZE";
            instances = static_cast<std::uint32_t>(bytes.size / 64u);
            for (std::uint32_t instance = 0u; instance < instances; ++instance) {
                if (!charge(records, input.maxRecordVisits))
                    return "RHI-BINDLESS-SCENE-CONTENT-RECORD-CAPACITY";
                std::array<float, 12u> transform;
                std::memcpy(
                    transform.data(), bytes.data + std::size_t(instance) * 64u, sizeof(transform));
                for (const auto lane : transform)
                    if (!std::isfinite(lane))
                        return "RHI-BINDLESS-SCENE-CONTENT-INSTANCE-TRANSFORM";
                // The last uint4 is packed instance metadata, not float data.
            }
        }
        if (input.family == Family::Skin) {
            Bytes bytes;
            if (const auto* id = ordinary(2u, DescriptorType::UniformBuffer, bytes))
                return id;
            BindlessPbrTemporalPalette32864 palettes;
            if (bytes.size != sizeof(palettes))
                return "RHI-BINDLESS-SCENE-CONTENT-PALETTE-SIZE";
            std::memcpy(&palettes, bytes.data, sizeof(palettes));
            if (const auto* id = BindlessPbrGeometryContract::validatePalette(palettes))
                return id;
        }
        // Family/1 addresses one runtime material buffer, with its element
        // selected by Push16. This is an explicit schema, never table capacity.
        if (snapshot.activeCount(materialId) != 1u)
            return "RHI-BINDLESS-SCENE-CONTENT-MATERIAL-BUFFERS";
        Bytes materials;
        if (const auto* id =
                table(materialId, 0u, DescriptorType::ReadOnlyStorageBuffer, materials))
            return id;
        const auto stride =
            terrain ? sizeof(BindlessTerrainMaterial240) : sizeof(BindlessPbrMaterial96);
        if (!materials.size || materials.size % stride || materials.size / stride > UINT32_MAX)
            return "RHI-BINDLESS-SCENE-CONTENT-MATERIAL-SIZE";
        const auto materialCount = static_cast<std::uint32_t>(materials.size / stride);
        // Resource checks inspect handle membership and table dimensions. Charge
        // their bounded table scans as well as descriptor lookup scans.
        const auto resourceBudget = [&](std::uint64_t scans) noexcept {
            const auto count = std::uint64_t(artifact.tables.size());
            if (count && scans > (input.maxDescriptorVisits - descriptors) / count)
                return false;
            descriptors += scans * count;
            return true;
        };
        for (std::uint32_t i = 0u; i < materialCount; ++i) {
            if (!charge(records, input.maxRecordVisits))
                return "RHI-BINDLESS-SCENE-CONTENT-RECORD-CAPACITY";
            if (terrain) {
                if (!resourceBudget(2u * 27u + 1u))
                    return "RHI-BINDLESS-SCENE-CONTENT-DESCRIPTOR-CAPACITY";
                BindlessTerrainMaterial240 record;
                std::memcpy(&record, materials.data + i * stride, stride);
                if (const auto *id = BindlessTerrainMaterialContract::validateRecord(record,
                        snapshot.generation(),
                        [&](BindlessResourceHandle handle, std::uint32_t domain) noexcept {
                            return BindlessMaterialResourceContract::handle(snapshot, handle)
                                && (domain == 2u
                                    || BindlessMaterialResourceContract::sampledDomain(artifact,
                                        handle.tableId,
                                        record.domains[2u],
                                        domain == 0u ? TextureViewDimension::Texture2D
                                                     : TextureViewDimension::Texture2DArray));
                        }))
                    return id;
            } else {
                if (!resourceBudget(2u * 5u + 1u))
                    return "RHI-BINDLESS-SCENE-CONTENT-DESCRIPTOR-CAPACITY";
                BindlessPbrMaterial96 record;
                std::memcpy(&record, materials.data + i * stride, stride);
                if (const auto *id = BindlessPbrMaterialContract::validateRecord(record,
                        snapshot.generation(),
                        [&](BindlessResourceHandle handle, bool sampler) noexcept {
                            return BindlessMaterialResourceContract::handle(snapshot, handle)
                                && (sampler
                                    || BindlessMaterialResourceContract::sampledDomain(artifact,
                                        handle.tableId,
                                        record.generation[3u],
                                        TextureViewDimension::Texture2D));
                        }))
                    return id;
            }
        }
        if (terrain) {
            Bytes bytes, pages, foam;
            if (const auto* id = ordinary(3u, DescriptorType::UniformBuffer, bytes))
                return id;
            BindlessTerrainGeometryParameters64 parameters;
            if (bytes.size != sizeof(parameters))
                return "RHI-BINDLESS-SCENE-CONTENT-TERRAIN-PARAMETERS-SIZE";
            std::memcpy(&parameters, bytes.data, sizeof(parameters));
            if (const auto* id =
                    table(5u, parameters.pages[0u], DescriptorType::ReadOnlyStorageBuffer, pages))
                return id;
            if (const auto* id =
                    table(6u, parameters.pages[3u], DescriptorType::ReadOnlyStorageBuffer, foam))
                return id;
            if (pages.size % sizeof(BindlessTerrainPage32)
                || pages.size / sizeof(BindlessTerrainPage32) > UINT32_MAX
                || foam.size % sizeof(std::uint32_t))
                return "RHI-BINDLESS-SCENE-CONTENT-TERRAIN-PAGE-SIZE";
            if (const auto* id = BindlessTerrainGeometryContract::validateParameters(parameters,
                    snapshot.activeCount(5u),
                    static_cast<std::uint32_t>(pages.size / sizeof(BindlessTerrainPage32)),
                    snapshot.activeCount(6u),
                    foam.size / sizeof(std::uint32_t)))
                return id;
            for (std::uint32_t material = 0u; material < materialCount; ++material) {
                BindlessTerrainMaterial240 record;
                std::memcpy(&record, materials.data + material * stride, stride);
                std::uint32_t layersA = 0u, layersB = 0u;
                if (const auto* id = arrayLayers(record.domains[1u], record.routing[0u], layersA))
                    return id;
                if (const auto* id = arrayLayers(record.domains[1u], record.routing[1u], layersB))
                    return id;
                const auto generation = std::uint64_t(record.generation[2u])
                    | (std::uint64_t(record.generation[3u]) << 32u);
                for (std::uint32_t page = 0u; page < parameters.pages[1u]; ++page) {
                    if (!charge(records, input.maxRecordVisits))
                        return "RHI-BINDLESS-SCENE-CONTENT-RECORD-CAPACITY";
                    BindlessTerrainPage32 value, checked;
                    std::memcpy(
                        &value, pages.data + std::size_t(page) * sizeof(value), sizeof(value));
                    if (value.routing[3u] || value.routing[1u] != std::uint32_t(generation)
                        || value.routing[2u] != std::uint32_t(generation >> 32u))
                        return "RHI-BINDLESS-SCENE-CONTENT-PAGE-GENERATION";
                    if (const auto* id =
                            BindlessTerrainMaterialContract::encodePage(value.worldBounds,
                                value.routing[0u],
                                layersA,
                                layersB,
                                generation,
                                checked))
                        return id;
                }
            }
        }
        const auto& usage = *stream->originalInputs()->originalScalars()->originalUsage();
        const auto& arguments = *stream->originalInputs()->originalScalars()->originalArguments();
        bool selected = false, pushed = false;
        std::size_t passCommand = SIZE_MAX;
        std::uint64_t commands = 0u, draws = 0u;
        BindlessPbrGeometryDraw16 draw;
        for (const auto& op : stream->operations()) {
            if (!charge(commands, input.maxCommandVisits))
                return "RHI-BINDLESS-SCENE-CONTENT-COMMAND-CAPACITY";
            using K = CommandUsageKind;
            if (op.kind == K::BeginRenderPass) {
                passCommand = op.commandIndex;
                selected = pushed = false;
            } else if (op.kind == K::BindPreparedGraphicsBinding) {
                selected = op.referenceCount == 1u && op.firstReference < usage.references().size()
                    && usage.references()[op.firstReference].sourceIndex
                        == inventory->originalLeaseIndex(closure->originalRoot());
                pushed = false;
                if (selected) {
                    if (passCommand >= usage.commands().size())
                        return "RHI-BINDLESS-SCENE-CONTENT-PASS";
                    const auto& pass = usage.commands()[passCommand];
                    if (!pass.referenceCount)
                        return "RHI-BINDLESS-SCENE-CONTENT-ATTACHMENT";
                    for (std::size_t i = 0u; i < pass.referenceCount; ++i) {
                        if (!charge(descriptors, input.maxDescriptorVisits))
                            return "RHI-BINDLESS-SCENE-CONTENT-DESCRIPTOR-CAPACITY";
                        const auto source = usage.references()[pass.firstReference + i].sourceIndex;
                        if (source >= inventory->sources().size())
                            return "RHI-BINDLESS-SCENE-CONTENT-ATTACHMENT";
                        const auto* view =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &inventory->sources()[source]);
                        if (!view)
                            return "RHI-BINDLESS-SCENE-CONTENT-ATTACHMENT";
                        const auto& extent =
                            (*view)->originalParentLease()->originalObject()->desc().extent;
                        if (extent.width != frame.outputExtent[0u]
                            || extent.height != frame.outputExtent[1u])
                            return "RHI-BINDLESS-SCENE-CONTENT-ATTACHMENT-EXTENT";
                    }
                }
            } else if (op.kind == K::BindPreparedComputeBinding || op.kind == K::EndRenderPass) {
                selected = pushed = false;
                passCommand = SIZE_MAX;
            } else if (selected && op.kind == K::PushConstants) {
                const auto& record = arguments.records()[op.commandIndex];
                const auto* push = record.scalar.template get<CommandScalarArguments::Push>();
                if (!push || push->offset || push->size != sizeof(draw)
                    || push->stages != ShaderStage::Vertex || record.byteCount != sizeof(draw))
                    return "RHI-BINDLESS-SCENE-CONTENT-PUSH";
                std::memcpy(&draw, arguments.bytes(op.commandIndex), sizeof(draw));
                pushed = true;
            } else if (selected && (op.kind == K::Draw || op.kind == K::DrawIndexed)) {
                if (!pushed)
                    return "RHI-BINDLESS-SCENE-CONTENT-PUSH-MISSING";
                const auto& scalar = arguments.records()[op.commandIndex].scalar;
                std::uint32_t first = 0u, count = 0u;
                if (op.kind == K::Draw) {
                    const auto* value = scalar.template get<CommandScalarArguments::Draw>();
                    if (!value)
                        return "RHI-BINDLESS-SCENE-CONTENT-DRAW";
                    first = value->firstInstance;
                    count = value->instances;
                } else {
                    const auto* value = scalar.template get<CommandScalarArguments::DrawIndexed>();
                    if (!value)
                        return "RHI-BINDLESS-SCENE-CONTENT-DRAW";
                    first = value->firstInstance;
                    count = value->instances;
                }
                if (const auto* id = BindlessPbrGeometryContract::validateDraw(
                        draw, materialCount, instances, first, count, instanced))
                    return id;
                pushed = false;
                ++draws;
            } else if (selected
                && (op.kind == K::DrawIndexedIndirect || op.kind == K::DrawIndexedIndirectCount))
                return "RHI-BINDLESS-SCENE-CONTENT-PRODUCED-PROFILE-REQUIRED";
        }
        return draws ? nullptr : "RHI-BINDLESS-SCENE-CONTENT-DRAW-MISSING";
    }
};
} // namespace arp::test
