#include "core/file_io.h"
#include "core/sha256.h"
#include "rhi/backends/webgpu/webgpu_original_canvas.h"
#include "rhi/conformance/clear_execution.h"
#include "rhi/conformance/clear_workload.h"
#include "rhi/conformance/matrix.h"
#include "rhi/frontend/resource_texture_resource_layout.h"
#include "rhi/semantics/conformance_evidence_admission.h"

#include <cassert>
#include <cstring>
#include <type_traits>

using namespace arp;

int main()
{
    static_assert(!std::is_default_constructible_v<rhi_webgpu::WebGpuOriginalCanvas>);
    static_assert(!std::is_copy_constructible_v<rhi_webgpu::WebGpuOriginalCanvas>);
    rhi::SwapchainDesc surface;
    surface.extent = {48, 32};
    surface.format = rhi::TextureFormat::Bgra8Unorm;
    const auto canvas = rhi_webgpu::WebGpuCanvasTextureContract::describe(surface);
    assert(canvas.extent.width == 48 && canvas.extent.height == 32 && canvas.extent.depth == 1);
    assert(canvas.format == surface.format && canvas.initialState == rhi::ResourceState::Undefined);
    assert(canvas.usage == (rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc));
    assert((canvas.usage & rhi::TextureUsage::CopyDst) == rhi::TextureUsage::None);
    assert(rhi::TextureResourceLayout::capture(canvas).matches(canvas));
    static_assert(!std::is_default_constructible_v<rhi::OriginalConformanceRun>);
    static_assert(!std::is_copy_constructible_v<rhi::OriginalConformanceRun>);
    static_assert(!std::is_move_constructible_v<rhi::OriginalConformanceRun>);
    const auto absent = rhi::RhiConformanceRunner::auditOriginal(nullptr);
    assert(!absent.originalOutputValidated && !absent.productionReady && absent.diagnostic);
    rhi::OriginalConformanceRequest observation{1, 3, true, 4096 * 4 * 3, 256 * 1024};
    assert(!rhi::OriginalConformanceContract::validate(observation));
    rhi::ConformanceClearExecutionRequest native{observation, 1024 * 1024, 5000000000};
    assert(!rhi::ConformanceClearExecutionContract::validate(native));
    auto invalidNative = native;
    invalidNative.maxNativeReadbackBytes = 0;
    assert(rhi::ConformanceClearExecutionContract::validate(invalidNative));
    invalidNative = native;
    invalidNative.completionTimeoutNanoseconds = 0;
    assert(rhi::ConformanceClearExecutionContract::validate(invalidNative));
    invalidNative.completionTimeoutNanoseconds = UINT64_MAX;
    assert(rhi::ConformanceClearExecutionContract::validate(invalidNative));
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Metal,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)})
        assert(rhi::ConformanceClearExecutionContract::loweringDiagnostic(backend));
    assert(!rhi::ConformanceClearExecutionContract::loweringDiagnostic(rhi::Backend::Vulkan));
    assert(!rhi::ConformanceClearExecutionContract::loweringDiagnostic(rhi::Backend::WebGPU));
    for (int field = 0; field < 5; ++field) {
        auto invalid = observation;
        if (field == 0)
            invalid.contractVersion = 0;
        if (field == 1)
            invalid.prototypeOptIn = false;
        if (field == 2)
            invalid.maxFrames = 0;
        if (field == 3)
            invalid.maxOutputBytes = 0;
        if (field == 4)
            invalid.maxCpuMetadataBytes = 0;
        assert(rhi::OriginalConformanceContract::validate(invalid));
    }
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
        assert(rhi::OriginalConformanceContract::sourceDiagnostic(backend));
    assert(!rhi::OriginalConformanceContract::sourceDiagnostic(rhi::Backend::WebGL));
    assert(rhi::OriginalConformanceContract::sourceDiagnostic(static_cast<rhi::Backend>(255)));
    constexpr rhi::ConformanceWorkloadBudget budget{8192, 4096, 3};
    const auto input = core::readTextFile(
        ARP_SOURCE_DIR "/tests/fixtures/rhi_conformance_clear.yaml", budget.maxYamlBytes);
    assert(input.success);
    const char* id = nullptr;
    auto workload = rhi::ConformanceClearWorkload::parse(input.text, budget, id);
    assert(workload && !id && workload->frames().size() == 3);
    assert(workload->sourceSha256() == core::Sha256::hexDigest(input.text));
    assert(workload->frames()[1].extent.width == 48 && workload->frames()[1].extent.height == 32);
    assert(workload->frames()[0].rgba8[0] == 32);
    const auto color = workload->frames()[0].clearColor();
    assert(color.r == 32.f / 255.f && color.g == 128.f / 255.f && color.a == 1.f);
    for (int mode = 0; mode < 5; ++mode) {
        auto low = budget;
        if (mode == 0)
            low.maxFrames = 2;
        if (mode == 1)
            low.maxPixelsPerFrame = 4095;
        if (mode == 2)
            low.maxYamlBytes = input.text.size() - 1;
        if (mode == 3)
            low.maxFrames = 0;
        if (mode == 4)
            low.maxPixelsPerFrame = 0;
        assert(!rhi::ConformanceClearWorkload::parse(input.text, low, id));
        assert(std::strcmp(id, "RHI-CONFORMANCE-WORKLOAD-BUDGET") == 0);
    }
    const auto rejected = [&](core::StringView old, core::StringView replacement) {
        auto invalid = input.text;
        const auto offset = invalid.find(old);
        assert(offset != core::pmr::String::npos);
        invalid.replace(offset, old.size(), replacement);
        assert(!rhi::ConformanceClearWorkload::parse(invalid, budget, id) && id);
    };
    rejected("contractVersion: 1", "contractVersion: 2");
    rejected("contractVersion: 1", "contractVersion: true");
    rejected("width: 64", "width: 0");
    rejected("width: 64", "width: 4294967296");
    rejected("width: 64", "width: -1");
    rejected("height: 64", "height: false");
    rejected("[32, 128, 191, 255]", "[32, 128, 191, 0]");
    rejected("[32, 128, 191, 255]", "[32, 128, 191, 255, 0]");
    rejected("[32, 128, 191, 255]", "[256, 128, 191, 255]");
    rejected("[32, 128, 191, 255]", "[32.0, 128, 191, 255]");
    rejected("type: ConformanceClearWorkload", "type: Triangle");
    rejected("id: clear-resize-linear-unorm8-v1", "id: ''");
    rejected("id: clear-resize-linear-unorm8-v1", "id: one\nextra: true");
    rejected("extent: {width: 64, height: 64}", "extent: {width: 64, height: 64, extra: 0}");
    auto whitespace = input.text + "\n";
    auto changed = rhi::ConformanceClearWorkload::parse(whitespace, budget, id);
    assert(changed && changed->sourceSha256() != workload->sourceSha256());
    const auto secondDocument = input.text + "\n---\nignored: true\n";
    assert(!rhi::ConformanceClearWorkload::parse(secondDocument, budget, id));
    assert(std::strcmp(id, "RHI-CONFORMANCE-WORKLOAD-SCHEMA") == 0);
    // Intent and its digest cannot reopen any legacy backend readiness gate.
    for (auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL}) {
        assert(rhi::semantics::ConformanceEvidenceAdmission::descriptorDiagnostic(backend));
        assert(rhi::semantics::ConformanceEvidenceAdmission::runtimeDiagnostic(backend));
        assert(rhi::semantics::ConformanceEvidenceAdmission::platformDiagnostic(backend));
    }
}
