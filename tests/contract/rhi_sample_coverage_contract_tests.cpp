#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;
    assert(!sampleMaskHasFullCoverage(0u, UINT64_MAX));
    assert(!sampleMaskHasFullCoverage(65u, UINT64_MAX));
    for (const auto count : {1u, 2u, 4u, 8u, 16u, 32u, 64u}) {
        const auto active = count == 64u ? UINT64_MAX : (std::uint64_t{1} << count) - 1u;
        assert(sampleMaskHasFullCoverage(count, active));
        assert(sampleMaskHasFullCoverage(count, UINT64_MAX));
        assert(!sampleMaskHasFullCoverage(count, active & ~std::uint64_t{1}));
        assert(!sampleMaskHasFullCoverage(count, 0u));
    }
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        auto contract = nativeBackendCapabilityContract(backend);
        RenderPipelineDesc desc;
        desc.colorFormat = TextureFormat::Bgra8Unorm;
        auto full = lowerRenderPipelineState(contract, desc);
        assert(full.success);
        desc.multisample.sampleMask = 0u;
        auto zero = lowerRenderPipelineState(contract, desc);
        if (backend == Backend::Metal) {
            assert(!zero.success);
            assert(zero.fallbackMode == FallbackMode::DiagnosticFailure);
            assert(zero.diagnostics == "RHI-METAL-SAMPLE-MASK-SHADER-LOWERING-REQUIRED");
        } else {
            assert(zero.success);
        }
    }
}
