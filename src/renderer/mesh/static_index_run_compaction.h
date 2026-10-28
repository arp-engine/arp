#pragma once

#include "core/algorithm/index_runs.h"
#include "core/cpu_allocator.h"
#include "rhi/gpu_device.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace arp::renderer {
// PrototypeGpuCopyV1. Only relocates admitted immutable triangle runs. The
// caller owns source lifetime and an authentic completion lease for every slot.
// prepare/clear require the caller's normal GPU drain (or device-loss path).
// Destination barriers/dependencies belong to the graph; source read states
// are restored within recordCopies on the same graphics queue.
class StaticIndexRunCompaction final {
public:
    struct Binding {
        rhi::GpuBuffer* source = nullptr;
        core::IndexRun parent;
    };

    struct Key {
        std::uint64_t owner = 0, revision = 0;

        bool operator==(const Key& other) const
        {
            return owner == other.owner && revision == other.revision;
        }
    };

    struct Budget {
        std::uint32_t slots = 0;
        std::size_t runs = 0;
        std::uint64_t residentBytes = 0;
    };

    struct Copy {
        rhi::BufferCopyDesc range;
    };

    void prepare(rhi::GpuDevice& device, const core::pmr::Vector<Binding>& bindings, Budget budget)
    {
        if (prepared() || bindings.empty() || !budget.slots || !budget.runs
            || !budget.residentBytes)
            fail("INDEX-COMPACTION-PREPARE-INVALID");
        std::uint64_t indices = 0;
        for (const auto& binding : bindings) {
            if (!binding.source || !binding.parent.count || binding.parent.first % 3u
                || binding.parent.count % 3u
                || !hasUsage(binding.source->desc().usage, rhi::ResourceUsage::CopySrc)
                || !hasUsage(binding.source->desc().usage, rhi::ResourceUsage::IndexBuffer)
                || binding.parent.first > binding.source->desc().sizeBytes / 4u
                || binding.parent.count
                    > binding.source->desc().sizeBytes / 4u - binding.parent.first
                || binding.parent.count > UINT32_MAX - indices)
                fail("INDEX-COMPACTION-BINDING-INVALID");
            indices += binding.parent.count;
        }
        if (indices > budget.residentBytes / 4u / budget.slots)
            fail("INDEX-COMPACTION-RESIDENT-BUDGET");
        bindings_ = bindings;
        offsets_.reserve(bindings.size());
        std::size_t offset = 0;
        for (const auto& binding : bindings) {
            offsets_.push_back(offset);
            offset += binding.parent.count;
            if (std::find(sources_.begin(), sources_.end(), binding.source) == sources_.end())
                sources_.push_back(binding.source);
        }
        slots_.resize(budget.slots);
        for (auto& slot : slots_) {
            slot.buffer = core::shareUnique(device.createBuffer({"StaticIndexCompaction",
                indices * 4u,
                rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::ShaderRead}));
            if (!slot.buffer)
                fail("INDEX-COMPACTION-BUFFER-CREATE");
            slot.accepted.resize(bindings.size());
            slot.candidate.resize(bindings.size());
            slot.active.resize(bindings.size());
            slot.copies.reserve(budget.runs);
            slot.acceptedRuns.reserve(budget.runs);
            slot.candidateRuns.reserve(budget.runs);
            slot.selectedRuns.reserve(budget.runs);
            slot.touched.resize(sources_.size());
        }
        budget_ = budget;
        prepared_ = true;
    }

    bool prepared() const noexcept { return prepared_; }

    void clear() { *this = StaticIndexRunCompaction{}; }

    // The caller has checked the selected slot's completion lease before begin.
    void begin(std::uint32_t index)
    {
        auto& slot = get(index);
        std::fill(slot.active.begin(), slot.active.end(), false);
        std::fill(slot.touched.begin(), slot.touched.end(), false);
        // Inactive bindings keep their cached keys/runs so a disappearing
        // carrier can reappear without losing the still-valid GPU segment.
        slot.candidate = slot.accepted;
        slot.selectedRuns.clear();
        slot.copies.clear();
        slot.begun = true;
        slot.sealed = false;
    }

    core::IndexRun select(std::uint32_t index,
        std::size_t bindingIndex,
        Key key,
        rhi::GpuBuffer& source,
        core::IndexRun parent,
        const core::IndexRun* runs,
        std::size_t runCount)
    {
        auto& slot = get(index);
        if (!slot.begun || bindingIndex >= bindings_.size() || !key.owner || !key.revision)
            fail("INDEX-COMPACTION-SELECTION-INVALID");
        const auto& binding = bindings_[bindingIndex];
        if (slot.active[bindingIndex] || binding.source != &source
            || binding.parent.first != parent.first || binding.parent.count != parent.count)
            fail("INDEX-COMPACTION-SELECTION-BINDING");
        const core::CheckedIndexRunSpan checked(runs, runCount, {0, runCount}, parent, UINT32_MAX);
        std::size_t count = 0;
        for (const auto& run : checked) {
            if ((run.first - parent.first) % 3u || run.count % 3u)
                fail("INDEX-COMPACTION-TRIANGLES-INVALID");
            count += run.count;
        }
        const auto& old = slot.accepted[bindingIndex];
        if (runCount > budget_.runs - slot.selectedRuns.size())
            fail("INDEX-COMPACTION-RUN-BUDGET");
        const bool same = old.key == key;
        if (same) {
            if (old.runs.count != runCount)
                fail("INDEX-COMPACTION-REVISION-RUN-MISMATCH");
            for (std::size_t i = 0; i < runCount; ++i) {
                const auto& previous = slot.acceptedRuns[old.runs.first + i];
                if (previous.first != runs[i].first || previous.count != runs[i].count)
                    fail("INDEX-COMPACTION-REVISION-RUN-MISMATCH");
            }
        } else {
            if (runCount > budget_.runs - slot.copies.size())
                fail("INDEX-COMPACTION-COPY-BUDGET");
            std::size_t destination = offsets_[bindingIndex];
            for (const auto& run : checked) {
                slot.copies.push_back({{source.id(),
                    slot.buffer->id(),
                    std::uint64_t(run.first) * 4u,
                    std::uint64_t(destination) * 4u,
                    std::uint64_t(run.count) * 4u}});
                destination += run.count;
            }
            for (std::size_t i = 0; i < sources_.size(); ++i)
                slot.touched[i] = slot.touched[i] || (sources_[i] == &source && runCount);
        }
        const auto selectedFirst = slot.selectedRuns.size();
        for (const auto& run : checked)
            slot.selectedRuns.push_back(run);
        slot.candidate[bindingIndex] = {key, {selectedFirst, runCount}, count};
        slot.active[bindingIndex] = true;
        // Rebuild all candidate run spans once in seal(), avoiding append-only
        // cache growth over camera movement and independent carrier updates.
        return {offsets_[bindingIndex], count};
    }

    // Preserve inactive cached selections without append-only growth. The exact
    // validated run bytes from select also become the accepted cache identity.
    void seal(std::uint32_t index)
    {
        auto& slot = get(index);
        if (!slot.begun)
            fail("INDEX-COMPACTION-SEAL-INVALID");
        slot.candidateRuns.clear();
        for (std::size_t i = 0; i < bindings_.size(); ++i) {
            const auto first = slot.candidateRuns.size();
            if (slot.active[i]) {
                const auto selected = slot.candidate[i].runs;
                if (selected.count > budget_.runs - first)
                    fail("INDEX-COMPACTION-RUN-BUDGET");
                for (std::size_t j = 0; j < selected.count; ++j)
                    slot.candidateRuns.push_back(slot.selectedRuns[selected.first + j]);
            } else {
                const auto old = slot.accepted[i].runs;
                if (old.count > budget_.runs - first)
                    fail("INDEX-COMPACTION-RUN-BUDGET");
                for (std::size_t j = 0; j < old.count; ++j)
                    slot.candidateRuns.push_back(slot.acceptedRuns[old.first + j]);
            }
            slot.candidate[i].runs = {first, slot.candidateRuns.size() - first};
        }
        slot.begun = false;
        slot.sealed = true;
    }

    bool readyForSubmission(std::uint32_t index) const noexcept
    {
        return prepared_ && index < slots_.size() && slots_[index].sealed;
    }

    bool accept(std::uint32_t index) noexcept
    {
        if (!readyForSubmission(index))
            return false;
        auto& slot = slots_[index];
        slot.accepted = slot.candidate;
        slot.acceptedRuns = slot.candidateRuns;
        return true;
    }

    const core::SharedPtr<rhi::GpuBuffer>& buffer(std::uint32_t index) const
    {
        return get(index).buffer;
    }

    const core::pmr::Vector<Copy>& copies(std::uint32_t index) const { return get(index).copies; }

    core::IndexRun selected(std::uint32_t index, std::size_t binding) const
    {
        const auto& slot = get(index);
        if (!slot.sealed || binding >= bindings_.size() || !slot.active[binding])
            fail("INDEX-COMPACTION-DRAW-BINDING");
        return {offsets_[binding], slot.candidate[binding].indexCount};
    }

    void recordCopies(std::uint32_t index, rhi::CommandEncoder& commands) const
    {
        const auto& slot = get(index);
        if (!slot.sealed)
            fail("INDEX-COMPACTION-NOT-SEALED");
        if (slot.copies.empty())
            return;
        const auto transition = [&](rhi::GpuBuffer& source, bool toCopy) {
            rhi::ResourceBarrierDesc barrier;
            barrier.resource = source.id();
            barrier.before = toCopy ? rhi::ResourceState::ShaderRead : rhi::ResourceState::CopySrc;
            barrier.after = toCopy ? rhi::ResourceState::CopySrc : rhi::ResourceState::ShaderRead;
            barrier.sourceStages =
                toCopy ? rhi::PipelineStage::VertexInput : rhi::PipelineStage::Transfer;
            barrier.destinationStages =
                toCopy ? rhi::PipelineStage::Transfer : rhi::PipelineStage::VertexInput;
            barrier.sourceAccess =
                toCopy ? rhi::ResourceAccess::IndexRead : rhi::ResourceAccess::TransferRead;
            barrier.destinationAccess =
                toCopy ? rhi::ResourceAccess::TransferRead : rhi::ResourceAccess::IndexRead;
            require(commands.transition(source, barrier));
        };
        for (std::size_t i = 0; i < sources_.size(); ++i)
            if (slot.touched[i])
                transition(*sources_[i], true);
        auto transfer = commands.beginTransferPass({"StaticIndexCompaction.Copy"});
        if (!transfer)
            fail("INDEX-COMPACTION-TRANSFER-BEGIN");
        for (const auto& copy : slot.copies)
            require(transfer->copyBufferToBuffer(copy.range));
        require(transfer->end());
        for (std::size_t i = 0; i < sources_.size(); ++i)
            if (slot.touched[i])
                transition(*sources_[i], false);
    }

private:
    struct Record {
        Key key;
        core::IndexRun runs;
        std::size_t indexCount = 0;
    };

    struct Slot {
        core::SharedPtr<rhi::GpuBuffer> buffer;
        core::pmr::Vector<Record> accepted{&core::defaultCpuMemoryResource()},
            candidate{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<core::IndexRun> acceptedRuns{&core::defaultCpuMemoryResource()},
            candidateRuns{&core::defaultCpuMemoryResource()},
            selectedRuns{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<bool> active = core::pmr::Vector<bool>(&core::defaultCpuMemoryResource());
        core::pmr::Vector<bool> touched =
            core::pmr::Vector<bool>(&core::defaultCpuMemoryResource());
        core::pmr::Vector<Copy> copies{&core::defaultCpuMemoryResource()};
        bool begun = false;
        bool sealed = false;
    };

    static bool hasUsage(rhi::ResourceUsage usage, rhi::ResourceUsage bit)
    {
        return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(bit)) != 0;
    }

    template <class Result> static void require(const Result& result)
    {
        if (!result.success)
            throw core::DiagnosticError(result.diagnostics.c_str());
    }

    [[noreturn]] static void fail(const char* message) { throw core::DiagnosticError(message); }

    Slot& get(std::uint32_t index)
    {
        if (!prepared_ || index >= slots_.size())
            fail("INDEX-COMPACTION-SLOT-INVALID");
        return slots_[index];
    }

    const Slot& get(std::uint32_t index) const
    {
        if (!prepared_ || index >= slots_.size())
            fail("INDEX-COMPACTION-SLOT-INVALID");
        return slots_[index];
    }

    core::pmr::Vector<Binding> bindings_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::size_t> offsets_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<rhi::GpuBuffer*> sources_ =
        core::pmr::Vector<rhi::GpuBuffer*>(&core::defaultCpuMemoryResource());
    core::pmr::Vector<Slot> slots_{&core::defaultCpuMemoryResource()};
    Budget budget_{};
    bool prepared_ = false;
};
} // namespace arp::renderer
