// Contract tests for the shared device validation helpers in
// rhi/frontend/device_validation.h.
//
// validateSharedDeviceInvariants consolidates the 5 invariant checks that were
// previously duplicated verbatim across describeVulkanRhiDevice,
// describeMetalRhiDevice and describeWebGpuRhiDevice (capability-contract
// backend match, memory budget nonzero, no native handle exposure, no per-draw
// pipeline creation, no per-frame shader compilation). makeSharedDeviceCapabilities
// consolidates the common DeviceCapabilities skeleton.
//
// Status: Contract Complete.

#include "rhi/frontend/device_validation.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cassert>
#include <cstdint>
#include <cstring>

#ifdef NDEBUG
#error Device validation tests require assertions
#endif

namespace {

namespace core = arp::core;

using namespace arp::rhi;
using arp::rhi::semantics::NativeBackendCapabilityContract;

NativeBackendCapabilityContract makeContract(Backend backend)
{
    NativeBackendCapabilityContract contract;
    contract.backend = backend;
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

void assertValidDescPassesAndStampsReportFields()
{
    const auto contract = makeContract(Backend::Vulkan);
    const auto report = validateSharedDeviceInvariants(
        Backend::Vulkan, "Vulkan", contract.backend, 64u * 1024u * 1024u, false, false, false);
    assert(report.success);
    assert(report.diagnostics.empty());
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(!report.perDrawPipelineCreationAllowed);
    assert(!report.perFrameShaderCompilationAllowed);
}

void assertCapabilityContractBackendMismatchFailsFirst()
{
    // A Vulkan contract presented to the Metal expected-backend must fail with
    // the backend-mismatch diagnostic, even when every other invariant passes.
    const auto contract = makeContract(Backend::Vulkan);
    const auto report = validateSharedDeviceInvariants(
        Backend::Metal, "Metal", contract.backend, 64u * 1024u * 1024u, false, false, false);
    assert(!report.success);
    assert(report.diagnostics.find("capability contract backend does not match")
        != std::pmr::string::npos);
}

void assertZeroMemoryBudgetFails()
{
    const auto contract = makeContract(Backend::Vulkan);
    const auto report = validateSharedDeviceInvariants(
        Backend::Vulkan, "Vulkan", contract.backend, 0, false, false, false);
    assert(!report.success);
    assert(report.diagnostics.find("sanitized memory budget telemetry") != std::pmr::string::npos);
}

void assertNativeHandleExposureFails()
{
    const auto contract = makeContract(Backend::Metal);
    const auto report = validateSharedDeviceInvariants(
        Backend::Metal, "Metal", contract.backend, 1024u, true, false, false);
    assert(!report.success);
    assert(report.diagnostics.find("must not expose native handles") != std::pmr::string::npos);
}

void assertPerDrawPipelineCreationFails()
{
    const auto contract = makeContract(Backend::WebGPU);
    const auto report = validateSharedDeviceInvariants(
        Backend::WebGPU, "WebGPU", contract.backend, 1024u, false, true, false);
    assert(!report.success);
    assert(report.diagnostics.find("per-draw pipeline creation") != std::pmr::string::npos);
}

void assertPerFrameShaderCompilationFails()
{
    const auto contract = makeContract(Backend::WebGPU);
    const auto report = validateSharedDeviceInvariants(
        Backend::WebGPU, "WebGPU", contract.backend, 1024u, false, false, true);
    assert(!report.success);
    assert(report.diagnostics.find("per-frame shader compilation") != std::pmr::string::npos);
}

void assertInvariantCheckOrderIsContractThenBudgetThenHandlesThenDrawThenShader()
{
    // When multiple invariants fail, the first one in priority order wins.
    // Contract mismatch has the highest priority, so even with all other
    // invariants also failing, the contract diagnostic is returned.
    const auto vulkanContract = makeContract(Backend::Vulkan);
    const auto report = validateSharedDeviceInvariants(Backend::Metal,
        "Metal",
        vulkanContract.backend,
        0,     // also failing
        true,  // also failing
        true,  // also failing
        true); // also failing
    assert(!report.success);
    assert(report.diagnostics.find("capability contract backend does not match")
        != std::pmr::string::npos);
}

void assertMakeSharedDeviceCapabilitiesFillsCommonFields()
{
    const auto caps =
        makeSharedDeviceCapabilities(Backend::Vulkan, "RHI Vulkan", std::pmr::string{"MyDevice"});
    assert(caps.backend == Backend::Vulkan);
    assert(caps.deviceName == "MyDevice");
    assert(!caps.core.graphics);
    assert(!caps.core.compute);
    assert(!caps.explicitFeatures.explicitBarriers);
    assert(caps.swapchain.requestedImageCount == 0);
    assert(caps.swapchain.maxFramesInFlight == 0);
    assert(!caps.memory.deviceLocal);
    assert(!caps.memory.hostVisible);
    assert(caps.memory.backendFallbacksHiddenFromUpperLayer);
    // Backend-specific fields default to false; the caller overrides them.
    assert(!caps.explicitFeatures.pushConstants);
    assert(!caps.explicitFeatures.secondaryCommandBuffers);
    assert(!caps.memory.transientAliasing);
}

void assertMakeSharedDeviceCapabilitiesUsesFallbackWhenLabelEmpty()
{
    const auto caps = makeSharedDeviceCapabilities(Backend::Metal, "RHI Metal", std::pmr::string{});
    assert(caps.deviceName == "RHI Metal");
}

void assertMakeSharedDeviceCapabilitiesWorksForAllBackends()
{
    // Identity metadata cannot grant an executable capability.
    for (auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(-1),
             static_cast<Backend>(255)}) {
        const auto caps = makeSharedDeviceCapabilities(backend, "RHI Backend", std::pmr::string{});
        assert(caps.backend == backend);
        assert(!caps.core.graphics);
        assert(!caps.core.compute);
        assert(!caps.memory.deviceLocal);
        assert(!caps.memory.hostVisible);
        assert(!caps.features.computeDispatch);
        assert(!caps.features.pushConstants);
        assert(!caps.explicitFeatures.explicitBarriers);
        assert(caps.swapchain.requestedImageCount == 0u);
        assert(caps.swapchain.maxFramesInFlight == 0u);
    }
}

void assertUnimplementedAndUnknownConstructionIsRejected()
{
    for (const auto backend :
        {Backend::WebGL, static_cast<Backend>(-1), static_cast<Backend>(255)}) {
        for (const bool extension : {false, true}) {
            const auto report = validateSharedDeviceInvariants(
                backend, "caller chosen name", backend, 1024u, false, false, false, extension);
            assert(!report.success);
            const char* expected = backend == Backend::WebGL
                ? "RHI-WEBGL1-DEVICE-CONSTRUCTION-UNAVAILABLE"
                : "RHI-DEVICE-CONSTRUCTION-BACKEND-UNKNOWN";
            assert(report.diagnostics.find(expected) != report.diagnostics.npos);
        }
    }
}

void assertConstructionIdentityAndOptionMatrix()
{
    const Backend identities[] = {Backend::Null,
        Backend::Vulkan,
        Backend::Metal,
        Backend::WebGPU,
        Backend::WebGL,
        static_cast<Backend>(-1),
        static_cast<Backend>(255)};
    for (const auto expected : identities) {
        for (const auto supplied : identities) {
            for (unsigned flags = 0u; flags < 32u; ++flags) {
                const bool extension = (flags & 1u) != 0u;
                const bool budget = (flags & 2u) != 0u;
                const bool handles = (flags & 4u) != 0u;
                const bool draws = (flags & 8u) != 0u;
                const bool shaders = (flags & 16u) != 0u;
                const bool implemented = expected == Backend::Null || expected == Backend::Vulkan
                    || expected == Backend::Metal || expected == Backend::WebGPU;
                const auto result = validateSharedDeviceInvariants(expected,
                    "metadata only",
                    supplied,
                    budget ? 1024u : 0u,
                    handles,
                    draws,
                    shaders,
                    extension);
                assert(result.success
                    == (implemented && expected == supplied && budget
                        && (extension || (!handles && !draws && !shaders))));
                if (!implemented) {
                    const char* id = expected == Backend::WebGL
                        ? "RHI-WEBGL1-DEVICE-CONSTRUCTION-UNAVAILABLE"
                        : "RHI-DEVICE-CONSTRUCTION-BACKEND-UNKNOWN";
                    assert(result.diagnostics.find(id) != result.diagnostics.npos);
                }
            }
        }
    }
}

void assertRevocationAndOwnedMetadata()
{
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)}) {
        core::pmr::String label{
            "owned metadata long enough to require storage", &core::defaultCpuMemoryResource()};
        auto caps = makeSharedDeviceCapabilities(backend, "fallback", label);
        const auto original = caps.deviceName;
        label.clear();
        assert(caps.deviceName == original);
        assert(caps.deviceName.get_allocator().resource() == &core::defaultCpuMemoryResource());
        assert(
            caps.executionStatuses.get_allocator().resource() == &core::defaultCpuMemoryResource());
        assert(caps.queryExecutionStatuses.get_allocator().resource()
            == &core::defaultCpuMemoryResource());
        assert(caps.pushConstantLoweringDiagnosticId.get_allocator().resource()
            == &core::defaultCpuMemoryResource());
        assert(caps.pushConstantLoweringTelemetryLabel.get_allocator().resource()
            == &core::defaultCpuMemoryResource());
        caps.core = {true, true};
        caps.explicitFeatures = {true, true, true};
        caps.memory.deviceLocal = caps.memory.hostVisible = caps.memory.transientAliasing = true;
        caps.swapchain = {3u, 4u};
        caps.features.computeDispatch = caps.features.nativeSync = true;
        caps.features.timestampQueries = caps.features.textureCompressionBc = true;
        caps.features.subpassInputAttachmentDegraded = true;
        caps.pushConstantLoweringHasExecutablePayload = true;
        caps.pushConstantLoweringOutcome = PushConstantLoweringOutcome::Native;
        caps.executionStatuses.push_back({EngineFeature::ComputeDispatch,
            FallbackMode::Native,
            true,
            false,
            false,
            core::pmr::String{&core::defaultCpuMemoryResource()}});
        revokeUnavailableNativeDeviceCapabilities(caps);
        synchronizeExecutionStatuses(caps);
        assert(caps.backend == backend && caps.deviceName == original);
        assert(!caps.core.graphics && !caps.core.compute);
        assert(!caps.features.computeDispatch && !caps.features.nativeSync);
        assert(!caps.features.timestampQueries && !caps.features.textureCompressionBc);
        assert(!caps.features.subpassInputAttachmentDegraded);
        assert(
            !caps.memory.deviceLocal && !caps.memory.hostVisible && !caps.memory.transientAliasing);
        assert(!caps.explicitFeatures.explicitBarriers && !caps.explicitFeatures.pushConstants);
        assert(!caps.explicitFeatures.secondaryCommandBuffers);
        assert(caps.swapchain.requestedImageCount == 0u && caps.swapchain.maxFramesInFlight == 0u);
        assert(!caps.pushConstantLoweringHasExecutablePayload);
        assert(caps.pushConstantLoweringOutcome == PushConstantLoweringOutcome::DiagnosticFailure);
        assert(caps.pushConstantLoweringDiagnosticId == "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE");
        assert(caps.executionStatuses.size() == 1u && !caps.executionStatuses.front().executable);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "seed") == 0) {
        assertMakeSharedDeviceCapabilitiesFillsCommonFields();
        assertMakeSharedDeviceCapabilitiesWorksForAllBackends();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "identity") == 0) {
        assertUnimplementedAndUnknownConstructionIsRejected();
        return 0;
    }
    if (argc != 1)
        return 2;
    assertValidDescPassesAndStampsReportFields();
    assertCapabilityContractBackendMismatchFailsFirst();
    assertZeroMemoryBudgetFails();
    assertNativeHandleExposureFails();
    assertPerDrawPipelineCreationFails();
    assertPerFrameShaderCompilationFails();
    assertInvariantCheckOrderIsContractThenBudgetThenHandlesThenDrawThenShader();
    assertMakeSharedDeviceCapabilitiesFillsCommonFields();
    assertMakeSharedDeviceCapabilitiesUsesFallbackWhenLabelEmpty();
    assertMakeSharedDeviceCapabilitiesWorksForAllBackends();
    assertUnimplementedAndUnknownConstructionIsRejected();
    assertConstructionIdentityAndOptionMatrix();
    assertRevocationAndOwnedMetadata();
    return 0;
}
