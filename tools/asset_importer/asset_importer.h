#pragma once

#include "assets/asset_pipeline.h"
#include "core/memory/cpu_allocator.h"
#include "gui/gui.h"
#include "tools/asset_importer/script_runner.h"
#include "ui/ui_document.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace arp::tools::asset_importer {

struct Options {
    bool selfTest = false;
    std::pmr::string materialJsonPath;
    std::pmr::string gltfJsonPath;
    std::pmr::string uiLayoutJsonPath;
    std::pmr::string uiPsdPath;
    std::pmr::string uiManifestPath;
    std::pmr::string outputUiPath;
    std::pmr::string unityPrefabFolder;
    std::pmr::string unityParticlePrefabPath;
    std::pmr::string unityScriptPath;
    std::pmr::string scriptAotPackagePath;
    std::pmr::string scriptAotModule;
    std::pmr::string unityScriptInputPath;
    std::pmr::string unityScriptOutputPath;
    std::pmr::string outputPackagePath;
    std::pmr::string prefabAlias;
    std::pmr::string prefabDisplayName;
    std::pmr::string basicShaderSlangPath;
    std::pmr::string stylizedShaderSlangPath;
    std::pmr::string guiShaderSlangPath;
    std::pmr::string startupSplashShaderSlangPath;
};

inline std::filesystem::path scriptToolHostLibrary(
    const std::filesystem::path& executablePath, const std::filesystem::path& projectRoot)
{
#if defined(ARP_SCRIPT_AOT_ONLY_TOOL)
    constexpr const char* libraryName = "arp_script_aot_tool_host.dll";
    const auto fallback = projectRoot / "build" / "win-vulkan-release" / libraryName;
#else
    constexpr const char* libraryName = "arp_engine.dll";
    const auto fallback = projectRoot / "build" / "win-auto-debug" / libraryName;
#endif
    return executablePath.empty() ? fallback : executablePath.parent_path() / libraryName;
}

struct UiLayoutJsonSchema {
    std::pmr::string typeName;
    std::pmr::vector<std::pmr::string> requiredFields;
    std::pmr::vector<std::pmr::string> requiredNodeFields;
    std::pmr::vector<std::pmr::string> optionalNodeFields;

    static UiLayoutJsonSchema retainedGui()
    {
        UiLayoutJsonSchema schema;
        schema.typeName = "GuiLayout";
        schema.requiredFields = {
            "type",
            "name",
            "nodes",
        };
        schema.requiredNodeFields = {
            "id",
            "type",
        };
        schema.optionalNodeFields = {
            "parent",
            "text",
            "width",
            "height",
            "min",
            "max",
            "value",
            "checked",
        };
        return schema;
    }
};

struct UiLayoutNode {
    struct State {
        uint64_t id = 0;
        std::pmr::string text;
        gui::Style style;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float value = 0.0f;
        bool checked = false;
    };

    uint64_t parentId = 0;
    gui::NodeType type = gui::NodeType::Panel;
    State node;
};

struct UiLayoutCache {
    uint32_t nodeCount = 0;
    uint32_t textRunCount = 0;
    uint32_t stableHash = 0;
};

struct UiLayoutImportResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::pmr::string name;
    std::pmr::vector<UiLayoutNode> nodes;
    UiLayoutCache cache;
};

inline uint32_t hashString(uint32_t hash, const std::pmr::string& value)
{
    for (const auto ch : value) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

enum class HotReloadKind { Shader, MaterialJson, UiLayoutJson };

struct HotReloadEvent {
    bool changed = false;
    bool withinBudget = true;
    HotReloadKind kind = HotReloadKind::Shader;
    std::pmr::string path;
    uint32_t contentHash = 0;
};

struct HotReloadWatch {
    std::pmr::string path;
    HotReloadKind kind = HotReloadKind::Shader;
    uint32_t contentHash = 0;
};

using assets::MobileTextureCompression;
using assets::RuntimeAssetPackageDesc;
using assets::RuntimeAssetPackagePlan;
using assets::RuntimeMaterialAssetDesc;
using assets::RuntimeMeshAssetDesc;
using assets::RuntimeSceneManifestDesc;
using assets::RuntimeShaderReferenceDesc;
using assets::RuntimeTextureAssetDesc;
using assets::RuntimeUiLayoutAssetDesc;

class HotReloadRegistry {
public:
    float latencyBudgetSeconds = 2.0f;

    static HotReloadRegistry withDefaults() { return HotReloadRegistry{}; }

    HotReloadEvent updateFromContent(const std::pmr::string& path,
        HotReloadKind kind,
        const std::pmr::string& content,
        double elapsedSeconds)
    {
        const auto hash = hashString(2166136261u, content);
        HotReloadEvent event;
        event.kind = kind;
        event.path = path;
        event.contentHash = hash;
        event.withinBudget = elapsedSeconds <= static_cast<double>(latencyBudgetSeconds);

        auto found =
            std::find_if(watches_.begin(), watches_.end(), [&](const HotReloadWatch& watch) {
                return watch.path == path;
            });
        if (found == watches_.end()) {
            watches_.push_back(HotReloadWatch{path, kind, hash});
            event.changed = true;
            return event;
        }

        found->kind = kind;
        if (found->contentHash != hash) {
            found->contentHash = hash;
            event.changed = true;
        }
        return event;
    }

    uint32_t watchedCount() const { return static_cast<uint32_t>(watches_.size()); }

private:
    std::pmr::vector<HotReloadWatch> watches_;
};

inline Options parseOptions(int argc, const char* const* argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::pmr::string arg = argv[index];
        if (arg == "--self-test") {
            options.selfTest = true;
        } else if (arg == "--material-json" && index + 1 < argc) {
            options.materialJsonPath = argv[++index];
        } else if (arg == "--gltf-json" && index + 1 < argc) {
            options.gltfJsonPath = argv[++index];
        } else if (arg == "--ui-layout-json" && index + 1 < argc) {
            options.uiLayoutJsonPath = argv[++index];
        } else if (arg == "--ui-psd" && index + 1 < argc) {
            options.uiPsdPath = argv[++index];
        } else if (arg == "--ui-manifest" && index + 1 < argc) {
            options.uiManifestPath = argv[++index];
        } else if (arg == "--output-ui" && index + 1 < argc) {
            options.outputUiPath = argv[++index];
        } else if (arg == "--unity-prefab-folder" && index + 1 < argc) {
            options.unityPrefabFolder = argv[++index];
        } else if (arg == "--unity-particle-prefab" && index + 1 < argc) {
            options.unityParticlePrefabPath = argv[++index];
        } else if (arg == "--unity-script" && index + 1 < argc) {
            options.unityScriptPath = argv[++index];
        } else if (arg == "--script-aot-package" && index + 1 < argc) {
            options.scriptAotPackagePath = argv[++index];
        } else if (arg == "--script-aot-module" && index + 1 < argc) {
            options.scriptAotModule = argv[++index];
        } else if (arg == "--script-input" && index + 1 < argc) {
            options.unityScriptInputPath = argv[++index];
        } else if (arg == "--script-output" && index + 1 < argc) {
            options.unityScriptOutputPath = argv[++index];
        } else if (arg == "--output-package" && index + 1 < argc) {
            options.outputPackagePath = argv[++index];
        } else if (arg == "--prefab-alias" && index + 1 < argc) {
            options.prefabAlias = argv[++index];
        } else if (arg == "--prefab-display-name" && index + 1 < argc) {
            options.prefabDisplayName = argv[++index];
        } else if (arg == "--basic-shader-slang" && index + 1 < argc) {
            options.basicShaderSlangPath = argv[++index];
        } else if (arg == "--stylized-shader-slang" && index + 1 < argc) {
            options.stylizedShaderSlangPath = argv[++index];
        } else if (arg == "--gui-shader-slang" && index + 1 < argc) {
            options.guiShaderSlangPath = argv[++index];
        } else if (arg == "--startup-splash-shader-slang" && index + 1 < argc) {
            options.startupSplashShaderSlangPath = argv[++index];
        }
    }
    return options;
}

inline bool extractObjectArrayAfterKey(
    const std::pmr::string& json, const char* key, std::pmr::vector<std::pmr::string>& out)
{
    std::pmr::string arrayText;
    if (!assets::detail::extractArrayTextAfterKey(json, key, arrayText)) {
        return false;
    }

    int depth = 0;
    size_t objectStart = std::pmr::string::npos;
    for (size_t index = 0; index < arrayText.size(); ++index) {
        if (arrayText[index] == '{') {
            if (depth == 0) {
                objectStart = index;
            }
            ++depth;
        } else if (arrayText[index] == '}') {
            --depth;
            if (depth == 0 && objectStart != std::pmr::string::npos) {
                out.push_back(arrayText.substr(objectStart, index - objectStart + 1));
                objectStart = std::pmr::string::npos;
            }
        }
    }

    return !out.empty();
}

inline bool parseBoolAfterKey(const std::pmr::string& json, const char* key, bool defaultValue)
{
    const auto keyOffset = json.find(key);
    if (keyOffset == std::pmr::string::npos) {
        return defaultValue;
    }

    const auto colon = json.find(':', keyOffset);
    if (colon == std::pmr::string::npos) {
        return defaultValue;
    }

    const auto cursor = assets::detail::skipWhitespace(json.c_str() + colon + 1);
    if (std::pmr::string(cursor).find("true") == 0) {
        return true;
    }
    if (std::pmr::string(cursor).find("false") == 0) {
        return false;
    }
    return defaultValue;
}

inline gui::NodeType parseNodeType(const std::pmr::string& type)
{
    if (type == "Label") {
        return gui::NodeType::Label;
    }
    if (type == "Button") {
        return gui::NodeType::Button;
    }
    if (type == "Checkbox") {
        return gui::NodeType::Checkbox;
    }
    if (type == "Slider") {
        return gui::NodeType::Slider;
    }
    if (type == "TextInput") {
        return gui::NodeType::TextInput;
    }
    if (type == "NumericInput") {
        return gui::NodeType::NumericInput;
    }
    if (type == "ScrollView") {
        return gui::NodeType::ScrollView;
    }
    if (type == "Dropdown") {
        return gui::NodeType::Dropdown;
    }
    if (type == "Tabs") {
        return gui::NodeType::Tabs;
    }
    if (type == "TreeView") {
        return gui::NodeType::TreeView;
    }
    if (type == "ColorPicker") {
        return gui::NodeType::ColorPicker;
    }
    if (type == "Image") {
        return gui::NodeType::Image;
    }
    return gui::NodeType::Panel;
}

struct UiMockPsdLayer {
    std::pmr::string id;
    std::pmr::string diagnosticName;
    std::pmr::string kind;
};

struct UiMockPsdLayerTree {
    bool success = false;
    std::pmr::string diagnostics;
    float width = 0.0f;
    float height = 0.0f;
    float dpi = 0.0f;
    std::pmr::vector<UiMockPsdLayer> layers;
};

struct UiPsdManifestImportResult {
    bool success = false;
    std::pmr::string diagnostics;
    arp::ui::UiDocument document;
};

inline arp::ui::UiControlType parseUiControlType(const std::pmr::string& type)
{
    if (type == "Panel") {
        return arp::ui::UiControlType::Panel;
    }
    if (type == "Label") {
        return arp::ui::UiControlType::Label;
    }
    if (type == "Button") {
        return arp::ui::UiControlType::Button;
    }
    if (type == "Slider") {
        return arp::ui::UiControlType::Slider;
    }
    if (type == "Checkbox") {
        return arp::ui::UiControlType::Checkbox;
    }
    if (type == "TextInput") {
        return arp::ui::UiControlType::TextInput;
    }
    if (type == "NumericInput") {
        return arp::ui::UiControlType::NumericInput;
    }
    if (type == "ScrollView") {
        return arp::ui::UiControlType::ScrollView;
    }
    if (type == "Dropdown") {
        return arp::ui::UiControlType::Dropdown;
    }
    if (type == "Tabs") {
        return arp::ui::UiControlType::Tabs;
    }
    if (type == "TreeView") {
        return arp::ui::UiControlType::TreeView;
    }
    if (type == "ColorPicker") {
        return arp::ui::UiControlType::ColorPicker;
    }
    if (type == "Image") {
        return arp::ui::UiControlType::Image;
    }
    return arp::ui::UiControlType::Unknown;
}

inline bool jsonHasKey(const std::pmr::string& json, const char* key)
{
    return json.find(key) != std::pmr::string::npos;
}

inline arp::ui::UiTextureAlphaMode parseUiTextureAlphaMode(const std::pmr::string& value)
{
    if (value == "opaque") {
        return arp::ui::UiTextureAlphaMode::Opaque;
    }
    if (value == "straight") {
        return arp::ui::UiTextureAlphaMode::Straight;
    }
    if (value == "premultiplied") {
        return arp::ui::UiTextureAlphaMode::Premultiplied;
    }
    return arp::ui::UiTextureAlphaMode::Unknown;
}

inline arp::ui::UiTextureMipIntent parseUiTextureMipIntent(const std::pmr::string& value)
{
    if (value == "none") {
        return arp::ui::UiTextureMipIntent::None;
    }
    if (value == "generate") {
        return arp::ui::UiTextureMipIntent::Generate;
    }
    return arp::ui::UiTextureMipIntent::Unknown;
}

inline arp::ui::UiTextureCompressionIntent parseUiTextureCompressionIntent(
    const std::pmr::string& value)
{
    if (value == "ui-lossless") {
        return arp::ui::UiTextureCompressionIntent::UiLossless;
    }
    if (value == "platform-default") {
        return arp::ui::UiTextureCompressionIntent::PlatformDefault;
    }
    return arp::ui::UiTextureCompressionIntent::Unknown;
}

inline arp::ui::UiTextureSamplerIntent parseUiTextureSamplerIntent(const std::pmr::string& value)
{
    if (value == "linear-clamp") {
        return arp::ui::UiTextureSamplerIntent::LinearClamp;
    }
    if (value == "nearest-clamp") {
        return arp::ui::UiTextureSamplerIntent::NearestClamp;
    }
    return arp::ui::UiTextureSamplerIntent::Unknown;
}

inline UiMockPsdLayerTree importMockPsdLayerTree(const std::pmr::string& json)
{
    UiMockPsdLayerTree result;
    std::pmr::string type;
    if (!assets::detail::parseStringAfterKey(json, "\"type\"", type)
        || type != "MockPsdLayerTree") {
        result.diagnostics = "mock PSD fixture must declare type MockPsdLayerTree";
        return result;
    }
    result.width = assets::detail::parseFloatAfterKey(json, "\"width\"", 0.0f);
    result.height = assets::detail::parseFloatAfterKey(json, "\"height\"", 0.0f);
    result.dpi = assets::detail::parseFloatAfterKey(json, "\"dpi\"", 0.0f);
    if (result.width <= 0.0f || result.height <= 0.0f || result.dpi <= 0.0f) {
        result.diagnostics = "mock PSD fixture requires explicit canvas width, height, and dpi";
        return result;
    }

    std::pmr::vector<std::pmr::string> layerObjects;
    if (!extractObjectArrayAfterKey(json, "\"layers\"", layerObjects)) {
        result.diagnostics = "mock PSD fixture missing layers array";
        return result;
    }
    for (const auto& object : layerObjects) {
        UiMockPsdLayer layer;
        if (!assets::detail::parseStringAfterKey(object, "\"id\"", layer.id)) {
            result.diagnostics = "mock PSD layer missing id";
            return result;
        }
        assets::detail::parseStringAfterKey(object, "\"name\"", layer.diagnosticName);
        if (!assets::detail::parseStringAfterKey(object, "\"kind\"", layer.kind)) {
            result.diagnostics = "mock PSD layer missing explicit kind";
            return result;
        }
        if (layer.kind != "group" && layer.kind != "pixel") {
            result.diagnostics = "unsupported PSD source layer kind: ";
            result.diagnostics += layer.kind;
            return result;
        }
        result.layers.push_back(layer);
    }
    result.success = true;
    return result;
}

inline bool mockPsdLayerExists(const UiMockPsdLayerTree& psd, const std::pmr::string& layerId)
{
    return std::find_if(psd.layers.begin(), psd.layers.end(), [&](const UiMockPsdLayer& layer) {
        return layer.id == layerId;
    }) != psd.layers.end();
}

inline UiPsdManifestImportResult importUiFromPsdManifest(
    const std::pmr::string& psdJson, const std::pmr::string& manifestJson)
{
    UiPsdManifestImportResult result;
    const auto psd = importMockPsdLayerTree(psdJson);
    if (!psd.success) {
        result.diagnostics = psd.diagnostics;
        return result;
    }

    std::pmr::string type;
    if (!assets::detail::parseStringAfterKey(manifestJson, "\"type\"", type)
        || type != "ArpUiManifest") {
        result.diagnostics = "UI manifest must declare type ArpUiManifest";
        return result;
    }

    arp::ui::UiDocumentDesc desc;
    if (!assets::detail::parseStringAfterKey(manifestJson, "\"canvasId\"", desc.canvas.canvasId)
        || !assets::detail::parseStringAfterKey(
            manifestJson, "\"contractVersion\"", desc.canvas.contractVersion)) {
        result.diagnostics = "UI manifest requires canvasId and contractVersion";
        return result;
    }
    desc.canvas.width = psd.width;
    desc.canvas.height = psd.height;
    desc.canvas.hasExplicitSize = true;
    desc.canvas.designDpi = psd.dpi;
    desc.canvas.hasExplicitDpi = true;

    std::pmr::vector<std::pmr::string> nodeObjects;
    if (!extractObjectArrayAfterKey(manifestJson, "\"nodes\"", nodeObjects)) {
        result.diagnostics = "UI manifest missing nodes array";
        return result;
    }

    std::pmr::vector<std::pmr::string> textureObjects;
    if (extractObjectArrayAfterKey(manifestJson, "\"textures\"", textureObjects)) {
        for (const auto& object : textureObjects) {
            arp::ui::UiDocumentTextureEntry texture;
            if (!assets::detail::parseStringAfterKey(object, "\"resourceId\"", texture.resourceId)
                || !assets::detail::parseStringAfterKey(object, "\"layer\"", texture.sourceLayerId)
                || !assets::detail::parseStringAfterKey(object, "\"atlas\"", texture.atlasId)) {
                result.diagnostics =
                    "UI manifest texture requires resourceId, layer, and atlas metadata";
                return result;
            }
            if (!mockPsdLayerExists(psd, texture.sourceLayerId)) {
                result.diagnostics = "UI manifest texture references missing PSD layer binding: ";
                result.diagnostics += texture.sourceLayerId;
                return result;
            }
            texture.sourceRect = arp::ui::LayoutRect::fromOriginSize(
                assets::detail::parseFloatAfterKey(object, "\"x\"", -1.0f),
                assets::detail::parseFloatAfterKey(object, "\"y\"", -1.0f),
                assets::detail::parseFloatAfterKey(object, "\"width\"", -1.0f),
                assets::detail::parseFloatAfterKey(object, "\"height\"", -1.0f));
            texture.pivot = {
                assets::detail::parseFloatAfterKey(object, "\"pivotX\"", 0.0f),
                assets::detail::parseFloatAfterKey(object, "\"pivotY\"", 0.0f),
            };
            texture.nineSlice = {
                assets::detail::parseFloatAfterKey(object, "\"sliceLeft\"", -1.0f),
                assets::detail::parseFloatAfterKey(object, "\"sliceTop\"", -1.0f),
                assets::detail::parseFloatAfterKey(object, "\"sliceRight\"", -1.0f),
                assets::detail::parseFloatAfterKey(object, "\"sliceBottom\"", -1.0f),
            };
            std::pmr::string alpha;
            std::pmr::string mips;
            std::pmr::string compression;
            std::pmr::string sampler;
            assets::detail::parseStringAfterKey(object, "\"alpha\"", alpha);
            assets::detail::parseStringAfterKey(object, "\"mips\"", mips);
            assets::detail::parseStringAfterKey(object, "\"compression\"", compression);
            assets::detail::parseStringAfterKey(object, "\"sampler\"", sampler);
            texture.alphaMode = parseUiTextureAlphaMode(alpha);
            texture.hasExplicitSrgb = jsonHasKey(object, "\"srgb\"");
            texture.srgb = parseBoolAfterKey(object, "\"srgb\"", false);
            texture.mipIntent = parseUiTextureMipIntent(mips);
            texture.compressionIntent = parseUiTextureCompressionIntent(compression);
            texture.samplerIntent = parseUiTextureSamplerIntent(sampler);
            desc.textures.push_back(texture);
        }
    }

    std::pmr::vector<std::pmr::string> usedLayerBindings;
    for (const auto& object : nodeObjects) {
        arp::ui::UiDocumentNodeDesc node;
        const auto id = assets::detail::parseUIntAfterKey(object, "\"id\"", 0u);
        if (id == 0u) {
            result.diagnostics = "UI manifest node missing non-zero id";
            return result;
        }
        node.nodeId = arp::ui::UiDocumentNodeId::fromValue(id);
        const auto parent = assets::detail::parseUIntAfterKey(object, "\"parent\"", 0u);
        node.parentNodeId = parent == 0u ? arp::ui::UiDocumentNodeId::invalid()
                                         : arp::ui::UiDocumentNodeId::fromValue(parent);

        std::pmr::string controlType;
        if (!assets::detail::parseStringAfterKey(object, "\"type\"", controlType)) {
            result.diagnostics = "UI manifest node missing control type";
            return result;
        }
        node.controlType = parseUiControlType(controlType);

        if (!assets::detail::parseStringAfterKey(
                object, "\"layer\"", node.psdLayerBinding.sourceLayerId)) {
            result.diagnostics = "UI manifest node missing PSD layer binding";
            return result;
        }
        if (!mockPsdLayerExists(psd, node.psdLayerBinding.sourceLayerId)) {
            result.diagnostics = "UI manifest references missing PSD layer binding: ";
            result.diagnostics += node.psdLayerBinding.sourceLayerId;
            return result;
        }
        if (std::find(usedLayerBindings.begin(),
                usedLayerBindings.end(),
                node.psdLayerBinding.sourceLayerId)
            != usedLayerBindings.end()) {
            result.diagnostics = "UI manifest contains duplicate PSD layer binding: ";
            result.diagnostics += node.psdLayerBinding.sourceLayerId;
            return result;
        }
        usedLayerBindings.push_back(node.psdLayerBinding.sourceLayerId);
        if (!assets::detail::parseStringAfterKey(object, "\"style\"", node.styleId)
            || !assets::detail::parseStringAfterKey(object, "\"state\"", node.stateId)
            || !assets::detail::parseStringAfterKey(object, "\"event\"", node.eventRole)) {
            result.diagnostics = "UI manifest node requires explicit style, state, and event role";
            return result;
        }
        assets::detail::parseStringAfterKey(object, "\"text\"", node.text);
        node.layout = arp::ui::LayoutRect::fromOriginSize(
            assets::detail::parseFloatAfterKey(object, "\"x\"", 0.0f),
            assets::detail::parseFloatAfterKey(object, "\"y\"", 0.0f),
            assets::detail::parseFloatAfterKey(object, "\"width\"", -1.0f),
            assets::detail::parseFloatAfterKey(object, "\"height\"", -1.0f));
        if (!node.layout.valid()) {
            result.diagnostics =
                "UI manifest node requires explicit non-negative layout width and height";
            return result;
        }

        std::pmr::string resourceRole;
        std::pmr::string resourceId;
        const auto hasResourceRole =
            assets::detail::parseStringAfterKey(object, "\"resourceRole\"", resourceRole);
        const auto hasResourceId =
            assets::detail::parseStringAfterKey(object, "\"resourceId\"", resourceId);
        if (hasResourceRole || hasResourceId) {
            if (!hasResourceRole || !hasResourceId) {
                result.diagnostics =
                    "UI manifest resource binding requires both resourceRole and resourceId";
                return result;
            }
            node.resourceBindings.push_back(arp::ui::UiDocumentResourceBinding{
                arp::core::pmr::String(
                    resourceRole.c_str(), &arp::core::defaultCpuMemoryResource()),
                arp::core::pmr::String(resourceId.c_str(), &arp::core::defaultCpuMemoryResource()),
                true,
            });
        }
        desc.nodes.push_back(node);
    }

    result.document = arp::ui::UiDocument(std::move(desc));
    const auto validation = arp::ui::UiDocumentValidator().validate(result.document);
    if (!validation.ok()) {
        result.diagnostics = arp::core::toStdString(validation.diagnostic().message);
        return result;
    }
    result.success = true;
    return result;
}

inline UiLayoutImportResult importUiLayoutFromJson(const std::pmr::string& json)
{
    UiLayoutImportResult result;

    std::pmr::string type;
    if (!assets::detail::parseStringAfterKey(json, "\"type\"", type) || type != "GuiLayout") {
        result.diagnostics = "UI layout JSON must declare type GuiLayout";
        return result;
    }

    if (!assets::detail::parseStringAfterKey(json, "\"name\"", result.name)) {
        result.diagnostics = "UI layout JSON missing required name";
        return result;
    }

    std::pmr::vector<std::pmr::string> nodeObjects;
    if (!extractObjectArrayAfterKey(json, "\"nodes\"", nodeObjects)) {
        result.diagnostics = "UI layout JSON missing nodes array";
        return result;
    }

    uint32_t hash = 2166136261u;
    hash = hashString(hash, result.name);
    for (const auto& object : nodeObjects) {
        const auto id = assets::detail::parseUIntAfterKey(object, "\"id\"", 0);
        if (id == 0) {
            result.diagnostics = "UI layout node missing non-zero id";
            return result;
        }

        std::pmr::string nodeType;
        if (!assets::detail::parseStringAfterKey(object, "\"type\"", nodeType)) {
            result.diagnostics = "UI layout node missing type";
            return result;
        }

        std::pmr::string text;
        assets::detail::parseStringAfterKey(object, "\"text\"", text);
        const auto width = assets::detail::parseFloatAfterKey(object, "\"width\"", 0.0f);
        const auto height = assets::detail::parseFloatAfterKey(object, "\"height\"", 0.0f);
        const auto minValue = assets::detail::parseFloatAfterKey(object, "\"min\"", 0.0f);
        const auto maxValue = assets::detail::parseFloatAfterKey(object, "\"max\"", 1.0f);
        const auto value = assets::detail::parseFloatAfterKey(object, "\"value\"", 0.0f);
        const auto checked = parseBoolAfterKey(object, "\"checked\"", false);
        const auto parent = assets::detail::parseUIntAfterKey(object, "\"parent\"", 0);

        const auto parsedType = parseNodeType(nodeType);
        result.nodes.push_back(UiLayoutNode{
            parent,
            parsedType,
            UiLayoutNode::State{
                id,
                text,
                gui::Style{{width, height}, {}, {}, 0.0f},
                minValue,
                maxValue,
                value,
                checked,
            },
        });

        hash ^= static_cast<uint32_t>(id);
        hash *= 16777619u;
        hash = hashString(hash, nodeType);
        hash = hashString(hash, text);
        if (!text.empty()) {
            ++result.cache.textRunCount;
        }
    }

    result.cache.nodeCount = static_cast<uint32_t>(result.nodes.size());
    result.cache.stableHash = hash;
    result.success = true;
    return result;
}

inline bool readTextFile(const std::pmr::string& path, std::pmr::string& out)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.good()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = arp::core::toPmrString(buffer.str());
    return true;
}

inline const char* uiTextureAlphaModeName(arp::ui::UiTextureAlphaMode value);
inline const char* uiTextureMipIntentName(arp::ui::UiTextureMipIntent value);
inline const char* uiTextureCompressionIntentName(arp::ui::UiTextureCompressionIntent value);
inline const char* uiTextureSamplerIntentName(arp::ui::UiTextureSamplerIntent value);

inline bool writeArpUiDocument(const arp::ui::UiDocument& document,
    const std::pmr::string& path,
    std::pmr::string& diagnostics)
{
    const auto outputPath = std::filesystem::path(path.c_str());
    std::error_code error;
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            diagnostics = "failed to create .arpui output directory: ";
            diagnostics += outputPath.parent_path().generic_string();
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.good()) {
        diagnostics = "failed to open .arpui output: ";
        diagnostics += path;
        return false;
    }

    output << "{\n";
    output << "  \"type\": \"ArpUiDocument\",\n";
    output << "  \"contractVersion\": \"" << document.canvas().contractVersion << "\",\n";
    output << "  \"canvasId\": \"" << document.canvas().canvasId << "\",\n";
    output << "  \"stableHash\": " << document.stableHash() << ",\n";
    output << "  \"canvas\": { \"width\": " << document.canvas().width
           << ", \"height\": " << document.canvas().height
           << ", \"dpi\": " << document.canvas().designDpi << " },\n";
    output << "  \"textures\": [\n";
    for (std::size_t index = 0; index < document.textures().size(); ++index) {
        const auto& texture = document.textures()[index];
        output << "    { \"resourceId\": \"" << texture.resourceId << "\", \"layer\": \""
               << texture.sourceLayerId << "\", \"atlas\": \"" << texture.atlasId
               << "\", \"x\": " << texture.sourceRect.x << ", \"y\": " << texture.sourceRect.y
               << ", \"width\": " << texture.sourceRect.width
               << ", \"height\": " << texture.sourceRect.height
               << ", \"pivotX\": " << texture.pivot.x << ", \"pivotY\": " << texture.pivot.y
               << ", \"sliceLeft\": " << texture.nineSlice.left
               << ", \"sliceTop\": " << texture.nineSlice.top
               << ", \"sliceRight\": " << texture.nineSlice.right
               << ", \"sliceBottom\": " << texture.nineSlice.bottom << ", \"alpha\": \""
               << uiTextureAlphaModeName(texture.alphaMode) << "\""
               << ", \"srgb\": " << (texture.srgb ? "true" : "false") << ", \"mips\": \""
               << uiTextureMipIntentName(texture.mipIntent) << "\""
               << ", \"compression\": \""
               << uiTextureCompressionIntentName(texture.compressionIntent) << "\""
               << ", \"sampler\": \"" << uiTextureSamplerIntentName(texture.samplerIntent) << "\""
               << " }";
        if (index + 1u < document.textures().size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ],\n";
    output << "  \"nodes\": [\n";
    for (std::size_t index = 0; index < document.nodes().size(); ++index) {
        const auto& node = document.nodes()[index];
        output << "    { \"id\": " << node.nodeId.value()
               << ", \"parent\": " << node.parentNodeId.value() << ", \"type\": \""
               << arp::ui::controlTypeName(node.controlType) << "\", \"layer\": \""
               << node.psdLayerBinding.sourceLayerId << "\", \"x\": " << node.layout.x
               << ", \"y\": " << node.layout.y << ", \"width\": " << node.layout.width
               << ", \"height\": " << node.layout.height << ", \"style\": \"" << node.styleId
               << "\", \"state\": \"" << node.stateId << "\", \"event\": \"" << node.eventRole
               << "\"";
        if (!node.text.empty()) {
            output << ", \"text\": \"" << node.text << "\"";
        }
        if (!node.resourceBindings.empty()) {
            output << ", \"resources\": [";
            for (std::size_t resourceIndex = 0; resourceIndex < node.resourceBindings.size();
                ++resourceIndex) {
                const auto& binding = node.resourceBindings[resourceIndex];
                output << "{ \"role\": \"" << binding.role << "\", \"resourceId\": \""
                       << binding.resourceId
                       << "\", \"required\": " << (binding.required ? "true" : "false") << " }";
                if (resourceIndex + 1u < node.resourceBindings.size()) {
                    output << ", ";
                }
            }
            output << "]";
        }
        output << " }";
        if (index + 1u < document.nodes().size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n";
    output << "}\n";
    return true;
}

inline const char* uiTextureAlphaModeName(arp::ui::UiTextureAlphaMode value)
{
    switch (value) {
    case arp::ui::UiTextureAlphaMode::Opaque:
        return "opaque";
    case arp::ui::UiTextureAlphaMode::Straight:
        return "straight";
    case arp::ui::UiTextureAlphaMode::Premultiplied:
        return "premultiplied";
    case arp::ui::UiTextureAlphaMode::Unknown:
        return "unknown";
    }
    return "unknown";
}

inline const char* uiTextureMipIntentName(arp::ui::UiTextureMipIntent value)
{
    switch (value) {
    case arp::ui::UiTextureMipIntent::None:
        return "none";
    case arp::ui::UiTextureMipIntent::Generate:
        return "generate";
    case arp::ui::UiTextureMipIntent::Unknown:
        return "unknown";
    }
    return "unknown";
}

inline const char* uiTextureCompressionIntentName(arp::ui::UiTextureCompressionIntent value)
{
    switch (value) {
    case arp::ui::UiTextureCompressionIntent::UiLossless:
        return "ui-lossless";
    case arp::ui::UiTextureCompressionIntent::PlatformDefault:
        return "platform-default";
    case arp::ui::UiTextureCompressionIntent::Unknown:
        return "unknown";
    }
    return "unknown";
}

inline const char* uiTextureSamplerIntentName(arp::ui::UiTextureSamplerIntent value)
{
    switch (value) {
    case arp::ui::UiTextureSamplerIntent::LinearClamp:
        return "linear-clamp";
    case arp::ui::UiTextureSamplerIntent::NearestClamp:
        return "nearest-clamp";
    case arp::ui::UiTextureSamplerIntent::Unknown:
        return "unknown";
    }
    return "unknown";
}

inline int runSelfTest(std::ostream& out, std::ostream& err)
{
    const auto material = assets::importSceneMaterialFromJson(R"json(
    {
      "type": "SceneStylizedPbr",
      "name": "SelfTestMaterial",
      "baseColorTexture": "generated/self_test_base.ktx2"
    }
    )json");
    if (!material.success) {
        err << material.diagnostics << '\n';
        return 1;
    }

    const auto mesh = assets::importStaticMeshFromGltfJson(R"json(
    {
      "asset": { "version": "2.0" },
      "meshes": [
        {
          "primitives": [
            {
              "attributes": {
                "POSITION": [0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0]
              },
              "indices": [0, 1, 2]
            }
          ]
        }
      ]
    }
    )json");
    if (!mesh.success) {
        err << mesh.diagnostics << '\n';
        return 1;
    }

    const auto layout = importUiLayoutFromJson(R"json(
    {
      "type": "GuiLayout",
      "name": "SelfTestLayout",
      "nodes": [
        { "id": 100, "type": "Panel", "text": "Tools", "width": 240, "height": 120 },
        { "id": 101, "parent": 100, "type": "Checkbox", "text": "Bloom", "width": 140, "height": 24, "checked": true }
      ]
    }
    )json");
    if (!layout.success) {
        err << layout.diagnostics << '\n';
        return 1;
    }

    out << "self-test ok: material=" << material.material.name
        << " vertices=" << mesh.mesh.vertices.size() << " uiNodes=" << layout.cache.nodeCount
        << '\n';
    return 0;
}

inline int runImporter(int argc, const char* const* argv, std::ostream& out, std::ostream& err)
{
    const auto options = parseOptions(argc, argv);
    if (options.selfTest) {
        return runSelfTest(out, err);
    }

    if (!options.unityScriptPath.empty() || !options.scriptAotPackagePath.empty()) {
        if (options.unityScriptInputPath.empty()) {
            err << "Unity script execution requires --script-input <path>\n";
            return 2;
        }
        if (options.scriptAotPackagePath.empty() != options.scriptAotModule.empty()) {
            err << "Dynamic AOT execution requires both --script-aot-package"
                << " and --script-aot-module\n";
            return 2;
        }
        const auto scriptPath = std::filesystem::path(options.unityScriptPath.c_str());
        const auto inputPath = std::filesystem::path(options.unityScriptInputPath.c_str());
        const auto projectRoot = std::filesystem::path(ARP_SOURCE_DIR);
        auto arguments = core::pmr::Vector<scriptvm::Value>{&core::defaultCpuMemoryResource()};
        arguments.emplace_back(scriptvm::makeString(inputPath.generic_string()));
        arguments.emplace_back(scriptvm::makeString(projectRoot.generic_string()));
        const bool hasScriptOutput = !options.unityScriptOutputPath.empty();
        if (hasScriptOutput) {
            arguments.emplace_back(scriptvm::makeString(
                std::filesystem::path(options.unityScriptOutputPath.c_str()).generic_string()));
        }
        ScriptToolRunner runner(projectRoot,
            projectRoot / "Libs",
            std::filesystem::temp_directory_path() / "arp_import_script_plugins",
            scriptToolHostLibrary(
                argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path(), projectRoot));
        const auto functionName = hasScriptOutput ? "export" : "main";
        const auto result = options.scriptAotPackagePath.empty()
            ? runner.runFileFunction(scriptPath, functionName, arguments)
            : runner.runPackageFunction(std::filesystem::path(options.scriptAotPackagePath.c_str()),
                  options.scriptAotModule.c_str(),
                  functionName,
                  arguments);
        if (!result.success) {
            err << result.diagnostics << '\n';
            return 1;
        }
        int scriptStatus = -1;
        if (!scriptvm::tryGetInt(result.value, scriptStatus)) {
            err << "Unity import script must return an int status\n";
            return 1;
        }
        if (scriptStatus != 0) {
            err << "Unity import script failed with status " << scriptStatus << '\n';
            return 1;
        }
        out << "Unity import script ok: "
            << (options.scriptAotPackagePath.empty()
                       ? scriptPath.generic_string()
                       : std::filesystem::path(options.scriptAotPackagePath.c_str())
                             .generic_string())
            << '\n';
        return 0;
    }

    if (!options.unityParticlePrefabPath.empty()) {
        if (options.outputPackagePath.empty()) {
            err << "Unity particle conversion requires --output-package <dir>\n";
            return 2;
        }
        const auto projectRoot = std::filesystem::path(ARP_SOURCE_DIR);
        const auto executablePath =
            argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path();
        ScriptToolRunner runner(projectRoot,
            projectRoot / "Libs",
            std::filesystem::temp_directory_path() / "arp_import_script_plugins",
            scriptToolHostLibrary(executablePath, projectRoot));
        core::pmr::Vector<scriptvm::Value> arguments{&core::defaultCpuMemoryResource()};
        arguments.emplace_back(scriptvm::makeString(
            std::filesystem::path(options.unityParticlePrefabPath.c_str()).generic_string()));
        arguments.emplace_back(scriptvm::makeString(
            std::filesystem::path(options.outputPackagePath.c_str()).generic_string()));
        arguments.emplace_back(scriptvm::makeString(std::string_view("UnityParticleEffect")));
        arguments.emplace_back(scriptvm::makeString(std::string_view("particle/unity-effect")));
        arguments.emplace_back(scriptvm::makeString(std::string_view("Prototype Complete")));
        arguments.emplace_back(scriptvm::makeString(projectRoot.generic_string()));
        const auto result =
            runner.runFileFunction(projectRoot / "tools" / "unity_import" / "unity_import.arp",
                "import_particle_package",
                arguments);
        if (!result.success) {
            err << result.diagnostics << '\n';
            return 1;
        }
        int scriptStatus = -1;
        if (!scriptvm::tryGetInt(result.value, scriptStatus) || scriptStatus != 0) {
            err << "Unity particle package script failed with status " << scriptStatus << '\n';
            return 1;
        }
        out << "Unity particle package ok: UnityParticleEffect"
            << " entries=1"
            << " output=" << options.outputPackagePath << '\n';
        return 0;
    }

    if (!options.unityPrefabFolder.empty()) {
        if (options.outputPackagePath.empty()) {
            err << "Unity prefab conversion requires --output-package <dir>\n";
            return 2;
        }
        const std::string_view alias = options.prefabAlias.empty()
            ? std::string_view("prefab/male000")
            : std::string_view(options.prefabAlias.data(), options.prefabAlias.size());
        const std::string_view displayName = options.prefabDisplayName.empty()
            ? std::string_view("male000")
            : std::string_view(options.prefabDisplayName.data(), options.prefabDisplayName.size());
        const std::string_view basicShader = options.basicShaderSlangPath.empty()
            ? std::string_view("shaders/samples/material_preview_pbr.slang")
            : std::string_view(
                  options.basicShaderSlangPath.data(), options.basicShaderSlangPath.size());
        const std::string_view stylizedShader = options.stylizedShaderSlangPath.empty()
            ? std::string_view("shaders/samples/stylized_scene.slang")
            : std::string_view(
                  options.stylizedShaderSlangPath.data(), options.stylizedShaderSlangPath.size());
        const std::string_view guiShader = options.guiShaderSlangPath.empty()
            ? std::string_view("shaders/gui/gui_overlay.slang")
            : std::string_view(
                  options.guiShaderSlangPath.data(), options.guiShaderSlangPath.size());
        const std::string_view startupShader = options.startupSplashShaderSlangPath.empty()
            ? std::string_view("shaders/samples/startup_splash.slang")
            : std::string_view(options.startupSplashShaderSlangPath.data(),
                  options.startupSplashShaderSlangPath.size());
        const auto projectRoot = std::filesystem::path(ARP_SOURCE_DIR);
        const auto executablePath =
            argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path();
        ScriptToolRunner runner(projectRoot,
            projectRoot / "Libs",
            std::filesystem::temp_directory_path() / "arp_import_script_plugins",
            scriptToolHostLibrary(executablePath, projectRoot));
        core::pmr::Vector<scriptvm::Value> arguments{&core::defaultCpuMemoryResource()};
        arguments.emplace_back(scriptvm::makeString(
            std::filesystem::path(options.unityPrefabFolder.c_str()).generic_string()));
        arguments.emplace_back(scriptvm::makeString(
            std::filesystem::path(options.outputPackagePath.c_str()).generic_string()));
        arguments.emplace_back(scriptvm::makeString(alias));
        arguments.emplace_back(scriptvm::makeString(displayName));
        arguments.emplace_back(scriptvm::makeString(projectRoot.generic_string()));
        arguments.emplace_back(scriptvm::makeString(basicShader));
        arguments.emplace_back(scriptvm::makeString(stylizedShader));
        arguments.emplace_back(scriptvm::makeString(guiShader));
        arguments.emplace_back(scriptvm::makeString(startupShader));
        const auto result =
            runner.runFileFunction(projectRoot / "tools" / "unity_import" / "unity_import.arp",
                "import_character_package",
                arguments);
        if (!result.success) {
            err << result.diagnostics << '\n';
            return 1;
        }
        int scriptStatus = -1;
        if (!scriptvm::tryGetInt(result.value, scriptStatus) || scriptStatus != 0) {
            err << "External prefab package script failed with status " << scriptStatus << '\n';
            return 1;
        }
        out << "External prefab package ok: " << displayName
            << " output=" << options.outputPackagePath << '\n';
        return 0;
    }

    if (!options.uiPsdPath.empty() || !options.uiManifestPath.empty()
        || !options.outputUiPath.empty()) {
        if (options.uiPsdPath.empty() || options.uiManifestPath.empty()
            || options.outputUiPath.empty()) {
            err << "UI PSD manifest import requires --ui-psd <file> --ui-manifest <file> --output-ui <file.arpui>\n";
            return 2;
        }

        std::pmr::string psdJson;
        std::pmr::string manifestJson;
        if (!readTextFile(options.uiPsdPath, psdJson)) {
            err << "failed to read UI PSD fixture: " << options.uiPsdPath << '\n';
            return 1;
        }
        if (!readTextFile(options.uiManifestPath, manifestJson)) {
            err << "failed to read UI manifest: " << options.uiManifestPath << '\n';
            return 1;
        }

        const auto imported = importUiFromPsdManifest(psdJson, manifestJson);
        if (!imported.success) {
            err << imported.diagnostics << '\n';
            return 1;
        }

        std::pmr::string diagnostics;
        if (!writeArpUiDocument(imported.document, options.outputUiPath, diagnostics)) {
            err << diagnostics << '\n';
            return 1;
        }
        out << "arpui ok: nodes=" << imported.document.nodes().size()
            << " stableHash=" << imported.document.stableHash()
            << " output=" << options.outputUiPath << '\n';
        return 0;
    }

    if (!options.materialJsonPath.empty()) {
        std::pmr::string json;
        if (!readTextFile(options.materialJsonPath, json)) {
            err << "failed to read material JSON: " << options.materialJsonPath << '\n';
            return 1;
        }
        const auto imported = assets::importSceneMaterialFromJson(json);
        if (!imported.success) {
            err << imported.diagnostics << '\n';
            return 1;
        }
        out << "material ok: " << imported.material.name << '\n';
        return 0;
    }

    if (!options.gltfJsonPath.empty()) {
        std::pmr::string json;
        if (!readTextFile(options.gltfJsonPath, json)) {
            err << "failed to read glTF JSON: " << options.gltfJsonPath << '\n';
            return 1;
        }
        const auto imported = assets::importStaticMeshFromGltfJson(json);
        if (!imported.success) {
            err << imported.diagnostics << '\n';
            return 1;
        }
        out << "mesh ok: vertices=" << imported.mesh.vertices.size()
            << " indices=" << imported.mesh.indices.size() << '\n';
        return 0;
    }

    if (!options.uiLayoutJsonPath.empty()) {
        std::pmr::string json;
        if (!readTextFile(options.uiLayoutJsonPath, json)) {
            err << "failed to read UI layout JSON: " << options.uiLayoutJsonPath << '\n';
            return 1;
        }
        const auto imported = importUiLayoutFromJson(json);
        if (!imported.success) {
            err << imported.diagnostics << '\n';
            return 1;
        }
        out << "ui layout ok: " << imported.name << " nodes=" << imported.cache.nodeCount
            << " textRuns=" << imported.cache.textRunCount << '\n';
        return 0;
    }

    err << "usage: arp_asset_importer --self-test | --unity-script <file.arp>"
        << " --script-input <path> [--script-output <file>]"
        << " [--script-aot-package <manifest.yaml> --script-aot-module <id>]"
        << " | --material-json <file> | --gltf-json <file>"
        << " | --ui-layout-json <file> | --ui-psd <file>"
        << " --ui-manifest <file> --output-ui <file.arpui>"
        << " | --unity-particle-prefab <file> --output-package <dir>"
        << " | --unity-prefab-folder <dir> --output-package <dir>\n";
    return 2;
}

} // namespace arp::tools::asset_importer
