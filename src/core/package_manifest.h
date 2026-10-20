#pragma once

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"

#include <algorithm>
#include <string>
#include <vector>

namespace arp::core {

struct ShaderPackageManifest {
    bool includesReflection = false;
    bool includesPipelineCache = false;
};

struct PackageManifest {
    std::pmr::string name;
    std::pmr::string version;
    std::pmr::vector<std::pmr::string> publicHeaders;
    std::pmr::vector<std::pmr::string> internalHeaders;
    std::pmr::vector<std::pmr::string> runtimeAssetDirectories;
    ShaderPackageManifest shaderPackage;
    bool backendNeutralPublicApi = false;
};

struct PackageValidationResult {
    bool success = false;
    std::pmr::string diagnostics;
};

inline PackageManifest buildDefaultPackageManifest()
{
    PackageManifest manifest;
    manifest.name = "AnimeRenderPipeline";
    manifest.version = "0.1.0-roadmap";
    manifest.publicHeaders = {
        "rhi/gpu_device.h",
        "render_graph/render_graph.h",
        "gui/gui.h",
        "platform/render_system.h",
        "renderer/runtime_scene.h",
    };
    manifest.internalHeaders = {
        "rhi/backends/vulkan/vulkan_device.h",
        "rhi/backends/metal/metal_device.h",
    };
    manifest.runtimeAssetDirectories = {
        "assets",
        "shaders",
    };
    manifest.shaderPackage.includesReflection = true;
    manifest.shaderPackage.includesPipelineCache = true;
    manifest.backendNeutralPublicApi = true;
    return manifest;
}

inline bool containsPublicHeader(const PackageManifest& manifest, const std::pmr::string& header)
{
    return std::find(manifest.publicHeaders.begin(), manifest.publicHeaders.end(), header)
        != manifest.publicHeaders.end();
}

inline bool isBackendNativeHeader(const std::pmr::string& header)
{
    return header.find("vulkan") != std::pmr::string::npos
        || header.find("metal/") != std::pmr::string::npos
        || header.find("_mac.mm") != std::pmr::string::npos;
}

inline PackageValidationResult validatePackageManifest(const PackageManifest& manifest)
{
    PackageValidationResult result;
    result.success = !manifest.name.empty() && !manifest.version.empty()
        && !manifest.publicHeaders.empty() && manifest.shaderPackage.includesReflection
        && manifest.shaderPackage.includesPipelineCache && manifest.backendNeutralPublicApi;

    for (const auto& header : manifest.publicHeaders) {
        if (isBackendNativeHeader(header)) {
            result.success = false;
            arp::core::appendDiagnostic(
                result.diagnostics, "public package contains backend-native header: " + header);
        }
    }

    if (!result.success && result.diagnostics.empty()) {
        result.diagnostics = "package manifest is incomplete";
    }
    return result;
}

} // namespace arp::core
