#pragma once

#include "tools/cluster_previewer/cluster_preview_scene.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::tools::cluster_previewer {

enum class PreviewButtonRole {
    PreviewMode,
    BoundsVisibility,
    ColorMode,
    BuildSettingDecrease,
    BuildSettingIncrease,
    CutErrorDecrease,
    CutErrorIncrease,
    Rebuild
};

enum class ClusterPreviewBuildSetting { LeafTriangles, LeafVertices, GroupClusters };

struct ClusterPreviewBuildSettings {
    uint32_t maxTrianglesPerLeafCluster = 128;
    uint32_t maxVerticesPerLeafCluster = 256;
    uint32_t maxClustersPerGroup = 8;
};

struct PreviewButton {
    PreviewButtonRole role = PreviewButtonRole::PreviewMode;
    ClusterPreviewMode mode = ClusterPreviewMode::RawMesh;
    ClusterPreviewBuildSetting buildSetting = ClusterPreviewBuildSetting::LeafTriangles;
    std::pmr::string label;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t triangleCount = 0;
    uint32_t buildSettingValue = 0;
    uint32_t cutErrorScale = 0;
    bool active = false;
    bool hasTriangleCount = false;
};

class ClusterPreviewUiState {
public:
    ClusterPreviewUiState();

    ClusterPreviewMode mode() const;
    ClusterPreviewColorMode colorMode() const;
    ClusterPreviewBuildSettings buildSettings() const;
    uint32_t cutErrorScale() const;
    bool boundsVisible() const;
    bool buildSettingsDirty() const;
    void setMode(ClusterPreviewMode mode);
    void setColorMode(ClusterPreviewColorMode colorMode);
    void setBoundsVisible(bool visible);
    void setTriangleCount(ClusterPreviewMode mode, uint32_t triangleCount);
    bool consumeRebuildRequested();
    bool consumeCutSettingsChanged();
    const std::pmr::vector<PreviewButton>& layout(int width, int height);
    bool click(float x, float y, int width, int height);
    bool key(char key);

private:
    void rebuildLayout(int width, int height);
    void adjustBuildSetting(ClusterPreviewBuildSetting setting, int32_t delta);
    void adjustCutErrorScale(int32_t delta);
    void refreshBuildSettingButtons();
    void refreshCutErrorButtons();
    void requestRebuild();

    ClusterPreviewMode mode_ = ClusterPreviewMode::LeafClusters;
    ClusterPreviewColorMode colorMode_ = ClusterPreviewColorMode::ClusterColor;
    ClusterPreviewBuildSettings buildSettings_;
    uint32_t triangleCounts_[5]{};
    uint32_t cutErrorScale_ = 3;
    bool boundsVisible_ = true;
    bool buildSettingsDirty_ = false;
    bool cutSettingsChanged_ = false;
    bool rebuildRequested_ = false;
    int layoutWidth_ = 0;
    int layoutHeight_ = 0;
    std::pmr::vector<PreviewButton> buttons_;
};

const char* clusterPreviewModeLabel(ClusterPreviewMode mode);

} // namespace arp::tools::cluster_previewer
