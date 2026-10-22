#include "core/memory/cpu_allocator.h"
#include "renderer/ui/ui_atlas_runtime.h"
#include "renderer/ui/ui_glyph_atlas.h"

#include <cassert>
#include <cstdint>

namespace {

arp::renderer::ui::UnifiedUiAtlasRuntime makeTestRuntime()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = "test-atlas";
    baseAtlas.extent = {256u, 128u};
    baseAtlas.rgba8.resize(256u * 128u * 4u, 255u);
    baseAtlas.srgb = true;

    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.maxWidth = 256u;
    desc.maxHeight = 256u;
    desc.dynamicGlyphBudgetPerFrame = 16u;
    desc.shapingCapability = arp::renderer::ui::TextShapingCapability::None;
    desc.initialGeneration = 7u;

    return arp::renderer::ui::UnifiedUiAtlasRuntime::build(desc);
}

void recovery_plan_preserves_atlas_generation()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    const auto plan = runtime.makeRecoveryPlan();
    assert(plan.initialGeneration == 7u);
}

void recovery_plan_preserves_base_atlas_resource_id()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    const auto plan = runtime.makeRecoveryPlan();
    assert(plan.baseAtlas.resourceId == "test-atlas");
    assert(plan.baseAtlas.extent.width == 256u);
    assert(plan.baseAtlas.extent.height == 128u);
}

void recovery_plan_preserves_glyph_page_dimensions()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    const auto plan = runtime.makeRecoveryPlan();
    assert(plan.maxWidth == 256u);
    assert(plan.maxHeight == 256u);
    assert(plan.dynamicGlyphBudgetPerFrame == 16u);
}

void recovery_plan_preserves_shaping_capability()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    const auto plan = runtime.makeRecoveryPlan();
    assert(plan.shapingCapability == arp::renderer::ui::TextShapingCapability::None);
}

void recovery_plan_contains_seed_entries_after_resolve()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    arp::renderer::ui::UiDrawCommand cmd;
    cmd.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    cmd.text = "A";
    cmd.fontToken = "default";
    cmd.styleId = "body";
    cmd.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 64.0f, 16.0f);
    cmd.clipRect = cmd.rect;
    cmd.sortKey = 0u;
    commands.push_back(cmd);

    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());

    const auto plan = runtime.makeRecoveryPlan();
    assert(plan.seedEntries != nullptr);
    assert(!plan.seedEntries->empty());
}

void recovery_plan_rebuilds_equivalent_runtime()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    arp::renderer::ui::UiDrawCommand cmd;
    cmd.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    cmd.text = "AB";
    cmd.fontToken = "default";
    cmd.styleId = "body";
    cmd.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 64.0f, 16.0f);
    cmd.clipRect = cmd.rect;
    cmd.sortKey = 0u;
    commands.push_back(cmd);

    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());

    const auto plan = runtime.makeRecoveryPlan();
    auto recovered = arp::renderer::ui::UnifiedUiAtlasRuntime::build(plan);
    assert(recovered.ok());

    // The recovered runtime must preserve generation and logical resource id.
    assert(recovered.glyphAtlas().generation() == runtime.glyphAtlas().generation());
    assert(recovered.logicalAtlasResourceId() == runtime.logicalAtlasResourceId());
    assert(recovered.glyphAtlas().entryCount() == runtime.glyphAtlas().entryCount());
    assert(recovered.glyphAtlas().pageCount() == runtime.glyphAtlas().pageCount());
}

void recovery_plan_contains_no_native_handles()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    const auto plan = runtime.makeRecoveryPlan();
    // The plan must be backend-neutral: no native handles, only logical ids,
    // generations, upload intents, and descriptor requirements.
    assert(plan.baseAtlas.resourceId == "test-atlas");
    assert(!runtime.exposesNativeBackendHandles());
}

void recovery_plan_fails_when_exceeding_retention_budget()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    // The test runtime atlas is 256 * 128 * 4 = 131072 bytes.
    // Set a budget well below that to force failure.
    arp::renderer::ui::UiResourceRecoveryRetentionPolicy policy;
    policy.maxRecoveryDataBytes = 64u;

    const auto result = runtime.makeRecoveryPlan(policy);
    assert(!result.ok());
    assert(result.diagnostics().find("budget") != arp::core::pmr::String::npos);
}

void recovery_plan_succeeds_within_retention_budget()
{
    auto runtime = makeTestRuntime();
    assert(runtime.ok());

    // Set a generous budget that accommodates the test atlas.
    arp::renderer::ui::UiResourceRecoveryRetentionPolicy policy;
    policy.maxRecoveryDataBytes = 256u * 128u * 4u + 1024u;

    const auto result = runtime.makeRecoveryPlan(policy);
    assert(result.ok());
    assert(result.plan().baseAtlas.resourceId == "test-atlas");
}

} // namespace

int main()
{
    recovery_plan_preserves_atlas_generation();
    recovery_plan_preserves_base_atlas_resource_id();
    recovery_plan_preserves_glyph_page_dimensions();
    recovery_plan_preserves_shaping_capability();
    recovery_plan_contains_seed_entries_after_resolve();
    recovery_plan_rebuilds_equivalent_runtime();
    recovery_plan_contains_no_native_handles();
    recovery_plan_fails_when_exceeding_retention_budget();
    recovery_plan_succeeds_within_retention_budget();
    return 0;
}
