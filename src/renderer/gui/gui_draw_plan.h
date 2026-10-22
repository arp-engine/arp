#pragma once

#include "core/memory/cpu_allocator.h"
#include "gui/gui.h"
#include "renderer/ui/ui_draw_plan.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace arp::renderer {

struct GuiPushConstants {
    float screenSize[2];
    float rectOrigin[2];
    float rectSize[2];
    float uvOrigin[2]{};
    float uvSize[2]{1.0f, 1.0f};
    float flags[2]{};
    float color[4];
};

static_assert(offsetof(GuiPushConstants, screenSize) == 0u);
static_assert(offsetof(GuiPushConstants, rectOrigin) == 8u);
static_assert(offsetof(GuiPushConstants, rectSize) == 16u);
static_assert(offsetof(GuiPushConstants, uvOrigin) == 24u);
static_assert(offsetof(GuiPushConstants, uvSize) == 32u);
static_assert(offsetof(GuiPushConstants, flags) == 40u);
static_assert(offsetof(GuiPushConstants, color) == 48u);
static_assert(sizeof(GuiPushConstants) == 64u);

struct GuiDrawCommand {
    uint32_t vertexCount = 6;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    bool text = false;
    bool textured = false;
    core::pmr::String textureResourceId{&core::defaultCpuMemoryResource()};
    GuiPushConstants pushConstants{};
    bool externalTexture = false;
};

struct GuiDrawPlan {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<GuiDrawCommand> commands{&core::defaultCpuMemoryResource()};

    uint64_t pushConstantSizeBytes() const { return sizeof(GuiPushConstants); }
};

struct GuiCoordinateMapping {
    std::uint32_t contractVersion = 0;
    bool flipRectangleY = false;
    bool flipTextureUvY = false;
};

/// Convert the logical top-left UI rectangle into the render-target coordinate
/// space consumed by the backend-neutral overlay shader. The shader always
/// receives the same pixel-to-NDC formula; only the RHI render-target origin
/// is resolved here in C++.
inline void applyGuiRenderTargetOrigin(GuiPushConstants& push, rhi::Backend backend)
{
    if (!rhi::semantics::uiCoordinatesRequireYFlip(backend)) {
        return;
    }
    push.rectOrigin[1] = push.screenSize[1] - push.rectOrigin[1] - push.rectSize[1];
}

inline GuiPushConstants buildGuiPushConstants(const gui::Rect& rect,
    const gui::Color& color,
    const gui::Rect& uvRect,
    bool textured,
    const rhi::Extent2D& extent,
    rhi::Backend backend = rhi::Backend::Null)
{
    GuiPushConstants push{};
    push.screenSize[0] = static_cast<float>(extent.width);
    push.screenSize[1] = static_cast<float>(extent.height);
    push.rectOrigin[0] = rect.x;
    push.rectOrigin[1] = rect.y;
    push.rectSize[0] = rect.width;
    push.rectSize[1] = rect.height;
    push.uvOrigin[0] = uvRect.x;
    push.uvOrigin[1] = uvRect.y;
    push.uvSize[0] = uvRect.width;
    push.uvSize[1] = uvRect.height;
    if (textured) {
        rhi::semantics::applyCpuRasterTextureUvYConvention(
            &push.uvOrigin[1], &push.uvSize[1], backend);
    }
    push.flags[0] = textured ? 1.0f : 0.0f;
    push.color[0] = color.r;
    push.color[1] = color.g;
    push.color[2] = color.b;
    push.color[3] = color.a;
    applyGuiRenderTargetOrigin(push, backend);
    return push;
}

inline GuiPushConstants buildGuiPushConstants(const gui::Rect& rect,
    const gui::Color& color,
    const rhi::Extent2D& extent,
    rhi::Backend backend = rhi::Backend::Null)
{
    return buildGuiPushConstants(
        rect, color, gui::Rect{0.0f, 0.0f, 1.0f, 1.0f}, false, extent, backend);
}

inline GuiPushConstants buildGuiPushConstants(const gui::DebugDrawQuad& quad,
    const rhi::Extent2D& extent,
    rhi::Backend backend = rhi::Backend::Null)
{
    if (quad.customColor) {
        return buildGuiPushConstants(
            quad.rect, quad.color, quad.textureUvRect, quad.textured, extent, backend);
    }

    gui::Color color{0.28f, 0.28f, 0.28f, quad.type == gui::NodeType::Label ? 0.0f : 0.88f};
    if (quad.type == gui::NodeType::Panel || quad.type == gui::NodeType::ScrollView) {
        color = gui::Color{0.12f, 0.12f, 0.12f, 0.96f};
    } else if (quad.type == gui::NodeType::Button || quad.type == gui::NodeType::Tabs) {
        color = gui::Color{0.28f, 0.28f, 0.28f, 0.88f};
    } else if (quad.type == gui::NodeType::Dropdown || quad.type == gui::NodeType::TreeView) {
        color = gui::Color{0.28f, 0.28f, 0.28f, 0.86f};
    }

    if (quad.visualState == gui::VisualState::Hovered) {
        color = gui::Color{0.38f, 0.38f, 0.38f, 0.92f};
    } else if (quad.visualState == gui::VisualState::Active) {
        color = gui::Color{0.48f, 0.48f, 0.48f, 0.96f};
    } else if (quad.visualState == gui::VisualState::Focused) {
        color = gui::Color{0.42f, 0.42f, 0.42f, 0.90f};
    } else if (quad.visualState == gui::VisualState::Disabled) {
        color.a *= 0.35f;
    }
    return buildGuiPushConstants(
        quad.rect, color, quad.textureUvRect, quad.textured, extent, backend);
}

inline GuiPushConstants buildGuiPushConstants(const gui::DebugDrawTextQuad& quad,
    const rhi::Extent2D& extent,
    rhi::Backend backend = rhi::Backend::Null)
{
    return buildGuiPushConstants(quad.rect, quad.color, extent, backend);
}

inline GuiPushConstants buildUiPushConstants(const arp::renderer::ui::UiDrawCommand& command,
    const rhi::Extent2D& extent,
    const GuiCoordinateMapping& coordinates)
{
    GuiPushConstants push{};
    push.screenSize[0] = static_cast<float>(extent.width);
    push.screenSize[1] = static_cast<float>(extent.height);
    push.rectOrigin[0] = command.rect.x;
    push.rectOrigin[1] = command.rect.y;
    push.rectSize[0] = command.rect.width;
    push.rectSize[1] = command.rect.height;
    push.uvOrigin[0] = command.textureSourceRect.x;
    push.uvOrigin[1] = command.textureSourceRect.y;
    push.uvSize[0] = command.textureSourceRect.width;
    push.uvSize[1] = command.textureSourceRect.height;
    const bool textured = command.kind == arp::renderer::ui::UiDrawCommandKind::Image
        || command.kind == arp::renderer::ui::UiDrawCommandKind::Text
        || command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad;
    if (textured && coordinates.flipTextureUvY) {
        push.uvOrigin[1] += push.uvSize[1];
        push.uvSize[1] = -push.uvSize[1];
    }
    push.flags[0] = textured ? 1.0f : 0.0f;
    // flags[1]: SDF mode flag for the overlay fragment shader. When 1.0, the
    // shader samples the atlas alpha as a signed distance field and
    // reconstructs sharp edges via smoothstep. (Previously stored
    // atlasPageIndex, which the shader never consumed.)
    push.flags[1] = command.sdfMode ? 1.0f : 0.0f;
    push.color[0] = command.color[0];
    push.color[1] = command.color[1];
    push.color[2] = command.color[2];
    push.color[3] = command.color[3];
    if (coordinates.flipRectangleY)
        push.rectOrigin[1] = push.screenSize[1] - push.rectOrigin[1] - push.rectSize[1];
    return push;
}

inline GuiPushConstants buildUiPushConstants(const arp::renderer::ui::UiDrawCommand& command,
    const rhi::Extent2D& extent,
    rhi::Backend backend = rhi::Backend::Null)
{
    float origin = 0, size = 1;
    rhi::semantics::applyCpuRasterTextureUvYConvention(&origin, &size, backend);
    return buildUiPushConstants(command,
        extent,
        GuiCoordinateMapping{1, rhi::semantics::uiCoordinatesRequireYFlip(backend), size < 0});
}

inline GuiDrawPlan buildGuiDrawPlan(const gui::DebugDrawData& DebugDrawData,
    const rhi::Extent2D& extent,
    rhi::Backend backend = rhi::Backend::Null)
{
    const auto commandCount = DebugDrawData.quads.size() + DebugDrawData.textQuads.size();
    if (commandCount == 0) {
        GuiDrawPlan plan;
        plan.diagnostics = "GUI draw data is empty";
        return plan;
    }

    GuiDrawPlan plan;
    plan.success = true;
    plan.commands.reserve(commandCount);
    for (const auto& quad : DebugDrawData.quads) {
        plan.commands.push_back(GuiDrawCommand{
            6,
            1,
            0,
            0,
            false,
            quad.textured,
            quad.textureResourceId,
            buildGuiPushConstants(quad, extent, backend),
        });
    }
    for (const auto& quad : DebugDrawData.textQuads) {
        plan.commands.push_back(GuiDrawCommand{
            6,
            1,
            0,
            0,
            true,
            false,
            {},
            buildGuiPushConstants(quad, extent, backend),
        });
    }
    return plan;
}

inline GuiDrawPlan buildGuiDrawPlan(
    const arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand>& commands,
    const rhi::Extent2D& extent,
    const GuiCoordinateMapping& coordinates)
{
    if (coordinates.contractVersion != 1) {
        GuiDrawPlan plan;
        plan.diagnostics = "GUI-COORDINATE-MAPPING-VERSION";
        return plan;
    }
    if (commands.empty()) {
        GuiDrawPlan plan;
        plan.diagnostics = "UI draw command list is empty";
        return plan;
    }

    GuiDrawPlan plan;
    plan.success = true;
    plan.commands.reserve(commands.size());
    for (const auto& command : commands) {
        if (command.kind == arp::renderer::ui::UiDrawCommandKind::Text
            && (command.textureResourceId.empty() || !command.hasTextureSourceRect
                || !command.textureSourceRect.valid())) {
            plan.success = false;
            plan.diagnostics = "UI text command requires a cached text atlas source rect";
            return plan;
        }
        if (command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad
            && (command.textureResourceId.empty() || !command.hasTextureSourceRect
                || !command.textureSourceRect.valid() || !command.hasAtlasPageIndex)) {
            plan.success = false;
            plan.diagnostics =
                "UI glyph quad command requires explicit atlas resource, source rect, and page index";
            return plan;
        }
        if (command.textureSource == arp::renderer::ui::UiTextureSource::External
            && (command.kind != arp::renderer::ui::UiDrawCommandKind::Image
                || command.textureResourceId.empty() || !command.hasTextureSourceRect
                || !command.textureSourceRect.valid())) {
            plan.success = false;
            plan.diagnostics =
                "external UI texture requires an image, resource identity, and valid source rect";
            return plan;
        }
        const auto textured = command.kind == arp::renderer::ui::UiDrawCommandKind::Image
            || command.kind == arp::renderer::ui::UiDrawCommandKind::Text
            || command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad;
        plan.commands.push_back(GuiDrawCommand{
            6,
            1,
            0,
            0,
            command.kind == arp::renderer::ui::UiDrawCommandKind::Text
                || command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad,
            textured,
            textured ? command.textureResourceId
                     : std::pmr::string(&arp::core::defaultCpuMemoryResource()),
            buildUiPushConstants(command, extent, coordinates),
            command.textureSource == arp::renderer::ui::UiTextureSource::External,
        });
    }
    if (plan.commands.empty()) {
        plan.success = false;
        plan.diagnostics =
            "UI draw command list contains no renderable box, image, or cached text commands";
    }
    return plan;
}

inline GuiDrawPlan buildGuiDrawPlan(
    const arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand>& commands,
    const rhi::Extent2D& extent,
    rhi::Backend backend = rhi::Backend::Null)
{
    float origin = 0, size = 1;
    rhi::semantics::applyCpuRasterTextureUvYConvention(&origin, &size, backend);
    return buildGuiDrawPlan(commands,
        extent,
        GuiCoordinateMapping{1, rhi::semantics::uiCoordinatesRequireYFlip(backend), size < 0});
}

} // namespace arp::renderer
