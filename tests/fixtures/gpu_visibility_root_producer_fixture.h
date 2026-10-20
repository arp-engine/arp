#pragma once
#include "pcg/gpu_visibility_root_producer.h"
#include "pcg/gpu_visibility_root_readback.h"

#include <cassert>

namespace arp::test {
class GpuVisibilityRootProducerFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Producer = pcg::GpuVisibilityRootProducerContract;
        using K = CommandUsageKind;
        using Scalar = CommandScalarArguments;
        Producer::Report sources;
        for (std::size_t i = 0u; i < sources.roots.size(); ++i)
            sources.roots[i] = 100u + i;
        for (std::size_t i = 0u; i < sources.sources.size(); ++i)
            sources.sources[i] = i;
        pcg::GpuVisibilityLayoutContract::Input input;
        input.instanceCount = 4u;
        input.groupCount = 2u;
        input.instanceCapacity = 5u;
        input.groupCapacity = 3u;
        input.cascades = 3u;
        core::pmr::Vector<OriginalCommandStreamProgram::Operation> operations;
        core::pmr::Vector<OriginalCommandUsage::Reference> references;
        core::pmr::Vector<CommandVariableArguments::Input> arguments;
        const auto append = [&](K kind, std::initializer_list<std::uint32_t> refs, Scalar scalar) {
            OriginalCommandStreamProgram::Operation op;
            op.commandIndex = operations.size();
            op.kind = kind;
            op.firstReference = references.size();
            op.referenceCount = refs.size();
            operations.push_back(op);
            for (const auto source : refs)
                references.push_back({source});
            CommandVariableArguments::Input fields{scalar, {}};
            arguments.push_back(fields);
        };
        const auto none = [](K kind) {
            return *Scalar::capture(kind, Scalar::None{});
        };
        for (std::size_t phase = 0u; phase < 4u; ++phase) {
            append(K::BeginComputePass, {}, none(K::BeginComputePass));
            append(K::BindPreparedComputeBinding,
                {static_cast<std::uint32_t>(sources.roots[phase])},
                none(K::BindPreparedComputeBinding));
            pcg::GpuVisibilityDispatchContract::Dispatch dispatch;
            assert(!pcg::GpuVisibilityDispatchContract::derive(input,
                static_cast<pcg::GpuVisibilityDispatchContract::Phase>(phase),
                64u,
                dispatch));
            append(K::Dispatch,
                {},
                *Scalar::capture(
                    K::Dispatch, Scalar::Dispatch{dispatch.x, dispatch.y, dispatch.z}));
            append(K::EndComputePass, {}, none(K::EndComputePass));
        }
        append(K::BeginRenderPass, {}, none(K::BeginRenderPass));
        for (std::uint32_t cascade = 0u; cascade < 3u; ++cascade)
            append(K::DrawIndexedIndirect,
                {5u},
                *Scalar::capture(
                    K::DrawIndexedIndirect, Scalar::DrawIndirect{cascade * 3u * 20u, 2u, 20u}));
        append(K::EndRenderPass, {}, none(K::EndRenderPass));
        const Producer::Budget budget{1u, true, 48u, operations.size()};
        const auto inspect = [&](const Producer::Budget& b) {
            return Producer::inspectCommandOrder(
                sources, input, 64u, b, operations, references, arguments, 20u, 21u);
        };
        const auto baseline = core::defaultCpuAllocator().stats().allocationCount;
        const auto accepted = inspect(budget);
        assert(!accepted.diagnostic && accepted.commandVisits == operations.size()
            && accepted.indirectCalls == 3u && accepted.indirectRecords == 6u
            && accepted.dispatches[0u] == 2u && accepted.dispatches[3u] == 14u
            && !accepted.nativeExecutionProven());
        const auto reject = [&](const Producer::Budget& b, const char* expected) {
            const auto report = inspect(b);
            assert(report.diagnostic && core::StringView(report.diagnostic) == expected
                && !report.commandVisits && !report.indirectCalls && !report.indirectRecords
                && report.roots[0u] == SIZE_MAX && report.sources[0u] == SIZE_MAX);
        };
        auto less = budget;
        --less.maxCommandVisits;
        reject(less, "PCG-GPU-VISIBILITY-ROOT-COMMAND-CAPACITY");
        const auto originalDispatch = arguments[14u].scalar;
        arguments[14u].scalar = *Scalar::capture(K::Dispatch, Scalar::Dispatch{2u, 1u, 1u});
        reject(budget, "PCG-GPU-VISIBILITY-DISPATCH-COVERAGE");
        arguments[14u].scalar = originalDispatch;
        operations[14u].kind = K::BeginRenderPass;
        reject(budget, "PCG-GPU-VISIBILITY-ROOT-CONSUMER-ORDER");
        operations[14u].kind = K::Dispatch;
        references[3u].sourceIndex = sources.roots[2u];
        reject(budget, "PCG-GPU-VISIBILITY-ROOT-BIND-ORDER");
        references[3u].sourceIndex = sources.roots[3u];
        const auto originalDraw = arguments[18u].scalar;
        // Physical cascade stride is groupCapacity, never groupCount.
        arguments[18u].scalar =
            *Scalar::capture(K::DrawIndexedIndirect, Scalar::DrawIndirect{40u, 2u, 20u});
        reject(budget, "PCG-GPU-VISIBILITY-ROOT-INDIRECT-CORRESPONDENCE");
        arguments[18u].scalar = originalDraw;
        operations[17u].kind = K::ClearSnapshotFault;
        reject(budget, "PCG-GPU-VISIBILITY-ROOT-FAULT-RESET-AFTER-PRODUCER");
        operations[17u].kind = K::DrawIndexedIndirect;
        operations[14u].kind = K::DispatchIndirect;
        reject(budget, "PCG-GPU-VISIBILITY-ROOT-FOREIGN-PRODUCER");
        operations[14u].kind = K::Dispatch;
        --operations[17u].commandIndex;
        reject(budget, "PCG-GPU-VISIBILITY-ROOT-COMMAND-REFERENCES");
        ++operations[17u].commandIndex;
        assert(!inspect(budget).diagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == baseline);
        // The original results must both observe the final producer and every
        // graphics consumer, even when the fault result is requested first.
        using Readbacks = pcg::GpuVisibilityRootReadbackContract;
        append(K::BeginTransferPass, {}, none(K::BeginTransferPass));
        const auto controlCopy = operations.size();
        append(K::BufferCopy, {7u, 30u}, none(K::BufferCopy));
        const auto faultCopy = operations.size();
        append(K::BufferCopy, {21u, 31u}, none(K::BufferCopy));
        append(K::EndTransferPass, {}, none(K::EndTransferPass));
        append(K::Finish, {}, none(K::Finish));
        const auto readbackAllocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto inspectReadbacks =
            [&](std::size_t control, std::size_t fault, std::uint64_t visits) {
                return Readbacks::inspectCopyOrder(operations, 14u, control, fault, visits);
            };
        assert(!inspectReadbacks(controlCopy, faultCopy, operations.size()).diagnostic);
        assert(!inspectReadbacks(faultCopy, controlCopy, operations.size()).diagnostic);
        const auto failed = inspectReadbacks(controlCopy, faultCopy, operations.size() - 1u);
        assert(failed.diagnostic && !failed.commandVisits);
        assert(inspectReadbacks(controlCopy, controlCopy, operations.size()).diagnostic);
        assert(inspectReadbacks(17u, faultCopy, operations.size()).diagnostic);
        operations[controlCopy].kind = K::Dispatch;
        assert(inspectReadbacks(controlCopy, faultCopy, operations.size()).diagnostic);
        operations[controlCopy].kind = K::BufferCopy;
        operations.back().kind = K::BeginRenderPass;
        assert(inspectReadbacks(controlCopy, faultCopy, operations.size()).diagnostic);
        operations.back().kind = K::Finish;
        operations[controlCopy - 1u].kind = K::BeginRenderPass;
        assert(inspectReadbacks(controlCopy, faultCopy, operations.size()).diagnostic);
        operations[controlCopy - 1u].kind = K::BeginTransferPass;
        assert(!inspectReadbacks(controlCopy, faultCopy, operations.size()).diagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == readbackAllocations);
    }
};
} // namespace arp::test
