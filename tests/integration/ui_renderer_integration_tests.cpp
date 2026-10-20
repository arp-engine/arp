#include "renderer/gui/gui_draw_plan.h"
#include "renderer/ui/ui_draw_plan.h"
#include "renderer/ui/ui_text_atlas.h"
#include "tests/fixtures/ui_document_fixture.h"
#include "ui/ui_document.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

void writeFile(const std::filesystem::path& path, const char* content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    assert(output.good());
    output << content;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "src" / "renderer" / "ui" / "gui_renderer.h")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
}

void arpui_runtime_extracts_renderer_facing_draw_records()
{
    const auto path = std::filesystem::path("artifacts/ui_renderer_integration/hud.arpui");
    writeFile(path, arp::tests::uiDocumentFixture());

    arp::ui::UiArpuiRuntimeLoadDesc load;
    load.path = path;
    const auto loaded = arp::ui::UiArpuiRuntimeLoader().load(load);
    assert(loaded.ok());

    const auto layout = loaded.runtime().canvas().resolveLayout();
    assert(layout.ok());
    const auto extracted = arp::ui::RenderExtractor().extract(loaded.runtime().canvas(), layout);
    assert(extracted.ok());

    const auto planned = arp::ui::DrawPlanner().plan(extracted.queue());
    assert(planned.ok());
    assert(!planned.plan().exposesNativeBackendHandles());
    assert(planned.plan().recordCount() == 3u);
    assert(planned.plan().textRecordCount() == 1u);
    assert(planned.plan().imageRecordCount() == 1u);

    const auto& text = planned.plan().records()[1];
    assert(text.kind == arp::ui::DrawRecordKind::Text);
    assert(text.text == arp::core::pmr::String("Score", &arp::core::defaultCpuMemoryResource()));
    assert(text.styleId == arp::core::pmr::String("body", &arp::core::defaultCpuMemoryResource()));
    assert(text.clipRect.valid());
    assert(text.zOrder == text.sortKey);

    const auto& image = planned.plan().records()[2];
    assert(image.kind == arp::ui::DrawRecordKind::Image);
    assert(image.textureResourceId
        == arp::core::pmr::String(
            "22222222-2222-4222-8222-222222222222", &arp::core::defaultCpuMemoryResource()));
    assert(image.hasTextureSourceRect);
    assert(
        image.textureSourceRect == arp::ui::LayoutRect::fromOriginSize(8.0f, 16.0f, 64.0f, 24.0f));
    assert(image.styleId == arp::core::pmr::String("icon", &arp::core::defaultCpuMemoryResource()));
    assert(image.clipRect.valid());
    assert(image.zOrder == image.sortKey);
}

void invalid_ui_queue_fails_deterministically()
{
    arp::ui::RenderQueue queue;
    arp::ui::UIRenderItem item;
    item.kind = arp::ui::UIRenderItemKind::Image;
    item.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 32.0f, 32.0f);
    item.clipRect = item.rect;
    queue.addItem(item);

    const auto planned = arp::ui::DrawPlanner().plan(queue);
    assert(!planned.ok());
    assert(planned.diagnostic().code == arp::ui::UiDiagnosticCode::MissingRendererResourceBinding);
}

void renderer_ui_lowering_consumes_unified_ui_draw_plan_contract()
{
    arp::ui::DrawPlan plan;
    arp::ui::DrawRecord box;
    box.kind = arp::ui::DrawRecordKind::Box;
    box.widgetId = arp::ui::WidgetId::fromValue(7);
    box.rect = arp::ui::LayoutRect::fromOriginSize(2.0f, 3.0f, 40.0f, 20.0f);
    box.clipRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 80.0f, 60.0f);
    box.styleId = arp::core::pmr::String("panel", &arp::core::defaultCpuMemoryResource());
    box.sortKey = 3;
    plan.addRecord(box);

    arp::ui::DrawRecord image;
    image.kind = arp::ui::DrawRecordKind::Image;
    image.widgetId = arp::ui::WidgetId::fromValue(8);
    image.rect = arp::ui::LayoutRect::fromOriginSize(4.0f, 8.0f, 32.0f, 32.0f);
    image.clipRect = box.clipRect;
    image.textureResourceId =
        arp::core::pmr::String("atlas/icon", &arp::core::defaultCpuMemoryResource());
    image.textureSourceRect = arp::ui::LayoutRect::fromOriginSize(0.25f, 0.5f, 0.125f, 0.25f);
    image.hasTextureSourceRect = true;
    image.styleId = arp::core::pmr::String("icon", &arp::core::defaultCpuMemoryResource());
    image.sortKey = 4;
    plan.addRecord(image);

    const auto lowered =
        arp::renderer::ui::UiDrawPlanLowerer().lower(plan, arp::rhi::Extent2D{320u, 180u});
    assert(lowered.ok());
    assert(!lowered.exposesNativeBackendHandles());
    assert(lowered.commands().size() == 2u);
    assert(lowered.commands()[0].kind == arp::renderer::ui::UiDrawCommandKind::Box);
    assert(lowered.commands()[0].styleId
        == arp::core::pmr::String("panel", &arp::core::defaultCpuMemoryResource()));
    assert(lowered.commands()[0].sortKey == 3u);
    assert(lowered.commands()[1].kind == arp::renderer::ui::UiDrawCommandKind::Image);
    assert(lowered.commands()[1].textureResourceId
        == arp::core::pmr::String("atlas/icon", &arp::core::defaultCpuMemoryResource()));
    assert(lowered.commands()[1].hasTextureSourceRect);
    assert(lowered.commands()[1].textureSourceRect == image.textureSourceRect);
    assert(lowered.commands()[1].clipRect == image.clipRect);
}

void renderer_gui_plan_preserves_explicit_external_texture_source()
{
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    auto viewport = arp::renderer::ui::UiDrawCommand{};
    viewport.kind = arp::renderer::ui::UiDrawCommandKind::Image;
    viewport.rect = arp::ui::LayoutRect::fromOriginSize(10.0f, 20.0f, 120.0f, 80.0f);
    viewport.clipRect = viewport.rect;
    viewport.textureSourceRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 1.0f, 1.0f);
    viewport.hasTextureSourceRect = true;
    viewport.textureResourceId =
        arp::core::pmr::String("editor://scene-viewport", &arp::core::defaultCpuMemoryResource());
    viewport.textureSource = arp::renderer::ui::UiTextureSource::External;
    commands.push_back(viewport);

    const auto plan = arp::renderer::buildGuiDrawPlan(commands, {640u, 360u});
    assert(plan.success);
    assert(plan.commands.size() == 1u);
    assert(plan.commands.front().externalTexture);
    assert(plan.commands.front().textured);
    assert(plan.commands.front().textureResourceId == std::pmr::string("editor://scene-viewport"));

    commands.front().hasTextureSourceRect = false;
    const auto missingRect = arp::renderer::buildGuiDrawPlan(commands, {640u, 360u});
    assert(!missingRect.success);
    assert(missingRect.diagnostics.find("valid source rect") != std::pmr::string::npos);
}

void renderer_ui_rhi_backend_contract_reports_support_or_diagnostics()
{
    const auto contract = arp::renderer::ui::UiRhiBackendContract().evaluate();
    assert(contract.semantic.pipelineLayoutName
        == arp::core::pmr::String("UiPipelineLayout", &arp::core::defaultCpuMemoryResource()));
    assert(contract.semantic.frameDescriptorLayoutName
        == arp::core::pmr::String("UiFrameSet", &arp::core::defaultCpuMemoryResource()));
    assert(contract.semantic.atlasDescriptorLayoutName
        == arp::core::pmr::String("UiAtlasSet", &arp::core::defaultCpuMemoryResource()));
    assert(contract.semantic.explicitResourceStates);
    assert(contract.semantic.backendOwnedUploadStaging);
    assert(contract.semantic.alphaBlendEnabled);
    assert(!contract.semantic.depthEnabled);
    assert(!contract.semantic.cullEnabled);
    assert(contract.backendReports.size() == 5u);
    assert(contract.backendReports[0].backend == arp::rhi::Backend::Null);
    assert(contract.backendReports[0].supported);
    for (const auto& report : contract.backendReports) {
        assert(report.backend == arp::rhi::Backend::Null
            || report.backend == arp::rhi::Backend::Vulkan
            || report.backend == arp::rhi::Backend::Metal
            || report.backend == arp::rhi::Backend::WebGPU
            || report.backend == arp::rhi::Backend::WebGL);
        assert(report.supported || !report.diagnostics.empty());
        assert(!report.exposesNativeBackendHandles);
    }
}

void renderer_ui_text_atlas_lowers_text_to_cached_textured_quads()
{
    arp::renderer::ui::UiDrawCommand textCommand;
    textCommand.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    textCommand.rect = arp::ui::LayoutRect::fromOriginSize(12.0f, 16.0f, 96.0f, 22.0f);
    textCommand.clipRect = textCommand.rect;
    textCommand.text = arp::core::pmr::String("Score 100", &arp::core::defaultCpuMemoryResource());
    textCommand.styleId = arp::core::pmr::String("body", &arp::core::defaultCpuMemoryResource());

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(std::move(textCommand));

    arp::renderer::GuiAtlasTextureDesc imageAtlas;
    imageAtlas.resourceId = std::pmr::string("texture/ui_base_atlas");
    imageAtlas.extent = {4u, 4u};
    imageAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::renderer::ui::UiTextAtlasBuildDesc buildDesc;
    buildDesc.baseAtlas = std::move(imageAtlas);
    buildDesc.commands = &commands;
    buildDesc.maxWidth = 320u;
    buildDesc.maxHeight = 256u;
    const auto built = arp::renderer::ui::UiTextAtlasBuilder().build(std::move(buildDesc));
    assert(built.ok());
    assert(built.atlas().valid());
    assert(built.atlas().extent.height > 4u);
    assert(!built.exposesNativeBackendHandles());
    assert(built.perFrameSourceParsingCount() == 0u);
    assert(built.commands().size() == 1u);
    assert(built.commands()[0].kind == arp::renderer::ui::UiDrawCommandKind::Text);
    assert(built.commands()[0].hasTextureSourceRect);
    assert(!built.commands()[0].textureResourceId.empty());
    assert(built.commands()[0].textureResourceId
        == arp::core::pmr::String(
            "texture/ui_base_atlas+ui_text_atlas", &arp::core::defaultCpuMemoryResource()));
    assert(built.commands()[0].textureSourceRect.width > 0.0f);
    assert(built.commands()[0].textureSourceRect.height > 0.0f);
    assert(built.generatedTextRunCount() == 1u);
    assert(built.generatedDrawCommandCount() == 1u);
}

void renderer_ui_gui_atlas_upload_uses_l2_upload_texture_contract()
{
    const auto root = projectRoot();
    const auto source = readFile(root / "src" / "renderer" / "ui" / "gui_renderer.h");
    assert(source.find("transfer->uploadTexture(uploadDesc)") != std::string::npos);
    assert(source.find("transfer->copyBufferToTexture({") == std::string::npos);
    assert(source.find("uploadDesc.backendOwnedStaging = true;") != std::string::npos);
    // Dynamic atlas sub-rect uploads must set destinationOffset so the native
    // buffer-to-image copy lands in the glyph's atlas slot instead of (0,0).
    assert(source.find("uploadDesc.destinationOffset =") != std::string::npos);
}

void rhi_vulkan_native_upload_texture_lowers_destination_offset_to_image_offset()
{
    const auto root = projectRoot();
    const auto source =
        readFile(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_commands.cpp");
    // The L2 destinationOffset must lower to VkBufferImageCopy.imageOffset in
    // the Vulkan backend. Without this, sub-rect uploads overwrite (0,0).
    assert(source.find("region.imageOffset = {") != std::string::npos);
    assert(source.find("desc.destinationOffset.width") != std::string::npos);
    assert(source.find("desc.destinationOffset.height") != std::string::npos);
}

} // namespace

int main()
{
    arpui_runtime_extracts_renderer_facing_draw_records();
    invalid_ui_queue_fails_deterministically();
    renderer_ui_lowering_consumes_unified_ui_draw_plan_contract();
    renderer_gui_plan_preserves_explicit_external_texture_source();
    renderer_ui_rhi_backend_contract_reports_support_or_diagnostics();
    renderer_ui_text_atlas_lowers_text_to_cached_textured_quads();
    renderer_ui_gui_atlas_upload_uses_l2_upload_texture_contract();
    rhi_vulkan_native_upload_texture_lowers_destination_offset_to_image_offset();
    return 0;
}
