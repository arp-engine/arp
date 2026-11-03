#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "rhi/semantics/webgl_pipeline_creation.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace arp::rhi;
using namespace arp::rhi::semantics;
std::size_t checks = 0;

void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}

RenderPipelineDesc descriptor()
{
    RenderPipelineDesc desc;
    desc.label = "ColorBlendFactors/1";
    desc.colorFormat = TextureFormat::Rgba8Unorm;
    desc.colorTargets.push_back(
        {TextureFormat::Rgba8Unorm, true, {}, {}, ColorWriteMask::All, false});
    desc.hasViewport = desc.hasScissor = true;
    desc.viewport = {0, 0, 64, 64, 0, 1};
    desc.scissor = {0, 0, 64, 64};
    desc.depthFormat = TextureFormat::Unknown;
    desc.depthStencil.depthTestEnabled = desc.depthStencil.depthWriteEnabled = false;
    return desc;
}
} // namespace

int main()
{
    try {
        static_assert(static_cast<int>(BlendFactor::OneMinusDstAlpha) == 5);
        static_assert(static_cast<int>(BlendFactor::SrcColor) == 6);
        static_assert(static_cast<int>(BlendFactor::OneMinusDstColor) == 9);
        const std::array<Backend, 4> backends{
            Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU};
        std::array<std::uint64_t, 10> keys{};
        for (std::size_t value = 0; value < keys.size(); ++value) {
            auto desc = descriptor();
            desc.colorTargets[0].colorBlend.sourceFactor = static_cast<BlendFactor>(value);
            desc.colorTargets[0].alphaBlend.destinationFactor = static_cast<BlendFactor>(value);
            check(validateRenderPipelineState(desc).success, "Valid component factor refused");
            const WebGlPipelineCreationRequest webGlRequest{1, {1, 1024, 4096}, {}};
            check(
                WebGlPipelineCreationContract::state(Backend::WebGL, desc, webGlRequest) == nullptr,
                "WebGL component factor admission differs from L2");
            keys[value] = stableRenderPipelineStateKey(desc);
            for (std::size_t prior = 0; prior < value; ++prior)
                check(keys[prior] != keys[value], "Distinct blend factors aliased pipeline keys");
            for (auto backend : backends) {
                const auto lowered =
                    lowerRenderPipelineState(nativeBackendCapabilityContract(backend), desc);
                check(lowered.success && lowered.blendWriteMaskLowered
                        && lowered.pipelineCacheKey == keys[value],
                    "Core backend failed component factor lowering");
                check(hasNativePipelineStateSemantic(
                          lowered, NativePipelineStateSemantic::L2ComponentColorBlendFactors)
                        == (value >= 6),
                    "Lowering lost component-factor selection");
            }
        }
        for (bool enabled : {false, true}) {
            for (int slot = 0; slot < 4; ++slot) {
                auto desc = descriptor();
                auto& target = desc.colorTargets[0];
                target.blendEnabled = enabled;
                auto& blend = slot < 2 ? target.colorBlend : target.alphaBlend;
                (slot % 2 ? blend.destinationFactor : blend.sourceFactor) =
                    static_cast<BlendFactor>(255);
                const auto rejected = validateRenderPipelineState(desc);
                check(!rejected.success && rejected.unsupportedStateDiagnosed
                        && rejected.diagnostics == "RHI-PIPELINE-BLEND-FACTOR",
                    "Unknown blend factor accepted");
                for (auto backend : backends) {
                    const auto lowered =
                        lowerRenderPipelineState(nativeBackendCapabilityContract(backend), desc);
                    check(!lowered.success
                            && lowered.fallbackMode == FallbackMode::DiagnosticFailure
                            && lowered.diagnostics == "RHI-PIPELINE-BLEND-FACTOR",
                        "Backend substituted an invalid factor");
                }
            }
        }
        auto bad = descriptor();
        bad.colorTargets[0].alphaBlend.operation = static_cast<BlendOp>(255);
        check(validateRenderPipelineState(bad).diagnostics == "RHI-PIPELINE-BLEND-OPERATION",
            "Unknown blend operation accepted");
        auto disabled = descriptor();
        disabled.colorTargets[0].colorBlend.sourceFactor = BlendFactor::DstColor;
        disabled.colorTargets[0].blendEnabled = false;
        const auto lowered =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), disabled);
        check(lowered.success
                && !hasNativePipelineStateSemantic(
                    lowered, NativePipelineStateSemantic::L2ComponentColorBlendFactors),
            "Disabled blend acquired execution semantics");
        std::cout << "ColorBlendFactors/1 contract checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
