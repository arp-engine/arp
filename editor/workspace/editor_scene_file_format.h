#pragma once

#include "editor/workspace/editor_scene_script_policy.h"

#include <stdexcept>

namespace arp::editor {
// Deterministic Editor wire dispatch. Retained observations are distinct from
// contextual cold admission; an unsupported version never selects another codec.
class EditorSceneFileFormat final {
public:
    static std::uint32_t version(std::string_view yaml)
    {
        if (yaml.empty() || yaml.size() > EditorSceneAuthoringCodec::maxDocumentBytes)
            throw std::invalid_argument("EDITOR-SCENE-DOCUMENT-LIMIT");
        const auto root = core::YamlContract::parseSingleDocument(yaml);
        if (core::YamlContract::text(root, "type") != "arp.editor-scene")
            throw std::invalid_argument("EDITOR-SCENE-FILE-TYPE");
        const auto version = core::YamlContract::uint32(root, "contractVersion");
        if (version != EditorSceneAuthoringCodec::contractVersion
            && version != EditorSceneScriptCodec::contractVersion)
            throw std::invalid_argument("EDITOR-SCENE-FILE-VERSION");
        return version;
    }

    static bool validateStructure(std::string_view yaml, core::pmr::String& diagnostic)
    {
        if (version(yaml) == EditorSceneAuthoringCodec::contractVersion)
            return EditorSceneAuthoringCodec::validateStructure(yaml, diagnostic);
        return EditorSceneScriptCodec::validateStructure(yaml,
            EditorSceneScriptPolicy::fields(),
            EditorSceneScriptPolicy::documentPreparationBytes,
            diagnostic);
    }

    static bool observe(const EditorSceneDocument& document,
        std::uint32_t version,
        const std::filesystem::path& root,
        const ProjectManifest* project,
        core::pmr::String& yaml,
        core::pmr::String& diagnostic)
    {
        if (version == EditorSceneAuthoringCodec::contractVersion)
            return EditorSceneAuthoringCodec::encode(document, yaml, diagnostic);
        if (version == EditorSceneScriptCodec::contractVersion && project)
            return EditorSceneScriptCodec::observeSnapshot(
                document, EditorSceneScriptPolicy::inputs(root, *project), yaml, diagnostic);
        diagnostic = "EDITOR-SCENE-FORMAT-CONTEXT";
        return false;
    }

    static bool observeHash(const EditorSceneDocument& document,
        std::uint32_t version,
        const std::filesystem::path& root,
        const ProjectManifest* project,
        core::pmr::String& hash,
        core::pmr::String& diagnostic)
    {
        if (version == EditorSceneAuthoringCodec::contractVersion)
            return EditorSceneAuthoringCodec::contentHash(document, hash, diagnostic);
        if (version == EditorSceneScriptCodec::contractVersion && project)
            return EditorSceneScriptCodec::observeContentHash(
                document, EditorSceneScriptPolicy::inputs(root, *project), hash, diagnostic);
        diagnostic = "EDITOR-SCENE-FORMAT-CONTEXT";
        return false;
    }

    static bool restore(std::string_view yaml,
        EditorSceneDocument& document,
        const std::filesystem::path& root,
        const ProjectManifest* project,
        core::pmr::String& diagnostic)
    {
        if (version(yaml) == EditorSceneAuthoringCodec::contractVersion) {
            const auto resources = document.resourceManagerProvider();
            return EditorSceneAuthoringCodec::decode(yaml,
                document,
                &EditorSceneAuthoringCodec::isResourceReady,
                resources ? resources() : nullptr,
                diagnostic);
        }
        if (project)
            return EditorSceneScriptCodec::decode(
                yaml, document, EditorSceneScriptPolicy::inputs(root, *project), diagnostic);
        diagnostic = "EDITOR-SCENE-FORMAT-CONTEXT";
        return false;
    }
};
} // namespace arp::editor
