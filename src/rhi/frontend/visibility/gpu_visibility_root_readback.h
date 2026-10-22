#pragma once
#include "core/encoding/unsigned_element_span.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "rhi/frontend/visibility/gpu_visibility_root_producer.h"

namespace arp::rhi::visibility {
// Join the two original copy results to the actual producer and consumers.
// This is cold correspondence, never a second completion or publication owner.
class GpuVisibilityRootReadbackContract final {
public:
    using Binding = rhi::OriginalCommandBufferReadbackBinding;
    using Producer = GpuVisibilityRootProducerContract;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxCommandVisits = 0u, maxResultVisits = 0u;
        rhi::OriginalCommandReadbackResultContract::Budget results;
    };

    struct Report {
        const char* diagnostic = nullptr;
        std::size_t controlBinding = SIZE_MAX, faultBinding = SIZE_MAX;
        std::uint64_t commandVisits = 0u, declaredBytes = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "PCG-GPU-VISIBILITY-READBACK-VERSION";
        if (!b.optIn)
            return "PCG-GPU-VISIBILITY-READBACK-OPT-IN";
        if (!b.maxCommandVisits || !b.maxResultVisits)
            return "PCG-GPU-VISIBILITY-READBACK-BUDGET";
        return rhi::OriginalCommandReadbackResultContract::validateIntent(b.results);
    }

    static Report inspect(const Producer::Report& producer,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& stream,
        const GpuVisibilityLayoutContract::Input& layout,
        const std::shared_ptr<const Binding>* bindings,
        std::size_t count,
        const Budget& budget) noexcept
    {
        using namespace rhi;
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (const auto* id = validateBudget(budget))
            return reject(id);
        if (producer.diagnostic || !producer.indirectRecords
            || producer.dispatches.back() == SIZE_MAX || producer.sources[7u] == SIZE_MAX)
            return reject("PCG-GPU-VISIBILITY-READBACK-PRODUCER");
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(stream))
            return reject(id);
        if (!bindings || count != 2u)
            return reject("PCG-GPU-VISIBILITY-READBACK-COUNT");
        const auto joined = OriginalCommandBufferReadbackResultContract::inspect(
            bindings, count, budget.maxResultVisits, budget.results);
        if (joined.diagnostic)
            return reject(joined.diagnostic);
        const auto& inventory =
            stream->originalInputs()->originalScalars()->originalUsage()->originalSources();
        if (producer.roots[0u] >= inventory->sources().size())
            return reject("PCG-GPU-VISIBILITY-READBACK-PRODUCER");
        const auto* producerRoot =
            std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                &inventory->sources()[producer.roots[0u]]);
        if (!producerRoot)
            return reject("PCG-GPU-VISIBILITY-READBACK-PRODUCER");
        Report result;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& binding = bindings[i];
            if (!core::SharedOwnerProjection::sameOwner(stream, binding->originalProgram()))
                return reject("PCG-GPU-VISIBILITY-READBACK-STREAM");
            if (binding->rootSourceIndex() >= inventory->sources().size())
                return reject("PCG-GPU-VISIBILITY-READBACK-DOMAIN");
            const auto* bindingRoot =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                    &inventory->sources()[binding->rootSourceIndex()]);
            if (!bindingRoot
                || !core::SharedOwnerProjection::sameOwner(
                    (*producerRoot)->originalSnapshot(), (*bindingRoot)->originalSnapshot())
                || !core::SharedOwnerProjection::sameOwner(
                    (*producerRoot)->authorization(), (*bindingRoot)->authorization()))
                return reject("PCG-GPU-VISIBILITY-READBACK-DOMAIN");
            const auto* descriptor =
                stream->originalInputs()->descriptorAt(binding->commandIndex());
            const auto* copy =
                descriptor ? std::get_if<TransferCopyArguments>(descriptor) : nullptr;
            BufferCopyDesc fields;
            if (!copy || !copy->write(fields))
                return reject("PCG-GPU-VISIBILITY-READBACK-COPY");
            if (binding->isOriginalSnapshotFaultReadback()) {
                if (result.faultBinding != SIZE_MAX || fields.sourceOffsetBytes
                    || fields.destinationOffsetBytes || fields.sizeBytes != sizeof(std::uint32_t))
                    return reject("PCG-GPU-VISIBILITY-READBACK-FAULT");
                result.faultBinding = i;
            } else {
                // Read the complete Control ABI. A short prefix could hide a
                // failed cascade. Exact-sized destinations also permit WebGPU
                // native whole-buffer mapping without an invented range map.
                if ((layout.cascades != 1u && layout.cascades != 3u)
                    || result.controlBinding != SIZE_MAX
                    || binding->rootSourceIndex() != producer.roots[3u]
                    || inventory->originalLeaseIndex(binding->originalCopySource())
                        != producer.sources[7u]
                    || fields.sourceOffsetBytes || fields.destinationOffsetBytes
                    || fields.sizeBytes != (1u + layout.cascades) * sizeof(std::uint32_t)
                    || binding->originalReadbackSource()->layout().sizeBytes != fields.sizeBytes)
                    return reject("PCG-GPU-VISIBILITY-READBACK-CONTROL");
                result.controlBinding = i;
            }
        }
        if (result.controlBinding == SIZE_MAX || result.faultBinding == SIZE_MAX)
            return reject("PCG-GPU-VISIBILITY-READBACK-ROLES");
        const auto order = inspectCopyOrder(stream->operations(),
            producer.dispatches.back(),
            bindings[result.controlBinding]->commandIndex(),
            bindings[result.faultBinding]->commandIndex(),
            budget.maxCommandVisits);
        if (order.diagnostic)
            return reject(order.diagnostic);
        result.commandVisits = order.commandVisits;
        result.declaredBytes = joined.required.declaredReadbackBytes;
        return result;
    }

    // Positional checks are shared with CPU tests. The public inspector above
    // additionally authenticates the original stream, graph, copies and owners.
    template <class Operations>
    static Report inspectCopyOrder(const Operations& operations,
        std::size_t finalize,
        std::size_t controlCopy,
        std::size_t faultCopy,
        std::uint64_t maxVisits) noexcept
    {
        using K = rhi::CommandUsageKind;
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (finalize == SIZE_MAX || controlCopy == faultCopy || controlCopy == SIZE_MAX
            || faultCopy == SIZE_MAX)
            return reject("PCG-GPU-VISIBILITY-READBACK-ORDER-INPUTS");
        Report result;
        bool finalSeen = false, consumerSeen = false, render = false, transfer = false;
        bool controlSeen = false, faultSeen = false, finished = false;
        for (const auto& op : operations) {
            if (result.commandVisits == maxVisits)
                return reject("PCG-GPU-VISIBILITY-READBACK-COMMAND-CAPACITY");
            if (op.commandIndex != result.commandVisits || finished)
                return reject("PCG-GPU-VISIBILITY-READBACK-COMMAND-ORDER");
            ++result.commandVisits;
            if (op.commandIndex == finalize) {
                if (op.kind != K::Dispatch || controlSeen || faultSeen)
                    return reject("PCG-GPU-VISIBILITY-READBACK-FINALIZE");
                finalSeen = true;
            }
            if (op.kind == K::BeginRenderPass) {
                if (!finalSeen || render || transfer || controlSeen || faultSeen)
                    return reject("PCG-GPU-VISIBILITY-READBACK-CONSUMER-ORDER");
                render = true;
            } else if (op.kind == K::DrawIndexedIndirect) {
                if (!render || !finalSeen)
                    return reject("PCG-GPU-VISIBILITY-READBACK-CONSUMER-ORDER");
                consumerSeen = true;
            } else if (op.kind == K::EndRenderPass) {
                if (!render)
                    return reject("PCG-GPU-VISIBILITY-READBACK-CONSUMER-ORDER");
                render = false;
            } else if (op.kind == K::BeginTransferPass) {
                if (render || transfer)
                    return reject("PCG-GPU-VISIBILITY-READBACK-TRANSFER-ORDER");
                transfer = true;
            } else if (op.kind == K::EndTransferPass) {
                if (!transfer)
                    return reject("PCG-GPU-VISIBILITY-READBACK-TRANSFER-ORDER");
                transfer = false;
            } else if (op.kind == K::BufferCopy) {
                if (!finalSeen || !consumerSeen || render || !transfer
                    || (op.commandIndex != controlCopy && op.commandIndex != faultCopy))
                    return reject("PCG-GPU-VISIBILITY-READBACK-COPY-ORDER");
                controlSeen = controlSeen || op.commandIndex == controlCopy;
                faultSeen = faultSeen || op.commandIndex == faultCopy;
            } else if (op.kind == K::ClearSnapshotFault || op.kind == K::ClearFaultBuffer
                || op.kind == K::BeginComputePass || op.kind == K::Dispatch
                || op.kind == K::DispatchIndirect) {
                if (controlSeen || faultSeen || (finalSeen && op.commandIndex != finalize))
                    return reject("PCG-GPU-VISIBILITY-READBACK-WRITE-AFTER-FINALIZE");
            } else if (op.kind == K::Finish) {
                if (render || transfer || !controlSeen || !faultSeen)
                    return reject("PCG-GPU-VISIBILITY-READBACK-INCOMPLETE");
                finished = true;
            }
        }
        return finalSeen && consumerSeen && controlSeen && faultSeen && finished
            ? result
            : reject("PCG-GPU-VISIBILITY-READBACK-INCOMPLETE");
    }

    // Inspect only bytes released by the original completion publisher. A
    // physical draw count is groupCount, including holes; faults suppress it.
    static const char* inspectCompletedBytes(const void* control,
        std::size_t controlBytes,
        const void* fault,
        std::size_t faultBytes,
        const GpuVisibilityLayoutContract::Input& layout) noexcept
    {
        if (!control || !fault || (layout.cascades != 1u && layout.cascades != 3u)
            || !layout.groupCount || layout.groupCount > layout.groupCapacity
            || controlBytes != (1u + layout.cascades) * sizeof(std::uint32_t)
            || faultBytes != sizeof(std::uint32_t))
            return "PCG-GPU-VISIBILITY-READBACK-RESULT-SIZE";
        using Words = core::UnsignedElementSpan;
        const Words controls(
            control, controlBytes, Words::Width::Uint32, Words::ByteOrder::LittleEndian);
        const Words faults(fault, faultBytes, Words::Width::Uint32, Words::ByteOrder::LittleEndian);
        std::uint32_t status = 0u, resourceFault = 0u;
        if (!controls.read(0u, status) || !faults.read(0u, resourceFault))
            return "PCG-GPU-VISIBILITY-READBACK-RESULT-SIZE";
        // A resource fault may arise in finalize (counts suppressed) or in a
        // later graphics consumer (counts unchanged). Neither is clean output.
        if (resourceFault)
            return "PCG-GPU-VISIBILITY-READBACK-RESOURCE-FAULT";
        for (std::uint32_t cascade = 0u; cascade < layout.cascades; ++cascade) {
            std::uint32_t count = 0u;
            if (!controls.read(1u + cascade, count) || count != (status ? 0u : layout.groupCount))
                return "PCG-GPU-VISIBILITY-READBACK-DRAW-COUNT";
        }
        return status ? "PCG-GPU-VISIBILITY-READBACK-PRODUCER-FAULT" : nullptr;
    }
};
} // namespace arp::rhi::visibility
