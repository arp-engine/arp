#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/internal_asset_package.h"
#include "ui/ui.h"

#include <cstdint>
#include <filesystem>

namespace arp::ui {

class UiDocumentNodeId {
public:
    UiDocumentNodeId() = default;

    static UiDocumentNodeId invalid() { return UiDocumentNodeId(); }

    static UiDocumentNodeId fromValue(std::uint64_t value) { return UiDocumentNodeId(value); }

    bool valid() const { return value_ != 0u; }

    std::uint64_t value() const { return value_; }

    friend bool operator==(UiDocumentNodeId lhs, UiDocumentNodeId rhs)
    {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(UiDocumentNodeId lhs, UiDocumentNodeId rhs) { return !(lhs == rhs); }

private:
    explicit UiDocumentNodeId(std::uint64_t value) : value_(value) {}

    std::uint64_t value_ = 0u;
};

enum class UiDocumentDiagnosticCode {
    None,
    MissingCanvasMetadata,
    InvalidNodeId,
    DuplicateNodeId,
    InvalidParentId,
    UnsupportedControlType,
    MissingRequiredResourceBinding,
    MissingPsdLayerBinding,
    InvalidTextureMetadata,
    InvalidTextInputContract,
};

struct UiDocumentDiagnostic {
    UiDocumentDiagnosticCode code = UiDocumentDiagnosticCode::None;
    arp::core::pmr::String message{&arp::core::defaultCpuMemoryResource()};
};

struct UiDocumentCanvasMetadata {
    arp::core::pmr::String canvasId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String contractVersion{&arp::core::defaultCpuMemoryResource()};
    float width = 0.0f;
    float height = 0.0f;
    bool hasExplicitSize = false;
    float designDpi = 0.0f;
    bool hasExplicitDpi = false;
};

struct UiDocumentResourceBinding {
    arp::core::pmr::String role{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    bool required = false;
};

struct UiDocumentPsdLayerBinding {
    arp::core::pmr::String sourceLayerId{&arp::core::defaultCpuMemoryResource()};
};

enum class UiTextureAlphaMode {
    Unknown,
    Opaque,
    Straight,
    Premultiplied,
};

enum class UiTextureMipIntent {
    Unknown,
    None,
    Generate,
};

enum class UiTextureCompressionIntent {
    Unknown,
    UiLossless,
    PlatformDefault,
};

enum class UiTextureSamplerIntent {
    Unknown,
    LinearClamp,
    NearestClamp,
};

struct UiNineSliceBorder {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct UiDocumentTextureEntry {
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String sourceLayerId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasId{&arp::core::defaultCpuMemoryResource()};
    LayoutRect sourceRect;
    UiPoint pivot;
    UiNineSliceBorder nineSlice;
    UiTextureAlphaMode alphaMode = UiTextureAlphaMode::Unknown;
    bool srgb = false;
    bool hasExplicitSrgb = false;
    UiTextureMipIntent mipIntent = UiTextureMipIntent::Unknown;
    UiTextureCompressionIntent compressionIntent = UiTextureCompressionIntent::Unknown;
    UiTextureSamplerIntent samplerIntent = UiTextureSamplerIntent::Unknown;
};

struct UiDocumentNodeDesc {
    UiDocumentNodeId nodeId;
    UiDocumentNodeId parentNodeId;
    arp::core::pmr::String sourceId{&arp::core::defaultCpuMemoryResource()};
    UiControlType controlType = UiControlType::Unknown;
    std::uint32_t maxTextBytes = TextInputContract::defaultMaxTextBytes;
    LayoutRect layout;
    UiLayoutMode layoutMode = UiLayoutMode::None;
    UiStackLayout stackLayout;
    UiLayoutParticipation layoutParticipation = UiLayoutParticipation::Absolute;
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String stateId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String eventRole{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiDocumentResourceBinding> resourceBindings;
    UiDocumentPsdLayerBinding psdLayerBinding;
};

struct UiDocumentDesc {
    UiDocumentCanvasMetadata canvas;
    arp::core::pmr::Vector<UiDocumentNodeDesc> nodes;
    arp::core::pmr::Vector<UiDocumentTextureEntry> textures;
};

struct NodeDesc {
    NodeId nodeId;
    NodeId parentNodeId;
    UiControlType controlType = UiControlType::Unknown;
    std::uint32_t maxTextBytes = TextInputContract::defaultMaxTextBytes;
    LayoutRect layout;
    UiLayoutMode layoutMode = UiLayoutMode::None;
    UiStackLayout stackLayout;
    UiLayoutParticipation layoutParticipation = UiLayoutParticipation::Absolute;
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String stateId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String eventRole{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiDocumentResourceBinding> resourceBindings;
    UiDocumentPsdLayerBinding psdLayerBinding;
};

struct DocumentDesc {
    UiDocumentCanvasMetadata canvas;
    arp::core::pmr::Vector<NodeDesc> nodes;
    arp::core::pmr::Vector<UiDocumentTextureEntry> textures;
};

class ARP_API UiDocument final {
public:
    UiDocument() = default;
    explicit UiDocument(UiDocumentDesc desc);

    const UiDocumentCanvasMetadata& canvas() const { return desc_.canvas; }

    const arp::core::pmr::Vector<UiDocumentNodeDesc>& nodes() const { return desc_.nodes; }

    const arp::core::pmr::Vector<UiDocumentTextureEntry>& textures() const
    {
        return desc_.textures;
    }

    std::uint64_t stableHash() const;

private:
    UiDocumentDesc desc_;
};

class Document final {
public:
    Document() = default;
    explicit Document(DocumentDesc desc);

    const UiDocumentCanvasMetadata& canvas() const { return desc_.canvas; }

    const arp::core::pmr::Vector<NodeDesc>& nodes() const { return desc_.nodes; }

    const arp::core::pmr::Vector<UiDocumentTextureEntry>& textures() const
    {
        return desc_.textures;
    }

    std::uint64_t stableHash() const { return document_.stableHash(); }

    const UiDocument& legacyDocumentForMigration() const { return document_; }

private:
    DocumentDesc desc_;
    UiDocument document_;
};

class UiDocumentValidationResult final {
public:
    static UiDocumentValidationResult success();
    static UiDocumentValidationResult failure(
        UiDocumentDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDocumentDiagnosticCode::None; }

    const UiDocumentDiagnostic& diagnostic() const { return diagnostic_; }

private:
    explicit UiDocumentValidationResult(UiDocumentDiagnostic diagnostic);

    UiDocumentDiagnostic diagnostic_;
};

class ARP_API UiDocumentValidator final {
public:
    UiDocumentValidationResult validate(const UiDocument& document) const;
};

enum class UiGameSafeAreaPolicy {
    Invalid,
    RespectPlatformSafeArea,
    FullBleed,
};

enum class UiGameModalLayer {
    Invalid,
    Base,
    Overlay,
    BlockingModal,
};

enum class UiGameScreenDiagnosticCode {
    None,
    UnknownScreenOrState,
    MissingCommandBinding,
    InvalidInputAction,
    CyclicNavigation,
    ModalLayerConflict,
    MissingGameBinding,
    InvalidSafeAreaPolicy,
    LayerNameSemanticInference,
};

struct UiGameScreenDiagnostic {
    UiGameScreenDiagnosticCode code = UiGameScreenDiagnosticCode::None;
    arp::core::pmr::String message{&arp::core::defaultCpuMemoryResource()};
};

struct UiGameInputAction {
    arp::core::pmr::String actionId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String commandRole{&arp::core::defaultCpuMemoryResource()};
};

struct UiGameCommandBinding {
    arp::core::pmr::String commandRole{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String gameBinding{&arp::core::defaultCpuMemoryResource()};
};

struct UiGameDataBinding {
    arp::core::pmr::String bindingId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String gameBinding{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String visibilityCondition{&arp::core::defaultCpuMemoryResource()};
    UiControlValueKind valueKind = UiControlValueKind::Text;
};

struct UiGameNavigationEdge {
    arp::core::pmr::String fromViewId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String toViewId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String inputAction{&arp::core::defaultCpuMemoryResource()};
};

struct UiGameFocusScope {
    arp::core::pmr::String scopeId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String defaultViewId{&arp::core::defaultCpuMemoryResource()};
};

struct UiGameTransition {
    arp::core::pmr::String fromStateId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String toStateId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String commandRole{&arp::core::defaultCpuMemoryResource()};
};

struct UiGameScreenDesc {
    arp::core::pmr::String screenId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String viewId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String stateId{&arp::core::defaultCpuMemoryResource()};
    UiGameSafeAreaPolicy safeAreaPolicy = UiGameSafeAreaPolicy::Invalid;
    UiGameModalLayer modalLayer = UiGameModalLayer::Invalid;
    bool allowLayerNameSemanticInference = false;
    arp::core::pmr::Vector<UiGameInputAction> inputActions{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiGameCommandBinding> commandBindings{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiGameDataBinding> dataBindings{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiGameNavigationEdge> navigation{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiGameFocusScope> focusScopes{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiGameTransition> transitions{&arp::core::defaultCpuMemoryResource()};
};

class UiGameScreenDocument final {
public:
    UiGameScreenDocument() = default;

    void addScreen(UiGameScreenDesc screen);

    const arp::core::pmr::Vector<UiGameScreenDesc>& screens() const { return screens_; }

    std::uint64_t stableHash() const;

private:
    arp::core::pmr::Vector<UiGameScreenDesc> screens_{&arp::core::defaultCpuMemoryResource()};
};

class UiGameScreenValidationResult final {
public:
    static UiGameScreenValidationResult success();
    static UiGameScreenValidationResult failure(
        UiGameScreenDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiGameScreenDiagnosticCode::None; }

    const UiGameScreenDiagnostic& diagnostic() const { return diagnostic_; }

private:
    explicit UiGameScreenValidationResult(UiGameScreenDiagnostic diagnostic);

    UiGameScreenDiagnostic diagnostic_;
};

class UiGameScreenValidator final {
public:
    UiGameScreenValidationResult validate(const UiGameScreenDocument& document) const;
};

// A factory binding lives for one synchronous load; the runtime owns its product.
class IUiControlFactory {
public:
    virtual ~IUiControlFactory() = default;
    virtual arp::core::UniquePtr<Control> create(const UiControlDesc& desc) const = 0;
};

template <class T> class UiControlFactory final : public IUiControlFactory {
public:
    arp::core::UniquePtr<Control> create(const UiControlDesc&) const override
    {
        return arp::core::makeUnique<T>();
    }
};

class UiControlFactoryRegistry final {
public:
    struct Binding {
        arp::core::pmr::String sourceId;
        arp::core::UniquePtr<IUiControlFactory> factory;
    };

    explicit UiControlFactoryRegistry(size_t maxBindings) : bindings_(maxBindings) {}

    UiResult registerFactory(
        arp::core::pmr::String sourceId, arp::core::UniquePtr<IUiControlFactory> factory);
    const IUiControlFactory* find(const arp::core::pmr::String& sourceId) const;

    const arp::core::pmr::Vector<Binding>& bindings() const { return bindings_.values(); }

private:
    arp::core::BoundedSequence<Binding> bindings_;
};

struct UiArpuiRuntimeLoadDesc {
    std::filesystem::path path;
    arp::core::pmr::String sourceYaml{&arp::core::defaultCpuMemoryResource()};
    const UiControlFactoryRegistry* factories = nullptr;
    // Zero derives the exact static document size; specify spare capacity for mutations.
    size_t maxWidgetCount = 0;
};

struct UiRuntimeNodeBinding {
    arp::core::pmr::String sourceId{&arp::core::defaultCpuMemoryResource()};
    UiDocumentNodeId nodeId;
    WidgetId widgetId;
};

class UiArpuiRuntimeLoadResult final {
public:
    static UiArpuiRuntimeLoadResult success(UiDocument document,
        arp::core::UniquePtr<UiControlRuntime> runtime,
        arp::core::pmr::Vector<UiRuntimeNodeBinding> bindings = {});
    static UiArpuiRuntimeLoadResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    UiArpuiRuntimeLoadResult() = default;
    UiArpuiRuntimeLoadResult(UiArpuiRuntimeLoadResult&&) noexcept = default;
    UiArpuiRuntimeLoadResult& operator=(UiArpuiRuntimeLoadResult&&) noexcept = default;

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const UiDocument& document() const { return document_; }

    UiControlRuntime& runtime() { return *runtime_; }

    const UiControlRuntime& runtime() const { return *runtime_; }

    WidgetId widgetIdForSourceNodeId(const arp::core::pmr::String& sourceId) const;

private:
    UiDiagnostic diagnostic_;
    UiDocument document_;
    arp::core::UniquePtr<UiControlRuntime> runtime_;
    arp::core::pmr::Vector<UiRuntimeNodeBinding> bindings_;
};

class UiArpuiRuntimeLoader final {
public:
    UiArpuiRuntimeLoadResult load(const UiArpuiRuntimeLoadDesc& desc) const;
};

struct UiArpuiIrBuildDesc {
    std::filesystem::path path;
};

class UiArpuiIrBuildResult final {
public:
    static UiArpuiIrBuildResult success(arp::core::pmr::String documentYaml);
    static UiArpuiIrBuildResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::core::pmr::String& documentYaml() const { return documentYaml_; }

private:
    UiDiagnostic diagnostic_;
    arp::core::pmr::String documentYaml_{&arp::core::defaultCpuMemoryResource()};
};

class UiArpuiIrBuilder final {
public:
    UiArpuiIrBuildResult build(const UiArpuiIrBuildDesc& desc) const;
};

class UiArpuiIrRuntimeLoader final {
public:
    UiArpuiRuntimeLoadResult load(const UiArpuiIrBuildDesc& desc) const;
};

struct UiArpuiResourcePackageDesc {
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String documentYaml{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<arp::resource::ResourceDependency> dependencies{
        &arp::core::defaultCpuMemoryResource()};
};

struct UiArpuiResourcePackageBuildResult {
    bool success = false;
    arp::resource::InternalAssetPackageEntry entry;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

class UiArpuiResourcePackageBuilder final {
public:
    UiArpuiResourcePackageBuildResult build(const UiArpuiResourcePackageDesc& desc) const;
};

struct UiArpuiPackageRuntimeLoadDesc {
    std::filesystem::path packageDirectory;
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    const UiControlFactoryRegistry* factories = nullptr;
    size_t maxWidgetCount = 0;
};

class UiArpuiPackageRuntimeLoader final {
public:
    UiArpuiRuntimeLoadResult load(const UiArpuiPackageRuntimeLoadDesc& desc) const;
};

class DocumentComponentMountResult final {
public:
    static DocumentComponentMountResult success();
    static DocumentComponentMountResult failure(
        UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

private:
    explicit DocumentComponentMountResult(UiDiagnostic diagnostic);

    UiDiagnostic diagnostic_;
};

class DocumentComponentMounter final {
public:
    DocumentComponentMountResult mount(DocumentComponent& component) const;
};

enum class UiThemeTokenValueKind {
    Color,
    Text,
};

struct UiThemeTokenValue {
    UiThemeTokenValueKind kind = UiThemeTokenValueKind::Color;
    UiControlColorValue color;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};

    static UiThemeTokenValue fromColor(float r, float g, float b, float a);
    static UiThemeTokenValue fromText(arp::core::pmr::String value);
};

struct UiThemeToken {
    arp::core::pmr::String id{&arp::core::defaultCpuMemoryResource()};
    UiThemeTokenValue value;
};

class UiTheme final {
public:
    UiTheme() = default;
    explicit UiTheme(arp::core::pmr::String themeId);

    const arp::core::pmr::String& themeId() const { return themeId_; }

    void addToken(UiThemeToken token);
    const UiThemeToken* findToken(const arp::core::pmr::String& tokenId) const;

private:
    arp::core::pmr::String themeId_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiThemeToken> tokens_;
};

enum class UiAnchorMode {
    Invalid,
    Fill,
    Stretch,
};

struct UiAnchors {
    UiAnchorMode mode = UiAnchorMode::Invalid;

    static UiAnchors fill();
    static UiAnchors stretch();
    static UiAnchors invalidForTest();

    bool valid() const { return mode == UiAnchorMode::Fill || mode == UiAnchorMode::Stretch; }
};

struct UiSizeConstraints {
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;

    static UiSizeConstraints minMax(
        float minWidth, float minHeight, float maxWidth, float maxHeight);
    bool valid() const;
};

enum class UiClippingMode {
    Inherit,
    ClipToBounds,
};

struct UiClipping {
    UiClippingMode mode = UiClippingMode::Inherit;

    static UiClipping inherit();
    static UiClipping clipToBounds();
};

enum class UiStyleState {
    Normal,
    Disabled,
    Hover,
    Focus,
    Active,
};

struct UiStyleStateToken {
    UiStyleState state = UiStyleState::Disabled;
    arp::core::pmr::String tokenId{&arp::core::defaultCpuMemoryResource()};
};

class UiStyle final {
public:
    static UiStyle fromToken(arp::core::pmr::String tokenId);

    const arp::core::pmr::String& baseTokenId() const { return baseTokenId_; }

    const arp::core::pmr::Vector<UiStyleStateToken>& stateTokens() const { return stateTokens_; }

    void addState(UiStyleState state, arp::core::pmr::String tokenId);

private:
    arp::core::pmr::String baseTokenId_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiStyleStateToken> stateTokens_;
};

struct UiLayoutStyleNodeDesc {
    UiDocumentNodeId nodeId;
    UiDocumentNodeId parentNodeId;
    UiAnchors anchors;
    UiBoxEdges padding;
    UiBoxEdges margin;
    UiSizeConstraints constraints;
    Size fixedSize;
    Size percentSize;
    UiLayoutMode layout = UiLayoutMode::None;
    UiStackLayout stack;
    UiGridLayout grid;
    int zOrder = 0;
    UiClipping clipping;
    UiStyle style;
    UiStyleState state = UiStyleState::Normal;
};

class UiLayoutStyleThemeDocument final {
public:
    UiLayoutStyleThemeDocument() = default;
    explicit UiLayoutStyleThemeDocument(arp::core::pmr::String documentId);

    void setCanvasRect(LayoutRect rect) { canvasRect_ = rect; }

    void setTheme(UiTheme theme) { theme_ = std::move(theme); }

    void addNode(UiLayoutStyleNodeDesc node);

    const arp::core::pmr::String& documentId() const { return documentId_; }

    LayoutRect canvasRect() const { return canvasRect_; }

    const UiTheme& theme() const { return theme_; }

    const arp::core::pmr::Vector<UiLayoutStyleNodeDesc>& nodes() const { return nodes_; }

private:
    arp::core::pmr::String documentId_{&arp::core::defaultCpuMemoryResource()};
    LayoutRect canvasRect_;
    UiTheme theme_;
    arp::core::pmr::Vector<UiLayoutStyleNodeDesc> nodes_;
};

struct UiResolvedLayoutStyleNode {
    UiDocumentNodeId nodeId;
    LayoutRect rect;
    LayoutRect clipRect;
    UiGridLayout grid;
    int zOrder = 0;
    bool clipsToBounds = false;
    arp::core::pmr::String resolvedStyleToken{&arp::core::defaultCpuMemoryResource()};
};

class UiLayoutStyleThemeResult final {
public:
    static UiLayoutStyleThemeResult success(
        arp::core::pmr::Vector<UiResolvedLayoutStyleNode> nodes);
    static UiLayoutStyleThemeResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::core::pmr::Vector<UiResolvedLayoutStyleNode>& nodes() const { return nodes_; }

    const UiResolvedLayoutStyleNode& requireNode(UiDocumentNodeId nodeId) const;

private:
    UiLayoutStyleThemeResult(
        UiDiagnostic diagnostic, arp::core::pmr::Vector<UiResolvedLayoutStyleNode> nodes);

    UiDiagnostic diagnostic_;
    arp::core::pmr::Vector<UiResolvedLayoutStyleNode> nodes_;
};

class UiLayoutStyleThemeResolver final {
public:
    UiLayoutStyleThemeResult resolve(const UiLayoutStyleThemeDocument& document) const;
};

} // namespace arp::ui
