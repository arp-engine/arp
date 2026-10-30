#pragma once
#include "editor/workspace/editor_shell.h"

namespace arp::editor::validation {

// Explicit synthetic validation assets, never an absent-resource fallback.
inline constexpr const char* kViewportSmokeMeshId = "7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b1";
inline constexpr const char* kViewportSmokeMaterialId = "c8591a23-0f99-4070-a4d0-d79ab87379eb";
inline constexpr const char* kViewportSmokeCubeMeshId = "7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b2";
inline constexpr const char* kViewportSmokeCubeMaterialId = "c8591a23-0f99-4070-a4d0-d79ab87379ec";
inline constexpr const char* kViewportSmokeSourceId = "b055987e-d04c-42ef-a3f1-f77721eac8f7";
inline constexpr const char* kViewportSmokeShaderId = "9e3f0b60-93f7-42b8-88e7-32e0c9f0bc40";
inline constexpr const char* kViewportSmokeSrgbTextureId = "86ccf943-c70e-4ddc-b7c4-f1a6c02ff115";
inline constexpr const char* kViewportSmokeLinearTextureId = "fd0155f8-360c-43d3-bdb3-081a9848c1a7";

bool installViewportSmokeSceneResources(EditorShell&, core::pmr::String&);
bool installViewportSmokeScene(EditorShell&, core::pmr::String&, std::string_view fixtureRoot);

} // namespace arp::editor::validation
