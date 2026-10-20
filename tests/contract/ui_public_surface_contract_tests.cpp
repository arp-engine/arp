#include "ui/ui.h"
#include "ui/ui_document.h"

#include <cassert>
#include <type_traits>

namespace {

template <typename T> void requireClass()
{
    static_assert(std::is_class_v<T>);
}

void long_term_ui_public_surface_names_are_available()
{
    requireClass<arp::ui::Document>();
    requireClass<arp::ui::DocumentDesc>();
    requireClass<arp::ui::NodeId>();
    requireClass<arp::ui::NodeDesc>();
    requireClass<arp::ui::Runtime>();
    requireClass<arp::ui::Control>();
    requireClass<arp::ui::ControlDesc>();
    requireClass<arp::ui::Panel>();
    requireClass<arp::ui::Text>();
    requireClass<arp::ui::Image>();
    requireClass<arp::ui::Button>();
    requireClass<arp::ui::Slider>();
    requireClass<arp::ui::Checkbox>();
    requireClass<arp::ui::TextInput>();
    requireClass<arp::ui::NumericInput>();
    requireClass<arp::ui::ScrollView>();
    requireClass<arp::ui::Dropdown>();
    requireClass<arp::ui::Tabs>();
    requireClass<arp::ui::TreeView>();
    requireClass<arp::ui::ColorPicker>();
    requireClass<arp::ui::InputRouter>();
    requireClass<arp::ui::RenderExtractor>();
    requireClass<arp::ui::RenderQueue>();
    requireClass<arp::ui::DrawPlan>();
    requireClass<arp::ui::DocumentComponent>();
    requireClass<arp::ui::WorldAnchorComponent>();
    requireClass<arp::ui::World2DAnchorComponent>();
    requireClass<arp::ui::ScreenAnchorComponent>();

    static_assert(!std::is_same_v<arp::ui::Document, arp::ui::UiDocument>);
    static_assert(!std::is_same_v<arp::ui::DocumentDesc, arp::ui::UiDocumentDesc>);
    static_assert(!std::is_same_v<arp::ui::NodeId, arp::ui::WidgetId>);
    static_assert(!std::is_same_v<arp::ui::Runtime, arp::ui::UiControlRuntime>);
    static_assert(!std::is_same_v<arp::ui::Control, arp::ui::UiControl>);
}

arp::ui::CanvasDesc canvasDesc()
{
    arp::ui::CanvasDesc desc;
    desc.viewport = arp::ui::UiViewport::explicitPixels(320.0f, 180.0f);
    desc.safeArea = arp::ui::UiSafeArea::none();
    desc.scalePolicy = arp::ui::UiScalePolicy::fixed(1.0f);
    return desc;
}

void long_term_document_name_owns_document_surface()
{
    arp::ui::DocumentDesc desc;
    desc.canvas.canvasId = arp::core::pmr::String("hud", &arp::core::defaultCpuMemoryResource());
    desc.canvas.contractVersion =
        arp::core::pmr::String("ui.document/v1", &arp::core::defaultCpuMemoryResource());
    desc.canvas.width = 320.0f;
    desc.canvas.height = 180.0f;
    desc.canvas.hasExplicitSize = true;
    desc.canvas.designDpi = 96.0f;
    desc.canvas.hasExplicitDpi = true;

    arp::ui::NodeDesc root;
    root.nodeId = arp::ui::NodeId::fromValue(1);
    root.controlType = arp::ui::UiControlType::Panel;
    root.layout = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 320.0f, 180.0f);
    root.psdLayerBinding.sourceLayerId =
        arp::core::pmr::String("root_layer", &arp::core::defaultCpuMemoryResource());
    desc.nodes.push_back(root);

    const arp::ui::Document document(std::move(desc));
    assert(document.canvas().canvasId
        == arp::core::pmr::String("hud", &arp::core::defaultCpuMemoryResource()));
    assert(document.nodes().size() == 1u);
    assert(document.stableHash() != 0u);
}

void long_term_runtime_name_owns_runtime_surface()
{
    arp::ui::Runtime runtime(
        arp::core::pmr::String("hud", &arp::core::defaultCpuMemoryResource()), canvasDesc());

    arp::ui::ControlDesc button;
    button.type = arp::ui::UiControlType::Button;
    button.widget.name = arp::core::pmr::String("start", &arp::core::defaultCpuMemoryResource());
    button.widget.localRect = arp::ui::LayoutRect::fromOriginSize(8.0f, 8.0f, 96.0f, 32.0f);
    button.widget.controlType = arp::ui::UiControlType::Button;
    button.initialValue = arp::ui::UiControlValue::fromBool(false);

    const auto created = runtime.createControl(std::move(button), runtime.rootId());
    assert(created.ok());
    assert(runtime.findControl(created.widgetId()) != nullptr);
    assert(runtime.controlCount() == 1u);
}

} // namespace

int main()
{
    long_term_ui_public_surface_names_are_available();
    long_term_document_name_owns_document_surface();
    long_term_runtime_name_owns_runtime_surface();
    return 0;
}
