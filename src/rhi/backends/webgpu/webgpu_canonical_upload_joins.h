#pragma once

#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::rhi {
class OriginalCommandRecordingSeal;
}

namespace arp::rhi_webgpu {
class WebGpuCanonicalBufferUploadSource;
class WebGpuCanonicalTextureContentUploadSource;
class WebGpuCanonicalNativeClosure;

// No native handle, journal or GPU proof is exposed. Ordinals are the exact
// original commands; the immutable entries retain pre-existing byte authority.
class WebGpuCanonicalUploadJoins final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCopyPassLowering;
    friend class WebGpuCanonicalColdAdmission;
    friend class WebGpuOrdinaryRecording;
    using Root = rhi::OriginalCommandStateTransaction;

    class ConstructionKey {
        friend class WebGpuCanonicalUploadJoins;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalCommandUploadJoinBudget;
    using Source = std::variant<std::shared_ptr<const WebGpuCanonicalBufferUploadSource>,
        std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>>;

    struct Input {
        std::size_t commandIndex;
        Source source;
    };

    WebGpuCanonicalUploadJoins(ConstructionKey,
        std::shared_ptr<Root> root,
        Budget budget,
        std::size_t count,
        std::uint32_t owners,
        std::uint64_t stagingBytes,
        std::uint64_t destinationBytes)
        : root_(std::move(root)), budget_(budget), entries_(count), native_(count), owners_(owners),
          stagingBytes_(stagingBytes), destinationBytes_(destinationBytes)
    {}

    static std::shared_ptr<const WebGpuCanonicalUploadJoins> prepareOriginal(std::shared_ptr<Root>,
        const rhi::GpuDevice&,
        const Input*,
        std::size_t,
        const Budget&,
        const char*&);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalUploadJoins>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-UPLOAD-JOIN-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept;

    const auto& originalRoot() const noexcept { return root_; }

    std::size_t uploadCount() const noexcept { return entries_.values().size(); }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    std::uint64_t stagingResourceBytes() const noexcept { return stagingBytes_; }

    std::uint64_t destinationResourceBytes() const noexcept { return destinationBytes_; }

    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandUploadJoinLowering::lowering(
            rhi::Backend::WebGPU)
            .diagnostic;
    }

private:
    const char* validateRetained(const rhi::OriginalCommandRecordingSeal&) const noexcept;
    const char* validateWithSeal(const rhi::OriginalCommandRecordingSeal*) const noexcept;
    static const char* inspect(const Root&,
        const Input*,
        std::size_t,
        const Budget&,
        std::uint32_t&,
        std::uint64_t&,
        std::uint64_t&,
        const rhi::OriginalCommandRecordingSeal* = nullptr) noexcept;
    using Native = std::array<WebGpuTransferResourceObservation, 2>;
    static const char* capture(const Root&,
        const Input&,
        Native&,
        const rhi::OriginalCommandRecordingSeal* = nullptr) noexcept;
    const std::shared_ptr<Root> root_;
    const Budget budget_;
    core::BoundedSequence<Input> entries_;
    core::BoundedSequence<Native> native_;
    const std::uint32_t owners_;
    const std::uint64_t stagingBytes_, destinationBytes_;
};
} // namespace arp::rhi_webgpu
