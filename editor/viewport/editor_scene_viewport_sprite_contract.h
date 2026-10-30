#pragma once
#include "2d/sprite_draw_plan.h"
#include "editor/viewport/editor_scene_viewport_surface_profile.h"
#include "rhi/gpu_pipeline.h"
#include "shader/shader_compiler.h"

#include <string_view>

namespace arp::editor {

ARP_API bool validateEditorSceneViewportSpriteReflection(const shader::ShaderReflection&);
ARP_API rhi::RenderPipelineDesc buildEditorSceneViewportSpritePipeline(
    std::string_view source, EditorSceneViewportSurfaceProfile profile = {});

} // namespace arp::editor
