#pragma once

#include "core/memory/initialized_byte_buffer.h"
#include "shader/webgl_shader_program_artifact.h"

#include <cstring>

namespace arp::shader {
// CPU-owned bytes only: no API accepts a native buffer or GPU-written source.
// Partial CPU updates are explicit; untouched bytes cannot pass initialized().
class WebGlCpuUniformData final {
public:
    explicit WebGlCpuUniformData(std::size_t bytes, bool atomicPush = false)
        : atomicPush_(atomicPush), bytes_(bytes)
    {}

    const char* update(std::size_t offset, const void* data, std::size_t bytes) noexcept
    {
        if (atomicPush_ && (offset || bytes != bytes_.size()))
            return "SHADER-WEBGL1-PUSH-FULL-BLOCK";
        return bytes_.write(offset, data, bytes) ? nullptr : "SHADER-WEBGL1-CPU-UPDATE-RANGE";
    }

    bool initialized(std::size_t offset, std::size_t size) const noexcept
    {
        return bytes_.initialized(offset, size);
    }

    const std::uint8_t* data() const noexcept { return bytes_.data(); }

    std::size_t size() const noexcept { return bytes_.size(); }

    bool atomicPush() const noexcept { return atomicPush_; }

private:
    bool atomicPush_;
    core::InitializedByteBuffer bytes_;
};

class WebGlUniformSnapshots final {
public:
    struct Budget {
        std::uint32_t maxDraws = 0;
        std::uint64_t maxBytes = 0;
    };

    struct Binding {
        bool push = false;
        std::uint32_t set = 0, binding = 0;
        const WebGlCpuUniformData* source = nullptr;
        std::uint64_t offset = 0, range = 0, dynamicOffset = 0, dynamicAlignment = 0;
        // Original resource CPU bytes. Exactly one source kind is required.
        // This adapter shares the same initialization/range/copy algorithm.
        const core::InitializedByteBuffer* cpuBytes = nullptr;

        std::size_t sourceSize() const noexcept
        {
            return source ? source->size() : cpuBytes ? cpuBytes->size() : 0;
        }

        const std::uint8_t* sourceData() const noexcept
        {
            return source ? source->data() : cpuBytes ? cpuBytes->data() : nullptr;
        }

        bool initialized(std::size_t offset, std::size_t size) const noexcept
        {
            return source ? source->initialized(offset, size)
                          : cpuBytes && cpuBytes->initialized(offset, size);
        }
    };

    struct Slot {
        bool push = false;
        std::uint32_t set = 0, binding = 0, size = 0;
        std::size_t offset = 0;
    };

    const char* prepare(std::shared_ptr<const WebGlShaderProgramArtifact> program, Budget budget)
    {
        if (program_)
            return "SHADER-WEBGL1-SNAPSHOT-ALREADY-PREPARED";
        if (auto id = WebGlShaderProgramArtifact::validateOriginal(program))
            return id;
        if (!budget.maxDraws || !budget.maxBytes)
            return "SHADER-WEBGL1-SNAPSHOT-BUDGET";
        core::pmr::Vector<Slot> slots{&core::defaultCpuMemoryResource()};
        std::uint64_t stride = 0;
        for (auto* stage : {&program->vertex(), &program->fragment()})
            for (const auto& block : stage->blocks) {
                bool exists = false;
                for (const auto& slot : slots)
                    if (slot.push == block.push && slot.set == block.set
                        && slot.binding == block.binding) {
                        if (slot.size != block.size)
                            return "SHADER-WEBGL1-CROSS-STAGE-BLOCK-SIZE";
                        exists = true;
                    }
                if (exists)
                    continue;
                if (stride > budget.maxBytes || block.size > budget.maxBytes - stride)
                    return "SHADER-WEBGL1-SNAPSHOT-BUDGET";
                slots.push_back({block.push,
                    block.set,
                    block.binding,
                    block.size,
                    static_cast<std::size_t>(stride)});
                stride += block.size;
            }
        if ((stride && budget.maxDraws > budget.maxBytes / stride) || budget.maxBytes > SIZE_MAX)
            return "SHADER-WEBGL1-SNAPSHOT-BUDGET";
        // Fixed backing allocated at cold preparation. record() never allocates.
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
        bytes.resize(static_cast<std::size_t>(stride * budget.maxDraws));
        bytes_ = std::move(bytes);
        slots_ = std::move(slots);
        program_ = std::move(program);
        stride_ = stride;
        maxDraws_ = budget.maxDraws;
        return nullptr;
    }

    const char* validateRecord(const Binding* bindings, std::size_t count) const noexcept
    {
        if (!program_ || draws_ >= maxDraws_)
            return "SHADER-WEBGL1-SNAPSHOT-DRAW-BUDGET";
        if ((!bindings && count) || count != slots_.size())
            return "SHADER-WEBGL1-SNAPSHOT-COVERAGE";
        // Complete validation precedes any copy or cursor mutation.
        for (const auto& slot : slots_) {
            const Binding* found = nullptr;
            for (std::size_t i = 0; i < count; ++i) {
                const auto& b = bindings[i];
                if (b.push != slot.push || b.set != slot.set || b.binding != slot.binding)
                    continue;
                if (found)
                    return "SHADER-WEBGL1-SNAPSHOT-DUPLICATE";
                found = &b;
            }
            if (!found || bool(found->source) == bool(found->cpuBytes))
                return "SHADER-WEBGL1-SNAPSHOT-SOURCE";
            const auto& b = *found;
            if (!b.dynamicAlignment || b.dynamicOffset % b.dynamicAlignment
                || b.offset % b.dynamicAlignment)
                return "SHADER-WEBGL1-SNAPSHOT-DYNAMIC-ALIGNMENT";
            if (b.offset > b.sourceSize() || b.dynamicOffset > b.sourceSize() - b.offset
                || b.range < slot.size || b.range > b.sourceSize() - b.offset - b.dynamicOffset)
                return "SHADER-WEBGL1-SNAPSHOT-RANGE";
            if (slot.push
                && ((!b.source || !b.source->atomicPush()) || b.offset || b.dynamicOffset
                    || b.range != slot.size || b.sourceSize() != slot.size))
                return "SHADER-WEBGL1-PUSH-FULL-BLOCK";
            if (!b.initialized(b.offset + b.dynamicOffset, slot.size))
                return "SHADER-WEBGL1-SNAPSHOT-UNINITIALIZED";
        }
        return nullptr;
    }

    const char* record(const Binding* bindings, std::size_t count, std::uint32_t& draw) noexcept
    {
        draw = UINT32_MAX;
        if (const auto* id = validateRecord(bindings, count))
            return id;
        for (const auto& slot : slots_)
            for (std::size_t i = 0; i < count; ++i) {
                const auto& b = bindings[i];
                if (b.push == slot.push && b.set == slot.set && b.binding == slot.binding)
                    std::memcpy(bytes_.data() + draws_ * stride_ + slot.offset,
                        b.sourceData() + b.offset + b.dynamicOffset,
                        slot.size);
            }
        draw = draws_++;
        return nullptr;
    }

    const std::uint8_t* data(std::uint32_t draw, const Slot& slot) const noexcept
    {
        if (draw >= draws_)
            return nullptr;
        // A caller-synthesized Slot is not a range authority.
        for (const auto& original : slots_)
            if (&original == &slot)
                return bytes_.data() + draw * stride_ + slot.offset;
        return nullptr;
    }

    const auto& slots() const noexcept { return slots_; }

    std::uint32_t size() const noexcept { return draws_; }

    std::size_t capacityBytes() const noexcept { return bytes_.size(); }

    std::size_t recordedBytes() const noexcept { return draws_ * stride_; }

private:
    std::shared_ptr<const WebGlShaderProgramArtifact> program_;
    core::pmr::Vector<Slot> slots_{&core::defaultCpuMemoryResource()};
    core::ByteBuffer bytes_{&core::defaultCpuMemoryResource()};
    std::size_t stride_ = 0;
    std::uint32_t draws_ = 0, maxDraws_ = 0;
};
} // namespace arp::shader
