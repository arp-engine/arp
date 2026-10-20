#pragma once
#include "original_bindless_root_assembly_fixture.h"
#include "original_mixed_entry_flow_fixture.h"
#include "rhi/frontend/completed_depth_read_input.h"

namespace arp::test {
// Backend-independent custody checks over the same three prepared entries.
// A cold closure must neither create GPU resources nor grant execution.
class OriginalMixedRootClosureFixture final {
    template <class C, class = void> struct HasThreeClosureOwnerProjection : std::false_type {};

    template <class C>
    struct HasThreeClosureOwnerProjection<C,
        std::void_t<decltype(std::declval<const C&>().combinedNativeOwnerCount(
            std::declval<const C&>(),
            UINT32_MAX,
            std::declval<std::uint32_t&>(),
            std::declval<const C*>()))>> : std::true_type {};

public:
    // Canonical inputs shared by cold closure checks and the public stream.
    // Deduplication compares real owners; it never merges resource identities.
    static core::pmr::Vector<rhi::OriginalCommandResourceSources::Source> completeSources(
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& base,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& depth,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& results)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        core::pmr::Vector<Sources::Source> sources = base;
        const auto append = [&](const auto& owner) {
            for (const auto& source : sources)
                if (const auto* current = std::get_if<std::decay_t<decltype(owner)>>(&source))
                    if (core::SharedOwnerProjection::sameOwner(*current, owner))
                        return;
            sources.emplace_back(owner);
        };
        append(depth->originalViewSource()->originalParentLease());
        append(depth->originalViewSource());
        for (const auto role : {rhi::BindlessSnapshot::OriginalBufferRole::Metadata,
                 rhi::BindlessSnapshot::OriginalBufferRole::FaultLatch,
                 rhi::BindlessSnapshot::OriginalBufferRole::FaultReadback})
            append(snapshot->originalBufferSource(role));
        append(results);
        return sources;
    }

    template <class Closure>
    static void verify(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, 3u>& prepared,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& base,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& depth,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& results)
    {
        using namespace rhi;
        using Sources = OriginalCommandResourceSources;
        using Root = OriginalCommandBindlessRootSource;
        const auto metadata = results->originalMetadataQuota();
        const auto bytes = metadata->limitBytes();
        const auto sourceBaseline = metadata->liveBytes();
        const auto gpuBaseline = device.memoryStats().allocatedBytes;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        {
            auto sources = completeSources(base, snapshot, depth, results);
            OriginalCommandAdmissionBudget admission;
            admission.contractVersion = 1u;
            admission.prototypeOptIn = true;
            admission.maxCommands = admission.maxReferences = 1u;
            admission.maxInputDescriptors = static_cast<std::uint32_t>(prepared.front()
                    ->authorization()
                    ->package()
                    .artifact()
                    .physicalLayout.bindings.size());
            admission.maxGraphOwners = 512u;
            admission.maxStateTransactions = 1u;
            admission.maxCpuBytes.fill(bytes);
            for (const auto& input : sources)
                std::visit(
                    [&](const auto& owner) {
                        using T = std::remove_const_t<
                            typename std::decay_t<decltype(owner)>::element_type>;
                        ++admission.maxSourceOwners[static_cast<std::size_t>(T::domain)];
                    },
                    input);
            admission
                .maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::BindlessRoot)] =
                3u;
            auto inventory = Sources::prepareColdOriginalTyped(
                sources.data(), sources.size(), admission, {6u, true, 3u}, metadata, id);
            if (id)
                std::fprintf(stderr, "mixed closure13 inventory: %s\n", id);
            assert(inventory && !id);
            std::array<std::shared_ptr<const Root>, 3u> roots;
            for (unsigned i = 0u; i < roots.size(); ++i) {
                roots[i] = OriginalBindlessRootAssemblyFixture::prepare(
                    snapshot, prepared[i], inventory, device, metadata, id);
                if (id)
                    std::fprintf(stderr, "mixed closure13 root: %s\n", id);
                assert(roots[i] && !id && roots[i]->originalShaderEntryAccessVersion() == 1u);
                constexpr std::uint32_t masks[] = {0xc3u, 0xccu, 0xf8u};
                for (std::size_t access = 0u; access < roots[i]->size(); ++access) {
                    BindlessResourceAccess value;
                    std::size_t declaration = SIZE_MAX;
                    bool active = false;
                    assert(!roots[i]->readOriginalShaderDeclaration(
                        access, ShaderStage::Compute, value, declaration, active, 1u));
                    // The copy-only fault destination has no shader declaration.
                    assert(access < roots[i]->elements().size()
                            ? declaration < inventory->sources().size()
                            : declaration == SIZE_MAX);
                    const bool expected = access < roots[i]->elements().size()
                        && bool(masks[i]
                            & (1u << roots[i]->elements()[access].declaration.write.binding));
                    assert(active == expected);
                }
            }
            OriginalMixedEntryFlowFixture::verify(roots, inventory, device.backend());
            std::uint64_t depthBytes = 0u;
            assert(!semantics::TextureUploadDestinationValidation::footprint(
                depth->originalViewSource()->originalParentLease()->originalObject()->desc(),
                depthBytes));
            OriginalBindlessRootNativeClosureBudget budget{13u,
                true,
                static_cast<std::uint32_t>(roots.front()->elements().size() + 1u),
                256u,
                1024u,
                bytes};
            budget.completedDepthInputs = {depth->budget().contractVersion,
                true,
                depth->cells().size(),
                depthBytes,
                OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
            assert(!OriginalBindlessRootNativeClosureContract::validateBudget(budget));
            for (const auto& root : roots) {
                auto closure = Closure::prepareOriginal(root, inventory, device, budget, quota, id);
                if (id)
                    std::fprintf(
                        stderr, "mixed closure13 backend=%u: %s\n", unsigned(device.backend()), id);
                assert(closure && !id && !Closure::validateOriginal(closure));
                assert(!closure->nativeExecutionProven()
                    && closure->originalRoot().get() == root.get()
                    && closure->originalInventory().get() == inventory.get()
                    && closure->resourceEntryCount() == budget.maxResourceEntries);
                auto exact = budget;
                exact.maxNativeOwners = closure->nativeOwnerCount();
                exact.maxRetainedOwners = closure->retainedOwnerCount();
                auto forged = std::shared_ptr<const Closure>(closure.get(), [](const Closure*) {});
                assert(Closure::validateOriginal(forged));
                forged.reset();
                closure.reset();
                assert(!quota->liveBytes());
                for (unsigned field = 0u; field < 8u; ++field) {
                    auto bad = exact;
                    if (field == 0u)
                        --bad.maxResourceEntries;
                    if (field == 1u)
                        --bad.maxNativeOwners;
                    if (field == 2u)
                        --bad.maxRetainedOwners;
                    if (field == 3u)
                        bad.completedDepthInputs.contractVersion =
                            device.backend() == Backend::WebGPU ? 2u : 1u;
                    if (field == 4u)
                        bad.prototypeOptIn = false;
                    if (field == 5u) {
                        bad.contractVersion = 10u;
                        bad.completedDepthInputs = {};
                    }
                    if (field == 6u)
                        --bad.completedDepthInputs.maxCells;
                    if (field == 7u)
                        --bad.completedDepthInputs.maxTextureTexelBytes;
                    assert(
                        !Closure::prepareOriginal(root, inventory, device, bad, quota, id) && id);
                    assert(
                        !quota->liveBytes() && device.memoryStats().allocatedBytes == gpuBaseline);
                }
                {
                    core::CpuMemoryQuotaLease pressure(quota, bytes);
                    assert(pressure.charged());
                    assert(
                        !Closure::prepareOriginal(root, inventory, device, exact, quota, id) && id);
                }
                closure = Closure::prepareOriginal(root, inventory, device, exact, quota, id);
                assert(closure && !id && !Closure::validateOriginal(closure));
                closure.reset();
                assert(!quota->liveBytes());
            }
            // Callback admission retains three physical groups. Reuse the same
            // native identity projection so their common buffers/images are
            // charged once, while each entry's pipeline/group remains retained.
            if constexpr (HasThreeClosureOwnerProjection<Closure>::value) {
                std::array<std::shared_ptr<const Closure>, 3u> closures;
                for (unsigned i = 0u; i < closures.size(); ++i) {
                    closures[i] =
                        Closure::prepareOriginal(roots[i], inventory, device, budget, quota, id);
                    assert(closures[i] && !id);
                }
                std::uint32_t pair = 0u, triple = 0u, reverse = 0u;
                assert(!closures[0]->combinedNativeOwnerCount(
                    *closures[1], budget.maxNativeOwners, pair));
                assert(!closures[0]->combinedNativeOwnerCount(
                    *closures[1], budget.maxNativeOwners, triple, closures[2].get()));
                assert(!closures[2]->combinedNativeOwnerCount(
                    *closures[0], triple, reverse, closures[1].get()));
                assert(reverse == triple && pair < triple);
                const auto separate = std::uint64_t(closures[0]->nativeOwnerCount())
                    + closures[1]->nativeOwnerCount() + closures[2]->nativeOwnerCount();
                assert(triple < separate);
                auto rejected = UINT32_MAX;
                assert(closures[0]->combinedNativeOwnerCount(
                           *closures[1], triple - 1u, rejected, closures[2].get())
                    && !rejected);
                assert(!closures[0]->combinedNativeOwnerCount(
                           *closures[1], triple, rejected, closures[2].get())
                    && rejected == triple);
                for (auto& closure : closures)
                    closure.reset();
                assert(!quota->liveBytes());
                std::printf(
                    "BINDLESS_MIXED_NATIVE_OWNER_UNION3_OK pair%u triple%u exact-less deduplicated retirement0\n",
                    pair,
                    triple);
            }
        }
        assert(metadata->liveBytes() == sourceBaseline && !quota->liveBytes()
            && device.memoryStats().allocatedBytes == gpuBaseline);
        std::printf(
            "BINDLESS_MIXED_ROOT_CLOSURE13_OK backend=%u shared-roots3 original-proofs exact-less quota0 execution-closed\n",
            unsigned(device.backend()));
    }
};
} // namespace arp::test
