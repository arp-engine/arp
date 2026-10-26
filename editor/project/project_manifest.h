#pragma once

#include "core/memory/cpu_allocator.h"
#include "physics/2d/physics_world_2d.h"
#include "scriptvm/aot/aot_v5.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace arp::editor {

enum class ProjectTemplateKind { Empty, Game2D, Game3D };

// Contract budgets apply before publishing a candidate. Parsing is a cold path.
struct ProjectManifestBudget {
    std::size_t maxDocumentBytes = 1024u * 1024u;
    std::size_t maxAssetPackages = 256u;
    std::size_t maxScriptModules = 256u;
    std::size_t maxModuleDependencies = 256u;
};

// Explicit opt-in for cold ScriptComponent preparation. Absence is a legacy
// source declaration, not permission to synthesize execution/instance budgets.
struct ProjectScriptAuthoring {
    static constexpr std::uint32_t contractVersion = 1;
    std::uint64_t maxSourceBytes = 0;
    std::uint64_t preparationBytes = 0;
    std::uint32_t maxInstances = 0;
    scriptvm::AotExecutionBudgetV5 execution;
    core::pmr::Vector<core::pmr::String> imports;
};

struct ProjectScriptModule {
    core::pmr::String id;
    core::pmr::String resourceId;
    core::pmr::String source;
    std::uint32_t schemaVersion = 0;
    core::pmr::Vector<core::pmr::String> dependencies;
    std::optional<ProjectScriptAuthoring> authoring;
};

// Editor-owned authoring data. Players consume exported startup data and assets,
// never this manifest. An empty entryScene is serializable, but fails preflight.
struct ProjectManifest {
    core::pmr::String id;
    core::pmr::String name;
    core::pmr::String engineContract;
    core::pmr::String entryScene;
    core::pmr::String editorMetadata;
    std::optional<physics2d::WorldConfig> physics2d;
    core::pmr::Vector<core::pmr::String> assetPackages;
    core::pmr::Vector<ProjectScriptModule> scriptModules;
};

struct ProjectDiagnostic {
    const char* code = nullptr;
    core::pmr::String field;
};

// Editor preflight supplies the engine identity and Ready typed-resource validator.
// No local version string, path scan or default resource proves launch readiness.
struct ProjectLaunchValidation {
    std::string_view engineContract;
    void* resourceContext = nullptr;
    bool (*isReadyResource)(void*, std::string_view type, std::string_view id) = nullptr;
    bool (*isPreparedModule)(void*, const ProjectScriptModule&) = nullptr;
};

enum class ProjectPathIntent { ExistingFile, ExistingDirectory, NewFile, NewDirectory };

class ProjectManifestCodec final {
public:
    static constexpr std::uint32_t contractVersion = 1;

    static bool decode(std::string_view yaml,
        ProjectManifest& destination,
        ProjectDiagnostic& diagnostic,
        ProjectManifestBudget budget = {});
    static bool encode(const ProjectManifest& manifest,
        core::pmr::String& destination,
        ProjectDiagnostic& diagnostic,
        ProjectManifestBudget budget = {});
    static bool moduleOrder(const ProjectManifest& manifest,
        core::pmr::Vector<std::size_t>& destination,
        ProjectDiagnostic& diagnostic);
    static bool validateForLaunch(const ProjectManifest& manifest,
        const ProjectLaunchValidation& context,
        ProjectDiagnostic& diagnostic,
        ProjectManifestBudget budget = {});
    static bool resolvePath(const std::filesystem::path& root,
        std::string_view relative,
        ProjectPathIntent intent,
        std::filesystem::path& destination,
        ProjectDiagnostic& diagnostic);
};

} // namespace arp::editor
