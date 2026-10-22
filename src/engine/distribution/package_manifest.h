#pragma once

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <string>
#include <vector>

namespace arp::core {

struct ShaderPackageManifest {
    bool includesReflection = false;
    bool includesPipelineCache = false;
};

struct PackageManifest {
    pmr::String name;
    pmr::String version;
    pmr::Vector<pmr::String> publicHeaders;
    pmr::Vector<pmr::String> internalHeaders;
    pmr::Vector<pmr::String> runtimeAssetDirectories;
    ShaderPackageManifest shaderPackage;
    bool backendNeutralPublicApi = false;
};

struct PackageValidationResult {
    bool success = false;
    pmr::String diagnostics;
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

inline bool containsPublicHeader(const PackageManifest& manifest, const pmr::String& header)
{
    return std::find(manifest.publicHeaders.begin(), manifest.publicHeaders.end(), header)
        != manifest.publicHeaders.end();
}

inline bool isBackendNativeHeader(const pmr::String& header)
{
    return header.find("vulkan") != pmr::String::npos || header.find("metal/") != pmr::String::npos
        || header.find("_mac.mm") != pmr::String::npos;
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
