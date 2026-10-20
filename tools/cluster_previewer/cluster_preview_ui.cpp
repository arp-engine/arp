#include "tools/cluster_previewer/cluster_preview_ui.h"

#include <algorithm>

namespace arp::tools::cluster_previewer {

namespace {

constexpr float kButtonX = 14.0f;
constexpr float kButtonY = 14.0f;
constexpr float kButtonWidth = 112.0f;
constexpr float kBuildButtonWidth = 88.0f;
constexpr float kBuildStepButtonWidth = 34.0f;
constexpr float kButtonHeight = 30.0f;
constexpr float kButtonGap = 8.0f;
constexpr uint32_t kMinCutErrorScale = 1;
constexpr uint32_t kMaxCutErrorScale = 8;

bool inside(const PreviewButton& button, float x, float y)
{
    return x >= button.x && x <= button.x + button.width && y >= button.y
        && y <= button.y + button.height;
}

uint32_t modeIndex(ClusterPreviewMode mode)
{
    switch (mode) {
    case ClusterPreviewMode::RawMesh:
        return 0;
    case ClusterPreviewMode::LeafClusters:
        return 1;
    case ClusterPreviewMode::ParentClusters:
        return 2;
    case ClusterPreviewMode::RootClusters:
        return 3;
    case ClusterPreviewMode::ErrorCut:
        return 4;
    }
    return 0;
}

uint32_t buildSettingValue(
    const ClusterPreviewBuildSettings& settings, ClusterPreviewBuildSetting setting)
{
    switch (setting) {
    case ClusterPreviewBuildSetting::LeafTriangles:
        return settings.maxTrianglesPerLeafCluster;
    case ClusterPreviewBuildSetting::LeafVertices:
        return settings.maxVerticesPerLeafCluster;
    case ClusterPreviewBuildSetting::GroupClusters:
        return settings.maxClustersPerGroup;
    }
    return 0;
}

ClusterPreviewColorMode nextColorMode(ClusterPreviewColorMode colorMode)
{
    switch (colorMode) {
    case ClusterPreviewColorMode::ClusterColor:
        return ClusterPreviewColorMode::Shaded;
    case ClusterPreviewColorMode::Shaded:
        return ClusterPreviewColorMode::Textured;
    case ClusterPreviewColorMode::Textured:
        return ClusterPreviewColorMode::ClusterColor;
    }
    return ClusterPreviewColorMode::ClusterColor;
}

} // namespace

ClusterPreviewUiState::ClusterPreviewUiState()
{
    rebuildLayout(960, 640);
}

ClusterPreviewMode ClusterPreviewUiState::mode() const
{
    return mode_;
}

bool ClusterPreviewUiState::boundsVisible() const
{
    return boundsVisible_;
}

ClusterPreviewColorMode ClusterPreviewUiState::colorMode() const
{
    return colorMode_;
}

ClusterPreviewBuildSettings ClusterPreviewUiState::buildSettings() const
{
    return buildSettings_;
}

uint32_t ClusterPreviewUiState::cutErrorScale() const
{
    return cutErrorScale_;
}

bool ClusterPreviewUiState::buildSettingsDirty() const
{
    return buildSettingsDirty_;
}

void ClusterPreviewUiState::setMode(ClusterPreviewMode mode)
{
    mode_ = mode;
    for (auto& button : buttons_) {
        if (button.role == PreviewButtonRole::PreviewMode) {
            button.active = button.mode == mode_;
        }
    }
}

void ClusterPreviewUiState::setColorMode(ClusterPreviewColorMode colorMode)
{
    colorMode_ = colorMode;
    for (auto& button : buttons_) {
        if (button.role == PreviewButtonRole::ColorMode) {
            button.active = colorMode_ != ClusterPreviewColorMode::Shaded;
        }
    }
}

void ClusterPreviewUiState::setBoundsVisible(bool visible)
{
    boundsVisible_ = visible;
    for (auto& button : buttons_) {
        if (button.role == PreviewButtonRole::BoundsVisibility) {
            button.active = boundsVisible_;
        }
    }
}

void ClusterPreviewUiState::setTriangleCount(ClusterPreviewMode mode, uint32_t triangleCount)
{
    triangleCounts_[modeIndex(mode)] = triangleCount;
    for (auto& button : buttons_) {
        if (button.role == PreviewButtonRole::PreviewMode && button.mode == mode) {
            button.triangleCount = triangleCount;
            button.hasTriangleCount = true;
        }
    }
}

bool ClusterPreviewUiState::consumeRebuildRequested()
{
    const auto requested = rebuildRequested_;
    rebuildRequested_ = false;
    return requested;
}

bool ClusterPreviewUiState::consumeCutSettingsChanged()
{
    const auto changed = cutSettingsChanged_;
    cutSettingsChanged_ = false;
    return changed;
}

const std::pmr::vector<PreviewButton>& ClusterPreviewUiState::layout(int width, int height)
{
    if (width != layoutWidth_ || height != layoutHeight_ || buttons_.empty()) {
        rebuildLayout(width, height);
    }
    return buttons_;
}

bool ClusterPreviewUiState::click(float x, float y, int width, int height)
{
    const auto& currentButtons = layout(width, height);
    for (const auto& button : currentButtons) {
        if (inside(button, x, y)) {
            if (button.role == PreviewButtonRole::BoundsVisibility) {
                setBoundsVisible(!boundsVisible_);
            } else if (button.role == PreviewButtonRole::ColorMode) {
                setColorMode(nextColorMode(colorMode_));
            } else if (button.role == PreviewButtonRole::BuildSettingDecrease) {
                adjustBuildSetting(button.buildSetting, -1);
            } else if (button.role == PreviewButtonRole::BuildSettingIncrease) {
                adjustBuildSetting(button.buildSetting, 1);
            } else if (button.role == PreviewButtonRole::CutErrorDecrease) {
                adjustCutErrorScale(-1);
            } else if (button.role == PreviewButtonRole::CutErrorIncrease) {
                adjustCutErrorScale(1);
            } else if (button.role == PreviewButtonRole::Rebuild) {
                requestRebuild();
            } else {
                setMode(button.mode);
            }
            return true;
        }
    }
    return false;
}

bool ClusterPreviewUiState::key(char key)
{
    switch (key) {
    case '1':
        setMode(ClusterPreviewMode::RawMesh);
        return true;
    case '2':
        setMode(ClusterPreviewMode::LeafClusters);
        return true;
    case '3':
        setMode(ClusterPreviewMode::ParentClusters);
        return true;
    case '4':
        setMode(ClusterPreviewMode::RootClusters);
        return true;
    case '5':
        setMode(ClusterPreviewMode::ErrorCut);
        return true;
    case 'B':
    case 'b':
        setBoundsVisible(!boundsVisible_);
        return true;
    case 'C':
    case 'c':
        setColorMode(nextColorMode(colorMode_));
        return true;
    case 'R':
    case 'r':
        requestRebuild();
        return true;
    default:
        return false;
    }
}

void ClusterPreviewUiState::adjustBuildSetting(ClusterPreviewBuildSetting setting, int32_t delta)
{
    auto adjust = [&](uint32_t& value, uint32_t minValue, uint32_t maxValue, uint32_t step) {
        const auto signedStep = static_cast<int32_t>(step) * delta;
        const auto next = static_cast<int32_t>(value) + signedStep;
        value = static_cast<uint32_t>(std::max<int32_t>(static_cast<int32_t>(minValue),
            std::min<int32_t>(static_cast<int32_t>(maxValue), next)));
    };

    switch (setting) {
    case ClusterPreviewBuildSetting::LeafTriangles:
        adjust(buildSettings_.maxTrianglesPerLeafCluster, 16u, 512u, 16u);
        break;
    case ClusterPreviewBuildSetting::LeafVertices:
        adjust(buildSettings_.maxVerticesPerLeafCluster, 16u, 1024u, 16u);
        break;
    case ClusterPreviewBuildSetting::GroupClusters:
        adjust(buildSettings_.maxClustersPerGroup, 2u, 16u, 1u);
        break;
    }
    buildSettingsDirty_ = true;
    refreshBuildSettingButtons();
}

void ClusterPreviewUiState::adjustCutErrorScale(int32_t delta)
{
    const auto next = static_cast<int32_t>(cutErrorScale_) + delta;
    const auto clamped =
        static_cast<uint32_t>(std::max<int32_t>(static_cast<int32_t>(kMinCutErrorScale),
            std::min<int32_t>(static_cast<int32_t>(kMaxCutErrorScale), next)));
    if (clamped == cutErrorScale_) {
        return;
    }

    cutErrorScale_ = clamped;
    cutSettingsChanged_ = true;
    refreshCutErrorButtons();
}

void ClusterPreviewUiState::requestRebuild()
{
    rebuildRequested_ = true;
    buildSettingsDirty_ = false;
    refreshBuildSettingButtons();
}

void ClusterPreviewUiState::refreshBuildSettingButtons()
{
    for (auto& button : buttons_) {
        if (button.role == PreviewButtonRole::BuildSettingDecrease
            || button.role == PreviewButtonRole::BuildSettingIncrease) {
            button.buildSettingValue = buildSettingValue(buildSettings_, button.buildSetting);
        } else if (button.role == PreviewButtonRole::Rebuild) {
            button.active = buildSettingsDirty_;
        }
    }
}

void ClusterPreviewUiState::refreshCutErrorButtons()
{
    for (auto& button : buttons_) {
        if (button.role == PreviewButtonRole::CutErrorDecrease
            || button.role == PreviewButtonRole::CutErrorIncrease) {
            button.cutErrorScale = cutErrorScale_;
        }
    }
}

void ClusterPreviewUiState::rebuildLayout(int width, int height)
{
    layoutWidth_ = width;
    layoutHeight_ = height;
    buttons_.clear();
    const ClusterPreviewMode modes[] = {
        ClusterPreviewMode::RawMesh,
        ClusterPreviewMode::LeafClusters,
        ClusterPreviewMode::ParentClusters,
        ClusterPreviewMode::RootClusters,
        ClusterPreviewMode::ErrorCut,
    };
    for (uint32_t index = 0; index < 5; ++index) {
        PreviewButton button;
        button.role = PreviewButtonRole::PreviewMode;
        button.mode = modes[index];
        button.label = clusterPreviewModeLabel(modes[index]);
        button.x = kButtonX + static_cast<float>(index) * (kButtonWidth + kButtonGap);
        button.y = kButtonY;
        button.width = kButtonWidth;
        button.height = kButtonHeight;
        button.triangleCount = triangleCounts_[index];
        button.hasTriangleCount = true;
        button.active = button.mode == mode_;
        buttons_.push_back(button);
    }

    PreviewButton boundsButton;
    boundsButton.role = PreviewButtonRole::BoundsVisibility;
    boundsButton.label = "Bounds";
    boundsButton.x = kButtonX + 5.0f * (kButtonWidth + kButtonGap);
    boundsButton.y = kButtonY;
    boundsButton.width = kButtonWidth;
    boundsButton.height = kButtonHeight;
    boundsButton.active = boundsVisible_;
    buttons_.push_back(boundsButton);

    PreviewButton colorButton;
    colorButton.role = PreviewButtonRole::ColorMode;
    colorButton.label = "Color";
    colorButton.x = kButtonX + 6.0f * (kButtonWidth + kButtonGap);
    colorButton.y = kButtonY;
    colorButton.width = kButtonWidth;
    colorButton.height = kButtonHeight;
    colorButton.active = colorMode_ != ClusterPreviewColorMode::Shaded;
    buttons_.push_back(colorButton);

    const ClusterPreviewBuildSetting settings[] = {
        ClusterPreviewBuildSetting::LeafTriangles,
        ClusterPreviewBuildSetting::LeafVertices,
        ClusterPreviewBuildSetting::GroupClusters,
    };
    const float buildY = kButtonY + kButtonHeight + kButtonGap;
    for (uint32_t index = 0; index < 3; ++index) {
        const auto setting = settings[index];
        const auto value = buildSettingValue(buildSettings_, setting);
        const float x = kButtonX
            + static_cast<float>(index)
                * (kBuildButtonWidth + kBuildStepButtonWidth + kButtonGap * 2.0f);

        PreviewButton decreaseButton;
        decreaseButton.role = PreviewButtonRole::BuildSettingDecrease;
        decreaseButton.buildSetting = setting;
        decreaseButton.buildSettingValue = value;
        decreaseButton.x = x;
        decreaseButton.y = buildY;
        decreaseButton.width = kBuildButtonWidth;
        decreaseButton.height = kButtonHeight;
        buttons_.push_back(decreaseButton);

        PreviewButton increaseButton;
        increaseButton.role = PreviewButtonRole::BuildSettingIncrease;
        increaseButton.buildSetting = setting;
        increaseButton.buildSettingValue = value;
        increaseButton.x = x + kBuildButtonWidth + kButtonGap;
        increaseButton.y = buildY;
        increaseButton.width = kBuildStepButtonWidth;
        increaseButton.height = kButtonHeight;
        buttons_.push_back(increaseButton);
    }

    const float cutErrorX =
        kButtonX + 3.0f * (kBuildButtonWidth + kBuildStepButtonWidth + kButtonGap * 2.0f);
    PreviewButton cutErrorDecreaseButton;
    cutErrorDecreaseButton.role = PreviewButtonRole::CutErrorDecrease;
    cutErrorDecreaseButton.cutErrorScale = cutErrorScale_;
    cutErrorDecreaseButton.x = cutErrorX;
    cutErrorDecreaseButton.y = buildY;
    cutErrorDecreaseButton.width = kBuildButtonWidth;
    cutErrorDecreaseButton.height = kButtonHeight;
    buttons_.push_back(cutErrorDecreaseButton);

    PreviewButton cutErrorIncreaseButton;
    cutErrorIncreaseButton.role = PreviewButtonRole::CutErrorIncrease;
    cutErrorIncreaseButton.cutErrorScale = cutErrorScale_;
    cutErrorIncreaseButton.x = cutErrorX + kBuildButtonWidth + kButtonGap;
    cutErrorIncreaseButton.y = buildY;
    cutErrorIncreaseButton.width = kBuildStepButtonWidth;
    cutErrorIncreaseButton.height = kButtonHeight;
    buttons_.push_back(cutErrorIncreaseButton);

    PreviewButton rebuildButton;
    rebuildButton.role = PreviewButtonRole::Rebuild;
    rebuildButton.label = "Rebuild";
    rebuildButton.x =
        kButtonX + 4.0f * (kBuildButtonWidth + kBuildStepButtonWidth + kButtonGap * 2.0f);
    rebuildButton.y = buildY;
    rebuildButton.width = kButtonWidth;
    rebuildButton.height = kButtonHeight;
    rebuildButton.active = buildSettingsDirty_;
    buttons_.push_back(rebuildButton);
}

const char* clusterPreviewModeLabel(ClusterPreviewMode mode)
{
    switch (mode) {
    case ClusterPreviewMode::RawMesh:
        return "Raw";
    case ClusterPreviewMode::LeafClusters:
        return "Leaf";
    case ClusterPreviewMode::ParentClusters:
        return "Parents";
    case ClusterPreviewMode::RootClusters:
        return "Root";
    case ClusterPreviewMode::ErrorCut:
        return "Cut";
    }
    return "Unknown";
}

} // namespace arp::tools::cluster_previewer
