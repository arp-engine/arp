#pragma once

#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_scene_authoring_codec.h"

namespace arp::editor {

// Cold preparation for I4. No method writes a manifest, publishes live owners,
// clears history, or grants GPU retirement authority.
class EditorScenePublicationPreparation final {
public:
    static constexpr std::size_t maxRelativeDirectoryBytes = 512;
    enum class Intent : std::uint8_t { Register, Update };

    struct PackageSnapshot {
        core::pmr::String relativeDirectory;
        std::filesystem::path directory;
        core::pmr::String resourceId;
        core::pmr::String contentHash;
        core::pmr::String fingerprint;
    };

    struct DraftSnapshot {
        std::uint64_t documentEpoch = 0;
        core::pmr::String yaml;
        core::pmr::String contentHash;
    };

    struct Request {
        Intent intent = Intent::Register;
        std::filesystem::path root;
        core::pmr::String projectId;
        core::pmr::String manifestSnapshot;
        PackageSnapshot target;
        std::optional<PackageSnapshot> source;
        DraftSnapshot draft;
    };

    struct Candidate {
        Request submitted;
        core::pmr::String manifestYaml;
        // Normal retirement destroys the document first. Its frozen service
        // callbacks also retain the exact owners if the document is moved out.
        EditorProjectResources resources;
        core::UniquePtr<EditorSceneDocument> document;
    };

    // Explicit scene-only profile: one SceneAsset, one bound runtime document,
    // no external dependencies. Uses existing closure/codec inspection.
    static bool inspectPackage(const std::filesystem::path& root,
        std::string_view relativeDirectory,
        std::string_view resourceId,
        const scene::SceneComponentFactory& factory,
        PackageSnapshot& destination,
        core::pmr::String& diagnostic);
    // Scene owner thread only. Returned bytes contain no live owner references.
    static bool captureDraft(const EditorSceneDocument& document,
        std::uint64_t documentEpoch,
        DraftSnapshot& destination,
        core::pmr::String& diagnostic);
    // Worker-compatible. Reads immutable request data and independent owners.
    static core::UniquePtr<Candidate> prepareResources(
        const Request& request, core::pmr::String& diagnostic);
    // Owner thread only. Restores the current draft, never the project entry.
    static bool prepareDocument(Candidate& candidate, core::pmr::String& diagnostic);
    // Cold precommit freshness check, not a cross-process compare-and-swap.
    // The future command owner must separately validate live owner/generation,
    // history and GPU retirement before it can commit anything.
    static bool validateCurrent(Candidate& candidate,
        const std::filesystem::path& currentRoot,
        std::string_view currentProjectId,
        const DraftSnapshot& currentDraft,
        core::pmr::String& diagnostic);
};

} // namespace arp::editor
