#include "core/io/file_io.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/asset_importer/document_import.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {
using namespace arp;
using namespace tools::asset_importer;
constexpr std::size_t budget = 65536;

void verify(const ToolUiDocumentResult& result)
{
    assert(result.success && result.nodeCount == 4);
    assert(result.yaml.find("type: ui.document") != core::pmr::String::npos);
    ui::UiArpuiRuntimeLoadDesc desc;
    desc.sourceYaml = result.yaml;
    desc.sourceYamlProvided = true;
    desc.maxDocumentBytes = budget;
    const auto loaded = ui::UiArpuiRuntimeLoader{}.load(desc);
    assert(loaded.ok() && loaded.document().nodes().size() == 4
        && loaded.document().textures().size() == 1);
    assert(loaded.document().canvas().canvasId == u8"tool-document-画布");
    const auto& texture = loaded.document().textures().front();
    assert(texture.pivot.x == .5f && texture.sourceRect.x == 8 && texture.nineSlice.left == 4
        && texture.srgb && texture.hasExplicitSrgb
        && texture.samplerIntent == ui::UiTextureSamplerIntent::LinearClamp);
    bool title = false, input = false;
    for (const auto& node : loaded.document().nodes()) {
        if (node.controlType == ui::UiControlType::Label) {
            title = node.text == "Imported YAML: \"ARP\" & typed metadata"
                && node.fontToken == "font.visual.alpha";
        }
        if (node.controlType == ui::UiControlType::TextInput)
            input = node.maxTextBytes == 128 && node.layout.x == 252;
    }
    assert(title && input && loaded.document().stableHash() == result.stableHash);
}

void cli(const core::pmr::String& ir)
{
    unified_io_fixture::Workspace workspace;
    workspace.write("input.yaml", ir);
    workspace.write("output.arpui", "old document");
    DocumentImportOptions options;
    options.uiIr = core::FilePath::utf8(workspace.root / "input.yaml");
    options.outputUi = core::FilePath::utf8(workspace.root / "output.arpui");
    options.maxDocumentBytes = budget;
    std::ostringstream out, error;
    assert(DocumentImporter{}.run(options, out, error) == 0);
    const auto file = core::readTextFile(workspace.root / "output.arpui", budget);
    assert(file.success && file.text.find("type: ui.document") != core::pmr::String::npos);
    workspace.write("input.yaml", "type: unknown\ncontractVersion: 99\n");
    assert(DocumentImporter{}.run(options, out, error) == 1);
    const auto kept = core::readTextFile(workspace.root / "output.arpui", budget);
    assert(kept.success && kept.text == file.text);
}

void legacyProjection()
{
    ui::UiDocumentDesc desc;
    desc.canvas.canvasId = "legacy-tool";
    desc.canvas.contractVersion = "arpui.v1";
    desc.canvas.width = 320;
    desc.canvas.height = 180;
    desc.canvas.designDpi = 96;
    desc.canvas.hasExplicitSize = desc.canvas.hasExplicitDpi = true;
    ui::UiDocumentNodeDesc root;
    root.nodeId = ui::UiDocumentNodeId::fromValue(1);
    root.controlType = ui::UiControlType::Panel;
    root.layout = ui::LayoutRect::fromOriginSize(0, 0, 320, 180);
    root.psdLayerBinding.sourceLayerId = "layer-root";
    root.styleId = "background";
    root.stateId = "normal";
    root.eventRole = "none";
    desc.nodes.push_back(root);
    const auto converted = ToolUiDocumentCompiler::compileLegacy(ui::UiDocument(desc), budget);
    assert(converted.success && converted.nodeCount == 1);
    desc.nodes.front().sourceId = "must-not-be-lost";
    const auto rejected = ToolUiDocumentCompiler::compileLegacy(ui::UiDocument(desc), budget);
    assert(!rejected.success
        && rejected.diagnostics.find("UNREPRESENTABLE") != core::pmr::String::npos);
}
} // namespace

int main(int argc, char** argv)
{
    auto source = core::readTextFile(
        std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures/tool-documents/main.arpui.ir.yaml",
        budget);
    assert(source.success);
    const auto result = ToolUiDocumentCompiler::compileIr(source.text, budget);
    if (!result.success)
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    verify(result);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            verify(ToolUiDocumentCompiler::compileIr(source.text, budget));
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("UI document core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        assert(!ToolUiDocumentCompiler::compileIr(source.text, 1).success);
        assert(!ToolUiDocumentCompiler::compileIr("type: unknown\ncontractVersion: 99\n", budget)
                .success);
        auto invalid = source.text;
        const auto position = invalid.find("arpui.ir.v1");
        invalid.replace(position, std::strlen("arpui.ir.v1"), "arpui.ir.v99");
        assert(!ToolUiDocumentCompiler::compileIr(invalid, budget).success);
        cli(source.text);
        legacyProjection();
    }
    std::puts(
        "PASS tool YAML IR -> original UI builder -> formal loader, fields, publication and lifetime");
}
