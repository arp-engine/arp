#pragma once

#include "rhi/conformance/clear_workload.h"

namespace arp::rhi {
struct ConformanceStorageMrtFrame {
    Extent2D extent;
    std::array<float, 12> values{};
    std::array<std::uint8_t, 4> expectedColor0{}, expectedColor1{};
    std::array<float, 4> expectedStorage{};
};

class ConformanceStorageMrtWorkload final {
    class ConstructionKey {
        friend class ConformanceStorageMrtWorkload;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    ConformanceStorageMrtWorkload(ConstructionKey,
        core::pmr::String,
        core::pmr::String,
        core::pmr::String,
        core::pmr::String,
        bool,
        core::pmr::Vector<ConformanceStorageMrtFrame>);
    static core::UniquePtr<ConformanceStorageMrtWorkload> parse(
        core::StringView, const ConformanceWorkloadBudget&, const char*& diagnostic);
    const char* executionDiagnostic(const DeviceCapabilities&) const noexcept;

    core::StringView id() const noexcept { return id_; }

    core::StringView sourceSha256() const noexcept { return sourceSha256_; }

    core::StringView shaderSourceSha256() const noexcept { return shaderSourceSha256_; }

    core::StringView computeSourceSha256() const noexcept { return computeSourceSha256_; }

    core::ArrayView<ConformanceStorageMrtFrame> frames() const noexcept { return frames_; }

private:
    const core::pmr::String id_, sourceSha256_, shaderSourceSha256_, computeSourceSha256_;
    const bool requireTransientAliasing_;
    const core::pmr::Vector<ConformanceStorageMrtFrame> frames_;
};
} // namespace arp::rhi
