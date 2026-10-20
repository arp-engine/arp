#include "core/cpu_allocator.h"
#include "core/package_manifest.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::core;

    const auto manifest = buildDefaultPackageManifest();
    assert(manifest.name == std::pmr::string("AnimeRenderPipeline"));
    assert(!manifest.version.empty());
    assert(manifest.publicHeaders.size() >= 4);
    assert(containsPublicHeader(manifest, "rhi/gpu_device.h"));
    assert(containsPublicHeader(manifest, "render_graph/render_graph.h"));
    assert(!containsPublicHeader(manifest,
        "src/"
        "rhi/rhi.h"));
    assert(!containsPublicHeader(manifest, "rhi/backends/vulkan/vulkan_device.h"));
    assert(manifest.runtimeAssetDirectories.size() >= 1);
    assert(manifest.shaderPackage.includesReflection);
    assert(manifest.shaderPackage.includesPipelineCache);
    assert(manifest.backendNeutralPublicApi);

    const auto validation = validatePackageManifest(manifest);
    assert(validation.success);
    assert(validation.diagnostics.empty());

    auto invalid = manifest;
    invalid.publicHeaders.push_back("rhi/backends/vulkan/vulkan_device.h");
    const auto invalidResult = validatePackageManifest(invalid);
    assert(!invalidResult.success);
    assert(invalidResult.diagnostics.find("backend-native") != std::pmr::string::npos);

    return 0;
}
