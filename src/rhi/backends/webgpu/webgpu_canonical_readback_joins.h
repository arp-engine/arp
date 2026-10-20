#pragma once

#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/frontend/transfer_readback_copy.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::rhi {
class OriginalCommandRecordingSeal;
}

namespace arp::rhi_webgpu {
class WebGpuNativeBufferMapReceipt;
class WebGpuOriginalGeneralOutputSnapshot;

// Cold exact-root native fact projection only. The retained inputs are the
// original declarations, never legacy transaction/proof or replacement buffers.
class WebGpuCanonicalReadbackJoins final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalNativeClosure;
    friend class WebGpuOriginalTypedNativePayload;
    friend class WebGpuCanonicalCopyPassLowering;
    friend class WebGpuCanonicalColdAdmission;
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuCanonicalReadbackResultStorage;
    friend class WebGpuOrdinaryRecording;
    using Root = rhi::OriginalCommandStateTransaction;
    using Lowering = rhi::semantics::TextureReadbackCopyLowering;

    class ConstructionKey {
        friend class WebGpuCanonicalReadbackJoins;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalCommandReadbackJoinContract::Budget;

    struct Input {
        std::size_t commandIndex;
        std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource> source;
        std::shared_ptr<const WebGpuOriginalGeneralOutputSnapshot> generalOutput;
    };

    WebGpuCanonicalReadbackJoins(ConstructionKey,
        std::shared_ptr<Root> root,
        Budget budget,
        std::size_t count,
        std::size_t copies,
        std::uint32_t owners,
        std::uint64_t sourceBytes,
        std::uint64_t stagingBytes,
        std::shared_ptr<const rhi::OriginalCommandStreamProgram> program = {})
        : root_(std::move(root)), budget_(budget), entries_(count), native_(count), copies_(copies),
          owners_(owners), sourceBytes_(sourceBytes), stagingBytes_(stagingBytes),
          program_(std::move(program)),
          generalOutputStates_(budget.contractVersion == 4u ? count : 0u)
    {}

    static std::shared_ptr<const WebGpuCanonicalReadbackJoins> prepareOriginal(
        std::shared_ptr<Root>,
        const rhi::GpuDevice&,
        const Input*,
        std::size_t,
        const Budget&,
        const char*&);

    static std::shared_ptr<const WebGpuCanonicalReadbackJoins> prepareOriginalProgram(
        std::shared_ptr<Root>,
        std::shared_ptr<const rhi::OriginalCommandStreamProgram>,
        const rhi::GpuDevice&,
        const Input*,
        std::size_t,
        const Budget&,
        const char*&);

    static std::shared_ptr<const WebGpuCanonicalReadbackJoins> prepareOriginalGeneralOutput(
        std::shared_ptr<Root>,
        const rhi::GpuDevice&,
        const Input*,
        std::size_t,
        const Budget&,
        const char*&);

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalReadbackJoins>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-READBACK-JOIN-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept;

    const auto& originalRoot() const noexcept { return root_; }

    const auto& originalProgram() const noexcept { return program_; }

    std::uint32_t joinContractVersion() const noexcept { return budget_.contractVersion; }

    const auto& originalGeneralOutputIntent() const noexcept { return budget_.generalOutput; }

    std::size_t readbackCount() const noexcept { return entries_.values().size(); }

    std::size_t projectedCopyCount() const noexcept { return copies_.values().size(); }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    std::uint64_t sourceResourceBytes() const noexcept { return sourceBytes_; }

    std::uint64_t stagingResourceBytes() const noexcept { return stagingBytes_; }

    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandReadbackJoinLowering::lowering(
            rhi::Backend::WebGPU, budget_.contractVersion)
            .diagnostic;
    }

private:
    using Native = std::array<WebGpuTransferResourceObservation, 2>;

    struct ActiveMap {
        const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>& request;
        const WebGpuNativeBufferMapReceipt& receipt;
    };

    struct Copy {
        std::size_t commandIndex;
        Lowering::Decision decision;
    };

    static const char* capture(const Root&,
        const Input&,
        Native&,
        const ActiveMap* = nullptr,
        std::uint32_t version = 1u) noexcept;
    static std::shared_ptr<const WebGpuCanonicalReadbackJoins> prepareOriginalImpl(
        std::shared_ptr<Root>,
        std::shared_ptr<const rhi::OriginalCommandStreamProgram>,
        const rhi::GpuDevice&,
        const Input*,
        std::size_t,
        const Budget&,
        const char*&);
    const char* validateRetained(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&) const noexcept;
    const char* validateRetainedMapping(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&,
        const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>&,
        const WebGpuNativeBufferMapReceipt&) const noexcept;
    const char* validateWithSeal(
        const rhi::OriginalCommandRecordingSeal*, const ActiveMap* = nullptr) const noexcept;
    // Only the typed closure consumes this immutable Join2/3 projection after
    // authenticating its exact sealed Pending root. Ordinary/map guards remain.
    const char* validateImmutableProgramBindings() const noexcept;
    const char* validateFrozenEntries(
        const ActiveMap* = nullptr, bool queryNative = true) const noexcept;
    static const char* validateGeneralOutputBinding(const Root&,
        const Input&,
        const rhi::GpuTexture::CommittedStateObservation* = nullptr) noexcept;
    const char* validatePreparedGeneralOutput() const noexcept;
    const char* validateRetainedGeneralOutput(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&,
        const ActiveMap* = nullptr) const noexcept;
    static const char* inspect(const std::shared_ptr<Root>&,
        const Input*,
        std::size_t,
        const Budget&,
        std::uint32_t&,
        std::uint64_t&,
        std::uint64_t&,
        std::size_t&,
        const rhi::OriginalCommandRecordingSeal* = nullptr,
        const ActiveMap* = nullptr,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& = {}) noexcept;
    const std::shared_ptr<Root> root_;
    const Budget budget_;
    core::BoundedSequence<Input> entries_;
    core::BoundedSequence<Native> native_;
    core::BoundedSequence<Copy> copies_;
    const std::uint32_t owners_;
    const std::uint64_t sourceBytes_, stagingBytes_;
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram> program_;
    core::BoundedSequence<rhi::GpuTexture::CommittedStateObservation> generalOutputStates_;
};
} // namespace arp::rhi_webgpu
