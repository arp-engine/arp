#pragma once
#include "rhi/backends/webgpu/webgpu_native_runtime.h"

namespace arp::rhi {
struct OriginalCommandNativeColdCommandBudget;
struct OriginalTypedNativeColdCommandRequest;
struct OriginalBindlessRootColdCommandRequest;
struct OriginalBindlessRootExecutionInputs;
struct OriginalBindlessRootExecutionBudget;
} // namespace arp::rhi

namespace arp::rhi_webgpu {
class WebGpuCanonicalColdAdmission;
class WebGpuCanonicalColdCommand;
class WebGpuOriginalTypedColdAdmission;
class WebGpuOriginalTypedColdCommand;
class WebGpuBindlessRootColdAdmission;
class WebGpuBindlessRootColdCommand;
class WebGpuBindlessRootNativeProgram;
class WebGpuCanonicalReadbackResultStorage;
class WebGpuRhiAllocator;

// Backend-owned minimal command shell. Separate core metadata allocation from
// native opening so callers reserve all owner/callback graphs before GPU APIs.
class WebGpuNativeCommandPreparation final {
public:
    static std::shared_ptr<WebGpuNativeCommandPayload> allocate(
        std::shared_ptr<WebGpuNativeCommandStats>);
    static bool connect(WebGpuNativeCommandPayload&, void* expectedDevice, const char*& diagnostic);
    static std::shared_ptr<const WebGpuCanonicalColdCommand> prepareCanonical(
        std::shared_ptr<const WebGpuCanonicalColdAdmission>,
        const rhi::OriginalCommandNativeColdCommandBudget&,
        const char*& diagnostic);
    static const char* validateCanonical(
        const WebGpuCanonicalColdCommand&, bool requireClaim = true) noexcept;
    static std::shared_ptr<const WebGpuOriginalTypedColdCommand> prepareTypedOriginal(
        std::shared_ptr<const WebGpuOriginalTypedColdAdmission>,
        const rhi::OriginalTypedNativeColdCommandRequest&,
        const char*& diagnostic);
    static const char* validateTypedOriginal(const WebGpuOriginalTypedColdCommand&) noexcept;
    static const char* preflightBindlessRootPushUpload(
        const rhi::OriginalBindlessRootExecutionInputs&,
        const rhi::OriginalBindlessRootExecutionBudget&) noexcept;
    static std::shared_ptr<const WebGpuBindlessRootColdCommand> prepareBindlessRoot(
        std::shared_ptr<const WebGpuBindlessRootColdAdmission>,
        std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>,
        const rhi::OriginalBindlessRootColdCommandRequest&,
        const char*&,
        WebGpuRhiAllocator* = nullptr);
    static const char* validateBindlessRoot(const WebGpuBindlessRootColdCommand&) noexcept;
    static const char* validateTypedAdmissionRetained(
        const std::shared_ptr<const WebGpuOriginalTypedColdAdmission>&) noexcept;
    static bool open(WebGpuNativeCommandPayload&,
        void* expectedDevice,
        std::string_view label,
        const char*& diagnostic);

private:
    enum class ShellIdentity { Complete, Owner, Mode, Native };
    static ShellIdentity inspectShellIdentity(const std::shared_ptr<WebGpuNativeCommandPayload>&,
        const std::shared_ptr<const void>& admission,
        void* device,
        void* queue,
        void* closureDevice,
        std::uint64_t identity) noexcept;
    static const char* inspectTypedColdCommand(
        const std::shared_ptr<const WebGpuOriginalTypedColdAdmission>&,
        const rhi::OriginalTypedNativeColdCommandRequest&) noexcept;
    static const char* inspectBindlessRootColdCommand(
        const std::shared_ptr<const WebGpuBindlessRootColdAdmission>&,
        const std::shared_ptr<const WebGpuCanonicalReadbackResultStorage>&,
        const rhi::OriginalBindlessRootColdCommandRequest&) noexcept;
    static const char* validateBindlessRootUnclaimed(const WebGpuBindlessRootColdCommand&) noexcept;
    static const char* prepareBindlessRootProgram(WebGpuBindlessRootColdCommand&) noexcept;
    static const char* prepareBindlessRootPushUpload(
        WebGpuBindlessRootColdCommand&, WebGpuRhiAllocator*);
    static const char* validateBindlessRootProgram(const WebGpuBindlessRootColdCommand&) noexcept;
    static const char* validateBindlessRootDepthPending(
        const WebGpuBindlessRootColdCommand&) noexcept;
    static const char* inspectBindlessRootProgram(const WebGpuBindlessRootColdCommand&,
        WebGpuBindlessRootNativeProgram* destination) noexcept;
    friend class WebGpuCanonicalCallbackCustody;
    static const char* validateTypedRetained(const WebGpuOriginalTypedColdCommand&) noexcept;
    static const char* validateTypedUnclaimed(const WebGpuOriginalTypedColdCommand&) noexcept;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    // Runtime issuers only; public cold inspection cannot observe this as proof.
    static const char* validateCanonicalLive(const WebGpuCanonicalColdCommand&) noexcept;
    static const char* validateCanonicalIdentity(
        const WebGpuCanonicalColdCommand&, bool requireClaim) noexcept;
    static const char* inspectCanonicalCommandOwnership(
        const std::shared_ptr<const WebGpuCanonicalColdAdmission>&,
        const rhi::OriginalCommandNativeColdCommandBudget&) noexcept;
    static const char* inspectCanonicalColdCommand(
        const std::shared_ptr<const WebGpuCanonicalColdAdmission>&,
        const rhi::OriginalCommandNativeColdCommandBudget&) noexcept;
};
} // namespace arp::rhi_webgpu
