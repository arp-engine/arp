#pragma once
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/bindless_produced_program.h"

#include <array>
#include <cassert>

namespace arp::test {
class BindlessProducedProgramFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Contract = OriginalBindlessProducedProgramContract;
        using K = CommandUsageKind;
        const std::array<std::size_t, 8u> roots{100u, 101u, 102u, 103u, 200u, 200u, 201u, 201u};
        core::pmr::Vector<OriginalCommandStreamProgram::Operation> operations;
        core::pmr::Vector<OriginalCommandUsage::Reference> references;
        const auto append = [&](K kind, std::initializer_list<std::uint32_t> refs = {}) {
            const auto ordinal = operations.size();
            OriginalCommandStreamProgram::Operation op;
            op.commandIndex = ordinal;
            op.kind = kind;
            op.firstReference = references.size();
            op.referenceCount = refs.size();
            operations.push_back(op);
            for (const auto source : refs)
                references.push_back({source});
            return ordinal;
        };
        const auto clear = append(K::ClearSnapshotFault, {100u});
        for (std::uint32_t phase = 0u; phase < 4u; ++phase) {
            append(K::BeginComputePass);
            append(K::BindPreparedComputeBinding, {100u + phase});
            append(K::Dispatch);
            append(K::EndComputePass);
            append(K::BufferBarrier, {1u});
        }
        std::size_t firstPush = SIZE_MAX, firstDraw = SIZE_MAX, secondPass = SIZE_MAX;
        for (std::uint32_t pass = 0u; pass < 2u; ++pass) {
            const auto begin = append(K::BeginRenderPass, {20u + pass});
            if (pass)
                secondPass = begin;
            append(K::SetViewport);
            append(K::SetScissor);
            for (std::uint32_t draw = 0u; draw < 2u; ++draw) {
                append(K::BindPreparedGraphicsBinding, {200u + pass});
                append(K::BindVertexBuffer, {10u});
                append(K::BindIndexBuffer, {11u});
                const auto push = append(K::PushConstants, {200u + pass});
                const auto indirect = append(K::DrawIndexedIndirect, {5u});
                if (!pass && !draw) {
                    firstPush = push;
                    firstDraw = indirect;
                }
            }
            append(K::EndRenderPass);
            append(K::TextureBarrier, {30u + pass});
        }
        append(K::BeginTransferPass);
        append(K::BufferCopy, {1u, 40u});
        append(K::BufferCopy, {2u, 41u});
        append(K::EndTransferPass);
        append(K::Finish);
        const Contract::Budget budget{1u, true, operations.size()};
        {
            using Pass = OriginalBindlessGraphicsPassContract;
            Pass::RootSelection selection;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(
                !Pass::selectRoot(operations, references, secondPass, operations.size(), selection)
                && selection.source == 201u && selection.visits == operations.size());
            assert(Pass::selectRoot(
                       operations, references, secondPass, operations.size() - 1u, selection)
                && selection.source == SIZE_MAX && !selection.visits);
            assert(
                Pass::selectRoot(operations, references, SIZE_MAX, operations.size(), selection));
            auto& bind = operations[secondPass + 3u];
            const auto saved = references[bind.firstReference].sourceIndex;
            references[bind.firstReference].sourceIndex = 99u;
            assert(
                Pass::selectRoot(operations, references, secondPass, operations.size(), selection)
                && selection.source == SIZE_MAX);
            references[bind.firstReference].sourceIndex = saved;
            const auto end = operations.back();
            operations.back().kind = K::BeginRenderPass;
            operations.back().commandIndex = secondPass;
            assert(
                Pass::selectRoot(operations, references, secondPass, operations.size(), selection));
            operations.back() = end;
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        {
            using Attachments = OriginalBindlessProducedAttachmentContract;
            assert(!Attachments::validateIntent({}));
            assert(!Attachments::validateIntent({1u, true}));
            assert(Attachments::validateIntent({1u, false}));
            assert(Attachments::validateIntent({0u, true}));
            assert(Attachments::validateIntent({2u, true}));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
                const auto lowering = Attachments::lowering(backend, 1u);
                assert(lowering.preparationAvailable == (backend == Backend::WebGPU)
                    && lowering.validatorOnly == (backend == Backend::Null)
                    && !lowering.executionAvailable);
                assert(!Attachments::lowering(backend, 2u).preparationAvailable);
            }
            TextureDesc texture;
            texture.extent = {8u, 4u, 1u};
            texture.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
            TextureViewDesc view;
            view.usage = TextureUsage::RenderAttachment;
            for (const auto format :
                {TextureFormat::Rgba8Unorm, TextureFormat::Rgba16Float, TextureFormat::D32Float}) {
                texture.format = view.format = format;
                view.aspect =
                    format == TextureFormat::D32Float ? TextureAspect::Depth : TextureAspect::Color;
                assert(!Attachments::inspectView(texture, view));
                auto wrong = view;
                wrong.aspect =
                    format == TextureFormat::D32Float ? TextureAspect::Color : TextureAspect::Depth;
                assert(Attachments::inspectView(texture, wrong));
                wrong = view;
                wrong.baseMipLevel = 1u;
                assert(Attachments::inspectView(texture, wrong));
                auto wrongTexture = texture;
                wrongTexture.sampleCount = 4u;
                assert(Attachments::inspectView(wrongTexture, view));
                wrongTexture = texture;
                wrongTexture.arrayLayerCount = 2u;
                assert(Attachments::inspectView(wrongTexture, view));
            }
            assert(Attachments::acceptsContentControl(5u));
            assert(
                !Attachments::acceptsContentControl(4u) && !Attachments::acceptsContentControl(7u));
            OriginalBindlessRootNativeClosureBudget closure{4u, true, 16u, 32u, 64u, 65536u};
            closure.maxAttachmentViews = 3u;
            closure.producedAttachments = {1u, true};
            assert(!OriginalBindlessRootNativeClosureContract::validateBudget(closure));
            assert(!OriginalBindlessRootNativeClosureContract::validateGeometryBackend(
                Backend::WebGPU, closure));
            assert(OriginalBindlessRootNativeClosureContract::validateGeometryBackend(
                Backend::Vulkan, closure));
            closure.contractVersion = 1u;
            assert(OriginalBindlessRootNativeClosureContract::validateBudget(closure));
        }
        const auto inspect = [&](const Contract::Budget& selected) {
            return Contract::inspectOperations(
                operations,
                references,
                roots.size(),
                [&](std::size_t i) noexcept {
                    return roots[i];
                },
                100u,
                2u,
                selected);
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto accepted = inspect(budget);
        assert(!accepted.diagnostic && accepted.commandVisits == operations.size()
            && accepted.computePasses == 4u && accepted.renderPasses == 2u
            && accepted.dispatches == 4u && accepted.bindings == roots.size()
            && accepted.draws == 4u && accepted.copies == 2u && !accepted.nativeExecutionProven());
        const auto reject = [&](const Contract::Budget& selected, const char* expected) {
            const auto result = inspect(selected);
            assert(result.diagnostic && core::StringView(result.diagnostic) == expected
                && !result.commandVisits && !result.bindings && !result.draws && !result.copies
                && !result.nativeExecutionProven());
        };
        auto invalid = budget;
        --invalid.maxCommandVisits;
        reject(invalid, "RHI-BINDLESS-PRODUCED-PROGRAM-COMMAND-CAPACITY");
        invalid = budget;
        invalid.contractVersion = 0u;
        reject(invalid, "RHI-BINDLESS-PRODUCED-PROGRAM-VERSION");
        invalid = budget;
        invalid.optIn = false;
        reject(invalid, "RHI-BINDLESS-PRODUCED-PROGRAM-OPT-IN");
        references[operations[firstPush].firstReference].sourceIndex = 100u;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-ROOT");
        references[operations[firstPush].firstReference].sourceIndex = 200u;
        operations[firstPush].kind = K::BufferBarrier;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-BARRIER-SCOPE");
        operations[firstPush].kind = K::PushConstants;
        operations[firstDraw].kind = K::DrawIndexed;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-OPERATION");
        operations[firstDraw].kind = K::DrawIndexedIndirect;
        operations[secondPass].kind = K::BeginComputePass;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-PRODUCER-ORDER");
        operations[secondPass].kind = K::BeginRenderPass;
        references[operations[clear].firstReference].sourceIndex = 201u;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-CLEAR");
        references[operations[clear].firstReference].sourceIndex = 100u;
        ++operations[firstDraw].commandIndex;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-COMMAND-REFERENCES");
        --operations[firstDraw].commandIndex;
        const auto saved = operations[firstDraw].firstReference;
        operations[firstDraw].firstReference = SIZE_MAX;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-COMMAND-REFERENCES");
        operations[firstDraw].firstReference = saved;
        operations.back().kind = K::EndTransferPass;
        reject(budget, "RHI-BINDLESS-PRODUCED-PROGRAM-TRANSFER-END");
        operations.back().kind = K::Finish;
        assert(!inspect(budget).diagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        {
            // Two independent snapshot latches must both be cleared before
            // the first producer. This remains a structural value fixture;
            // original Root/copy/custody identities are checked separately.
            auto secondClear = operations.front();
            secondClear.firstReference = references.size();
            references.push_back({200u});
            operations.insert(operations.begin() + 1u, secondClear);
            auto extraCopy = operations[operations.size() - 3u];
            operations.insert(operations.end() - 2u, extraCopy);
            for (std::size_t i = 0u; i < operations.size(); ++i)
                operations[i].commandIndex = i;
            const std::array<std::size_t, 3u> faults{SIZE_MAX, 100u, 200u};
            const Contract::Budget scoped{2u, true, operations.size(), 1024u};
            const auto inspectScoped = [&](const Contract::Budget& selected) {
                return Contract::inspectOperations(
                    operations,
                    references,
                    roots.size(),
                    [&](std::size_t i) noexcept {
                        return roots[i];
                    },
                    100u,
                    3u,
                    selected,
                    faults.size(),
                    [&](std::size_t i) noexcept {
                        return faults[i];
                    });
            };
            const auto currentAllocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!inspectScoped(scoped).diagnostic);
            references[operations[1u].firstReference].sourceIndex = 100u;
            assert(inspectScoped(scoped).diagnostic);
            references[operations[1u].firstReference].sourceIndex = 201u;
            assert(inspectScoped(scoped).diagnostic);
            references[operations[1u].firstReference].sourceIndex = 200u;
            operations[1u].kind = K::BufferBarrier;
            assert(inspectScoped(scoped).diagnostic);
            operations[1u].kind = K::ClearSnapshotFault;
            auto shortFaults = scoped;
            shortFaults.maxFaultVisits = 1u;
            assert(inspectScoped(shortFaults).diagnostic);
            auto legacy = scoped;
            legacy.contractVersion = 1u;
            legacy.maxFaultVisits = 0u;
            assert(inspectScoped(legacy).diagnostic);
            assert(!inspectScoped(scoped).diagnostic);
            assert(core::defaultCpuAllocator().stats().allocationCount == currentAllocations);
        }
    }
};
} // namespace arp::test
