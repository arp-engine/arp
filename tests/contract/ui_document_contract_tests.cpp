#include "ui/ui_document.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::ui::UiDocumentNodeDesc makeNode(std::uint64_t id,
    std::uint64_t parentId,
    arp::ui::UiControlType controlType,
    const char* layerId)
{
    arp::ui::UiDocumentNodeDesc desc;
    desc.nodeId = arp::ui::UiDocumentNodeId::fromValue(id);
    desc.parentNodeId = parentId == 0 ? arp::ui::UiDocumentNodeId::invalid()
                                      : arp::ui::UiDocumentNodeId::fromValue(parentId);
    desc.controlType = controlType;
    desc.layout = arp::ui::LayoutRect::fromOriginSize(4.0f, 8.0f, 40.0f, 20.0f);
    desc.styleId = text("default");
    desc.text = text("label");
    desc.stateId = text("normal");
    desc.psdLayerBinding.sourceLayerId = text(layerId);
    return desc;
}

arp::ui::UiDocumentDesc validDocumentDesc()
{
    arp::ui::UiDocumentDesc desc;
    desc.canvas.canvasId = text("hud");
    desc.canvas.contractVersion = text("arpui.v1");
    desc.canvas.width = 320.0f;
    desc.canvas.height = 180.0f;
    desc.canvas.hasExplicitSize = true;
    desc.canvas.designDpi = 96.0f;
    desc.canvas.hasExplicitDpi = true;

    auto root = makeNode(1, 0, arp::ui::UiControlType::Panel, "layer_root");
    root.layout = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 320.0f, 180.0f);
    desc.nodes.push_back(root);

    desc.nodes.push_back(makeNode(2, 1, arp::ui::UiControlType::Label, "layer_label"));
    desc.nodes.push_back(makeNode(3, 1, arp::ui::UiControlType::Button, "layer_button"));
    desc.nodes.push_back(makeNode(4, 1, arp::ui::UiControlType::Slider, "layer_slider"));
    desc.nodes.push_back(makeNode(5, 1, arp::ui::UiControlType::Checkbox, "layer_checkbox"));
    desc.nodes.push_back(makeNode(6, 1, arp::ui::UiControlType::TextInput, "layer_text_input"));
    desc.nodes.push_back(
        makeNode(7, 1, arp::ui::UiControlType::NumericInput, "layer_numeric_input"));
    desc.nodes.push_back(makeNode(8, 1, arp::ui::UiControlType::ScrollView, "layer_scroll"));
    desc.nodes.push_back(makeNode(9, 1, arp::ui::UiControlType::Dropdown, "layer_dropdown"));
    desc.nodes.push_back(makeNode(10, 1, arp::ui::UiControlType::Tabs, "layer_tabs"));
    desc.nodes.push_back(makeNode(11, 1, arp::ui::UiControlType::TreeView, "layer_tree"));
    desc.nodes.push_back(makeNode(12, 1, arp::ui::UiControlType::ColorPicker, "layer_color"));

    auto image = makeNode(13, 1, arp::ui::UiControlType::Image, "layer_image");
    image.resourceBindings.push_back(arp::ui::UiDocumentResourceBinding{
        text("mainTexture"),
        text("tex/hud_portrait"),
        true,
    });
    desc.nodes.push_back(image);
    return desc;
}

void valid_document_has_stable_hash_and_all_retained_control_types()
{
    const auto desc = validDocumentDesc();
    const arp::ui::UiDocument document(desc);
    const arp::ui::UiDocumentValidator validator;
    const auto result = validator.validate(document);
    assert(result.ok());
    assert(document.nodes().size() == 13);
    assert(document.canvas().canvasId == text("hud"));
    assert(document.stableHash() != 0u);

    const arp::ui::UiDocument sameDocument(validDocumentDesc());
    assert(document.stableHash() == sameDocument.stableHash());

    auto changed = validDocumentDesc();
    changed.nodes[2].text = text("changed");
    const arp::ui::UiDocument changedDocument(changed);
    assert(document.stableHash() != changedDocument.stableHash());
}

void diagnostics_are_deterministic_for_invalid_arpui_documents()
{
    const arp::ui::UiDocumentValidator validator;

    {
        auto desc = validDocumentDesc();
        desc.canvas = {};
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::MissingCanvasMetadata);
    }
    {
        auto desc = validDocumentDesc();
        desc.nodes[2].nodeId = desc.nodes[1].nodeId;
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::DuplicateNodeId);
    }
    {
        auto desc = validDocumentDesc();
        desc.nodes[2].parentNodeId = arp::ui::UiDocumentNodeId::fromValue(999);
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::InvalidParentId);
    }
    {
        auto desc = validDocumentDesc();
        desc.nodes[2].controlType = static_cast<arp::ui::UiControlType>(999);
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::UnsupportedControlType);
    }
    {
        auto desc = validDocumentDesc();
        desc.nodes.back().resourceBindings.clear();
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(result.diagnostic().code
            == arp::ui::UiDocumentDiagnosticCode::MissingRequiredResourceBinding);
    }
    {
        auto desc = validDocumentDesc();
        desc.nodes[1].psdLayerBinding = {};
        const auto result = validator.validate(arp::ui::UiDocument(desc));
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiDocumentDiagnosticCode::MissingPsdLayerBinding);
    }
}

} // namespace

int main()
{
    valid_document_has_stable_hash_and_all_retained_control_types();
    diagnostics_are_deterministic_for_invalid_arpui_documents();
    return 0;
}
