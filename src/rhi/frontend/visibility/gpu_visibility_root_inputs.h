#pragma once
#include "rhi/frontend/visibility/gpu_visibility_frozen_inputs.h"
#include "rhi/frontend/visibility/gpu_visibility_root_producer.h"

namespace arp::rhi::visibility {
// Backend cold admission composes the real four producer Roots and the native
// actors that initialized their exact canonical leases. Native actors remain
// in original command custody through its seal; this borrowed inspection never
// creates a resource graph, completion receipt or GPU-content authority.
class GpuVisibilityRootInputContract final {
public:
    struct Budget {
        GpuVisibilityLayoutContract::Budget layout;
        GpuVisibilityRootProducerContract::Budget producer;
        GpuVisibilityFrozenInputContract::Budget frozen;
    };

    struct Report {
        const char* diagnostic = nullptr;
        GpuVisibilityRootProducerContract::Report producer;
        GpuVisibilityFrozenInputContract::Report frozen;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    // Cold native admission projects the actors actually retained in its four
    // original closures. No caller-authored array may stand in for that custody.
    template <class Closure, class Instance>
    static Report inspectClosures(const std::array<std::shared_ptr<const Closure>, 4u>& closures,
        const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& stream,
        const GpuVisibilityLayoutContract::Input& layout,
        const Instance* instances,
        const GpuVisibilityParameters& parameters,
        std::uint32_t queriedWorkgroupLimit,
        bool queriedNativeBase,
        const Budget& budget) noexcept
    {
        GpuVisibilityRootProducerContract::Roots roots;
        for (std::size_t i = 0u; i < closures.size(); ++i) {
            if (const auto* id = Closure::validateOriginal(closures[i]))
                return {id};
            if (!core::SharedOwnerProjection::sameOwner(
                    closures[i]->originalInventory(), closures[0u]->originalInventory()))
                return {"PCG-GPU-VISIBILITY-INPUT-CLOSURE-INVENTORY"};
            roots[i] = closures[i]->originalRoot();
        }
        const auto& inventory = closures[0u]->originalInventory();
        const auto producer = GpuVisibilityRootProducerContract::inspect(roots,
            inventory,
            graph,
            stream,
            layout,
            budget.layout,
            queriedWorkgroupLimit,
            budget.producer);
        if (producer.diagnostic)
            return {producer.diagnostic};
        using Owner = std::decay_t<decltype(closures[0u]->initializedShader(std::size_t{}))>;
        using Content = typename Owner::element_type;
        constexpr std::array<std::size_t, 4u> roles{0u, 1u, 2u, 8u};
        std::array<Owner, 4u> initializers;
        for (std::size_t i = 0u; i < roles.size(); ++i) {
            const auto source = producer.sources[roles[i]];
            const auto& owner = closures[0u]->initializedShader(source);
            if (!owner)
                return {"PCG-GPU-VISIBILITY-INPUT-CLOSURE-CUSTODY"};
            for (const auto& closure : closures)
                if (!core::SharedOwnerProjection::sameOwner(
                        owner, closure->initializedShader(source)))
                    return {"PCG-GPU-VISIBILITY-INPUT-CLOSURE-CUSTODY"};
            initializers[i] = owner;
        }
        return inspect<Content>(roots,
            inventory,
            graph,
            stream,
            layout,
            instances,
            parameters,
            initializers,
            queriedWorkgroupLimit,
            queriedNativeBase,
            budget);
    }

    // NativeContent is the backend's closed initialized-content actor, not an
    // upper-layer callback or caller-authored byte provider. Its static visitor
    // authenticates the shared owner, sealed writes, runtime/payload identity,
    // committed generation and exact original buffer lease before borrowing.
    template <class NativeContent, class Instance>
    static Report inspect(const GpuVisibilityRootProducerContract::Roots& roots,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& stream,
        const GpuVisibilityLayoutContract::Input& layout,
        const Instance* instances,
        const GpuVisibilityParameters& parameters,
        const std::array<std::shared_ptr<const NativeContent>, 4u>& initializers,
        std::uint32_t queriedWorkgroupLimit,
        bool queriedNativeBase,
        const Budget& budget) noexcept
    {
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        Report result;
        result.producer = GpuVisibilityRootProducerContract::inspect(roots,
            inventory,
            graph,
            stream,
            layout,
            budget.layout,
            queriedWorkgroupLimit,
            budget.producer);
        if (result.producer.diagnostic)
            return reject(result.producer.diagnostic);
        constexpr std::array<std::size_t, 4u> roles{0u, 1u, 2u, 8u};
        std::array<GpuVisibilityFrozenInputContract::Bytes, 4u> frozen;
        for (std::size_t i = 0u; i < roles.size(); ++i) {
            const auto source = result.producer.sources[roles[i]];
            if (source >= inventory->sources().size())
                return reject("PCG-GPU-VISIBILITY-INPUT-SOURCE");
            const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                &inventory->sources()[source]);
            if (!lease)
                return reject("PCG-GPU-VISIBILITY-INPUT-LEASE");
            if (const auto *id = NativeContent::visitInitializedShader(initializers[i],
                    *lease,
                    i == 3u ? rhi::DescriptorType::UniformBuffer
                            : rhi::DescriptorType::ReadOnlyStorageBuffer,
                    [&](const void* bytes, std::size_t size) noexcept -> const char* {
                        frozen[i] = {bytes, size};
                        return nullptr;
                    }))
                return reject(id);
        }
        const auto depthSource = result.producer.sources[9u];
        if (depthSource >= inventory->sources().size())
            return reject("PCG-GPU-VISIBILITY-INPUT-DEPTH");
        const auto* depth =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                &inventory->sources()[depthSource]);
        if (!depth)
            return reject("PCG-GPU-VISIBILITY-INPUT-DEPTH");
        result.frozen = GpuVisibilityFrozenInputContract::inspect(layout,
            budget.layout,
            instances,
            parameters,
            queriedNativeBase,
            &(*depth)->originalParentLease()->originalObject()->desc(),
            {frozen[0u], frozen[1u], frozen[2u], frozen[3u]},
            budget.frozen);
        return result.frozen.diagnostic ? reject(result.frozen.diagnostic) : result;
    }
};
} // namespace arp::rhi::visibility
