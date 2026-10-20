#pragma once
#include "rhi/frontend/command_geometry_content.h"
#include "rhi/frontend/command_sources.h"

namespace arp::rhi {
// Borrowed cold inputs. The authentic native closure copies the selected
// actors into its existing canonical entries before this view may retire.
struct OriginalBindlessInitializedSources {
    const std::shared_ptr<const OriginalInitializedGeometrySource>* geometry = nullptr;
    std::size_t geometryCount = 0u;
    const std::shared_ptr<const OriginalInitializedGeometrySource>* shader = nullptr;
    std::size_t shaderCount = 0u;

    bool empty() const noexcept { return !geometry && !geometryCount && !shader && !shaderCount; }
};

// Borrowed adapters over backend-issued actors and the original canonical
// entries. Their caller retains the actors in that native closure through its
// original seal/completion; no alternate inventory or content authority.
class OriginalBindlessInitializedShaderContract final {
public:
    using Budget = OriginalBindlessInitializedShaderBudget;

    static const char* validatePublicInputs(const Budget& budget,
        const std::shared_ptr<const OriginalInitializedGeometrySource>* contents,
        std::size_t count) noexcept
    {
        if (const auto* id = budget.diagnostic())
            return id;
        if (budget.empty())
            return contents || count ? "RHI-BINDLESS-INITIALIZED-SHADER-SCHEMA" : nullptr;
        if (!contents || !count || count > budget.maxActors)
            return "RHI-BINDLESS-INITIALIZED-SHADER-INPUTS";
        std::uint64_t bytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& actor = contents[i];
            if (!actor || !actor->hasQuotaBoundSharedOwner(actor))
                return "RHI-BINDLESS-INITIALIZED-SHADER-OWNER";
            if (actor->initializedBytes() > budget.maxBytes - bytes)
                return "RHI-BINDLESS-INITIALIZED-SHADER-BYTE-CAPACITY";
            bytes += actor->initializedBytes();
        }
        return nullptr;
    }

    // Backend-only cast of the authenticated public actors. The original
    // closure's cold quota scope owns this temporary conversion storage.
    template <class Content>
    static const char* project(const Budget& budget,
        const std::shared_ptr<const OriginalInitializedGeometrySource>* contents,
        std::size_t count,
        core::pmr::Vector<std::shared_ptr<const Content>>& output)
    {
        output.clear();
        if (const auto* id = validatePublicInputs(budget, contents, count))
            return id;
        output.reserve(count);
        for (std::size_t i = 0u; i < count; ++i) {
            auto actor = std::dynamic_pointer_cast<const Content>(contents[i]);
            if (const auto* id = Content::validateOriginal(actor)) {
                output.clear();
                return id;
            }
            output.push_back(std::move(actor));
        }
        return nullptr;
    }

    template <class Content>
    static const char* validateInputs(const OriginalCommandBindlessRootSource& root,
        const OriginalCommandResourceSources& inventory,
        Backend backend,
        const Budget& budget,
        const std::shared_ptr<const Content>* contents,
        std::size_t count) noexcept
    {
        if (const auto* id = budget.diagnostic())
            return id;
        if (budget.empty())
            return contents || count ? "RHI-BINDLESS-INITIALIZED-SHADER-SCHEMA" : nullptr;
        const auto lowering = semantics::OriginalBindlessInitializedShaderLowering::current(
            backend, budget.contractVersion);
        if (!lowering.correspondenceAvailable || lowering.diagnostic)
            return lowering.diagnostic;
        if (!contents || !count || count > budget.maxActors)
            return "RHI-BINDLESS-INITIALIZED-SHADER-INPUTS";
        std::uint64_t visits = 0u, bytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& actor = contents[i];
            if (const auto* id = Content::validateOriginal(actor))
                return id;
            const auto source = inventory.originalLeaseIndex(actor->originalLease());
            if (source == SIZE_MAX)
                return "RHI-BINDLESS-INITIALIZED-SHADER-INVENTORY";
            for (std::size_t prior = 0u; prior < i; ++prior) {
                if (const auto* id = visit(budget, visits))
                    return id;
                if (core::SharedOwnerProjection::overlapsOwner(actor, contents[prior])
                    || core::SharedOwnerProjection::sameOwner(
                        actor->originalLease(), contents[prior]->originalLease()))
                    return "RHI-BINDLESS-INITIALIZED-SHADER-DUPLICATE";
            }
            DescriptorType type = DescriptorType::StorageBuffer;
            bool found = false;
            const auto declaration = [&](const auto& element) noexcept -> const char* {
                if (const auto* id = visit(budget, visits))
                    return id;
                if (element.declaration.sourceIndex != source)
                    return nullptr;
                const auto selected = element.declaration.write.type;
                if ((selected != DescriptorType::UniformBuffer
                        && selected != DescriptorType::ReadOnlyStorageBuffer)
                    || (found && type != selected))
                    return "RHI-BINDLESS-INITIALIZED-SHADER-READONLY";
                found = true;
                type = selected;
                return nullptr;
            };
            for (const auto& element : root.elements())
                if (const auto* id = declaration(element))
                    return id;
            for (const auto& domain : root.ordinaryDomains())
                for (const auto& element : domain.source->frozenElements())
                    if (const auto* id = declaration(element))
                        return id;
            if (!found)
                return "RHI-BINDLESS-INITIALIZED-SHADER-UNBOUND";
            if (const auto* id = Content::visitInitializedShader(actor,
                    actor->originalLease(),
                    type,
                    [](const void*, std::size_t) noexcept -> const char* {
                        return nullptr;
                    }))
                return id;
            if (actor->initializedBytes() > budget.maxBytes - bytes)
                return "RHI-BINDLESS-INITIALIZED-SHADER-BYTE-CAPACITY";
            bytes += actor->initializedBytes();
        }
        return nullptr;
    }

    template <class Content, class Entry>
    static const char* capture(const OriginalCommandResourceSources& inventory,
        const Budget& budget,
        const std::shared_ptr<const Content>* contents,
        std::size_t count,
        Entry& entry,
        std::uint64_t& visits) noexcept
    {
        if (const auto* id = budget.diagnostic())
            return id;
        if (count > budget.maxActors || (count && !contents))
            return "RHI-BINDLESS-INITIALIZED-SHADER-INPUTS";
        for (std::size_t i = 0u; i < count; ++i) {
            if (const auto* id = visit(budget, visits))
                return id;
            if (inventory.originalLeaseIndex(contents[i]->originalLease()) != entry.sourceIndex)
                continue;
            const auto captured =
                contents[i]->originalLease()->originalObject()->observeOriginalCommittedState();
            if (!captured)
                return "RHI-BINDLESS-INITIALIZED-SHADER-STATE";
            entry.initializedShader = contents[i];
            entry.initializedShaderState.emplace(*captured);
        }
        return nullptr;
    }

    template <class Content, class Entries, class Seal>
    static const char* validateRetained(const OriginalCommandResourceSources& inventory,
        const Budget& budget,
        const Entries& entries,
        const std::shared_ptr<const Seal>& seal) noexcept
    {
        if (const auto* id = budget.diagnostic())
            return id;
        std::uint64_t visits = 0u, count = 0u, bytes = 0u;
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            const auto& actor = entry.initializedShader;
            if (!actor) {
                if (entry.initializedShaderState)
                    return "RHI-BINDLESS-INITIALIZED-SHADER-STATE";
                continue;
            }
            if (budget.empty() || !entry.initializedShaderState || entry.initializedGeometry
                || inventory.originalLeaseIndex(actor->originalLease()) != entry.sourceIndex)
                return "RHI-BINDLESS-INITIALIZED-SHADER-CORRESPONDENCE";
            if (const auto* id = seal
                    ? Content::validateOriginalPending(actor, seal, *entry.initializedShaderState)
                    : Content::validateOriginal(actor))
                return id;
            if (!seal)
                if (const auto* id =
                        actor->originalLease()->originalObject()->validateOriginalCommittedState(
                            *entry.initializedShaderState))
                    return id;
            bool duplicate = false;
            for (std::size_t prior = 0u; prior < i; ++prior) {
                if (const auto* id = visit(budget, visits))
                    return id;
                duplicate = duplicate
                    || core::SharedOwnerProjection::sameOwner(
                        actor, entries[prior].initializedShader);
            }
            if (duplicate)
                continue;
            if (count == budget.maxActors || actor->initializedBytes() > budget.maxBytes - bytes)
                return "RHI-BINDLESS-INITIALIZED-SHADER-CAPACITY";
            ++count;
            bytes += actor->initializedBytes();
        }
        return !budget.empty() && !count ? "RHI-BINDLESS-INITIALIZED-SHADER-COVERAGE" : nullptr;
    }

private:
    static const char* visit(const Budget& budget, std::uint64_t& visits) noexcept
    {
        if (visits == budget.maxBindingVisits)
            return "RHI-BINDLESS-INITIALIZED-SHADER-VISIT-CAPACITY";
        ++visits;
        return nullptr;
    }
};

class OriginalBindlessInitializedSourcesContract final {
public:
    static const char* validatePublicInputs(const OriginalBindlessRootNativeClosureBudget& budget,
        const OriginalBindlessInitializedSources& input) noexcept
    {
        if (const auto* id = OriginalBindlessRootNativeClosureContract::validateBudget(budget))
            return id;
        if (const auto* id = OriginalBindlessInitializedShaderContract::validatePublicInputs(
                budget.initializedShader, input.shader, input.shaderCount))
            return id;
        const bool geometry =
            OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(budget);
        if (geometry != bool(input.geometryCount) || (input.geometryCount && !input.geometry)
            || (!input.geometryCount && input.geometry)
            || input.geometryCount > budget.maxInitializedGeometryActors)
            return "RHI-BINDLESS-INITIALIZED-SOURCES-GEOMETRY-INPUTS";
        std::uint64_t bytes = 0u;
        for (std::size_t i = 0u; i < input.geometryCount; ++i) {
            const auto& actor = input.geometry[i];
            if (!actor || !actor->hasQuotaBoundSharedOwner(actor))
                return "RHI-BINDLESS-INITIALIZED-SOURCES-GEOMETRY-OWNER";
            if (actor->initializedBytes() > budget.maxInitializedGeometryBytes - bytes)
                return "RHI-BINDLESS-INITIALIZED-SOURCES-GEOMETRY-BYTE-CAPACITY";
            bytes += actor->initializedBytes();
        }
        return nullptr;
    }

    // Concrete, backend-issued actors alone may reach a native closure. A
    // public subclass/alias is not an initialization or content certificate.
    template <class Content>
    static const char* project(const OriginalBindlessRootNativeClosureBudget& budget,
        const OriginalBindlessInitializedSources& input,
        core::pmr::Vector<std::shared_ptr<const Content>>& geometry,
        core::pmr::Vector<std::shared_ptr<const Content>>& shader)
    {
        geometry.clear();
        shader.clear();
        if (const auto* id = validatePublicInputs(budget, input))
            return id;
        if (const auto* id = OriginalBindlessInitializedShaderContract::project(
                budget.initializedShader, input.shader, input.shaderCount, shader))
            return id;
        geometry.reserve(input.geometryCount);
        for (std::size_t i = 0u; i < input.geometryCount; ++i) {
            auto actor = std::dynamic_pointer_cast<const Content>(input.geometry[i]);
            if (const auto* id = Content::validateOriginal(actor))
                return id;
            geometry.push_back(std::move(actor));
        }
        return nullptr;
    }
};
} // namespace arp::rhi
