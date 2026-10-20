#pragma once

#include "core/cpu_allocator.h"
#include "editor/workspace/editor_scene_document.h"

#include <string_view>

namespace arp::editor {
using EditorSceneResourceValidator = bool (*)(
    void* context, std::string_view resourceType, std::string_view resourceId);

class EditorSceneAuthoringCodec final {
public:
    static constexpr std::uint32_t contractVersion = 1;
    static constexpr std::size_t maxDocumentBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t maxObjects = 512u;
    static constexpr std::size_t maxComponents = 2048u;
    static constexpr std::size_t maxPropertiesPerComponent = 256u;

    static bool encode(const EditorSceneDocument& document,
        core::pmr::String& yaml,
        core::pmr::String& diagnostic);
    static bool decode(std::string_view yaml,
        EditorSceneDocument& destination,
        EditorSceneResourceValidator resourceValidator,
        void* resourceContext,
        core::pmr::String& diagnostic,
        bool validateOnly = false);
};
} // namespace arp::editor
