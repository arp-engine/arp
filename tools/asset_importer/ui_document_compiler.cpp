#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "tools/asset_importer/asset_importer.h"
#include "tools/asset_importer/document_import.h"

namespace arp::tools::asset_importer {
namespace {
using Y = core::YamlContract;
using Node = Y::Node;

ToolUiDocumentResult failed(core::StringView message)
{
    ToolUiDocumentResult result;
    result.diagnostics = message;
    return result;
}

Node rect(ui::LayoutRect value)
{
    Node result = Node::mapping();
    result["x"] = value.x;
    result["y"] = value.y;
    result["width"] = value.width;
    result["height"] = value.height;
    return result;
}

// Only the legacy MockPsdManifest/1 fields have a defined projection. Rich
// documents must use the formal YAML IR rather than silently losing metadata.
core::pmr::String legacyIr(const ui::UiDocument& document)
{
    const auto validation = ui::UiDocumentValidator{}.validate(document);
    if (!validation.ok())
        throw core::DiagnosticError("TOOL-UI-LEGACY-INVALID-DOCUMENT");
    Node root = Node::mapping();
    root["type"] = "ArpUiAuthoringIR";
    root["contractVersion"] = "arpui.ir.v1";
    Node canvas = Node::mapping();
    canvas["id"] = document.canvas().canvasId.c_str();
    canvas["width"] = document.canvas().width;
    canvas["height"] = document.canvas().height;
    canvas["dpi"] = document.canvas().designDpi;
    root["canvas"] = std::move(canvas);
    Node textures = Node::sequence();
    for (const auto& value : document.textures()) {
        Node texture = Node::mapping();
        texture["resourceId"] = value.resourceId.c_str();
        texture["layer"] = value.sourceLayerId.c_str();
        texture["atlas"] = value.atlasId.c_str();
        texture["rect"] = rect(value.sourceRect);
        texture["pivotX"] = value.pivot.x;
        texture["pivotY"] = value.pivot.y;
        texture["sliceLeft"] = value.nineSlice.left;
        texture["sliceTop"] = value.nineSlice.top;
        texture["sliceRight"] = value.nineSlice.right;
        texture["sliceBottom"] = value.nineSlice.bottom;
        texture["alpha"] = uiTextureAlphaModeName(value.alphaMode);
        texture["srgb"] = value.srgb;
        texture["mips"] = uiTextureMipIntentName(value.mipIntent);
        texture["compression"] = uiTextureCompressionIntentName(value.compressionIntent);
        texture["sampler"] = uiTextureSamplerIntentName(value.samplerIntent);
        textures.get_value_ref<Node::sequence_type&>().push_back(std::move(texture));
    }
    root["textures"] = std::move(textures);
    Node nodes = Node::sequence();
    for (const auto& value : document.nodes()) {
        if (!value.sourceId.empty() || value.nodeId.value() > UINT32_MAX
            || value.parentNodeId.value() > UINT32_MAX || value.layoutMode != ui::UiLayoutMode::None
            || value.layoutParticipation != ui::UiLayoutParticipation::Absolute
            || value.resourceBindings.size() > 1
            || (!value.resourceBindings.empty() && !value.resourceBindings.front().required))
            throw core::DiagnosticError("TOOL-UI-LEGACY-UNREPRESENTABLE");
        Node node = Node::mapping();
        node["id"] = value.nodeId.value();
        node["parent"] = value.parentNodeId.value();
        node["type"] = ui::controlTypeName(value.controlType);
        node["layer"] = value.psdLayerBinding.sourceLayerId.c_str();
        node["rect"] = rect(value.layout);
        node["style"] = value.styleId.c_str();
        node["state"] = value.stateId.c_str();
        node["event"] = value.eventRole.c_str();
        node["text"] = value.text.c_str();
        node["fontToken"] = value.fontToken.c_str();
        if (value.controlType == ui::UiControlType::TextInput)
            node["maxTextBytes"] = value.maxTextBytes;
        if (!value.resourceBindings.empty()) {
            node["resourceRole"] = value.resourceBindings.front().role.c_str();
            node["resourceId"] = value.resourceBindings.front().resourceId.c_str();
        }
        nodes.get_value_ref<Node::sequence_type&>().push_back(std::move(node));
    }
    root["nodes"] = std::move(nodes);
    return Y::serialize(root);
}

} // namespace

ToolUiDocumentResult ToolUiDocumentCompiler::compileIr(core::StringView source, std::size_t budget)
{
    try {
        if (!budget || source.size() > budget)
            return failed("TOOL-UI-SOURCE-BUDGET");
        ui::UiArpuiIrBuildDesc desc;
        desc.sourceYaml = source;
        desc.sourceYamlProvided = true;
        desc.maxDocumentBytes = budget;
        const auto built = ui::UiArpuiIrBuilder{}.build(desc);
        if (!built.ok())
            return failed(built.diagnostic().message);
        ui::UiArpuiRuntimeLoadDesc load;
        load.sourceYamlProvided = true;
        load.sourceYaml = built.documentYaml();
        load.maxDocumentBytes = budget;
        const auto loaded = ui::UiArpuiRuntimeLoader{}.load(load);
        if (!loaded.ok())
            return failed(loaded.diagnostic().message);
        ToolUiDocumentResult result;
        result.yaml = built.documentYaml();
        result.nodeCount = loaded.document().nodes().size();
        result.stableHash = loaded.document().stableHash();
        result.success = true;
        return result;
    } catch (const std::bad_alloc&) {
        return failed("TOOL-UI-ALLOCATION");
    } catch (const std::exception& error) {
        return failed(error.what());
    }
}

ToolUiDocumentResult ToolUiDocumentCompiler::compileLegacy(
    const ui::UiDocument& document, std::size_t budget)
{
    try {
        return compileIr(legacyIr(document), budget);
    } catch (const std::exception& error) {
        return failed(error.what());
    }
}

} // namespace arp::tools::asset_importer
