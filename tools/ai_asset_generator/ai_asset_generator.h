#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace arp::tools::ai_asset_generator {

struct Environment {
    std::pmr::string meshyApiKey;

    static Environment fromProcess()
    {
        Environment environment;
        if (const auto* key = std::getenv("MESHY_API_KEY")) {
            environment.meshyApiKey = key;
        }
        return environment;
    }
};

struct Options {
    bool selfTest = false;
    std::pmr::string outputDirectory = "artifacts/ai-assets/production-demo";

    static Options productionDemoDefaults() { return Options{}; }
};

struct PromptAsset {
    std::pmr::string name;
    std::pmr::string prompt;
    std::pmr::string outputFile;
};

struct PromptManifest {
    std::pmr::string provider = "Meshy";
    std::pmr::string outputDirectory = "artifacts/ai-assets/production-demo";
    std::pmr::vector<PromptAsset> assets;
};

struct Result {
    bool success = false;
    std::pmr::string diagnostics;
};

inline PromptManifest buildProductionDemoPromptManifest()
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

inline std::pmr::string manifestJson(const PromptManifest& manifest)
{
    std::ostringstream json;
    json << "{\n";
    json << "  \"provider\": \"" << manifest.provider << "\",\n";
    json << "  \"outputDirectory\": \"" << manifest.outputDirectory << "\",\n";
    json << "  \"assets\": [\n";
    for (size_t index = 0; index < manifest.assets.size(); ++index) {
        const auto& asset = manifest.assets[index];
        json << "    { \"name\": \"" << asset.name << "\", \"prompt\": \"" << asset.prompt
             << "\", \"file\": \"" << asset.outputFile << "\" }";
        json << (index + 1 < manifest.assets.size() ? "," : "") << "\n";
    }
    json << "  ]\n";
    json << "}\n";
    return arp::core::toPmrString(json.str());
}

inline Result runGenerator(const Options& options, const Environment& environment)
{
    if (options.selfTest) {
        const auto manifest = buildProductionDemoPromptManifest();
        return Result{manifest.assets.size() == 6,
            manifest.assets.size() == 6 ? "" : "prompt manifest is incomplete"};
    }

    if (environment.meshyApiKey.empty()) {
        return Result{false, "MESHY_API_KEY is required to generate Meshy Text-to-3D assets"};
    }

    auto manifest = buildProductionDemoPromptManifest();
    manifest.outputDirectory = options.outputDirectory;
    std::error_code createError;
    std::filesystem::create_directories(options.outputDirectory, createError);
    if (createError) {
        return Result{false, "failed to create Meshy output directory " + options.outputDirectory};
    }
    const auto manifestPath = options.outputDirectory + "/asset_manifest.json";
    std::ofstream file(manifestPath.c_str(), std::ios::binary);
    if (!file) {
        return Result{false, "failed to write Meshy asset manifest at " + manifestPath};
    }

    file << manifestJson(manifest);
    return Result{
        true,
        "wrote Meshy prompt manifest; submit preview/refine Text-to-3D requests and download GLB files into the same directory",
    };
}

inline Options parseOptions(int argc, const char* const* argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::pmr::string arg = argv[index];
        if (arg == "--self-test") {
            options.selfTest = true;
        } else if (arg == "--out" && index + 1 < argc) {
            options.outputDirectory = argv[++index];
        }
    }
    return options;
}

} // namespace arp::tools::ai_asset_generator
