#include "editor/viewport/editor_scene_viewport_sprite_pass_contract.h"
#include "editor/viewport/editor_scene_viewport_sprite_projection.h"
#include "editor/viewport/editor_scene_viewport_tilemap_visibility.h"

#include <cassert>
#include <limits>
#include <string_view>
#include <utility>

int main()
{
    using namespace arp::editor;
    namespace core = arp::core;
    namespace scene2d = arp::scene2d;

    constexpr EditorSceneViewportSpritePassBudget budget{128u, 16u, 1024u * 1024u};
    EditorSceneViewportSpritePassDemand demand;
    demand.drawItems = 128u;
    demand.uniqueAtlases = 16u;
    demand.transientUploadBytes = 1024u * 1024u;
    assert(validateEditorSceneViewportSpritePass(demand, budget).valid());

    demand.drawItems++;
    auto result = validateEditorSceneViewportSpritePass(demand, budget);
    assert(result.diagnostic == EditorSceneViewportSpritePassDiagnostic::DrawBudgetExceeded);
    assert(std::string_view(editorSceneViewportSpritePassDiagnosticCode(result.diagnostic))
        == "ARP-EDITOR-VIEWPORT-SPRITE-PASS-DRAW-BUDGET-EXCEEDED");

    demand.drawItems = 0;
    demand.uniqueAtlases++;
    result = validateEditorSceneViewportSpritePass(demand, budget);
    assert(result.diagnostic == EditorSceneViewportSpritePassDiagnostic::AtlasBudgetExceeded);

    demand.uniqueAtlases = 0;
    demand.transientUploadBytes++;
    result = validateEditorSceneViewportSpritePass(demand, budget);
    assert(result.diagnostic == EditorSceneViewportSpritePassDiagnostic::UploadBudgetExceeded);

    demand.contractVersion++;
    result = validateEditorSceneViewportSpritePass(demand, budget);
    assert(
        result.diagnostic == EditorSceneViewportSpritePassDiagnostic::UnsupportedContractVersion);

    demand.contractVersion = kEditorSceneViewportSpritePassContractVersion;
    result = validateEditorSceneViewportSpritePass(demand, {});
    assert(result.diagnostic == EditorSceneViewportSpritePassDiagnostic::InvalidBudget);

    assert(EditorSceneViewportSpritePassSemantics::colorLoad
        == EditorSceneViewportSpritePassSemantics::ColorLoad::LoadExistingOpaqueColor);
    assert(EditorSceneViewportSpritePassSemantics::depthLoad
        == EditorSceneViewportSpritePassSemantics::DepthLoad::LoadExistingOpaqueDepth);
    assert(EditorSceneViewportSpritePassSemantics::depthAccess
        == EditorSceneViewportSpritePassSemantics::DepthAccess::TestLessEqualWithoutWrite);
    assert(EditorSceneViewportSpritePassSemantics::blend
        == EditorSceneViewportSpritePassSemantics::Blend::PremultipliedSourceOver);

    scene2d::SpriteInstance instance;
    instance.origin = {2.0f, 3.0f, 5.0f, 0.0f};
    instance.axisX = {7.0f, 11.0f, 13.0f, 0.0f};
    instance.axisY = {17.0f, 19.0f, 23.0f, 0.0f};
    instance.uv = {0.1f, 0.2f, 0.3f, 0.4f};
    instance.color = {0.5f, 0.6f, 0.7f, 0.8f};
    instance.parameters = {1.0f, 0.0f, 0.0f, 0.0f};
    core::Mat4 asymmetric;
    asymmetric.m = {2.0f,
        3.0f,
        5.0f,
        7.0f,
        11.0f,
        13.0f,
        17.0f,
        19.0f,
        23.0f,
        29.0f,
        31.0f,
        37.0f,
        41.0f,
        43.0f,
        47.0f,
        53.0f};
    scene2d::SpriteInstance projected;
    assert(projectEditorSceneViewportSpriteInstance(instance, asymmetric, projected));
    assert(projected.origin.x == 45.0f && projected.origin.y == 165.0f
        && projected.origin.z == 325.0f && projected.origin.w == 499.0f);
    assert(projected.axisX.x == 112.0f && projected.axisX.y == 441.0f && projected.axisX.z == 883.0f
        && projected.axisX.w == 1371.0f);
    assert(projected.axisY.x == 206.0f && projected.axisY.y == 825.0f
        && projected.axisY.z == 1655.0f && projected.axisY.w == 2595.0f);
    assert(projected.uv.x == instance.uv.x && projected.color.w == instance.color.w
        && projected.parameters.x == instance.parameters.x);
    asymmetric.m[0] = std::numeric_limits<float>::infinity();
    assert(!projectEditorSceneViewportSpriteInstance(instance, asymmetric, projected));

    const auto tileMapViewProjection =
        core::multiply(core::perspective(60.0f, 16.0f / 9.0f, 0.05f, 10000.0f),
            core::lookAt({-4.0f, -2.5f, 3.0f}, {0.0f, 0.0f, 0.0f}));
    const auto tileMapVisibleWorld =
        editorSceneViewportTileMapVisibleWorld(tileMapViewProjection, 1600u, 900u, 0.0f);
    assert(tileMapVisibleWorld.valid());
    assert(tileMapVisibleWorld.worldBounds.width > 0.0f
        && tileMapVisibleWorld.worldBounds.height > 0.0f);
    assert(editorSceneViewportTileMapVisibleWorld(tileMapViewProjection, 800u, 800u, 20000.0f)
               .diagnostic
        == EditorSceneViewportTileMapVisibilityDiagnostic::InvalidIntersection);

    const auto nullPlan =
        lowerEditorSceneViewportSpritePass(EditorSceneViewportSpritePassBackend::Null);
    assert(nullPlan.disposition
        == EditorSceneViewportSpritePassBackendDisposition::ContractValidationOnly);
    assert(admitEditorSceneViewportSpritePass(
               nullPlan, EditorSceneViewportSpritePassBackend::Null, false, false)
               .diagnostic
        == EditorSceneViewportSpritePassAdmissionDiagnostic::NullValidatorOnly);

    for (const auto [backend, disposition] :
        {std::pair{EditorSceneViewportSpritePassBackend::Vulkan,
             EditorSceneViewportSpritePassBackendDisposition::NativeL2MappingDefined},
            std::pair{EditorSceneViewportSpritePassBackend::Metal,
                EditorSceneViewportSpritePassBackendDisposition::NativeL2MappingDefined},
            std::pair{EditorSceneViewportSpritePassBackend::WebGPU,
                EditorSceneViewportSpritePassBackendDisposition::EquivalentL2MappingDefined}}) {
        auto plan = lowerEditorSceneViewportSpritePass(backend);
        assert(plan.disposition == disposition);
        const bool vulkan = backend == EditorSceneViewportSpritePassBackend::Vulkan;
        const auto admission = admitEditorSceneViewportSpritePass(plan, backend, vulkan, vulkan);
        assert(admission.executable() == vulkan);
        if (vulkan) {
            assert(admission.diagnostic == EditorSceneViewportSpritePassAdmissionDiagnostic::None);
            assert(!admitEditorSceneViewportSpritePass(plan, backend, false, true).executable());
            assert(admitEditorSceneViewportSpritePass(plan, backend, false, true).diagnostic
                == EditorSceneViewportSpritePassAdmissionDiagnostic::CapabilityUnavailable);
            assert(!admitEditorSceneViewportSpritePass(plan, backend, true, false).executable());
            assert(admitEditorSceneViewportSpritePass(plan, backend, true, false).diagnostic
                == EditorSceneViewportSpritePassAdmissionDiagnostic::ExecutorPayloadUnavailable);
        } else {
            assert(admission.diagnostic
                == EditorSceneViewportSpritePassAdmissionDiagnostic::CapabilityUnavailable);
        }
        assert(admitEditorSceneViewportSpritePass(plan,
                   backend == EditorSceneViewportSpritePassBackend::Vulkan
                       ? EditorSceneViewportSpritePassBackend::Metal
                       : EditorSceneViewportSpritePassBackend::Vulkan,
                   true,
                   true)
                   .diagnostic
            == EditorSceneViewportSpritePassAdmissionDiagnostic::BackendMismatch);
        plan.contractVersion++;
        assert(admitEditorSceneViewportSpritePass(plan, backend, true, true).diagnostic
            == EditorSceneViewportSpritePassAdmissionDiagnostic::UnsupportedPlanVersion);
        plan.contractVersion = kEditorSceneViewportSpritePassContractVersion;
        plan.disposition = static_cast<EditorSceneViewportSpritePassBackendDisposition>(255);
        assert(admitEditorSceneViewportSpritePass(plan, backend, true, true).diagnostic
            == EditorSceneViewportSpritePassAdmissionDiagnostic::ExecutorPayloadUnavailable);
    }
}
