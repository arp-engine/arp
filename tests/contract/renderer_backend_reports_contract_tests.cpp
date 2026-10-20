#include "renderer/pipeline/scene_runtime_pipeline_rhi_parity.h"
#include "renderer/ui/ui_draw_plan.h"

#include <cassert>
#include <cstring>
#include <limits>
#include <type_traits>

#ifdef NDEBUG
#error Semantic report tests require assertions
#endif

int main()
{
    using namespace arp;
    using Admission = rhi::semantics::SemanticReportBackendAdmission;
    static_assert(Admission::backends.size() == 5u);
    static_assert(renderer::ui::UiRhiBackendContract::contractVersion == 2u);
    static_assert(renderer::kSceneRuntimePipelineLegacyReportVersion == 1u);
    static_assert(renderer::kSceneRuntimePipelineReportVersion == 2u);
    static_assert(renderer::kSceneRuntimePipelineBackendReportCount == 4u);
    static_assert(renderer::kSceneRuntimePipelineBackendReportCountV2 == 5u);
    static_assert(
        std::is_same_v<decltype(renderer::SceneRuntimePipelineRhiParityResult::backendReports),
            std::array<renderer::SceneRuntimePipelineBackendSemanticReport, 4u>>);
    static_assert(
        std::is_same_v<decltype(renderer::SceneRuntimePipelineRhiParityResultV2::backendReports),
            std::array<renderer::SceneRuntimePipelineBackendSemanticReport, 5u>>);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(-1),
             static_cast<rhi::Backend>(255)}) {
        for (const auto version : {0u, 1u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
            const auto decision = Admission::evaluate(backend, version);
            const bool known = backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan
                || backend == rhi::Backend::Metal || backend == rhi::Backend::WebGPU
                || backend == rhi::Backend::WebGL;
            assert(!decision.executable);
            assert(decision.knownIdentity == (version == 1u && known));
            assert(decision.inspectStructuralContract
                == (version == 1u && known && backend != rhi::Backend::WebGL));
            assert(decision.validatorOnly == (version == 1u && backend == rhi::Backend::Null));
            const char* diagnostic = version != 1u ? "RHI-SEMANTIC-REPORT-CONTRACT-VERSION"
                : !known                           ? "RHI-SEMANTIC-REPORT-BACKEND-UNKNOWN"
                : backend == rhi::Backend::WebGL   ? "RHI-WEBGL1-SEMANTIC-REPORT-UNAVAILABLE"
                                                   : nullptr;
            if (diagnostic)
                assert(std::strcmp(decision.diagnostic, diagnostic) == 0);
            else
                assert(decision.diagnostic == nullptr);
        }
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    for (const auto policy : {renderer::ui::UiAtlasPagePolicy::SingleTexture,
             renderer::ui::UiAtlasPagePolicy::TextureArray,
             renderer::ui::UiAtlasPagePolicy::MultiPage}) {
        const auto report = renderer::ui::UiRhiBackendContract().evaluate(policy);
        assert(report.backendReports.size() == 5u);
        assert(report.backendReports.back().backend == rhi::Backend::WebGL);
        assert(!report.backendReports.back().supported);
        for (std::size_t index = 0; index < Admission::backends.size(); ++index) {
            const auto& entry = report.backendReports[index];
            assert(entry.backend == Admission::backends[index]);
            assert(entry.supported == (entry.backend == rhi::Backend::Null));
            assert(!entry.exposesNativeBackendHandles);
            assert(entry.pagePolicy == policy);
            assert(entry.loweringMode != renderer::ui::UiAtlasPageLoweringMode::Unspecified);
            if (entry.backend == rhi::Backend::WebGL) {
                assert(entry.diagnostics.find("RHI-WEBGL1-SEMANTIC-REPORT-UNAVAILABLE") == 0u);
                assert(!entry.textureArraySupported && !entry.multiPageBatchingSupported
                    && !entry.dynamicUploadSupported);
            }
        }
    }
    for (const auto invalid : {static_cast<renderer::ui::UiAtlasPagePolicy>(-1),
             static_cast<renderer::ui::UiAtlasPagePolicy>(255)}) {
        const auto report = renderer::ui::UiRhiBackendContract().evaluate(invalid);
        assert(report.semantic.loweringMode == renderer::ui::UiAtlasPageLoweringMode::Unspecified);
        assert(report.semantic.atlasPagePolicy == "unknown");
        assert(report.backendReports.size() == 5u);
        for (std::size_t index = 0; index < Admission::backends.size(); ++index) {
            const auto& entry = report.backendReports[index];
            assert(entry.backend == Admission::backends[index]);
            assert(!entry.supported && !entry.exposesNativeBackendHandles);
            assert(entry.loweringMode == renderer::ui::UiAtlasPageLoweringMode::Unspecified);
            assert(entry.diagnostics == "RHI-UI-SEMANTIC-REPORT-POLICY-UNKNOWN");
            assert(!entry.textureArraySupported && !entry.multiPageBatchingSupported
                && !entry.dynamicUploadSupported);
        }
    }
}
