#pragma once

#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"

#include <cassert>
#include <type_traits>

namespace arp::test {
class WebGpuRecordedCommandProofFixture final {
    // Compile-time negative type only: no synthetic recorder is instantiated.
    class UnapprovedRecorder final : public rhi_webgpu::WebGpuRecordedCommandProof {
    public:
        const char* diagnostic() const noexcept override { return "TEST-UNISSUED-RECORDER"; }

        bool hasRecordedCommand() const noexcept override { return false; }
    };

public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        static_assert(std::is_abstract_v<WebGpuRecordedCommandProof>);
        static_assert(std::has_virtual_destructor_v<WebGpuRecordedCommandProof>);
        static_assert(!std::is_copy_constructible_v<WebGpuRecordedCommandProof>);
        static_assert(!std::is_move_constructible_v<WebGpuRecordedCommandProof>);
        static_assert(!std::is_default_constructible_v<UnapprovedRecorder>);
        static_assert(!std::is_constructible_v<UnapprovedRecorder,
            std::shared_ptr<WebGpuNativeCommandPayload>>);
        static_assert(!std::is_convertible_v<std::shared_ptr<const WebGpuGenericCommandUsage>,
            std::shared_ptr<const WebGpuRecordedCommandProof>>);
        static_assert(!std::is_convertible_v<std::shared_ptr<const WebGpuGenericCommandProjection>,
            std::shared_ptr<const WebGpuRecordedCommandProof>>);
        static_assert(std::is_convertible_v<std::shared_ptr<const WebGpuOrdinaryRecording>,
            std::shared_ptr<const WebGpuRecordedCommandProof>>);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyUnrecorded(
        const std::shared_ptr<const rhi_webgpu::WebGpuRecordedCommandProof>& proof,
        const char* expectedDiagnostic)
    {
        using namespace rhi_webgpu;
        assert(proof && proof->hasQuotaBoundSharedOwner(proof) && !proof->hasRecordedCommand());
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const WebGpuOrdinarySubmissionBudget budget{
            1u, true, 1u, WebGpuOrdinarySubmission::callbackCount, quota->limitBytes()};
        const char* diagnostic = nullptr;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!WebGpuOrdinarySubmission::prepare(proof, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == expectedDiagnostic && !quota->liveBytes());
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
    }
#endif
};
} // namespace arp::test
