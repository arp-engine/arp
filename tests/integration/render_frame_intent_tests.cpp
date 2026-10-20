#include "core/cpu_allocator.h"
#include "renderer/frame/render_frame_intent.h"

#include <cassert>
#include <type_traits>
#include <utility>

namespace {

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

void assertHasDiagnostic(const arp::renderer::RenderFrameIntentValidationResult& result,
    arp::renderer::RenderFrameIntentDiagnosticCode code,
    const char* message)
{
    assert(!result.success);
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            assertContains(diagnostic.message, message);
            return;
        }
    }
    assert(false && "expected render frame intent diagnostic was not reported");
}

arp::renderer::RenderFrameAttachmentRequirement makeBackbufferAttachment()
{
    arp::renderer::RenderFrameAttachmentRequirement attachment;
    attachment.id = "Backbuffer";
    attachment.role = arp::renderer::RenderFrameAttachmentRole::BackbufferColor;
    attachment.extent = arp::rhi::Extent2D{640, 360};
    attachment.format = arp::rhi::TextureFormat::Bgra8Unorm;
    attachment.initialState = arp::rhi::ResourceState::Present;
    attachment.finalState = arp::rhi::ResourceState::Present;
    attachment.explicitContract = true;
    return attachment;
}

arp::renderer::RenderFrameAttachmentUse makeBackbufferWrite()
{
    arp::renderer::RenderFrameAttachmentUse write;
    write.attachmentId = "Backbuffer";
    write.state = arp::rhi::ResourceState::ColorAttachment;
    write.access = arp::renderer::RenderFrameAttachmentAccess::Write;
    return write;
}

arp::renderer::RenderFramePassIntent makeWorldUiPass()
{
    arp::renderer::RenderFramePassIntent pass;
    pass.id = "SceneRuntimeWorldUiPass";
    pass.stage = arp::renderer::RenderFrameStage::WorldOpaque;
    pass.attachmentUses.push_back(makeBackbufferWrite());
    return pass;
}

arp::renderer::RenderFrameIntent makeValidIntent()
{
    arp::renderer::RenderFrameIntent intent;
    intent.contractName = "tests/render_frame_intent";
    intent.contractVersion = 1u;
    intent.sceneRequirements.push_back({"MainScene", 2u, 1u, 1u, 1u});
    intent.postprocessRequirements.push_back({"NoPostprocess", false});
    intent.uiRequirements.push_back({"WorldUi", 3u});
    intent.attachmentRequirements.push_back(makeBackbufferAttachment());
    intent.preparedSubmitRequirements.push_back({"NoPreparedSubmit", false, {}, 0u});
    intent.passes.push_back(makeWorldUiPass());
    return intent;
}

void stage_order_is_deterministic()
{
    using arp::renderer::RenderFrameStage;

    const auto& order = arp::renderer::renderFrameStageOrder();
    assert(order.size() == 7u);
    assert(order[0] == RenderFrameStage::ShadowDepth);
    assert(order[1] == RenderFrameStage::WorldOpaque);
    assert(order[2] == RenderFrameStage::WorldTransparent);
    assert(order[3] == RenderFrameStage::StylizedAux);
    assert(order[4] == RenderFrameStage::Postprocess);
    assert(order[5] == RenderFrameStage::UI);
    assert(order[6] == RenderFrameStage::Debug);
    assert(arp::renderer::renderFrameStageSortKey(RenderFrameStage::ShadowDepth)
        < arp::renderer::renderFrameStageSortKey(RenderFrameStage::WorldOpaque));
    assert(arp::renderer::renderFrameStageSortKey(RenderFrameStage::UI)
        < arp::renderer::renderFrameStageSortKey(RenderFrameStage::Debug));
}

void intent_requirements_are_bounded_core_pmr_records()
{
    using Intent = arp::renderer::RenderFrameIntent;

    static_assert(std::is_same_v<decltype(std::declval<Intent&>().sceneRequirements),
        arp::core::pmr::Vector<arp::renderer::RenderFrameSceneRequirement>>);
    static_assert(std::is_same_v<decltype(std::declval<Intent&>().postprocessRequirements),
        arp::core::pmr::Vector<arp::renderer::RenderFramePostprocessRequirement>>);
    static_assert(std::is_same_v<decltype(std::declval<Intent&>().uiRequirements),
        arp::core::pmr::Vector<arp::renderer::RenderFrameUiRequirement>>);
    static_assert(std::is_same_v<decltype(std::declval<Intent&>().attachmentRequirements),
        arp::core::pmr::Vector<arp::renderer::RenderFrameAttachmentRequirement>>);
    static_assert(std::is_same_v<decltype(std::declval<Intent&>().preparedSubmitRequirements),
        arp::core::pmr::Vector<arp::renderer::RenderFramePreparedSubmitRequirement>>);

    const auto intent = makeValidIntent();
    assert(intent.budget.maxSceneRequirements == 4u);
    assert(intent.budget.maxPostprocessRequirements == 8u);
    assert(intent.budget.maxUiRequirements == 4u);
    assert(intent.budget.maxAttachmentRequirements == 16u);
    assert(intent.budget.maxPreparedSubmitRequirements == 4u);
    assert(intent.sceneRequirements.get_allocator().resource()
        == &arp::core::defaultCpuMemoryResource());
    assert(intent.postprocessRequirements.get_allocator().resource()
        == &arp::core::defaultCpuMemoryResource());
    assert(
        intent.uiRequirements.get_allocator().resource() == &arp::core::defaultCpuMemoryResource());
    assert(intent.attachmentRequirements.get_allocator().resource()
        == &arp::core::defaultCpuMemoryResource());
    assert(intent.preparedSubmitRequirements.get_allocator().resource()
        == &arp::core::defaultCpuMemoryResource());
}

void validation_accepts_explicit_one_pass_contract()
{
    const auto intent = makeValidIntent();
    const auto result = arp::renderer::RenderFrameIntentValidator().validate(intent);
    assert(result.success);
    assert(result.diagnostics.empty());
}

void validation_reports_deterministic_failures()
{
    auto invalidStage = makeValidIntent();
    invalidStage.passes[0].stage = arp::renderer::RenderFrameStage::Invalid;
    assertHasDiagnostic(arp::renderer::RenderFrameIntentValidator().validate(invalidStage),
        arp::renderer::RenderFrameIntentDiagnosticCode::InvalidStage,
        "invalid stage");

    auto duplicatePass = makeValidIntent();
    duplicatePass.passes.push_back(makeWorldUiPass());
    assertHasDiagnostic(arp::renderer::RenderFrameIntentValidator().validate(duplicatePass),
        arp::renderer::RenderFrameIntentDiagnosticCode::DuplicatePassId,
        "duplicate pass id");

    auto missingAttachment = makeValidIntent();
    missingAttachment.passes[0].attachmentUses[0].attachmentId = "MissingBackbuffer";
    assertHasDiagnostic(arp::renderer::RenderFrameIntentValidator().validate(missingAttachment),
        arp::renderer::RenderFrameIntentDiagnosticCode::MissingAttachmentContract,
        "attachment contract");

    auto unsupportedMultiPass = makeValidIntent();
    unsupportedMultiPass.requiresMultiplePhysicalPasses = true;
    unsupportedMultiPass.hasExplicitAttachmentContract = false;
    unsupportedMultiPass.hasExplicitPostprocessContract = false;
    unsupportedMultiPass.hasExplicitOffscreenContract = false;
    assertHasDiagnostic(arp::renderer::RenderFrameIntentValidator().validate(unsupportedMultiPass),
        arp::renderer::RenderFrameIntentDiagnosticCode::UnsupportedMultiPassIntent,
        "multi-pass");
}

void execution_decision_uses_core_pmr_records()
{
    arp::renderer::RenderFrameExecutionDecision decision;
    decision.mode = arp::renderer::RenderFrameExecutionMode::ComposeGraph;
    decision.statusLabel = "Contract Complete";
    decision.graphPasses.push_back(makeWorldUiPass());
    assert(
        decision.graphPasses.get_allocator().resource() == &arp::core::defaultCpuMemoryResource());
    assert(decision.graphPasses[0].id == arp::core::pmr::String("SceneRuntimeWorldUiPass"));
}

} // namespace

int main()
{
    stage_order_is_deterministic();
    intent_requirements_are_bounded_core_pmr_records();
    validation_accepts_explicit_one_pass_contract();
    validation_reports_deterministic_failures();
    execution_decision_uses_core_pmr_records();
    return 0;
}
