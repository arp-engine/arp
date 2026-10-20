#pragma once

#include "rhi/frontend/transfer_readback_statistics.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_resources.h"

#include <array>

namespace arp::rhi {

// One runtime-wide domain, independent of batch template capacity. Returns
// zero on exhaustion; callers must reject rather than reuse an old identity.
ARP_API std::uint64_t allocateTextureReadbackRequestId() noexcept;

// Consumer-side validation for TextureReadbackIdentity/1. This owns no GPU
// resources and performs no waits. The consumer declares a complete expected
// set before recording; missing/duplicate/stale/failed results latch failure.
// Capacity is a workload limit, never grown while commands execute. A terminal
// batch cannot be restarted to erase failures. Output correctness beyond
// numerical validity remains the consumer's domain-specific acceptance gate.
template <std::size_t Capacity> class TextureReadbackBatch final {
public:
    struct Counts {
        std::uint32_t expected = 0, scheduled = 0, completed = 0, valid = 0;
    };

    TextureReadbackBatch() = default;
    TextureReadbackBatch(const TextureReadbackBatch&) = delete;
    TextureReadbackBatch& operator=(const TextureReadbackBatch&) = delete;
    TextureReadbackBatch(TextureReadbackBatch&&) = delete;
    TextureReadbackBatch& operator=(TextureReadbackBatch&&) = delete;

    bool begin(std::uint64_t frameSerial, std::uint64_t generation)
    {
        if (begun_ || !frameSerial || !generation)
            return fail("RHI-READBACK-BATCH-BEGIN");
        begun_ = true;
        frameSerial_ = frameSerial;
        generation_ = generation;
        return !failed_;
    }

    bool declareExpected(std::string_view label)
    {
        if (!begun_ || sealed_ || failed_ || counts_.expected == Capacity || label.empty()
            || label.size() > 256u)
            return fail("RHI-READBACK-EXPECTED-INVALID");
        for (std::uint32_t i = 0; i < counts_.expected; ++i)
            if (entries_[i].label == label)
                return fail("RHI-READBACK-EXPECTED-DUPLICATE");
        const auto requestId = allocateTextureReadbackRequestId();
        if (!requestId)
            return fail("RHI-READBACK-REQUEST-ID-EXHAUSTED");
        auto& entry = entries_[counts_.expected];
        entry.label.assign(label.data(), label.size());
        entry.identity = {requestId, frameSerial_, generation_};
        ++counts_.expected;
        return true;
    }

    bool bindExpected(std::uint32_t index, const GpuTexture& texture)
    {
        if (!begun_ || sealed_ || failed_ || index >= counts_.expected
            || entries_[index].source.isValid() || !texture.id().isValid())
            return fail("RHI-READBACK-EXPECTED-SOURCE-INVALID");
        auto& entry = entries_[index];
        const auto& desc = texture.desc();
        entry.source = texture.id();
        entry.format = desc.format;
        entry.width = desc.extent.width;
        entry.height = desc.extent.height;
        return true;
    }

    bool expect(std::string_view label, const GpuTexture& texture)
    {
        if (!texture.id().isValid())
            return fail("RHI-READBACK-EXPECTED-INVALID");
        const auto index = counts_.expected;
        return declareExpected(label) && bindExpected(index, texture);
    }

    TextureReadbackIdentity requestIdentity(std::uint32_t index) const
    {
        return index < counts_.expected ? entries_[index].identity : TextureReadbackIdentity{};
    }

    bool seal()
    {
        if (!begun_ || sealed_ || failed_ || !counts_.expected)
            return fail("RHI-READBACK-EXPECTED-EMPTY-OR-SEALED");
        for (std::uint32_t i = 0; i < counts_.expected; ++i)
            if (!entries_[i].source.isValid())
                return fail("RHI-READBACK-EXPECTED-SOURCE-MISSING");
        sealed_ = true;
        return true;
    }

    bool schedule(TextureReadbackExtension& extension,
        CommandEncoder& commands,
        std::uint32_t index,
        const GpuTexture& texture)
    {
        if (!sealed_ || failed_ || index >= counts_.expected || entries_[index].scheduled
            || !(entries_[index].source == texture.id()))
            return fail("RHI-READBACK-SCHEDULE-IDENTITY");
        if (!extension.supportsTrackedRequests())
            return fail("RHI-READBACK-TRACKED-MODE-UNSUPPORTED");
        auto& entry = entries_[index];
        core::pmr::String diagnostic;
        if (!extension.scheduleTracked(commands, texture, entry.label, entry.identity, diagnostic))
            return fail(diagnostic.empty() ? std::string_view("RHI-READBACK-SCHEDULE-FAILED")
                                           : std::string_view(diagnostic));
        entry.scheduled = true;
        ++counts_.scheduled;
        return true;
    }

    bool consume(const TextureReadbackSample& sample)
    {
        const auto id = sample.identity.requestId;
        if (!sealed_ || !id)
            return fail("RHI-READBACK-RESULT-UNEXPECTED");
        std::uint32_t index = 0u;
        while (index < counts_.expected && entries_[index].identity.requestId != id)
            ++index;
        if (index == counts_.expected)
            return fail("RHI-READBACK-RESULT-UNEXPECTED");
        auto& entry = entries_[index];
        if (!(sample.identity == entry.identity) || !(sample.source == entry.source)
            || sample.textureFormat != entry.format || sample.width != entry.width
            || sample.height != entry.height || std::string_view(sample.label) != entry.label)
            return fail("RHI-READBACK-RESULT-IDENTITY");
        if (!entry.scheduled || entry.completed)
            return fail("RHI-READBACK-RESULT-DUPLICATE-OR-UNSCHEDULED");
        entry.completed = true;
        ++counts_.completed;
        if (sample.completion != TextureReadbackCompletion::Completed || !sample.valid
            || !sample.diagnostic.empty() || sample.channelStatisticsVersion != 1u
            || !sample.channelStatistics.valid)
            return fail("RHI-READBACK-RESULT-INVALID");
        const auto expectedChannels = textureReadbackChannelCount(entry.format);
        const auto pixels = std::uint64_t(entry.width) * entry.height;
        if (!expectedChannels || !pixels || pixels > UINT32_MAX
            || sample.channelStatistics.channelCount != expectedChannels)
            return fail("RHI-READBACK-RESULT-CHANNELS");
        for (std::uint32_t channel = 0; channel < expectedChannels; ++channel) {
            const auto& values = sample.channelStatistics.channels[channel];
            if (!values.valid || values.sampleCount != pixels || values.finiteCount != pixels
                || !std::isfinite(values.minValue) || !std::isfinite(values.maxValue)
                || !std::isfinite(values.meanValue) || !std::isfinite(values.centerValue))
                return fail("RHI-READBACK-RESULT-CHANNELS");
        }
        ++counts_.valid;
        return !failed_;
    }

    bool fail(std::string_view diagnostic)
    {
        if (!failed_)
            diagnostic_.assign(diagnostic.data(), diagnostic.size());
        failed_ = true;
        return false;
    }

    bool finish()
    {
        if (!complete())
            return fail("RHI-READBACK-INCOMPLETE-AT-EXIT");
        return true;
    }

    bool complete() const
    {
        return sealed_ && !failed_ && counts_.expected && counts_.scheduled == counts_.expected
            && counts_.completed == counts_.expected && counts_.valid == counts_.expected;
    }

    bool started() const { return begun_; }

    bool failed() const { return failed_; }

    Counts counts() const { return counts_; }

    const core::pmr::String& diagnostic() const { return diagnostic_; }

private:
    struct Entry {
        core::pmr::String label;
        TextureReadbackIdentity identity;
        GpuResourceId source;
        TextureFormat format = TextureFormat::Unknown;
        std::uint32_t width = 0, height = 0;
        bool scheduled = false, completed = false;
    };

    std::array<Entry, Capacity> entries_{};
    Counts counts_;
    std::uint64_t frameSerial_ = 0, generation_ = 0;
    bool begun_ = false, sealed_ = false, failed_ = false;
    core::pmr::String diagnostic_;
};

} // namespace arp::rhi
