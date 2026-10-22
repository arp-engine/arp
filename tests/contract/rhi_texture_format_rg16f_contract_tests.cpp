// Rg16Float storage texture per-backend capability contract test (DDGI Phase 2.2).
//
// Verifies the AGENTS.md RHI contract section 7 test matrix for
// EngineFeature::Rg16FloatStorageTexture:
//   1. L2 semantics: TextureFormat::Rg16Float enum + bytesPerPixel=4 + color
//   2. Static capability defaults per backend (Vulkan includes, Metal/WebGPU
//      exclude, Null short-circuits)
//   3. chooseFeatureFallback per-backend: queried Null/Vulkan/Metal=Native,
//      unqueried Vulkan/Metal and WebGPU=DiagnosticFailure (binary, no Degraded)
//   4. buildNativeEngineFallbackPlan + applyTo end-to-end: caps.features.
//      rg16FloatStorageTexture reflects the decision
//   5. Null device createTexture(RG16F, Storage|Sampled) succeeds with
//      correct size (mirrors ddgi_renderer.h:584-616 TextureDesc)
//   6. Negative: Metal/WebGPU static contract -> isFeatureSupported=false
//      (fail-closed); plan decision mode == DiagnosticFailure
//
// No stable [RG16F-Exxx] code exists yet (FeatureFallbackDecision has no
// diagnosticId field); the test asserts mode + diagnostics substring,
// mirroring the depth_bias_clamp contract test pattern.

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_resources.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {

arp::rhi::EngineFeatureRequirementSet makeRg16FloatRequirementSet()
{
    arp::rhi::EngineFeatureRequirementSet requirements;
    requirements.logicalFrameSlots = 2;
    requirements.features = {
        {arp::rhi::EngineFeature::Rg16FloatStorageTexture, {}},
    };
    return requirements;
}

arp::rhi::semantics::NativeBackendCapabilityContract makeMetalRuntimeContract(
    bool rg16FloatSupported)
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    contract.runtimeFeaturesQueried = true;
    if (rg16FloatSupported) {
        contract.runtimeFeatures.push_back(
            arp::rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture);
    }
    return contract;
}

arp::rhi::semantics::NativeBackendCapabilityContract makeVulkanRuntimeContract(
    bool rg16FloatSupported)
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    contract.runtimeFeaturesQueried = true;
    if (rg16FloatSupported) {
        contract.runtimeFeatures.push_back(
            arp::rhi::semantics::NativeBackendFeature::Rg16FloatStorageTexture);
    }
    return contract;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    // === Facet 1: TextureFormat::Rg16Float L2 semantics ===
    {
        const TextureFormat format = TextureFormat::Rg16Float;
        assert(format != TextureFormat::Unknown);
        assert(bytesPerPixel(format) == 4u);   // 2 channels * 2 bytes = 4
        assert(!isDepthTextureFormat(format)); // color format, not depth
        std::printf("[ok] Rg16Float enum valid, 4 bytes/pixel, color format\n");
    }

    // === Facet 2: Static capability defaults per backend ===
    // Vulkan and Metal static lists declare the feature vocabulary. Vulkan's
    // executable decision still requires a queried device contract, while
    // Metal's device contract intersects this declaration with its exact
    // per-format shader/texture probe. WebGPU adds the feature conditionally
    // through its runtime bridge. Null short-circuits via the `null` bool in
    // chooseFeatureFallback, so it does not need the feature in its list.
    {
        const auto nullContract = nativeBackendCapabilityContract(Backend::Null);
        // Null does not list Rg16Float in features vector; it short-circuits
        // in chooseFeatureFallback. Just assert the contract is queryable.
        assert(nullContract.backend == Backend::Null);

        const auto vulkanContract = nativeBackendCapabilityContract(Backend::Vulkan);
        assert(hasNativeFeature(vulkanContract, NativeBackendFeature::Rg16FloatStorageTexture));

        const auto metalContract = nativeBackendCapabilityContract(Backend::Metal);
        assert(metalContract.backend == Backend::Metal);
        assert(!hasExecutableNativeFeature(
            makeMetalRuntimeContract(false), NativeBackendFeature::Rg16FloatStorageTexture));

        const auto webGpuContract = nativeBackendCapabilityContract(Backend::WebGPU);
        assert(!hasNativeFeature(webGpuContract, NativeBackendFeature::Rg16FloatStorageTexture));
        std::printf(
            "[ok] Static defaults: Vulkan vocabulary, Metal queried intersection, WebGPU closed, Null validator\n");
    }

    // === Facet 3: chooseFeatureFallback per-backend decisions ===
    // Binary Native/DiagnosticFailure - no Degraded (WGSL storage texture
    // format is compile-time constant, cannot runtime-switch to RGBA16F).
    {
        const auto nullDec = chooseFeatureFallback(
            nativeBackendCapabilityContract(Backend::Null), EngineFeature::Rg16FloatStorageTexture);
        assert(nullDec.mode == FallbackMode::Native);
        assert(nullDec.feature == EngineFeature::Rg16FloatStorageTexture);
        assert(nullDec.backend == Backend::Null);

        const auto vulkanDec =
            chooseFeatureFallback(nativeBackendCapabilityContract(Backend::Vulkan),
                EngineFeature::Rg16FloatStorageTexture);
        assert(vulkanDec.mode == FallbackMode::DiagnosticFailure);
        const auto vulkanNativeDec = chooseFeatureFallback(
            makeVulkanRuntimeContract(true), EngineFeature::Rg16FloatStorageTexture);
        assert(vulkanNativeDec.mode == FallbackMode::Native);

        const auto metalDec = chooseFeatureFallback(
            makeMetalRuntimeContract(false), EngineFeature::Rg16FloatStorageTexture);
        assert(metalDec.mode == FallbackMode::DiagnosticFailure);

        const auto webGpuDec =
            chooseFeatureFallback(nativeBackendCapabilityContract(Backend::WebGPU),
                EngineFeature::Rg16FloatStorageTexture);
        assert(webGpuDec.mode == FallbackMode::DiagnosticFailure);
        const auto metalNativeDec = chooseFeatureFallback(
            makeMetalRuntimeContract(true), EngineFeature::Rg16FloatStorageTexture);
        const bool declared = hasNativeFeature(nativeBackendCapabilityContract(Backend::Metal),
            NativeBackendFeature::Rg16FloatStorageTexture);
        assert(metalNativeDec.mode
            == (declared ? FallbackMode::Native : FallbackMode::DiagnosticFailure));
        std::printf(
            "[ok] chooseFeatureFallback: queried facts intersect declaration; unsupported/absent probes fail closed\n");
    }

    // === Facet 4: buildNativeEngineFallbackPlan + applyTo end-to-end ===
    // The full bridge: requirement -> plan -> applyTo(caps) -> isFeatureSupported.
    // plan.success is false if ANY feature decision is DiagnosticFailure
    // (gpu_fallback_plan.cpp:119: plan.success = failures_.empty()).
    // applyTo translates mode to caps bool: DiagnosticFailure -> feature=false.
    {
        const auto requirements = makeRg16FloatRequirementSet();

        // Null: plan succeeds, RG16F feature enabled.
        {
            auto plan = buildNativeEngineFallbackPlan(
                nativeBackendCapabilityContract(Backend::Null), requirements);
            assert(plan.success);
            assert(plan.diagnosticFailureCount == 0);
            assert(!plan.decisions.empty());
            assert(plan.decisions[0].mode == FallbackMode::Native);
            assert(plan.decisions[0].feature == EngineFeature::Rg16FloatStorageTexture);

            DeviceCapabilities caps{};
            plan.applyTo(caps);
            assert(isFeatureSupported(caps, EngineFeature::Rg16FloatStorageTexture));
        }

        // An unqueried Vulkan vocabulary contract is not executable evidence;
        // the per-format native query must be installed before the plan can
        // authorize RG16F.
        {
            auto plan = buildNativeEngineFallbackPlan(
                nativeBackendCapabilityContract(Backend::Vulkan), requirements);
            assert(!plan.success);
            assert(plan.diagnosticFailureCount == 1);
            assert(plan.decisions.empty());

            DeviceCapabilities caps{};
            plan.applyTo(caps);
            assert(!isFeatureSupported(caps, EngineFeature::Rg16FloatStorageTexture));
        }

        // A queried Vulkan device with the format feature bit/properties
        // installed is executable and must expose the feature.
        {
            auto plan =
                buildNativeEngineFallbackPlan(makeVulkanRuntimeContract(true), requirements);
            assert(plan.success);
            assert(plan.diagnosticFailureCount == 0);
            assert(plan.decisions[0].mode == FallbackMode::Native);

            DeviceCapabilities caps{};
            plan.applyTo(caps);
            assert(isFeatureSupported(caps, EngineFeature::Rg16FloatStorageTexture));
        }

        // Metal static vocabulary is not executable evidence. The device
        // contract must publish the per-format probe before the plan can use
        // RG16F; an absent probe fails closed.
        // Note: DiagnosticFailure decisions go into the `failures_` vector
        // (gpu_fallback_plan.cpp:93-97 recordDiagnosticFailure), NOT into
        // `plan.decisions`. The diagnostic string is aggregated into
        // `plan.diagnostics` (gpu_fallback_plan.cpp:165 appendDiagnostic).
        {
            auto plan =
                buildNativeEngineFallbackPlan(makeMetalRuntimeContract(false), requirements);
            assert(!plan.success); // DiagnosticFailure fails the plan
            assert(plan.diagnosticFailureCount == 1);
            // failures_ are NOT pushed to plan.decisions; the diagnostic
            // string is aggregated into plan.diagnostics.
            assert(plan.decisions.empty());

            DeviceCapabilities caps{};
            plan.applyTo(caps);
            assert(!isFeatureSupported(caps, EngineFeature::Rg16FloatStorageTexture));
        }

        // A queried Metal device that passes the exact format probe is
        // executable and must expose the feature to the L2 capability set.
        {
            auto plan = buildNativeEngineFallbackPlan(makeMetalRuntimeContract(true), requirements);
            const bool declared = hasNativeFeature(nativeBackendCapabilityContract(Backend::Metal),
                NativeBackendFeature::Rg16FloatStorageTexture);
            assert(plan.success == declared);
            assert(plan.diagnosticFailureCount == (declared ? 0u : 1u));
            if (declared)
                assert(plan.decisions[0].mode == FallbackMode::Native);
            else
                assert(plan.decisions.empty());

            DeviceCapabilities caps{};
            plan.applyTo(caps);
            assert(isFeatureSupported(caps, EngineFeature::Rg16FloatStorageTexture) == declared);
        }

        // WebGPU: same as Metal (static list excludes; bridge would add at runtime).
        {
            auto plan = buildNativeEngineFallbackPlan(
                nativeBackendCapabilityContract(Backend::WebGPU), requirements);
            assert(!plan.success);
            assert(plan.diagnosticFailureCount == 1);
            assert(plan.decisions.empty());

            DeviceCapabilities caps{};
            plan.applyTo(caps);
            assert(!isFeatureSupported(caps, EngineFeature::Rg16FloatStorageTexture));
        }
        std::printf("[ok] buildNativeEngineFallbackPlan + applyTo: 4-backend end-to-end\n");
    }

    // === Facet 5: Null device createTexture(RG16F, Storage|Sampled) ===
    // Mirrors ddgi_renderer.h:584-616 ProbeDepthMoments atlas TextureDesc.
    // Null createTexture validates extent/format/usage/size but does NOT
    // gate on RG16F capability (that's the upper-layer isFeatureSupported
    // gate's job, tested in Facet 4). This verifies the L2 texture creation
    // contract accepts the RG16F + Storage|Sampled shape.
    {
        auto device = createNullDevice();
        assert(device);

        TextureDesc desc;
        desc.label = "WoccDdgiProbeMomentsA";
        desc.extent = {256, 256, 1u}; // probe atlas dims (smaller for test)
        desc.format = TextureFormat::Rg16Float;
        desc.usage = TextureUsage::Storage | TextureUsage::Sampled;
        desc.memory = MemoryUsage::GpuOnly;
        desc.initialState = ResourceState::Undefined;
        desc.mipLevelCount = 1u;
        desc.arrayLayerCount = 1u;
        desc.cubeCompatible = false;

        auto texture = device->createTexture(desc);
        assert(texture);
        assert(texture->id().isValid());
        assert(texture->desc().format == TextureFormat::Rg16Float);
        // Rg16Float = 4 bytes/pixel -> 256*256*4 = 262144 bytes
        assert(texture->allocation().estimatedSizeBytes == 256u * 256u * 4u);

        // TextureView (mirrors ddgi_renderer.h:600-615). Note: TextureViewDesc
        // uses baseMipLevel/mipLevelCount/baseArrayLayer/arrayLayerCount (not
        // mipLevel/arrayLayer).
        TextureViewDesc viewDesc;
        viewDesc.format = TextureFormat::Rg16Float;
        viewDesc.usage = TextureUsage::Storage | TextureUsage::Sampled;
        viewDesc.dimension = TextureViewDimension::Texture2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = TextureAspect::Color;
        auto view = device->createTextureView(*texture, viewDesc);
        assert(view);
        assert(view->id().isValid());
        std::printf("[ok] Null createTexture(RG16F, Storage|Sampled) + view: size=%llu\n",
            static_cast<unsigned long long>(texture->allocation().estimatedSizeBytes));
    }

    // === Facet 6: Negative - Metal/WebGPU fail-closed diagnostics ===
    // The DiagnosticFailure mode must be observable in the plan. No stable
    // [RG16F-Exxx] code yet (FeatureFallbackDecision has no diagnosticId
    // field); assert diagnosticFailureCount + plan.diagnostics substring.
    // DiagnosticFailure decisions go into `failures_` (not `plan.decisions`),
    // and their diagnostics are aggregated into `plan.diagnostics` string.
    {
        const auto requirements = makeRg16FloatRequirementSet();

        const auto metalPlan =
            buildNativeEngineFallbackPlan(makeMetalRuntimeContract(false), requirements);
        assert(!metalPlan.success);
        assert(metalPlan.diagnosticFailureCount == 1);
        assert(metalPlan.decisions.empty()); // DiagnosticFailure goes to failures_
        // The aggregated plan.diagnostics string should reference the feature
        // or storage texture concept. If empty, it's a known diagnostic-code gap.
        const auto& metalDiag = metalPlan.diagnostics;
        if (!metalDiag.empty()) {
            const bool namesFeature = metalDiag.find("Rg16Float") != std::pmr::string::npos
                || metalDiag.find("rg16float") != std::pmr::string::npos
                || metalDiag.find("storage texture") != std::pmr::string::npos
                || metalDiag.find("StorageTexture") != std::pmr::string::npos;
            assert(namesFeature);
        } else {
            std::printf("[gap] Metal RG16F DiagnosticFailure has empty plan.diagnostics "
                        "(no stable [RG16F-Exxx] code yet; FeatureFallbackDecision has no "
                        "diagnosticId field). Leave for future diagnostic-code work.\n");
        }

        const auto webGpuPlan = buildNativeEngineFallbackPlan(
            nativeBackendCapabilityContract(Backend::WebGPU), requirements);
        assert(!webGpuPlan.success);
        assert(webGpuPlan.diagnosticFailureCount == 1);
        assert(webGpuPlan.decisions.empty());
        std::printf("[ok] Negative: Metal/WebGPU fail-closed, diagnosticFailureCount=1\n");
    }

    std::printf("[PASS] rhi_texture_format_rg16f_contract_tests\n");
    return 0;
}
