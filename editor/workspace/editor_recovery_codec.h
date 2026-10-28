#pragma once

#include "core/memory/cpu_allocator.h"
#include "editor/project/project_input_snapshot.h"
#include "editor/project/project_package_snapshot.h"

#include <optional>
#include <string_view>

namespace arp::editor {

// Explicit preparation limits. Runtimes may reduce them, never silently grow
// them or discard documents that do not fit a recovery record.
struct EditorRecoveryBudget {
    static constexpr std::size_t maxRegionMetadataBytes = 4096u;
    std::size_t maxEnvelopeBytes = 48u * 1024u * 1024u;
    std::size_t maxPayloadBytes = 32u * 1024u * 1024u;
    std::size_t maxScripts = 16u;
    std::size_t maxPathBytes = 512u;
    std::size_t slotsPerSession = 2u;
    std::size_t maxSessions = 4u;
    // Two retained slots plus one AtomicFileService temporary per session.
    std::size_t maxRegionBytes = 576u * 1024u * 1024u + maxRegionMetadataBytes;
    std::uint32_t snapshotIntervalSeconds = 10u;
    // Cold region publication only; retries repeat the same exclusive rename.
    std::uint32_t maxRegionPublishAttempts = 4u;
    std::uint32_t regionPublishRetryMilliseconds = 25u;
};

using EditorRecoveryPackage = ProjectPackageBinding;

struct EditorRecoveryMetadata {
    core::pmr::String path;
    core::pmr::String fileBytes;
};

struct EditorRecoveryProject {
    core::pmr::String id;
    core::pmr::String draftYaml;
    core::pmr::String savedFileBytes;
    core::pmr::Vector<EditorRecoveryPackage> packages;
    std::optional<EditorRecoveryMetadata> metadata;
    core::pmr::Vector<ProjectInputBinding> inputFiles;
};

struct EditorRecoveryScene {
    core::pmr::String yaml;
    // Empty fields represent an unbound, unsaved document; YAML uses null.
    core::pmr::String savedContentHash;
    core::pmr::String path;
    core::pmr::String boundTarget;
    core::pmr::String savedFileBytes;
};

struct EditorRecoveryScript {
    core::pmr::String path;
    core::pmr::String text;
    core::pmr::String savedText;
    core::pmr::String savedFileBytes;
    core::pmr::String newline;
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    std::size_t scrollLine = 0;
};

// Cold data only: no live scene IDs, resource handles, runtime objects or Undo
// entries. Selection is already document-local in the existing scene YAML.
struct EditorRecoverySnapshot {
    core::pmr::String sessionId;
    std::uint64_t sequence = 0;
    std::uint64_t workspaceEpoch = 0;
    core::pmr::String projectRoot;
    std::optional<EditorRecoveryProject> project;
    EditorRecoveryScene scene;
    core::pmr::Vector<EditorRecoveryScript> scripts;
    std::optional<std::size_t> activeScript;
    core::pmr::String workspace;
};

class EditorRecoveryCodec final {
public:
    static constexpr std::uint32_t contractVersion = 2;
    static constexpr std::size_t maxMetadataBytes = 1024u * 1024u;

    static bool validateBudget(const EditorRecoveryBudget& budget, core::pmr::String& diagnostic);
    // Owner-thread cold validation only. Explicit nested v3/v4 wire structure
    // is checked; v4 field schema and module/service ownership require actual
    // EditorRecoveryPreparation admission. Concurrent-use approval is separate.
    // This does not prove resource readiness, resolved path identity or that
    // savedContentHash matches a resource-ready restoration of the baseline.
    static bool validate(const EditorRecoverySnapshot& snapshot,
        core::pmr::String& diagnostic,
        EditorRecoveryBudget budget = {});
    static bool encode(const EditorRecoverySnapshot& snapshot,
        core::pmr::String& destination,
        core::pmr::String& diagnostic,
        EditorRecoveryBudget budget = {});
    // Explicit, non-writing migration. v1 external inputs have no provenance
    // and cannot be reconstructed from current files. Failure preserves output.
    static bool migrateVersion1(std::string_view yaml,
        core::pmr::String& destination,
        core::pmr::String& diagnostic,
        EditorRecoveryBudget budget = {});
    static bool decode(std::string_view yaml,
        EditorRecoverySnapshot& destination,
        core::pmr::String& diagnostic,
        EditorRecoveryBudget budget = {});
};

} // namespace arp::editor
