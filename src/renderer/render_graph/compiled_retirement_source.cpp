#include "core/container/bounded_sequence.h"
#include "core/diagnostics/diagnostic_error.h"
#include "core/ownership/lifetime_epoch.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"

#include <stdexcept>

namespace arp::render_graph {

static_assert(std::is_nothrow_move_constructible_v<rhi::CommandEncoderDesc>);
static_assert(std::is_nothrow_move_constructible_v<CompiledFrameGraph>);
static_assert(std::is_nothrow_destructible_v<CompiledFrameGraph>);

CompiledFrameGraph& CompiledFrameGraph::operator=(const CompiledFrameGraph& other)
{
    if (this != &other) {
        CompiledFrameGraph replacement(other);
        core::replaceCpuObjectByMove(*this, replacement);
    }
    return *this;
}

CompiledFrameGraph& CompiledFrameGraph::operator=(CompiledFrameGraph&& other) noexcept
{
    core::replaceCpuObjectByMove(*this, other);
    return *this;
}

struct FrameGraph::RetirementSourceState {
    RetirementSourceState(rhi::semantics::FrameGraphCompiledRetirementSourceBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : budget(budget), quota(std::move(quota))
    {}

    const rhi::semantics::FrameGraphCompiledRetirementSourceBudget budget;
    const std::shared_ptr<core::CpuMemoryQuota> quota;
    core::LifetimeEpoch epoch;
};

struct CompiledFrameGraph::RetirementSource {
    struct Binding {
        ResourceKind kind;
        std::uint32_t handle;
        const void* object;
        std::uint64_t id;
    };

    RetirementSource(const std::shared_ptr<FrameGraph::RetirementSourceState>& graph,
        FrameGraphResourcePool::RetirementMetadataReservation slot,
        std::size_t bindingCount)
        : graph(graph), epoch(graph->epoch.ticket()), quota(graph->quota), slot(std::move(slot)),
          bindings(bindingCount), sharedImports(graph->budget.sharedImports.requested()),
          ownedResources(graph->budget.ownedResources.requested())
    {}

    const std::weak_ptr<FrameGraph::RetirementSourceState> graph;
    const core::LifetimeEpoch::Ticket epoch;
    const std::shared_ptr<core::CpuMemoryQuota> quota;
    FrameGraphResourcePool::RetirementMetadataReservation slot;
    core::BoundedSequence<Binding> bindings;
    const bool sharedImports;
    const bool ownedResources;
};

const char* CompiledFrameGraph::retirementSourceDiagnostic() const noexcept
{
    if (!retirementSource_)
        return "RG-COMPILED-SOURCE-MISSING";
    const auto graph = retirementSource_->graph.lock();
    if (!graph)
        return "RG-COMPILED-SOURCE-EXPIRED";
    if (!retirementSource_->epoch.sameCurrentEpoch(graph->epoch.ticket()))
        return "RG-COMPILED-SOURCE-EPOCH";
    return retirementSource_->slot.diagnostic();
}

const char* FrameGraph::prepareCompiledRetirementSource(
    const rhi::semantics::FrameGraphCompiledRetirementSourceBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota)
{
    if (const auto* id =
            rhi::semantics::FrameGraphCompiledRetirementSourceContract::validateIntent(budget))
        return id;
    if (ownedResourcesPublished())
        return "RG-OWNED-RESOURCES-ALREADY-PUBLISHED";
    if (transientResourceOwnersEnabled() && budget.ownedResources.contractVersion != 3u) {
        for (const auto& record : textures_)
            if (record.transient && !record.imported && !record.resolveTexture())
                return "RG-OWNED-RESOURCES-MATERIALIZATION-UNAVAILABLE";
    }
    if (mixedResourceOwnersEnabled() && budget.ownedResources.contractVersion < 2u) {
        for (const auto& record : textures_)
            if (record.textureRef)
                return "RG-OWNED-RESOURCES-MIXED-REQUEST";
        for (const auto& record : buffers_)
            if (record.bufferRef)
                return "RG-OWNED-RESOURCES-MIXED-REQUEST";
    }
    if (resourcePool_->ownsGraphResources() && !budget.ownedResources.requested())
        return "RG-OWNED-RESOURCES-SOURCE";
    if (budget.ownedResources.requested()) {
        if (const auto* id =
                rhi::semantics::FrameGraphOwnedResourcesContract::lowering(device_.backend())
                    .diagnostic)
            return id;
        if (const auto* id = resourcePool_->validateGraphAllocationDevice(device_))
            return id;
        if (!resourcePool_->retirementFrameStorageReport().capacity.contractVersion)
            return "RG-FRAME-STORAGE-SOURCE";
    }
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RG-COMPILED-SOURCE-QUOTA";
    if (!subgraphs_.empty() || !subpassGraphs_.empty())
        return "RG-COMPILED-SOURCE-COMPOSITION";
    if (budget.sharedImports.requested()) {
        const auto decision =
            rhi::semantics::FrameGraphSharedImportsContract::lowering(device_.backend());
        if (!decision.retentionAvailable)
            return decision.diagnostic;
        if (!resourcePool_->retirementFrameStorageReport().capacity.contractVersion)
            return "RG-FRAME-STORAGE-SOURCE";
    }
    if (const auto* id = resourcePool_->validateRetirementMetadataDevice(device_))
        return id;
    if (device_.overlapsOriginalDeviceCpuOwner(quota)
        || resourcePool_->overlapsRetirementMetadataQuota(quota))
        return "RG-COMPILED-SOURCE-QUOTA-DOMAIN";
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto replacement = core::makeShared<RetirementSourceState>(budget, std::move(quota));
        if (!replacement->epoch.isActive())
            return "RG-COMPILED-SOURCE-EPOCH";
        retirementSourceState_ = std::move(replacement);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "RG-COMPILED-SOURCE-ALLOCATION";
    }
}

void FrameGraph::invalidateCompiledRetirementSource() const noexcept
{
    if (retirementSourceState_)
        retirementSourceState_->epoch.advance();
}

const char* FrameGraph::sharedImportSourceDiagnostic() const noexcept
{
    if (!retirementSourceState_ || !retirementSourceState_->budget.sharedImports.requested())
        return nullptr;
    if (const auto* id = resourcePool_->validateRetirementMetadataDevice(device_))
        return id;
    const auto validateOwner = [&](const auto& owner, bool canonicalAtImport) -> const char* {
        if (!owner)
            return "RG-SHARED-IMPORTS-OWNER-UNAVAILABLE";
        // Reject before dereferencing an alias whose real owner may already
        // have died. This private observation only rejects; it never suffices
        // for positive admission or replaces original factory provenance.
        if (!canonicalAtImport)
            return "RG-SHARED-IMPORTS-SHARED-OWNER";
        if (!owner->sharedOwnerMatches(owner))
            return "RG-SHARED-IMPORTS-SHARED-OWNER";
        return device_.validateOriginalResourceRegistration(*owner);
    };
    for (const auto& record : textures_)
        if (record.textureRef || !mixedResourceOwnersEnabled())
            if (const auto* id =
                    validateOwner(record.textureRef, record.canonicalSharedOwnerAtImport))
                return id;
    for (const auto& record : buffers_)
        if (record.bufferRef || !mixedResourceOwnersEnabled())
            if (const auto* id =
                    validateOwner(record.bufferRef, record.canonicalSharedOwnerAtImport))
                return id;
    return nullptr;
}

bool FrameGraph::ownedResourcesPublished() const noexcept
{
    for (const auto& record : textures_)
        if (record.bundleTexture)
            return true;
    for (const auto& record : buffers_)
        if (record.bundleBuffer)
            return true;
    return false;
}

bool FrameGraph::mixedResourceOwnersEnabled() const noexcept
{
    return retirementSourceState_
        && (retirementSourceState_->budget.ownedResources.contractVersion == 2u
            || retirementSourceState_->budget.ownedResources.contractVersion == 3u)
        && retirementSourceState_->budget.sharedImports.requested();
}

bool FrameGraph::transientResourceOwnersEnabled() const noexcept
{
    return retirementSourceState_
        && retirementSourceState_->budget.ownedResources.contractVersion == 3u;
}

void FrameGraph::rejectUnjoinedGraphImport(bool shared) const
{
    if (resourcePool_->ownsGraphResources() && (!shared || !mixedResourceOwnersEnabled()))
        throw core::DiagnosticError("RG-OWNED-RESOURCES-IMPORT-UNAVAILABLE");
}

const char* FrameGraph::ownedResourceSourceDiagnostic() const noexcept
{
    if (!resourcePool_->ownsGraphResources())
        return nullptr;
    if (!retirementSourceState_ || !retirementSourceState_->budget.ownedResources.requested())
        return "RG-OWNED-RESOURCES-SOURCE";
    if (const auto* id = resourcePool_->validateGraphAllocationDevice(device_))
        return id;
    if (ownedResourcesPublished())
        return "RG-OWNED-RESOURCES-ALREADY-PUBLISHED";
    for (const auto& record : textures_) {
        if (mixedResourceOwnersEnabled() && record.textureRef) {
            if (record.texture || !record.imported || record.placed)
                return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
        } else if (transientResourceOwnersEnabled() && record.transient && !record.imported
            && !record.textureRef && !record.placed) {
            // Desc-only records are authenticated by the original factory
            // during publication, after all compiler traversal has completed.
        } else if (!record.texture || record.textureRef || record.imported || record.placed)
            return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
    }
    for (const auto& record : buffers_) {
        if (mixedResourceOwnersEnabled() && record.bufferRef) {
            if (record.buffer)
                return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
        } else if (!record.buffer || record.bufferRef)
            return "RG-OWNED-RESOURCES-OWNER-UNAVAILABLE";
    }
    return nullptr;
}

void FrameGraph::publishCompiledRetirementSource(CompiledFrameGraph& compiled)
{
    if (!retirementSourceState_)
        return;
    if (!subgraphs_.empty() || !subpassGraphs_.empty())
        throw core::DiagnosticError("RG-COMPILED-SOURCE-COMPOSITION");
    if (!retirementSourceState_->epoch.isActive())
        throw core::DiagnosticError("RG-COMPILED-SOURCE-EPOCH");
    const auto ceiling = retirementSourceState_->budget.maxResourceBindings;
    if (textures_.size() > ceiling || buffers_.size() > ceiling - textures_.size())
        throw core::DiagnosticError("RG-COMPILED-SOURCE-RESOURCE-CAPACITY");
    const bool retainImports = retirementSourceState_->budget.sharedImports.requested();
    const bool retainOwned = retirementSourceState_->budget.ownedResources.requested();
    if (transientResourceOwnersEnabled()
        && (!compiled.aliasPlan().empty() || !transientHeaps_.empty()))
        throw core::DiagnosticError("RG-OWNED-RESOURCES-ALIAS-MATERIALIZATION-UNAVAILABLE");
    const char* diagnostic = nullptr;
    auto slot = resourcePool_->reserveRetirementMetadata(diagnostic);
    if (diagnostic)
        throw core::DiagnosticError(diagnostic);
    if (resourcePool_->overlapsRetirementMetadataQuota(retirementSourceState_->quota))
        throw core::DiagnosticError("RG-COMPILED-SOURCE-QUOTA-DOMAIN");
    if (retainImports) {
        if (const auto* id = sharedImportSourceDiagnostic())
            throw core::DiagnosticError(id);
        if (!compiled.aliasPlan().empty() || !transientHeaps_.empty())
            throw core::DiagnosticError("RG-SHARED-IMPORTS-OWNER-UNAVAILABLE");
    }
    try {
        std::shared_ptr<CompiledFrameGraph::RetirementSource> source;
        {
            core::CpuMemoryQuotaScope scope(retirementSourceState_->quota);
            if (resourcePool_->retirementFrameStorageReport().capacity.contractVersion) {
                std::size_t textureOwners = 0u, bufferOwners = 0u, references = 0u;
                for (const auto& texture : textures_) {
                    if (texture.textureRef)
                        ++references;
                    else
                        ++textureOwners;
                }
                for (const auto& buffer : buffers_) {
                    if (buffer.imported())
                        ++references;
                    else
                        ++bufferOwners;
                }
                core::pmr::Set<std::uint32_t> aliasGroups;
                for (const auto& alias : compiled.aliasPlan())
                    aliasGroups.insert(alias.aliasGroup);
                if (const auto* id = resourcePool_->validateRetirementFrameStorage(
                        slot, textureOwners, bufferOwners, aliasGroups.size(), references))
                    throw core::DiagnosticError(id);
            }
            source = core::makeShared<CompiledFrameGraph::RetirementSource>(
                retirementSourceState_, std::move(slot), textures_.size() + buffers_.size());
        }
        if (retainImports && !retainOwned) {
            auto* retainer = resourcePool_->pinRetirementFrameReferences(source->slot, diagnostic);
            if (!retainer)
                throw core::DiagnosticError(diagnostic);
            // All fallible source/snapshot allocations are complete. The
            // immutable import-only inventory maps directly to original owners.
            if (!retainer->retainMapped(textures_.size() + buffers_.size(),
                    [&](std::size_t index) noexcept -> std::shared_ptr<const void> {
                        if (index < textures_.size())
                            return textures_[index].textureRef;
                        return buffers_[index - textures_.size()].bufferRef;
                    }))
                throw core::DiagnosticError("RG-FRAME-STORAGE-REFERENCE-CAPACITY");
        }
        if (retainOwned)
            if (const auto* id =
                    resourcePool_->adoptRetirementFrameOwners(source->slot, *this, source))
                throw core::DiagnosticError(id);
        // Binding storage was allocated before any ownership change. These
        // trivial bounded appends cannot allocate; /3 now observes real objects.
        for (const auto& record : textures_) {
            const auto* object = record.resolveTexture();
            source->bindings.append({ResourceKind::Texture,
                record.handle.id,
                object,
                object ? object->id().value : 0u});
        }
        for (const auto& record : buffers_) {
            const auto* object = record.resolveBuffer();
            source->bindings.append(
                {ResourceKind::Buffer, record.handle.id, object, object ? object->id().value : 0u});
        }
        compiled.retirementSource_ = std::move(source);
    } catch (const std::bad_alloc&) {
        throw core::DiagnosticError("RG-COMPILED-SOURCE-ALLOCATION");
    } catch (const std::length_error&) {
        throw core::DiagnosticError("RG-COMPILED-SOURCE-RESOURCE-CAPACITY");
    }
}

const char* FrameGraph::compiledRetirementExecutionDiagnostic(
    const CompiledFrameGraph& compiled) const noexcept
{
    if (resourcePool_->ownsGraphResources()
        || (retirementSourceState_ && retirementSourceState_->budget.ownedResources.requested())
        || (compiled.retirementSource_ && compiled.retirementSource_->ownedResources))
        return "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE";
    if ((retirementSourceState_ && retirementSourceState_->budget.sharedImports.requested())
        || (compiled.retirementSource_ && compiled.retirementSource_->sharedImports))
        return "RG-SHARED-IMPORTS-COMMAND-CUSTODY-UNAVAILABLE";
    return nullptr;
}

const char* FrameGraph::validateCompiledRetirementSource(
    const CompiledFrameGraph& compiled) const noexcept
{
    if (!compiled.retirementSource_)
        return "RG-COMPILED-SOURCE-MISSING";
    const auto& source = *compiled.retirementSource_;
    const auto graph = source.graph.lock();
    if (!graph)
        return "RG-COMPILED-SOURCE-EXPIRED";
    if (graph != retirementSourceState_)
        return "RG-COMPILED-SOURCE-GRAPH";
    if (const auto* id = compiled.retirementSourceDiagnostic())
        return id;
    if (const auto* id = resourcePool_->validateRetirementMetadata(source.slot))
        return id;
    const auto& bindings = source.bindings.values();
    if (bindings.size() < textures_.size() || bindings.size() - textures_.size() != buffers_.size())
        return "RG-COMPILED-SOURCE-RESOURCE-MISMATCH";
    std::size_t index = 0u;
    for (const auto& record : textures_) {
        const auto& binding = bindings[index++];
        const auto* object = record.resolveTexture();
        if (binding.kind != ResourceKind::Texture || binding.handle != record.handle.id
            || binding.object != object || binding.id != (object ? object->id().value : 0u))
            return "RG-COMPILED-SOURCE-RESOURCE-MISMATCH";
    }
    for (const auto& record : buffers_) {
        const auto& binding = bindings[index++];
        const auto* object = record.resolveBuffer();
        if (binding.kind != ResourceKind::Buffer || binding.handle != record.handle.id
            || binding.object != object || binding.id != (object ? object->id().value : 0u))
            return "RG-COMPILED-SOURCE-RESOURCE-MISMATCH";
    }
    return nullptr;
}

} // namespace arp::render_graph
