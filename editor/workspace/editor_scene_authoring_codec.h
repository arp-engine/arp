#pragma once

#include "core/memory/cpu_allocator.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_property_codec.h"

#include <string_view>

namespace arp::editor {
class EditorSceneAuthoringCodec final {
public:
    static constexpr std::uint32_t contractVersion = 3;
    static constexpr std::size_t maxDocumentBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t maxObjects = 512u;
    static constexpr std::size_t maxComponents = 2048u;
    static constexpr std::size_t maxPropertiesPerComponent = 256u;

    static bool encode(const EditorSceneDocument& document,
        core::pmr::String& yaml,
        core::pmr::String& diagnostic);
    // Selection is Editor view state and does not make authored content dirty.
    static bool contentHash(const EditorSceneDocument& document,
        core::pmr::String& hash,
        core::pmr::String& diagnostic);
    static bool isContentHash(std::string_view value) noexcept;
    // Shared typed Ready check for persistence, history and cold candidates.
    static bool isResourceReady(void* manager, std::string_view type, std::string_view id);
    static bool decode(std::string_view yaml,
        EditorSceneDocument& destination,
        EditorSceneResourceValidator resourceValidator,
        void* resourceContext,
        core::pmr::String& diagnostic,
        bool validateOnly = false);
    // Cold schema validation: no resource readiness claim, execution or IO.
    static bool validateStructure(std::string_view yaml, core::pmr::String& diagnostic);
    // Explicit offline v2 migration. Empty identity writes null, not a guessed ID.
    static bool migrateVersion2(std::string_view yaml,
        std::string_view runtimeSceneId,
        core::pmr::String& migrated,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
