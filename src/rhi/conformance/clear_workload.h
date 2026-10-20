#pragma once

#include "core/array_view.h"
#include "core/base64.h"
#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"

#include <array>

namespace arp::rhi {

struct ConformanceWorkloadBudget {
    std::uint64_t maxYamlBytes = 0, maxPixelsPerFrame = 0;
    std::uint32_t maxFrames = 0;
};

struct ConformanceClearFrame {
    Extent2D extent;
    std::array<std::uint8_t, 4> rgba8{};
    Color clearColor() const noexcept;
};

// Immutable intent, not authenticated execution or output evidence.
class ConformanceClearWorkload final {
    class ConstructionKey {
        friend class ConformanceClearWorkload;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    ConformanceClearWorkload(ConstructionKey,
        core::pmr::String,
        core::pmr::String,
        core::pmr::Vector<ConformanceClearFrame>);
    static core::UniquePtr<ConformanceClearWorkload> parse(
        core::StringView source, const ConformanceWorkloadBudget&, const char*& diagnostic);

    core::StringView id() const noexcept { return id_; }

    core::StringView sourceSha256() const noexcept { return sourceSha256_; }

    core::ArrayView<ConformanceClearFrame> frames() const noexcept { return frames_; }

private:
    const core::pmr::String id_, sourceSha256_;
    const core::pmr::Vector<ConformanceClearFrame> frames_;
};

} // namespace arp::rhi
