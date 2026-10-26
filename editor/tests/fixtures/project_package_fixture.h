#pragma once

#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"

#include <cassert>

namespace arp::editor::tests {

// Real package writer/reader fixture with explicit IDs and synthetic provenance.
class ProjectPackageFixture final {
public:
    static constexpr auto prefabId = "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1";
    static constexpr auto shaderId = "f0817aa6-f639-46c9-b9de-0ea317a2b94d";

    static resource::PrefabAssetGraphBuildDesc descriptor()
    {
        resource::PrefabAssetGraphBuildDesc desc;
        desc.ids.rootId = prefabId;
        desc.ids.prefabId = desc.ids.rootId;
        desc.ids.basicShaderId = shaderId;
        desc.alias = "prefab/editor-test";
        desc.displayName = "editor-test";
        desc.sourcePath = "synthetic/editor-test";
        desc.sourceKind = "SyntheticPrefabFolder";
        desc.shaderPaths.basicMaterialPreview = "shaders/basic.slang";
        desc.shaderPaths.stylizedScene = "shaders/stylized.slang";
        desc.shaderPaths.guiOverlay = "shaders/gui.slang";
        desc.startupSplashShaderPath = "shaders/startup.slang";
        return desc;
    }

    static resource::InternalAssetPackageDesc write(const std::filesystem::path& path)
    {
        const auto result = resource::PrefabAssetGraphBuilder().buildPackage(descriptor());
        assert(result.success);
        assert(resource::InternalAssetPackageWriter().write(result.package, path).success);
        return result.package;
    }
};
} // namespace arp::editor::tests
