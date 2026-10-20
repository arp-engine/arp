#include "ui/ui.h"
#include "ui/ui_document.h"

#include <cassert>
#include <type_traits>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::ui::LayoutRect rect(float x, float y, float width, float height)
{
    return arp::ui::LayoutRect{x, y, width, height};
}

void resolves_explicit_anchors_spacing_stack_grid_and_theme_tokens()
{
    arp::ui::UiTheme theme(text("hud-theme"));
    theme.addToken(
        {text("panel.background"), arp::ui::UiThemeTokenValue::fromColor(0.1f, 0.2f, 0.3f, 1.0f)});
    theme.addToken({text("text.primary"), arp::ui::UiThemeTokenValue::fromText(text("Inter-16"))});

    arp::ui::UiLayoutStyleThemeDocument document(text("hud-layout"));
    document.setCanvasRect(rect(0.0f, 0.0f, 320.0f, 180.0f));
    document.setTheme(theme);

    arp::ui::UiLayoutStyleNodeDesc stack;
    stack.nodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    stack.parentNodeId = arp::ui::UiDocumentNodeId::invalid();
    stack.anchors = arp::ui::UiAnchors::stretch();
    stack.padding = arp::ui::UiBoxEdges::all(8.0f);
    stack.margin = arp::ui::UiBoxEdges::all(2.0f);
    stack.constraints = arp::ui::UiSizeConstraints::minMax(120.0f, 64.0f, 320.0f, 180.0f);
    stack.layout = arp::ui::UiLayoutMode::Stack;
    stack.stack = arp::ui::UiStackLayout::vertical(4.0f);
    stack.zOrder = 10;
    stack.clipping = arp::ui::UiClipping::clipToBounds();
    stack.style = arp::ui::UiStyle::fromToken(text("panel.background"));
    stack.style.addState(arp::ui::UiStyleState::Disabled, text("panel.background"));
    stack.style.addState(arp::ui::UiStyleState::Hover, text("panel.background"));
    stack.style.addState(arp::ui::UiStyleState::Focus, text("panel.background"));
    stack.style.addState(arp::ui::UiStyleState::Active, text("panel.background"));
    document.addNode(stack);

    arp::ui::UiLayoutStyleNodeDesc grid;
    grid.nodeId = arp::ui::UiDocumentNodeId::fromValue(2);
    grid.parentNodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    grid.anchors = arp::ui::UiAnchors::fill();
    grid.padding = arp::ui::UiBoxEdges::axis(6.0f, 3.0f);
    grid.margin = arp::ui::UiBoxEdges::all(1.0f);
    grid.constraints = arp::ui::UiSizeConstraints::minMax(64.0f, 32.0f, 256.0f, 96.0f);
    grid.layout = arp::ui::UiLayoutMode::Grid;
    grid.grid = arp::ui::UiGridLayout::fixed(2, 2, 3.0f, 5.0f);
    grid.zOrder = 11;
    grid.clipping = arp::ui::UiClipping::inherit();
    grid.style = arp::ui::UiStyle::fromToken(text("text.primary"));
    document.addNode(grid);

    const auto result = arp::ui::UiLayoutStyleThemeResolver().resolve(document);
    assert(result.ok());
    assert(result.nodes().size() == 2u);
    assert(result.requireNode(arp::ui::UiDocumentNodeId::fromValue(1)).rect.width == 316.0f);
    assert(result.requireNode(arp::ui::UiDocumentNodeId::fromValue(1)).zOrder == 10);
    assert(result.requireNode(arp::ui::UiDocumentNodeId::fromValue(2)).grid.columns == 2u);
    assert(result.requireNode(arp::ui::UiDocumentNodeId::fromValue(2)).resolvedStyleToken
        == text("text.primary"));
}

void exposes_canonical_geometry_layout_types_in_ui_namespace()
{
    static_assert(std::is_same_v<decltype(arp::ui::Style{}.fixedSize), arp::ui::Size>);
    static_assert(std::is_same_v<decltype(arp::ui::Style{}.padding), arp::ui::Spacing>);

    arp::ui::Style style;
    style.axis = arp::ui::Axis::Column;
    style.align = arp::ui::Align::Stretch;
    style.fixedSize = {120.0f, 80.0f};
    style.percentSize = {0.5f, 0.25f};
    style.maxSize = {256.0f, 128.0f};
    style.padding = {2.0f, 4.0f, 6.0f, 8.0f};

    assert(style.axis == arp::ui::Axis::Column);
    assert(style.align == arp::ui::Align::Stretch);
    assert(style.fixedSize.width == 120.0f);
    assert(style.percentSize.height == 0.25f);
    assert(style.padding.bottom == 8.0f);
}

void resolves_percent_minmax_padding_gap_scroll_clipping_and_state_tokens()
{
    arp::ui::UiTheme theme(text("geometry-theme"));
    theme.addToken(
        {text("panel.base"), arp::ui::UiThemeTokenValue::fromColor(0.1f, 0.1f, 0.1f, 1.0f)});
    theme.addToken(
        {text("panel.hover"), arp::ui::UiThemeTokenValue::fromColor(0.2f, 0.2f, 0.2f, 1.0f)});
    theme.addToken(
        {text("item.base"), arp::ui::UiThemeTokenValue::fromColor(0.3f, 0.3f, 0.3f, 1.0f)});

    arp::ui::UiLayoutStyleThemeDocument document(text("geometry-layout"));
    document.setCanvasRect(rect(0.0f, 0.0f, 400.0f, 200.0f));
    document.setTheme(theme);

    arp::ui::UiLayoutStyleNodeDesc scroll;
    scroll.nodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    scroll.anchors = arp::ui::UiAnchors::fill();
    scroll.padding = arp::ui::UiBoxEdges::axis(10.0f, 8.0f);
    scroll.constraints = arp::ui::UiSizeConstraints::minMax(100.0f, 50.0f, 300.0f, 160.0f);
    scroll.percentSize = {0.75f, 0.8f};
    scroll.layout = arp::ui::UiLayoutMode::Stack;
    scroll.stack = arp::ui::UiStackLayout::vertical(6.0f);
    scroll.clipping = arp::ui::UiClipping::clipToBounds();
    scroll.style = arp::ui::UiStyle::fromToken(text("panel.base"));
    scroll.style.addState(arp::ui::UiStyleState::Hover, text("panel.hover"));
    scroll.state = arp::ui::UiStyleState::Hover;
    document.addNode(scroll);

    arp::ui::UiLayoutStyleNodeDesc first;
    first.nodeId = arp::ui::UiDocumentNodeId::fromValue(2);
    first.parentNodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    first.anchors = arp::ui::UiAnchors::fill();
    first.fixedSize = {0.0f, 40.0f};
    first.constraints = arp::ui::UiSizeConstraints::minMax(80.0f, 40.0f, 280.0f, 40.0f);
    first.style = arp::ui::UiStyle::fromToken(text("item.base"));
    document.addNode(first);

    arp::ui::UiLayoutStyleNodeDesc second = first;
    second.nodeId = arp::ui::UiDocumentNodeId::fromValue(3);
    second.fixedSize = {0.0f, 120.0f};
    second.constraints = arp::ui::UiSizeConstraints::minMax(80.0f, 120.0f, 280.0f, 120.0f);
    document.addNode(second);

    const auto result = arp::ui::UiLayoutStyleThemeResolver().resolve(document);
    assert(result.ok());

    const auto& scrollResolved = result.requireNode(arp::ui::UiDocumentNodeId::fromValue(1));
    assert(scrollResolved.rect.width == 300.0f);
    assert(scrollResolved.rect.height == 160.0f);
    assert(scrollResolved.clipRect.x == 0.0f);
    assert(scrollResolved.clipRect.width == 300.0f);
    assert(scrollResolved.clipsToBounds);
    assert(scrollResolved.resolvedStyleToken == text("panel.hover"));

    const auto& firstResolved = result.requireNode(arp::ui::UiDocumentNodeId::fromValue(2));
    assert(firstResolved.rect.x == 10.0f);
    assert(firstResolved.rect.y == 8.0f);
    assert(firstResolved.rect.width == 280.0f);
    assert(firstResolved.rect.height == 40.0f);
    assert(firstResolved.clipRect == scrollResolved.clipRect);

    const auto& secondResolved = result.requireNode(arp::ui::UiDocumentNodeId::fromValue(3));
    assert(secondResolved.rect.y == 54.0f);
    assert(secondResolved.rect.height == 120.0f);
    assert(secondResolved.clipRect.height == 160.0f);
}

void diagnostics_are_deterministic_for_invalid_layout_style_theme_contracts()
{
    arp::ui::UiTheme theme(text("diagnostic-theme"));
    theme.addToken({text("known"), arp::ui::UiThemeTokenValue::fromColor(1.0f, 1.0f, 1.0f, 1.0f)});

    arp::ui::UiLayoutStyleThemeDocument document(text("bad-anchor"));
    document.setCanvasRect(rect(0.0f, 0.0f, 100.0f, 100.0f));
    document.setTheme(theme);
    arp::ui::UiLayoutStyleNodeDesc node;
    node.nodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    node.anchors = arp::ui::UiAnchors::invalidForTest();
    node.constraints = arp::ui::UiSizeConstraints::minMax(10.0f, 10.0f, 20.0f, 20.0f);
    node.style = arp::ui::UiStyle::fromToken(text("known"));
    document.addNode(node);

    auto result = arp::ui::UiLayoutStyleThemeResolver().resolve(document);
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidLayoutAnchor);

    document = arp::ui::UiLayoutStyleThemeDocument(text("missing-token"));
    document.setCanvasRect(rect(0.0f, 0.0f, 100.0f, 100.0f));
    document.setTheme(theme);
    node = {};
    node.nodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    node.anchors = arp::ui::UiAnchors::fill();
    node.constraints = arp::ui::UiSizeConstraints::minMax(10.0f, 10.0f, 20.0f, 20.0f);
    node.style = arp::ui::UiStyle::fromToken(text("missing"));
    document.addNode(node);
    result = arp::ui::UiLayoutStyleThemeResolver().resolve(document);
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::MissingThemeToken);

    document = arp::ui::UiLayoutStyleThemeDocument(text("bad-constraint"));
    document.setCanvasRect(rect(0.0f, 0.0f, 100.0f, 100.0f));
    document.setTheme(theme);
    node = {};
    node.nodeId = arp::ui::UiDocumentNodeId::fromValue(1);
    node.anchors = arp::ui::UiAnchors::fill();
    node.constraints = arp::ui::UiSizeConstraints::minMax(80.0f, 10.0f, 20.0f, 20.0f);
    node.style = arp::ui::UiStyle::fromToken(text("known"));
    document.addNode(node);
    result = arp::ui::UiLayoutStyleThemeResolver().resolve(document);
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::ImpossibleLayoutConstraint);
}

} // namespace

int main()
{
    exposes_canonical_geometry_layout_types_in_ui_namespace();
    resolves_explicit_anchors_spacing_stack_grid_and_theme_tokens();
    resolves_percent_minmax_padding_gap_scroll_clipping_and_state_tokens();
    diagnostics_are_deterministic_for_invalid_layout_style_theme_contracts();
    return 0;
}
