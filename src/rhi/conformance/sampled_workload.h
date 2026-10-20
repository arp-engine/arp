#pragma once

#include "rhi/conformance/clear_workload.h"

namespace arp::rhi {

enum class ConformanceSampledResource : std::uint8_t {
    SourceA,
    SourceB,
    PingA,
    PingB,
    Output,
    Count
};

struct ConformanceSampledDraw {
    ConformanceSampledResource sourceA = ConformanceSampledResource::SourceA,
                               sourceB = ConformanceSampledResource::SourceB,
                               target = ConformanceSampledResource::Output;
    std::array<float, 8> push{};
    bool indexed = false;
    std::array<std::uint8_t, 4> expected{};
};

struct ConformanceSampledWorkloadBudget {
    std::uint64_t maxYamlBytes = 0, maxPixelsPerFrame = 0;
    std::uint32_t maxFrames = 0, maxDrawsPerFrame = 0;
};

struct ConformanceSampledFrame {
    Extent2D extent;
    std::array<std::uint8_t, 4> sourceA{}, sourceB{}, expected{};
    std::array<float, 32> uniforms{};
    std::array<float, 8> push{};
    bool indexed = false;
    core::pmr::Vector<ConformanceSampledDraw> draws{&core::defaultCpuMemoryResource()};
};

// Bounded immutable intent. Expected pixels are never execution inputs.
class ConformanceSampledWorkload final {
    class ConstructionKey {
        friend class ConformanceSampledWorkload;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    ConformanceSampledWorkload(ConstructionKey,
        core::pmr::String,
        core::pmr::String,
        core::pmr::String,
        std::array<float, 27>,
        std::array<std::uint16_t, 3>,
        core::pmr::Vector<ConformanceSampledFrame>);
    static core::UniquePtr<ConformanceSampledWorkload> parse(
        core::StringView, const ConformanceWorkloadBudget&, const char*& diagnostic);
    static core::UniquePtr<ConformanceSampledWorkload> parse(
        core::StringView, const ConformanceSampledWorkloadBudget&, const char*& diagnostic);

    core::StringView id() const noexcept { return id_; }

    core::StringView sourceSha256() const noexcept { return sourceSha256_; }

    core::StringView shaderSourceSha256() const noexcept { return shaderSourceSha256_; }

    const auto& vertices() const noexcept { return vertices_; }

    const auto& indices() const noexcept { return indices_; }

    core::ArrayView<ConformanceSampledFrame> frames() const noexcept { return frames_; }

private:
    const core::pmr::String id_, sourceSha256_, shaderSourceSha256_;
    const std::array<float, 27> vertices_;
    const std::array<std::uint16_t, 3> indices_;
    const core::pmr::Vector<ConformanceSampledFrame> frames_;
};
} // namespace arp::rhi
