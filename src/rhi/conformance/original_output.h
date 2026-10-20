#pragma once

#include "core/lifetime_epoch.h"
#include "rhi/conformance/sampled_workload.h"
#include "rhi/conformance/storage_mrt_workload.h"
#include "rhi/gpu_resource_types.h"

namespace arp::rhi_webgpu {
class WebGpuConformanceClearExecution;
}

namespace arp::rhi_vulkan {
class VulkanConformanceClearExecution;
}

namespace arp::rhi::webgl {
class WebGlOrderedGraphicsPayload;
class WebGlExecution;
} // namespace arp::rhi::webgl

namespace arp::rhi {

enum class ConformanceOutputRole { Canvas, Color1, Storage };

struct OriginalConformanceRequest {
    std::uint32_t contractVersion = 0, maxFrames = 0;
    bool prototypeOptIn = false;
    std::uint64_t maxOutputBytes = 0, maxCpuMetadataBytes = 0;
};

class OriginalConformanceContract final {
public:
    static const char* validate(const OriginalConformanceRequest& r) noexcept
    {
        if (r.contractVersion != 1 || !r.prototypeOptIn)
            return "RHI-CONFORMANCE-ORIGINAL-OPT-IN";
        if (!r.maxFrames || !r.maxOutputBytes || !r.maxCpuMetadataBytes)
            return "RHI-CONFORMANCE-ORIGINAL-BUDGET";
        return nullptr;
    }

    static const char* sourceDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return "RHI-CONFORMANCE-NULL-VALIDATOR-NO-OUTPUT";
        case Backend::Vulkan:
            return "RHI-CONFORMANCE-VULKAN-ORIGINAL-OUTPUT-JOIN-UNAVAILABLE";
        case Backend::Metal:
            return "RHI-CONFORMANCE-METAL-ORIGINAL-OUTPUT-JOIN-UNAVAILABLE";
        case Backend::WebGPU:
            return "RHI-CONFORMANCE-WEBGPU-SHARED-OUTPUT-JOIN-UNAVAILABLE";
        case Backend::WebGL:
            return nullptr;
        default:
            return "RHI-CONFORMANCE-ORIGINAL-BACKEND-UNKNOWN";
        }
    }
};

// Issuer-owned sealed history. Read-only consumers cannot import bytes or claims.
class OriginalConformanceRun final {
    friend class webgl::WebGlOrderedGraphicsPayload;
    friend class webgl::WebGlExecution;
    friend class rhi_vulkan::VulkanConformanceClearExecution;
    friend class rhi_webgpu::WebGpuConformanceClearExecution;

    class ConstructionKey {
        friend class webgl::WebGlOrderedGraphicsPayload;
        friend class rhi_vulkan::VulkanConformanceClearExecution;
        friend class rhi_webgpu::WebGpuConformanceClearExecution;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Frame {
        Extent2D extent;
        core::ByteBuffer pixels{&core::defaultCpuMemoryResource()};
        core::ByteBuffer color1{&core::defaultCpuMemoryResource()};
        core::ByteBuffer storage{&core::defaultCpuMemoryResource()};
        GpuResourceId canvas;
        std::shared_ptr<const void> shaderArtifact;
        std::uint64_t serial = 0;
        bool observed = false, presented = false;
    };

public:
    OriginalConformanceRun(ConstructionKey,
        core::UniquePtr<ConformanceClearWorkload> clear,
        core::UniquePtr<ConformanceSampledWorkload> sampled,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        core::LifetimeEpoch::Ticket device,
        core::LifetimeEpoch::Ticket context,
        core::LifetimeEpoch::Ticket domain,
        std::shared_ptr<const void> owner,
        const char* (*ownerDiagnostic)(const void*) noexcept,
        core::UniquePtr<ConformanceStorageMrtWorkload> storage = {})
        : clear_(std::move(clear)), sampled_(std::move(sampled)), storage_(std::move(storage)),
          quota_(std::move(quota)), device_(std::move(device)), context_(std::move(context)),
          domain_(std::move(domain)), owner_(std::move(owner)), ownerDiagnostic_(ownerDiagnostic),
          quotaBytes_(quota_->limitBytes())
    {
        const auto count = clear_ ? clear_->frames().size()
            : sampled_            ? sampled_->frames().size()
                                  : storage_->frames().size();
        frames_.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            auto& frame = frames_[i];
            frame.extent = clear_ ? clear_->frames()[i].extent
                : sampled_        ? sampled_->frames()[i].extent
                                  : storage_->frames()[i].extent;
            frame.pixels.resize(std::size_t(frame.extent.width) * frame.extent.height * 4);
            if (storage_) {
                frame.color1.resize(frame.pixels.size());
                frame.storage.resize(frame.pixels.size() * 2);
            }
        }
    }

    OriginalConformanceRun(const OriginalConformanceRun&) = delete;
    OriginalConformanceRun& operator=(const OriginalConformanceRun&) = delete;

    core::StringView workloadSha256() const noexcept
    {
        return clear_  ? clear_->sourceSha256()
            : sampled_ ? sampled_->sourceSha256()
                       : storage_->sourceSha256();
    }

    std::size_t frameCount() const noexcept { return frames_.size(); }

    std::size_t observedFrames() const noexcept { return next_; }

    core::ArrayView<std::uint8_t> pixels(std::size_t i) const noexcept
    {
        return i < next_ ? core::ArrayView<std::uint8_t>{frames_[i].pixels}
                         : core::ArrayView<std::uint8_t>{};
    }

    std::size_t outputCount() const noexcept { return storage_ ? 3 : 1; }

    core::ArrayView<std::uint8_t> pixels(std::size_t i, ConformanceOutputRole role) const noexcept
    {
        if (i >= next_)
            return {};
        switch (role) {
        case ConformanceOutputRole::Canvas:
            return frames_[i].pixels;
        case ConformanceOutputRole::Color1:
            return frames_[i].color1;
        case ConformanceOutputRole::Storage:
            return frames_[i].storage;
        default:
            return {};
        }
    }

    const char* diagnostic() const noexcept
    {
        if (!device_.isCurrent() || !context_.isCurrent() || !domain_.isCurrent())
            return "RHI-CONFORMANCE-ORIGINAL-OWNER-RETIRED";
        if (const auto* id = ownerDiagnostic_(owner_.get()))
            return id;
        if (quota_->limitBytes() != quotaBytes_)
            return "RHI-CONFORMANCE-ORIGINAL-QUOTA";
        if (failure_)
            return failure_;
        if (next_ != frames_.size())
            return "RHI-CONFORMANCE-ORIGINAL-INCOMPLETE";
        for (const auto& f : frames_)
            if (!f.observed || !f.presented)
                return "RHI-CONFORMANCE-ORIGINAL-PRESENT-INCOMPLETE";
        return nullptr;
    }

private:
    core::UniquePtr<ConformanceClearWorkload> clear_;
    core::UniquePtr<ConformanceSampledWorkload> sampled_;
    core::UniquePtr<ConformanceStorageMrtWorkload> storage_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::LifetimeEpoch::Ticket device_, context_, domain_;
    core::pmr::Vector<Frame> frames_{&core::defaultCpuMemoryResource()};
    std::shared_ptr<const void> owner_;
    const char* (*ownerDiagnostic_)(const void*) noexcept;
    std::uint64_t quotaBytes_;
    std::size_t next_ = 0;
    const char* failure_ = nullptr;
};

struct OriginalConformanceAuditReport {
    bool originalOutputValidated = false;
    bool backendReadinessPromoted = false, platformValidated = false, productionReady = false;
    std::size_t frames = 0, actualOutputBytes = 0;
    const char* diagnostic = "RHI-CONFORMANCE-ORIGINAL-SOURCE-REQUIRED";
};

class OriginalConformanceAudit final {
public:
    static OriginalConformanceAuditReport inspect(const OriginalConformanceRun* run) noexcept
    {
        OriginalConformanceAuditReport report;
        if (!run)
            return report;
        report.diagnostic = run->diagnostic();
        if (report.diagnostic)
            return report;
        report.originalOutputValidated = true;
        report.frames = run->frameCount();
        for (std::size_t i = 0; i < report.frames; ++i)
            for (std::size_t role = 0; role < run->outputCount(); ++role)
                report.actualOutputBytes +=
                    run->pixels(i, static_cast<ConformanceOutputRole>(role)).size();
        return report;
    }
};
} // namespace arp::rhi
