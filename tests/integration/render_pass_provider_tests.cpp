#include "core/cpu_allocator.h"
#include "renderer/frame/render_pass_provider.h"
#include "renderer/frame/scene_runtime_world_ui_pass_provider.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

arp::core::pmr::String readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

void assertNotContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) == arp::core::pmr::String::npos);
}

void assertHasDiagnostic(const arp::renderer::RenderPassProviderResult& result,
    arp::renderer::RenderPassProviderDiagnosticCode code,
    const char* message)
{
    assert(!result.success);
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            assertContains(diagnostic.message, message);
            return;
        }
    }
    assert(false && "expected render pass provider diagnostic was not reported");
}

arp::renderer::RenderFrameAttachmentUse makeBackbufferWrite(
    arp::rhi::ResourceState state = arp::rhi::ResourceState::ColorAttachment)
{
    arp::renderer::RenderFrameAttachmentUse use;
    use.attachmentId = "Backbuffer";
    use.state = state;
    use.access = arp::renderer::RenderFrameAttachmentAccess::Write;
    return use;
}

arp::renderer::RenderPassDeclaration makeDeclaration(const char* passId,
    arp::renderer::RenderFrameStage stage,
    std::size_t declaredOrder,
    const char* providerId)
{
    arp::renderer::RenderPassDeclaration declaration;
    declaration.id = passId;
    declaration.stage = stage;
    declaration.declaredOrder = declaredOrder;
    declaration.providerId = providerId;
    declaration.capabilityLabel = "tests/pass-provider";
    declaration.attachmentUses.push_back(makeBackbufferWrite());
    return declaration;
}

arp::renderer::RenderFrameIntent makeIntent()
{
    arp::renderer::RenderFrameIntent intent;
    intent.contractName = "tests/render_pass_provider";
    intent.contractVersion = 1u;

    arp::renderer::RenderFrameSceneRequirement scene;
    scene.id = "SceneRuntime";
    scene.resolvedCameraCount = 1u;
    scene.drawItemCount = 3u;
    scene.materialResourceCount = 3u;
    scene.textureResourceCount = 2u;
    scene.compatibleOnePassIntent = true;
    intent.sceneRequirements.push_back(scene);

    const arp::renderer::RenderFrameQueueKind kinds[] = {
        arp::renderer::RenderFrameQueueKind::Scene3D,
        arp::renderer::RenderFrameQueueKind::Scene2D,
        arp::renderer::RenderFrameQueueKind::UI,
    };
    for (std::size_t i = 0; i < 3u; ++i) {
        arp::renderer::RenderFrameQueueRequirement queue;
        queue.kind = kinds[i];
        queue.order = i;
        queue.drawItemCount = 1u;
        queue.materialResourceCount = 1u;
        queue.textureResourceCount = i == 0u ? 0u : 1u;
        queue.compatibleWithOnePass = true;
        intent.queueRequirements.push_back(queue);
    }

    return intent;
}

class StaticPassProvider final : public arp::renderer::IRenderPassProvider {
public:
    StaticPassProvider(const char* id,
        arp::renderer::RenderFrameStage stage,
        const char* capabilityLabel,
        arp::renderer::RenderPassDeclaration declaration)
        : id_(id), stage_(stage), capabilityLabel_(capabilityLabel)
    {
        declarations_.push_back(declaration);
    }

    const char* providerId() const override { return id_; }

    arp::renderer::RenderFrameStage stage() const override { return stage_; }

    const char* capabilityLabel() const override { return capabilityLabel_; }

    arp::renderer::RenderPassProviderResult provide(
        const arp::renderer::RenderFrameIntent&) const override
    {
        arp::renderer::RenderPassProviderResult result;
        result.success = true;
        for (const auto& declaration : declarations_) {
            result.passes.push_back(declaration);
        }
        return result;
    }

private:
    const char* id_ = nullptr;
    arp::renderer::RenderFrameStage stage_ = arp::renderer::RenderFrameStage::Invalid;
    const char* capabilityLabel_ = nullptr;
    arp::core::pmr::Vector<arp::renderer::RenderPassDeclaration> declarations_;
};

void registry_rejects_invalid_provider_contracts()
{
    auto declaration = makeDeclaration(
        "WorldOpaque", arp::renderer::RenderFrameStage::WorldOpaque, 0u, "ProviderA");
    StaticPassProvider providerA(
        "ProviderA", arp::renderer::RenderFrameStage::WorldOpaque, "tests/world", declaration);

    arp::renderer::RenderPassProviderRegistry registry;
    auto registration = registry.registerProvider(providerA);
    assert(registration.success);

    registration = registry.registerProvider(providerA);
    assert(!registration.success);
    assert(registration.diagnostics[0].code
        == arp::renderer::RenderPassProviderDiagnosticCode::DuplicateProviderId);

    StaticPassProvider invalidStage(
        "InvalidStage", arp::renderer::RenderFrameStage::Invalid, "tests/invalid", declaration);
    registration = registry.registerProvider(invalidStage);
    assert(!registration.success);
    assert(registration.diagnostics[0].code
        == arp::renderer::RenderPassProviderDiagnosticCode::UnsupportedStage);

    StaticPassProvider missingCapability(
        "MissingCapability", arp::renderer::RenderFrameStage::WorldOpaque, "", declaration);
    registration = registry.registerProvider(missingCapability);
    assert(!registration.success);
    assert(registration.diagnostics[0].code
        == arp::renderer::RenderPassProviderDiagnosticCode::MissingCapabilityLabel);
}

void registry_rejects_passes_without_explicit_attachment_state()
{
    auto declaration = makeDeclaration(
        "MissingState", arp::renderer::RenderFrameStage::WorldOpaque, 0u, "MissingStateProvider");
    declaration.attachmentUses[0].state = arp::rhi::ResourceState::Undefined;

    StaticPassProvider provider("MissingStateProvider",
        arp::renderer::RenderFrameStage::WorldOpaque,
        "tests/missing-state",
        declaration);

    arp::renderer::RenderPassProviderRegistry registry;
    assert(registry.registerProvider(provider).success);

    assertHasDiagnostic(registry.composePasses(makeIntent()),
        arp::renderer::RenderPassProviderDiagnosticCode::MissingAttachmentState,
        "attachment state");
}

void registry_sorts_by_stage_declared_order_then_provider_id()
{
    StaticPassProvider uiProvider("UiProvider",
        arp::renderer::RenderFrameStage::UI,
        "tests/ui",
        makeDeclaration("UiPass", arp::renderer::RenderFrameStage::UI, 0u, "UiProvider"));
    StaticPassProvider lateWorldProvider("LateWorldProvider",
        arp::renderer::RenderFrameStage::WorldOpaque,
        "tests/world-late",
        makeDeclaration(
            "WorldLate", arp::renderer::RenderFrameStage::WorldOpaque, 1u, "LateWorldProvider"));
    StaticPassProvider earlyWorldProvider("EarlyWorldProvider",
        arp::renderer::RenderFrameStage::WorldOpaque,
        "tests/world-early",
        makeDeclaration(
            "WorldEarly", arp::renderer::RenderFrameStage::WorldOpaque, 0u, "EarlyWorldProvider"));

    arp::renderer::RenderPassProviderRegistry registry;
    assert(registry.registerProvider(uiProvider).success);
    assert(registry.registerProvider(lateWorldProvider).success);
    assert(registry.registerProvider(earlyWorldProvider).success);

    const auto result = registry.composePasses(makeIntent());
    assert(result.success);
    assert(result.passes.size() == 3u);
    assert(result.passes[0].id == arp::core::pmr::String("WorldEarly"));
    assert(result.passes[1].id == arp::core::pmr::String("WorldLate"));
    assert(result.passes[2].id == arp::core::pmr::String("UiPass"));
}

void scene_runtime_world_ui_provider_declares_one_pass_and_semantics()
{
    const arp::renderer::SceneRuntimeWorldUiPassProvider provider;
    const auto result = provider.provide(makeIntent());
    assert(result.success);
    assert(result.passes.size() == 1u);
    assert(result.passes[0].id == arp::core::pmr::String("SceneRuntimeWorldUiPass"));
    assert(result.passes[0].stage == arp::renderer::RenderFrameStage::WorldOpaque);
    assert(result.passes[0].attachmentUses.size() == 1u);
    assert(result.passes[0].attachmentUses[0].attachmentId == arp::core::pmr::String("Backbuffer"));
    assert(result.passes[0].attachmentUses[0].state == arp::rhi::ResourceState::ColorAttachment);
    assert(result.passes[0].attachmentUses[0].access
        == arp::renderer::RenderFrameAttachmentAccess::Write);

    assert(result.semanticRecords.size() == 1u);
    assert(result.semanticRecords[0].passName == arp::core::pmr::String("SceneRuntimeWorldUiPass"));
    assert(result.semanticRecords[0].physicalPassCount == 1u);
    assert(result.semanticRecords[0].queues.size() == 3u);
    assert(
        result.semanticRecords[0].queues[0].kind == arp::renderer::RenderFrameQueueKind::Scene3D);
    assert(
        result.semanticRecords[0].queues[1].kind == arp::renderer::RenderFrameQueueKind::Scene2D);
    assert(result.semanticRecords[0].queues[2].kind == arp::renderer::RenderFrameQueueKind::UI);
}

void scene_runtime_world_ui_provider_rejects_implicit_multi_pass()
{
    auto intent = makeIntent();
    intent.requiresMultiplePhysicalPasses = true;

    const arp::renderer::SceneRuntimeWorldUiPassProvider provider;
    assertHasDiagnostic(provider.provide(intent),
        arp::renderer::RenderPassProviderDiagnosticCode::MultiPassMetadataMissing,
        "multi-pass");

    intent.hasExplicitAttachmentContract = true;
    const auto result = provider.provide(intent);
    assert(result.success);
}

void provider_sources_stay_backend_neutral()
{
    const auto root = projectRoot();
    const auto combined = readText(root / "src" / "renderer" / "frame" / "render_pass_provider.h")
        + readText(root / "src" / "renderer" / "frame" / "render_pass_provider.cpp")
        + readText(root / "src" / "renderer" / "frame" / "scene_runtime_world_ui_pass_provider.h")
        + readText(
            root / "src" / "renderer" / "frame" / "scene_runtime_world_ui_pass_provider.cpp");

    assertContains(combined, "core::pmr::Vector");
    assertContains(combined, "SceneRuntimeWorldUiPass");
    assertNotContains(combined, "vulkan");
    assertNotContains(combined, "Vulkan");
    assertNotContains(combined, "Metal");
    assertNotContains(combined, "WebGPU");
    assertNotContains(combined, "wgpu");
    assertNotContains(combined, "std::vector");
    assertNotContains(combined, "std::pmr::vector");
    assertNotContains(combined, "std::make_unique");
    assertNotContains(combined, "new ");
}

} // namespace

int main()
{
    registry_rejects_invalid_provider_contracts();
    registry_rejects_passes_without_explicit_attachment_state();
    registry_sorts_by_stage_declared_order_then_provider_id();
    scene_runtime_world_ui_provider_declares_one_pass_and_semantics();
    scene_runtime_world_ui_provider_rejects_implicit_multi_pass();
    provider_sources_stay_backend_neutral();
    return 0;
}
