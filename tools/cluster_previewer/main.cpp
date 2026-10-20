#include "assets/asset_pipeline.h"
#include "assets/virtualized_geometry.h"
#include "tools/cluster_previewer/cluster_preview_asset.h"
#include "tools/cluster_previewer/cluster_preview_camera.h"
#include "tools/cluster_previewer/cluster_preview_scene.h"
#include "tools/cluster_previewer/cluster_preview_ui.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>

namespace {

struct Options {
    bool hidden = false;
    bool smoke = false;
    uint32_t frames = 0;
    std::filesystem::path meshPath;
};

struct MouseState {
    bool orbiting = false;
    bool panning = false;
    float lastX = 0.0f;
    float lastY = 0.0f;
};

class PreviewGlTexture2D {
public:
    bool upload(const arp::tools::cluster_previewer::PreviewTextureAsset& texture)
    {
        destroy();
        if (!texture.valid || texture.width == 0u || texture.height == 0u
            || texture.rgba8.size()
                != static_cast<std::size_t>(texture.width)
                    * static_cast<std::size_t>(texture.height) * 4u) {
            return false;
        }

        glGenTextures(1, &id_);
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_RGBA,
            static_cast<GLsizei>(texture.width),
            static_cast<GLsizei>(texture.height),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            texture.rgba8.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        width_ = texture.width;
        height_ = texture.height;
        return true;
    }

    void destroy()
    {
        if (id_ != 0u) {
            glDeleteTextures(1, &id_);
            id_ = 0u;
        }
        width_ = 0;
        height_ = 0;
    }

    bool valid() const { return id_ != 0u; }

    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

using PreviewSceneMap = std::map<arp::tools::cluster_previewer::ClusterPreviewMode,
    arp::tools::cluster_previewer::ClusterPreviewScene>;

using PreviewSceneSetMap =
    std::map<arp::tools::cluster_previewer::ClusterPreviewColorMode, PreviewSceneMap>;

struct PreviewRuntimeData {
    bool success = false;
    std::string diagnostics;
    arp::assets::VirtualizedGeometryBuildResult hierarchy;
    PreviewSceneSetMap sceneSets;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--hidden") {
            options.hidden = true;
        } else if (arg == "--smoke") {
            options.smoke = true;
            options.hidden = true;
            options.frames = std::max<uint32_t>(options.frames, 1u);
        } else if (arg == "--frames" && index + 1 < argc) {
            options.frames = static_cast<uint32_t>(std::max(0, std::atoi(argv[++index])));
        } else if (arg == "--mesh" && index + 1 < argc) {
            options.meshPath = argv[++index];
        }
    }
    return options;
}

arp::assets::VirtualizedGeometryBuildPolicy previewBuildPolicy(
    const arp::tools::cluster_previewer::ClusterPreviewBuildSettings& settings)
{
    arp::assets::VirtualizedGeometryBuildPolicy policy;
    policy.maxTrianglesPerLeafCluster = settings.maxTrianglesPerLeafCluster;
    policy.maxVerticesPerLeafCluster = settings.maxVerticesPerLeafCluster;
    policy.maxClustersPerGroup = settings.maxClustersPerGroup;
    policy.simplificationTargetNumerator = 1;
    policy.simplificationTargetDenominator = 2;
    const auto completeLeafGroupTriangleCount =
        static_cast<uint64_t>(settings.maxTrianglesPerLeafCluster)
        * static_cast<uint64_t>(settings.maxClustersPerGroup);
    policy.maxTrianglesPerSimplificationPass = static_cast<uint32_t>(
        std::min<uint64_t>(completeLeafGroupTriangleCount, std::numeric_limits<uint32_t>::max()));
    policy.preserveMaterialBoundaries = true;
    return policy;
}

arp::tools::cluster_previewer::ClusterPreviewCutSettings previewCutSettings(
    const arp::tools::cluster_previewer::ClusterPreviewUiState& ui)
{
    arp::tools::cluster_previewer::ClusterPreviewCutSettings settings;
    settings.errorScale = static_cast<float>(ui.cutErrorScale());
    return settings;
}

bool buildPreviewSceneSets(const arp::assets::MeshAsset& mesh,
    const arp::assets::VirtualizedGeometryHierarchy& hierarchy,
    const arp::tools::cluster_previewer::ClusterPreviewCutSettings& cutSettings,
    PreviewSceneSetMap& sceneSets,
    std::string& diagnostics)
{
    PreviewSceneSetMap rebuiltSceneSets;
    auto buildSceneSet = [&](arp::tools::cluster_previewer::ClusterPreviewColorMode colorMode) {
        PreviewSceneMap scenes;
        for (const auto mode : {
                 arp::tools::cluster_previewer::ClusterPreviewMode::RawMesh,
                 arp::tools::cluster_previewer::ClusterPreviewMode::LeafClusters,
                 arp::tools::cluster_previewer::ClusterPreviewMode::ParentClusters,
                 arp::tools::cluster_previewer::ClusterPreviewMode::RootClusters,
                 arp::tools::cluster_previewer::ClusterPreviewMode::ErrorCut,
             }) {
            auto scene = arp::tools::cluster_previewer::buildClusterPreviewScene(
                mesh, hierarchy, mode, colorMode, cutSettings);
            if (!scene.success) {
                diagnostics = "cluster preview scene failed: ";
                diagnostics += scene.diagnostics;
                return false;
            }
            scenes.emplace(mode, std::move(scene));
        }
        rebuiltSceneSets.emplace(colorMode, std::move(scenes));
        return true;
    };

    if (!buildSceneSet(arp::tools::cluster_previewer::ClusterPreviewColorMode::Shaded)
        || !buildSceneSet(arp::tools::cluster_previewer::ClusterPreviewColorMode::ClusterColor)
        || !buildSceneSet(arp::tools::cluster_previewer::ClusterPreviewColorMode::Textured)) {
        return false;
    }

    sceneSets = std::move(rebuiltSceneSets);
    return true;
}

PreviewRuntimeData buildPreviewRuntimeData(const arp::assets::MeshAsset& mesh,
    const arp::tools::cluster_previewer::ClusterPreviewBuildSettings& settings,
    const arp::tools::cluster_previewer::ClusterPreviewCutSettings& cutSettings)
{
    PreviewRuntimeData data;
    data.hierarchy = arp::assets::buildVirtualizedGeometry(mesh, previewBuildPolicy(settings));
    if (!data.hierarchy.success) {
        data.diagnostics = "cluster hierarchy failed: ";
        data.diagnostics += data.hierarchy.diagnostics;
        return data;
    }

    if (!buildPreviewSceneSets(
            mesh, data.hierarchy.hierarchy, cutSettings, data.sceneSets, data.diagnostics)) {
        return data;
    }
    data.success = true;
    return data;
}

void beginScreenSpace(int width, int height)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

void endScreenSpace()
{
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

void drawRect(float x, float y, float width, float height, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

void drawRectOutline(float x, float y, float width, float height, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

void drawDigitSegments(float x, float y, float scale, int digit)
{
    constexpr int segmentMasks[] = {
        0b0111111,
        0b0000110,
        0b1011011,
        0b1001111,
        0b1100110,
        0b1101101,
        0b1111101,
        0b0000111,
        0b1111111,
        0b1101111,
    };
    if (digit < 0 || digit > 9) {
        return;
    }
    const int mask = segmentMasks[digit];
    const float w = 8.0f * scale;
    const float h = 14.0f * scale;
    const float t = 2.0f * scale;
    auto segment = [&](int bit, float sx, float sy, float sw, float sh) {
        if ((mask & (1 << bit)) != 0) {
            drawRect(x + sx, y + sy, sw, sh, 0.92f, 0.94f, 0.96f);
        }
    };
    segment(0, t, 0.0f, w - 2.0f * t, t);
    segment(1, w - t, t, t, h * 0.5f - t);
    segment(2, w - t, h * 0.5f, t, h * 0.5f - t);
    segment(3, t, h - t, w - 2.0f * t, t);
    segment(4, 0.0f, h * 0.5f, t, h * 0.5f - t);
    segment(5, 0.0f, t, t, h * 0.5f - t);
    segment(6, t, h * 0.5f - t * 0.5f, w - 2.0f * t, t);
}

void drawNumberSegments(float x, float y, float scale, uint32_t number)
{
    char digits[10]{};
    uint32_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + number % 10u);
        number /= 10u;
    } while (number > 0u && count < 9u);

    const float advance = 10.0f * scale;
    for (uint32_t index = 0; index < count; ++index) {
        const auto digit = digits[count - index - 1u] - '0';
        drawDigitSegments(x + static_cast<float>(index) * advance, y, scale, digit);
    }
}

void drawTriangleCountIcon(float x, float y, float scale)
{
    glColor3f(0.72f, 0.78f, 0.82f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y + 7.0f * scale);
    glVertex2f(x + 8.0f * scale, y);
    glVertex2f(x + 12.0f * scale, y + 8.0f * scale);
    glEnd();
}

void drawBoundsIcon(float x, float y, float width, float height, bool active)
{
    const auto x0 = x + width * 0.36f;
    const auto y0 = y + height * 0.34f;
    const auto x1 = x + width * 0.64f;
    const auto y1 = y + height * 0.68f;
    const auto offset = height * 0.16f;

    glColor3f(0.90f, 0.94f, 0.96f);
    glBegin(GL_LINES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glVertex2f(x0, y1);
    glVertex2f(x0, y0);

    glVertex2f(x0 + offset, y0 - offset);
    glVertex2f(x1 + offset, y0 - offset);
    glVertex2f(x1 + offset, y0 - offset);
    glVertex2f(x1 + offset, y1 - offset);
    glVertex2f(x1 + offset, y1 - offset);
    glVertex2f(x0 + offset, y1 - offset);
    glVertex2f(x0 + offset, y1 - offset);
    glVertex2f(x0 + offset, y0 - offset);

    glVertex2f(x0, y0);
    glVertex2f(x0 + offset, y0 - offset);
    glVertex2f(x1, y0);
    glVertex2f(x1 + offset, y0 - offset);
    glVertex2f(x1, y1);
    glVertex2f(x1 + offset, y1 - offset);
    glVertex2f(x0, y1);
    glVertex2f(x0 + offset, y1 - offset);
    glEnd();

    if (!active) {
        glColor3f(0.96f, 0.72f, 0.72f);
        glBegin(GL_LINES);
        glVertex2f(x + width * 0.32f, y + height * 0.74f);
        glVertex2f(x + width * 0.68f, y + height * 0.24f);
        glEnd();
    }
}

void drawColorModeIcon(float x,
    float y,
    float width,
    float height,
    arp::tools::cluster_previewer::ClusterPreviewColorMode colorMode)
{
    const float swatchSize = height * 0.28f;
    const float startX = x + width * 0.34f;
    const float startY = y + height * 0.31f;
    if (colorMode == arp::tools::cluster_previewer::ClusterPreviewColorMode::ClusterColor) {
        drawRect(startX, startY, swatchSize, swatchSize, 0.15f, 0.78f, 0.92f);
        drawRect(startX + swatchSize * 0.82f,
            startY + swatchSize * 0.34f,
            swatchSize,
            swatchSize,
            0.85f,
            0.24f,
            0.78f);
        drawRect(startX + swatchSize * 1.64f, startY, swatchSize, swatchSize, 0.96f, 0.48f, 0.20f);
    } else if (colorMode == arp::tools::cluster_previewer::ClusterPreviewColorMode::Textured) {
        drawRect(startX, startY, swatchSize, swatchSize, 0.86f, 0.82f, 0.58f);
        drawRect(startX + swatchSize, startY, swatchSize, swatchSize, 0.28f, 0.30f, 0.30f);
        drawRect(startX, startY + swatchSize, swatchSize, swatchSize, 0.28f, 0.30f, 0.30f);
        drawRect(
            startX + swatchSize, startY + swatchSize, swatchSize, swatchSize, 0.86f, 0.82f, 0.58f);
    } else {
        drawRect(startX, startY, swatchSize * 2.6f, swatchSize * 1.25f, 0.58f, 0.64f, 0.68f);
        drawRectOutline(startX, startY, swatchSize * 2.6f, swatchSize * 1.25f, 0.82f, 0.86f, 0.88f);
    }
}

void drawPlusIcon(float x, float y, float width, float height)
{
    const float cx = x + width * 0.5f;
    const float cy = y + height * 0.5f;
    const float extent = height * 0.20f;
    glColor3f(0.90f, 0.94f, 0.96f);
    glBegin(GL_LINES);
    glVertex2f(cx - extent, cy);
    glVertex2f(cx + extent, cy);
    glVertex2f(cx, cy - extent);
    glVertex2f(cx, cy + extent);
    glEnd();
}

void drawMinusIcon(float x, float y, float width, float height)
{
    const float cx = x + width * 0.18f;
    const float cy = y + height * 0.5f;
    const float extent = height * 0.14f;
    glColor3f(0.90f, 0.94f, 0.96f);
    glBegin(GL_LINES);
    glVertex2f(cx - extent, cy);
    glVertex2f(cx + extent, cy);
    glEnd();
}

void drawBuildSettingIcon(float x,
    float y,
    float scale,
    arp::tools::cluster_previewer::ClusterPreviewBuildSetting setting)
{
    switch (setting) {
    case arp::tools::cluster_previewer::ClusterPreviewBuildSetting::LeafTriangles:
        drawTriangleCountIcon(x, y + 2.0f * scale, scale);
        break;
    case arp::tools::cluster_previewer::ClusterPreviewBuildSetting::LeafVertices:
        glColor3f(0.72f, 0.78f, 0.82f);
        glPointSize(4.0f * scale);
        glBegin(GL_POINTS);
        glVertex2f(x, y + 7.0f * scale);
        glVertex2f(x + 7.0f * scale, y);
        glVertex2f(x + 13.0f * scale, y + 8.0f * scale);
        glEnd();
        break;
    case arp::tools::cluster_previewer::ClusterPreviewBuildSetting::GroupClusters:
        drawRectOutline(x, y, 7.0f * scale, 7.0f * scale, 0.72f, 0.78f, 0.82f);
        drawRectOutline(
            x + 6.0f * scale, y + 5.0f * scale, 7.0f * scale, 7.0f * scale, 0.72f, 0.78f, 0.82f);
        break;
    }
}

void drawCutErrorIcon(float x, float y, float scale)
{
    glColor3f(0.72f, 0.78f, 0.82f);
    glBegin(GL_LINE_STRIP);
    glVertex2f(x, y + 10.0f * scale);
    glVertex2f(x + 4.0f * scale, y + 4.0f * scale);
    glVertex2f(x + 8.0f * scale, y + 9.0f * scale);
    glVertex2f(x + 13.0f * scale, y + 2.0f * scale);
    glEnd();
    glBegin(GL_LINES);
    glVertex2f(x + 15.0f * scale, y + 1.0f * scale);
    glVertex2f(x + 15.0f * scale, y + 11.0f * scale);
    glEnd();
}

void drawRebuildIcon(float x, float y, float width, float height)
{
    const float cx = x + width * 0.5f;
    const float cy = y + height * 0.5f;
    const float radius = height * 0.22f;
    glColor3f(0.90f, 0.94f, 0.96f);
    glBegin(GL_LINE_STRIP);
    for (uint32_t step = 0; step <= 10; ++step) {
        const auto angle = 0.7f + static_cast<float>(step) * 4.3f / 10.0f;
        glVertex2f(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
    }
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2f(cx + radius * 0.75f, cy - radius * 0.70f);
    glVertex2f(cx + radius * 1.25f, cy - radius * 0.66f);
    glVertex2f(cx + radius * 0.92f, cy - radius * 0.22f);
    glEnd();
}

void drawPreviewUi(arp::tools::cluster_previewer::ClusterPreviewUiState& ui, int width, int height)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    beginScreenSpace(width, height);
    const auto& buttons = ui.layout(width, height);
    for (uint32_t index = 0; index < buttons.size(); ++index) {
        const auto& button = buttons[index];
        if (button.active) {
            drawRect(button.x, button.y, button.width, button.height, 0.25f, 0.49f, 0.82f);
            drawRectOutline(button.x, button.y, button.width, button.height, 0.90f, 0.94f, 1.0f);
        } else {
            drawRect(button.x, button.y, button.width, button.height, 0.16f, 0.18f, 0.20f);
            drawRectOutline(button.x, button.y, button.width, button.height, 0.42f, 0.46f, 0.50f);
        }
        if (button.role == arp::tools::cluster_previewer::PreviewButtonRole::BoundsVisibility) {
            drawBoundsIcon(button.x, button.y, button.width, button.height, button.active);
        } else if (button.role == arp::tools::cluster_previewer::PreviewButtonRole::ColorMode) {
            drawColorModeIcon(button.x, button.y, button.width, button.height, ui.colorMode());
        } else if (button.role
            == arp::tools::cluster_previewer::PreviewButtonRole::BuildSettingDecrease) {
            drawMinusIcon(button.x, button.y, button.width, button.height);
            drawBuildSettingIcon(button.x + 26.0f, button.y + 10.0f, 0.76f, button.buildSetting);
            drawNumberSegments(button.x + 45.0f, button.y + 9.0f, 0.72f, button.buildSettingValue);
        } else if (button.role
            == arp::tools::cluster_previewer::PreviewButtonRole::BuildSettingIncrease) {
            drawPlusIcon(button.x, button.y, button.width, button.height);
        } else if (button.role
            == arp::tools::cluster_previewer::PreviewButtonRole::CutErrorDecrease) {
            drawMinusIcon(button.x, button.y, button.width, button.height);
            drawCutErrorIcon(button.x + 25.0f, button.y + 9.0f, 0.78f);
            drawNumberSegments(button.x + 47.0f, button.y + 9.0f, 0.72f, button.cutErrorScale);
        } else if (button.role
            == arp::tools::cluster_previewer::PreviewButtonRole::CutErrorIncrease) {
            drawPlusIcon(button.x, button.y, button.width, button.height);
        } else if (button.role == arp::tools::cluster_previewer::PreviewButtonRole::Rebuild) {
            drawRebuildIcon(button.x, button.y, button.width, button.height);
        } else {
            drawDigitSegments(
                button.x + 12.0f, button.y + 8.0f, 1.0f, static_cast<int>(index + 1u));
            if (button.hasTriangleCount) {
                drawTriangleCountIcon(button.x + 33.0f, button.y + 11.0f, 0.72f);
                drawNumberSegments(button.x + 48.0f, button.y + 9.0f, 0.72f, button.triangleCount);
            }
        }
    }
    endScreenSpace();
}

void drawTriangles(const arp::tools::cluster_previewer::ClusterPreviewScene& scene,
    const PreviewGlTexture2D& texture,
    bool textured)
{
    if (textured && texture.valid()) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture.id());
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    glBegin(GL_TRIANGLES);
    for (const auto index : scene.triangleIndices) {
        const auto& vertex = scene.triangleVertices[index];
        glColor3f(vertex.r, vertex.g, vertex.b);
        if (textured && texture.valid()) {
            glTexCoord2f(vertex.u, vertex.v);
        }
        glVertex3f(vertex.x, vertex.y, vertex.z);
    }
    glEnd();
    if (textured && texture.valid()) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
}

void drawLines(const arp::tools::cluster_previewer::ClusterPreviewScene& scene)
{
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (const auto index : scene.lineIndices) {
        const auto& vertex = scene.lineVertices[index];
        glColor3f(vertex.r * 0.45f, vertex.g * 0.45f, vertex.b * 0.45f);
        glVertex3f(vertex.x, vertex.y, vertex.z);
    }
    glEnd();
}

void perspective(float fovYDegrees, float aspect, float nearPlane, float farPlane)
{
    const auto top = std::tan(fovYDegrees * 3.14159265358979323846 / 360.0) * nearPlane;
    const auto bottom = -top;
    const auto right = top * static_cast<double>(aspect);
    const auto left = -right;
    glFrustum(left, right, bottom, top, nearPlane, farPlane);
}

void lookAt(const arp::tools::cluster_previewer::CameraVec3& eye,
    const arp::tools::cluster_previewer::CameraVec3& target)
{
    const auto fx = target.x - eye.x;
    const auto fy = target.y - eye.y;
    const auto fz = target.z - eye.z;
    const auto fLength = std::sqrt(fx * fx + fy * fy + fz * fz);
    const auto fnx = fx / std::max(fLength, 0.0001f);
    const auto fny = fy / std::max(fLength, 0.0001f);
    const auto fnz = fz / std::max(fLength, 0.0001f);

    float sx = fny * 0.0f - fnz * 1.0f;
    float sy = fnz * 0.0f - fnx * 0.0f;
    float sz = fnx * 1.0f - fny * 0.0f;
    const auto sLength = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= std::max(sLength, 0.0001f);
    sy /= std::max(sLength, 0.0001f);
    sz /= std::max(sLength, 0.0001f);

    const float ux = sy * fnz - sz * fny;
    const float uy = sz * fnx - sx * fnz;
    const float uz = sx * fny - sy * fnx;

    const float matrix[] = {
        sx,
        ux,
        -fnx,
        0.0f,
        sy,
        uy,
        -fny,
        0.0f,
        sz,
        uz,
        -fnz,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    glMultMatrixf(matrix);
    glTranslatef(-eye.x, -eye.y, -eye.z);
}

void renderScene(const arp::tools::cluster_previewer::ClusterPreviewScene& scene,
    const arp::tools::cluster_previewer::OrbitPreviewCamera& camera,
    bool boundsVisible,
    const PreviewGlTexture2D& texture,
    bool textured,
    int width,
    int height)
{
    const auto& bounds = scene.bounds;
    const auto aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    const auto radius = std::max(bounds.radius, 1.0f);

    glViewport(0, 0, width, height);
    glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    perspective(45.0f, aspect, std::max(0.01f, radius * 0.01f), std::max(100.0f, radius * 80.0f));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    lookAt(camera.eye(), camera.target());
    drawTriangles(scene, texture, textured);
    if (boundsVisible) {
        glDisable(GL_DEPTH_TEST);
        drawLines(scene);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    const auto loadedMesh = arp::tools::cluster_previewer::loadPreviewMesh(options.meshPath);
    if (!loadedMesh.success) {
        std::cerr << "preview mesh load failed: " << loadedMesh.diagnostics << "\n";
        return 2;
    }
    if (loadedMesh.usedFallback && !loadedMesh.diagnostics.empty()) {
        std::cerr << loadedMesh.diagnostics << "\n";
    }
    const auto& mesh = loadedMesh.mesh;
    arp::tools::cluster_previewer::ClusterPreviewUiState ui;
    auto runtime = buildPreviewRuntimeData(mesh, ui.buildSettings(), previewCutSettings(ui));
    if (!runtime.success) {
        std::cerr << runtime.diagnostics << "\n";
        return 2;
    }

    auto refreshTriangleCounts = [&]() {
        const auto& countScenes = runtime.sceneSets.at(
            arp::tools::cluster_previewer::ClusterPreviewColorMode::ClusterColor);
        for (const auto mode : {
                 arp::tools::cluster_previewer::ClusterPreviewMode::RawMesh,
                 arp::tools::cluster_previewer::ClusterPreviewMode::LeafClusters,
                 arp::tools::cluster_previewer::ClusterPreviewMode::ParentClusters,
                 arp::tools::cluster_previewer::ClusterPreviewMode::RootClusters,
                 arp::tools::cluster_previewer::ClusterPreviewMode::ErrorCut,
             }) {
            ui.setTriangleCount(mode, countScenes.at(mode).triangleCount);
        }
    };
    refreshTriangleCounts();
    arp::tools::cluster_previewer::OrbitPreviewCamera camera;
    const auto& modelBounds =
        runtime.sceneSets.at(arp::tools::cluster_previewer::ClusterPreviewColorMode::Shaded)
            .at(arp::tools::cluster_previewer::ClusterPreviewMode::RawMesh)
            .bounds;
    camera.frameBounds(modelBounds);
    MouseState mouse;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 3;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (options.hidden) {
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_HIDDEN);
    }

    SDL_Window* window = SDL_CreateWindow("Cluster Previewer", 960, 640, flags);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 3;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 3;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);

    PreviewGlTexture2D baseColorTexture;
    if (loadedMesh.baseColorTexture.valid) {
        if (!baseColorTexture.upload(loadedMesh.baseColorTexture)) {
            std::cerr << "preview texture upload failed: "
                      << loadedMesh.baseColorTexture.sourceLabel << "\n";
        }
    } else if (!loadedMesh.baseColorTexture.diagnostics.empty()) {
        std::cerr << loadedMesh.baseColorTexture.diagnostics << "\n";
    }

    const auto& startupScene = runtime.sceneSets.at(ui.colorMode()).at(ui.mode());
    std::cout << "Cluster preview: source=" << loadedMesh.sourceLabel
              << " vertices=" << mesh.vertices.size() << " indices=" << mesh.indices.size()
              << " clusters=" << startupScene.clusterCount
              << " triangles=" << startupScene.triangleCount << " cutError=" << ui.cutErrorScale()
              << " texture="
              << (baseColorTexture.valid() ? loadedMesh.baseColorTexture.sourceLabel : "none")
              << " hierarchyHash=" << runtime.hierarchy.hierarchy.stableHash << "\n";

    uint32_t renderedFrames = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                int width = 960;
                int height = 640;
                SDL_GetWindowSize(window, &width, &height);
                const auto uiHandled = ui.click(event.button.x, event.button.y, width, height);
                if (!uiHandled) {
                    mouse.lastX = event.button.x;
                    mouse.lastY = event.button.y;
                    mouse.orbiting = event.button.button == SDL_BUTTON_LEFT;
                    mouse.panning = event.button.button == SDL_BUTTON_RIGHT;
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse.orbiting = false;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouse.panning = false;
                }
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                const auto dx = event.motion.x - mouse.lastX;
                const auto dy = event.motion.y - mouse.lastY;
                mouse.lastX = event.motion.x;
                mouse.lastY = event.motion.y;
                if (mouse.orbiting) {
                    camera.orbit(dx, dy);
                } else if (mouse.panning) {
                    int width = 960;
                    int height = 640;
                    SDL_GetWindowSize(window, &width, &height);
                    camera.pan(dx, dy, width, height);
                }
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                camera.zoom(event.wheel.y);
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                const auto key = event.key.key;
                if (key == SDLK_1) {
                    ui.key('1');
                } else if (key == SDLK_2) {
                    ui.key('2');
                } else if (key == SDLK_3) {
                    ui.key('3');
                } else if (key == SDLK_4) {
                    ui.key('4');
                } else if (key == SDLK_5) {
                    ui.key('5');
                } else if (key == SDLK_B) {
                    ui.key('b');
                } else if (key == SDLK_C) {
                    ui.key('c');
                } else if (key == SDLK_R) {
                    ui.key('r');
                }
            }
        }

        if (ui.consumeRebuildRequested()) {
            ui.consumeCutSettingsChanged();
            auto rebuilt =
                buildPreviewRuntimeData(mesh, ui.buildSettings(), previewCutSettings(ui));
            if (rebuilt.success) {
                runtime = std::move(rebuilt);
                refreshTriangleCounts();
                const auto& rebuiltBounds =
                    runtime.sceneSets
                        .at(arp::tools::cluster_previewer::ClusterPreviewColorMode::Shaded)
                        .at(arp::tools::cluster_previewer::ClusterPreviewMode::RawMesh)
                        .bounds;
                camera.frameBounds(rebuiltBounds);
                const auto& rebuiltScene = runtime.sceneSets.at(ui.colorMode()).at(ui.mode());
                std::cout << "Cluster preview rebuild: leafTris="
                          << ui.buildSettings().maxTrianglesPerLeafCluster
                          << " leafVerts=" << ui.buildSettings().maxVerticesPerLeafCluster
                          << " groups=" << ui.buildSettings().maxClustersPerGroup
                          << " cutError=" << ui.cutErrorScale()
                          << " clusters=" << rebuiltScene.clusterCount
                          << " triangles=" << rebuiltScene.triangleCount
                          << " hierarchyHash=" << runtime.hierarchy.hierarchy.stableHash << "\n";
            } else {
                std::cerr << rebuilt.diagnostics << "\n";
            }
        } else if (ui.consumeCutSettingsChanged()) {
            PreviewSceneSetMap rebuiltSceneSets;
            std::string diagnostics;
            if (buildPreviewSceneSets(mesh,
                    runtime.hierarchy.hierarchy,
                    previewCutSettings(ui),
                    rebuiltSceneSets,
                    diagnostics)) {
                runtime.sceneSets = std::move(rebuiltSceneSets);
                refreshTriangleCounts();
                const auto& rebuiltScene = runtime.sceneSets.at(ui.colorMode()).at(ui.mode());
                std::cout << "Cluster preview cut: error=" << ui.cutErrorScale()
                          << " clusters=" << rebuiltScene.clusterCount
                          << " triangles=" << rebuiltScene.triangleCount << "\n";
            } else {
                std::cerr << diagnostics << "\n";
            }
        }

        int width = 960;
        int height = 640;
        SDL_GetWindowSize(window, &width, &height);
        const auto& scenes = runtime.sceneSets.at(ui.colorMode());
        const auto& scene = scenes.at(ui.mode());
        renderScene(scene,
            camera,
            ui.boundsVisible(),
            baseColorTexture,
            ui.colorMode() == arp::tools::cluster_previewer::ClusterPreviewColorMode::Textured,
            width,
            height);
        drawPreviewUi(ui, width, height);
        SDL_GL_SwapWindow(window);
        ++renderedFrames;

        if (options.smoke && renderedFrames >= options.frames) {
            running = false;
        }
    }

    baseColorTexture.destroy();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
