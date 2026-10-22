#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_runtime_smoke.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

arp::rhi_metal::MetalRhiDeviceDesc makeMetalDesc()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;

    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.label = "MetalRuntimeHashContract";
    desc.capabilityContract = contract;
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    // The smoke uses a bounded backend-owned staging pool for the copy pass;
    // zero means "no budget supplied" and must fail device creation closed.
    desc.uploadStagingPageSizeBytes = 1u * 1024u * 1024u;
    desc.uploadStagingMaxPageCount = 2u;
    return desc;
}

std::string escapeJson(const std::pmr::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

void writeRuntimeReport(const std::filesystem::path& path,
    const arp::rhi_metal::MetalRhiAppleRuntimeSmokeReport& report)
{
    if (path.empty()) {
        return;
    }
    std::ofstream file(path);
    if (!file) {
        std::fprintf(stderr, "failed to write Metal runtime report: %s\n", path.c_str());
        return;
    }
    file << "{\n"
         << "  \"success\": " << (report.success ? "true" : "false") << ",\n"
         << "  \"runtimeAvailable\": " << (report.runtimeAvailable ? "true" : "false") << ",\n"
         << "  \"diagnosticOnly\": " << (report.diagnosticOnly ? "true" : "false") << ",\n"
         << "  \"publicDevicePathExercised\": "
         << (report.publicDevicePathExercised ? "true" : "false") << ",\n"
         << "  \"nativeResourceStatsObserved\": "
         << (report.nativeResourceStatsObserved ? "true" : "false") << ",\n"
         << "  \"nativeBufferCreated\": " << (report.nativeBufferCreated ? "true" : "false")
         << ",\n"
         << "  \"nativeBufferReadbackObserved\": "
         << (report.nativeBufferReadbackObserved ? "true" : "false") << ",\n"
         << "  \"nativeTextureCreated\": " << (report.nativeTextureCreated ? "true" : "false")
         << ",\n"
         << "  \"nativeTextureReadbackObserved\": "
         << (report.nativeTextureReadbackObserved ? "true" : "false") << ",\n"
         << "  \"nativeTextureViewCreated\": "
         << (report.nativeTextureViewCreated ? "true" : "false") << ",\n"
         << "  \"nativeSamplerCreated\": " << (report.nativeSamplerCreated ? "true" : "false")
         << ",\n"
         << "  \"nativeCommandStatsObserved\": "
         << (report.nativeCommandStatsObserved ? "true" : "false") << ",\n"
         << "  \"nativeQueueSubmitObserved\": "
         << (report.nativeQueueSubmitObserved ? "true" : "false") << ",\n"
         << "  \"nativeDrawableAcquireObserved\": "
         << (report.nativeDrawableAcquireObserved ? "true" : "false") << ",\n"
         << "  \"nativeDrawablePresentObserved\": "
         << (report.nativeDrawablePresentObserved ? "true" : "false") << ",\n"
         << "  \"nativeResizeObserved\": " << (report.nativeResizeObserved ? "true" : "false")
         << ",\n"
         << "  \"nativeLifecycleObserved\": " << (report.nativeLifecycleObserved ? "true" : "false")
         << ",\n"
         << "  \"nativeSyncPayloadCreated\": "
         << (report.nativeSyncPayloadCreated ? "true" : "false") << ",\n"
         << "  \"nativeSyncSignalSubmitObserved\": "
         << (report.nativeSyncSignalSubmitObserved ? "true" : "false") << ",\n"
         << "  \"nativeSyncWaitSubmitObserved\": "
         << (report.nativeSyncWaitSubmitObserved ? "true" : "false") << ",\n"
         << "  \"nativeSyncHostWaitObserved\": "
         << (report.nativeSyncHostWaitObserved ? "true" : "false") << ",\n"
         << "  \"nativeSyncCompletedValue\": " << report.nativeSyncCompletedValue << ",\n"
         << "  \"boundedFrameSlotCount\": " << report.boundedFrameSlotCount << ",\n"
         << "  \"deterministicReadbackHash\": " << report.deterministicReadbackHash << ",\n"
         << "  \"appleRuntimeRenderOutputHashCaptured\": "
         << (report.appleRuntimeRenderOutputHashCaptured ? "true" : "false") << ",\n"
         << "  \"deterministicRenderOutputHash\": " << report.deterministicRenderOutputHash << ",\n"
         << "  \"nativeHandleExposedToUpperLayer\": "
         << (report.nativeHandleExposedToUpperLayer ? "true" : "false") << ",\n"
         << "  \"diagnostics\": \"" << escapeJson(report.diagnostics) << "\"\n"
         << "}\n";
}

} // namespace

int main()
{
    using namespace arp::rhi_metal;

#if defined(ARP_SOURCE_DIR)
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto smokeHeader =
        readText(root / "src" / "rhi" / "backends" / "metal" / "metal_runtime_smoke.h");
    const auto smokeSource =
        readText(root / "src" / "rhi" / "backends" / "metal" / "metal_runtime_smoke.cpp");
    assert(smokeHeader.find("appleRuntimeRenderOutputHashCaptured") != std::pmr::string::npos);
    assert(smokeHeader.find("deterministicRenderOutputHash") != std::pmr::string::npos);
    assert(smokeHeader.find("appleRuntimeHashPlatformGap") != std::pmr::string::npos);
    assert(smokeHeader.find("nativeBufferReadbackObserved") != std::pmr::string::npos);
    assert(smokeHeader.find("nativeTextureReadbackObserved") != std::pmr::string::npos);
    assert(smokeHeader.find("deterministicReadbackHash") != std::pmr::string::npos);
    assert(smokeSource.find("deterministicMetalRenderOutputHash") == std::pmr::string::npos);
    assert(smokeSource.find("Metal screenshot/hash capture is a platform gap")
        != std::pmr::string::npos);
#endif

    MetalRhiAppleRuntimeSmokeReport emptyReport;
    assert(!emptyReport.appleRuntimeRenderOutputHashCaptured);
    assert(emptyReport.deterministicRenderOutputHash == 0u);
    assert(!emptyReport.appleRuntimeHashPlatformGap);

    MetalRhiAppleRuntimeSmokeDesc smokeDesc;
    smokeDesc.deviceDesc = makeMetalDesc();
    const auto smokeReport = runMetalRhiAppleRuntimeSmoke(smokeDesc);
    assert(!smokeReport.appleRuntimeRenderOutputHashCaptured);
    assert(smokeReport.renderOutputHashCaptureCount == 0u);
    assert(smokeReport.deterministicRenderOutputHash == 0u);
    assert(std::pmr::string(smokeReport.renderOutputEvidenceDiagnostic)
        == "RHI-OUTPUT-ORIGINAL-OBSERVATION-UNAVAILABLE");
    const auto reportPath = std::getenv("ARP_METAL_RUNTIME_REPORT");
    writeRuntimeReport(
        reportPath != nullptr ? std::filesystem::path(reportPath) : std::filesystem::path{},
        smokeReport);
    std::printf("MetalSmoke success=%d runtime=%d diagnostics=%s "
                "resources=%d commands=%d queue=%d acquire=%d present=%d resize=%d "
                "lifecycle=%d slots=%u syncPayload=%d syncSignal=%d syncWait=%d "
                "syncHostWait=%d syncValue=%llu bufferReadback=%d textureReadback=%d "
                "readbackHash=%llu hashCaptured=%d hash=%llu\n",
        smokeReport.success ? 1 : 0,
        smokeReport.runtimeAvailable ? 1 : 0,
        smokeReport.diagnostics.c_str(),
        smokeReport.nativeResourceStatsObserved ? 1 : 0,
        smokeReport.nativeCommandStatsObserved ? 1 : 0,
        smokeReport.nativeQueueSubmitObserved ? 1 : 0,
        smokeReport.nativeDrawableAcquireObserved ? 1 : 0,
        smokeReport.nativeDrawablePresentObserved ? 1 : 0,
        smokeReport.nativeResizeObserved ? 1 : 0,
        smokeReport.nativeLifecycleObserved ? 1 : 0,
        smokeReport.boundedFrameSlotCount,
        smokeReport.nativeSyncPayloadCreated ? 1 : 0,
        smokeReport.nativeSyncSignalSubmitObserved ? 1 : 0,
        smokeReport.nativeSyncWaitSubmitObserved ? 1 : 0,
        smokeReport.nativeSyncHostWaitObserved ? 1 : 0,
        static_cast<unsigned long long>(smokeReport.nativeSyncCompletedValue),
        smokeReport.nativeBufferReadbackObserved ? 1 : 0,
        smokeReport.nativeTextureReadbackObserved ? 1 : 0,
        static_cast<unsigned long long>(smokeReport.deterministicReadbackHash),
        smokeReport.appleRuntimeRenderOutputHashCaptured ? 1 : 0,
        static_cast<unsigned long long>(smokeReport.deterministicRenderOutputHash));
    const auto runtimeBoundary = describeMetalRhiNativeRuntimeBoundary();
    assert(smokeReport.runtimeAvailable == runtimeBoundary.runtimeAvailableOnThisPlatform);
    if (smokeReport.runtimeAvailable) {
        assert(smokeReport.nativeBufferReadbackObserved);
        assert(smokeReport.nativeTextureReadbackObserved);
        assert(smokeReport.deterministicReadbackHash != 0u);
        assert(!smokeReport.appleRuntimeRenderOutputHashCaptured);
        assert(smokeReport.deterministicRenderOutputHash == 0u);
        assert(!smokeReport.renderOutputHashIncludesDrawablePresent);
        assert(!smokeReport.appleRuntimeHashPlatformGap);
    } else {
        assert(!smokeReport.appleRuntimeRenderOutputHashCaptured);
        assert(smokeReport.deterministicRenderOutputHash == 0u);
        assert(smokeReport.appleRuntimeHashPlatformGap);
        assert(smokeReport.diagnostics.find("platform gap") != std::pmr::string::npos);
    }

    return 0;
}
