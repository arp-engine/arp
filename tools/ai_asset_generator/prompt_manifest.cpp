#include "core/encoding/yaml_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/utf8.h"
#include "tools/ai_asset_generator/ai_asset_generator.h"

#include <algorithm>

namespace arp::tools::ai_asset_generator {
namespace {
using Yaml = core::YamlContract;
using Node = Yaml::Node;

PromptManifestResult failure(core::StringView message)
{
    PromptManifestResult result;
    result.diagnostics = message;
    return result;
}

bool textValid(core::StringView value)
{
    return !value.empty() && value.size() <= PromptManifestLimits::maxFieldBytes
        && value.find('\0') == core::StringView::npos && !core::Utf8Decoder::validate(value);
}

const char* validate(const PromptManifest& manifest)
{
    if (manifest.provider != "Meshy" || !textValid(manifest.outputDirectory)
        || manifest.outputDirectory.size() > 512)
        return "AI-MANIFEST-METADATA";
    if (manifest.assets.empty() || manifest.assets.size() > PromptManifestLimits::maxAssets)
        return "AI-MANIFEST-ASSET-COUNT";
    for (auto it = manifest.assets.begin(); it != manifest.assets.end(); ++it) {
        if (!textValid(it->name) || !textValid(it->prompt) || !textValid(it->file))
            return "AI-MANIFEST-FIELD";
        if (!core::FilePath::isSafeRelativePath(it->file, 512) || it->file.size() < 5
            || it->file.compare(it->file.size() - 4, 4, ".glb") != 0)
            return "AI-MANIFEST-GLB-LOCATION";
        if (std::any_of(manifest.assets.begin(), it, [&](const auto& previous) {
                return previous.name == it->name || previous.file == it->file;
            }))
            return "AI-MANIFEST-DUPLICATE-ASSET";
    }
    return nullptr;
}

bool finiteBudget(std::uint64_t bytes)
{
    return bytes && bytes < SIZE_MAX;
}
} // namespace

PromptAsset::PromptAsset(
    core::StringView assetName, core::StringView description, core::StringView location)
    : name(assetName, &core::defaultCpuMemoryResource()),
      prompt(description, &core::defaultCpuMemoryResource()),
      file(location, &core::defaultCpuMemoryResource())
{}

bool PromptAsset::operator==(const PromptAsset& other) const
{
    return name == other.name && prompt == other.prompt && file == other.file;
}

bool PromptManifest::operator==(const PromptManifest& other) const
{
    return provider == other.provider && outputDirectory == other.outputDirectory
        && assets == other.assets;
}

PromptManifest buildProductionDemoPromptManifest()
{
    PromptManifest manifest;
    manifest.assets = {
        {"cliff_rock_cluster",
            "stylized anime open world cliff rock cluster, clean topology, game ready, no text",
            "cliff_rock_cluster.glb"},
        {"anime_pine_tree",
            "anime pine tree low poly, stylized foliage, game ready, no text",
            "anime_pine_tree.glb"},
        {"grass_tuft",
            "fantasy grass tuft for anime open world terrain, game ready, no text",
            "grass_tuft.glb"},
        {"shrine_gate",
            "small red shrine gate, anime fantasy open world prop, game ready, no text",
            "shrine_gate.glb"},
        {"stone_lantern",
            "weathered stone lantern, stylized anime prop, game ready, no text",
            "stone_lantern.glb"},
        {"hero_proxy",
            "anime adventurer hero proxy character, simple game ready 3D model, no text",
            "hero_proxy.glb"},
    };
    return manifest;
}

PromptManifestResult PromptManifestCodec::decode(core::StringView source, std::uint64_t budget)
{
    if (!finiteBudget(budget) || source.size() > budget)
        return failure("AI-MANIFEST-DOCUMENT-BUDGET");
    if (source.find('\0') != core::StringView::npos || core::Utf8Decoder::validate(source))
        return failure("AI-MANIFEST-TEXT");
    try {
        const auto root = Yaml::parseSingleDocument(source);
        Yaml::onlyKeys(root, {"type", "contractVersion", "provider", "outputDirectory", "assets"});
        if (Yaml::text(root, "type") != "AiAssetPromptManifest"
            || Yaml::uint32(root, "contractVersion") != 1)
            return failure("AI-MANIFEST-CONTRACT");
        PromptManifestResult result;
        result.manifest.provider = Yaml::text(root, "provider");
        result.manifest.outputDirectory = Yaml::text(root, "outputDirectory");
        const auto& assets = Yaml::sequence(root, "assets");
        if (assets.empty() || assets.size() > PromptManifestLimits::maxAssets)
            return failure("AI-MANIFEST-ASSET-COUNT");
        result.manifest.assets.reserve(assets.size());
        for (const auto& asset : assets) {
            Yaml::onlyKeys(asset, {"name", "prompt", "file", "format"});
            if (Yaml::text(asset, "format") != "glb")
                return failure("AI-MANIFEST-FORMAT");
            result.manifest.assets.emplace_back(
                Yaml::text(asset, "name"), Yaml::text(asset, "prompt"), Yaml::text(asset, "file"));
        }
        if (const auto* diagnostic = validate(result.manifest))
            return failure(diagnostic);
        result.success = true;
        return result;
    } catch (const std::bad_alloc&) {
        return failure("AI-MANIFEST-ALLOCATION");
    } catch (...) {
        return failure("AI-MANIFEST-SCHEMA");
    }
}

PromptManifestResult PromptManifestCodec::encode(
    const PromptManifest& manifest, std::uint64_t budget)
{
    if (!finiteBudget(budget))
        return failure("AI-MANIFEST-DOCUMENT-BUDGET");
    if (const auto* diagnostic = validate(manifest))
        return failure(diagnostic);
    try {
        Node root = Node::mapping();
        root["type"] = "AiAssetPromptManifest";
        root["contractVersion"] = 1;
        root["provider"] = manifest.provider.c_str();
        root["outputDirectory"] = manifest.outputDirectory.c_str();
        Node assets = Node::sequence();
        for (const auto& asset : manifest.assets) {
            Node item = Node::mapping();
            item["name"] = asset.name.c_str();
            item["prompt"] = asset.prompt.c_str();
            item["file"] = asset.file.c_str();
            item["format"] = "glb";
            assets.get_value_ref<Node::sequence_type&>().push_back(std::move(item));
        }
        root["assets"] = std::move(assets);
        PromptManifestResult result;
        result.yaml = Yaml::serialize(root);
        if (result.yaml.size() > budget)
            return failure("AI-MANIFEST-DOCUMENT-BUDGET");
        const auto loaded = decode(result.yaml, budget);
        if (!loaded.success || !(loaded.manifest == manifest))
            return failure("AI-MANIFEST-ROUND-TRIP");
        result.success = true;
        return result;
    } catch (const std::bad_alloc&) {
        return failure("AI-MANIFEST-ALLOCATION");
    } catch (...) {
        return failure("AI-MANIFEST-SCHEMA");
    }
}
} // namespace arp::tools::ai_asset_generator
