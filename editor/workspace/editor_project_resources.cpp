#include "editor/workspace/editor_project_resources.h"

#include "2d/sprite_resources.h"
#include "core/io/file_io.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_terrain_preview.h"
#include "editor/workspace/script_document_session.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"

#include <algorithm>
#include <exception>
#include <string_view>
#include <system_error>

namespace arp::editor {
namespace {
constexpr const char* kProjectManifestName = "project.arp.yaml";
constexpr std::size_t kMaxManifestBytes = EditorProjectResources::maxManifestBytes;

EditorProjectResourceLoadResult failure(EditorProjectResourceLoadResult result, const char* message)
{
    result.diagnostics = message;
    return result;
}

bool resolveContainedPath(const std::filesystem::path& root,
    std::string_view relative,
    ProjectPathIntent intent,
    std::filesystem::path& resolved)
{
    ProjectDiagnostic diagnostic;
    return ProjectManifestCodec::resolvePath(root, relative, intent, resolved, diagnostic);
}

bool readManifest(const std::filesystem::path& path,
    core::pmr::String& text,
    EditorProjectResourceLoadResult& result)
{
    const auto read = core::readTextFile(path, kMaxManifestBytes);
    if (!read.success) {
        result.diagnostics = "EDITOR-PROJECT-MANIFEST-READ: ";
        result.diagnostics += read.diagnostics;
        return false;
    }
    text = read.text;
    return true;
}

void reportProjectDiagnostic(
    EditorProjectResourceLoadResult& result, const ProjectDiagnostic& diagnostic)
{
    result.diagnostics = diagnostic.code;
    result.diagnostics += ": ";
    result.diagnostics += diagnostic.field;
}
} // namespace

EditorProjectResources::EditorProjectResources()
    : manager_(core::makeShared<resource::ResourceManager>())
{}

bool EditorProjectResources::bindSceneServices(
    EditorSceneDocument& document, core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (!manager_) {
        diagnostic = "EDITOR-SCENE-SERVICES-OWNER-UNAVAILABLE";
        return false;
    }
    try {
        // Prepare both callbacks before mutating the document. Components may
        // copy them and outlive this bundle; never capture its address.
        core::SharedCallback<resource::ResourceManager*()> resources([owner = manager_] {
            return owner.get();
        });
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> physics(
            [owner = physicsWorld2D_] {
                return owner;
            });
        document.setResourceManagerProvider(std::move(resources));
        document.setPhysicsWorld2DProvider(std::move(physics));
        return true;
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCENE-SERVICES-PREPARATION-FAILED";
        return false;
    }
}

EditorProjectResourceLoadResult EditorProjectResources::load(
    const std::filesystem::path& projectRoot)
{
    scene2d::registerSpriteFontResourceType();
    EditorProjectResourceLoadResult result;
    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
        result = failure(
            std::move(result), "EDITOR-PROJECT-ROOT: project root is not a readable directory");
        lastLoad_ = result;
        return result;
    }

    const auto manifestPath = root / kProjectManifestName;
    const auto status = std::filesystem::symlink_status(manifestPath, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        result = failure(
            std::move(result), "EDITOR-PROJECT-MANIFEST-STAT: cannot inspect project manifest");
        lastLoad_ = result;
        return result;
    }
    error.clear();
    if (status.type() == std::filesystem::file_type::not_found) {
        auto emptyManager = core::makeShared<resource::ResourceManager>();
        manager_ = std::move(emptyManager);
        physicsWorld2D_.reset();
        packageRoots_.clear();
        resourceLocations_ = {};
        manifestSnapshot_.clear();
        manifest_.reset();
        metadata_ = {};
        metadataSnapshot_.clear();
        inputBindings_.clear();
        packageBindings_.clear();
        packageProvenanceDiagnostic_ = "EDITOR-PROJECT-PACKAGE-PROVENANCE-NO-PROJECT";
        result.success = true;
        result.diagnostics =
            "EDITOR-PROJECT-MANIFEST-MISSING: directory browsing only; create or open an authoring project before project operations";
        lastLoad_ = result;
        return result;
    }
    result.manifestFound = true;
    std::filesystem::path canonicalManifest;
    if (!resolveContainedPath(
            root, kProjectManifestName, ProjectPathIntent::ExistingFile, canonicalManifest)
        || !std::filesystem::is_regular_file(canonicalManifest, error) || error) {
        result = failure(std::move(result),
            "EDITOR-PROJECT-MANIFEST-PATH: manifest must resolve inside project root");
        lastLoad_ = result;
        return result;
    }

    core::pmr::String yaml(&core::defaultCpuMemoryResource());
    if (!readManifest(canonicalManifest, yaml, result)) {
        lastLoad_ = result;
        return result;
    }
    return loadManifest(root, yaml);
}

EditorProjectResourceLoadResult EditorProjectResources::loadManifest(
    const std::filesystem::path& projectRoot, std::string_view yaml)
{
    scene2d::registerSpriteFontResourceType();
    EditorProjectResourceLoadResult result;
    result.manifestFound = true;
    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(root, error) || error
        || yaml.size() > kMaxManifestBytes) {
        result.diagnostics = "EDITOR-PROJECT-CANDIDATE-ROOT-OR-SIZE";
        lastLoad_ = result;
        return result;
    }
    ProjectManifest project;
    ProjectMetadata metadata;
    core::pmr::String metadataBytes;
    ProjectInputSnapshot inputs(root);
    ProjectDiagnostic diagnostic;
    if (!ProjectManifestCodec::decode(yaml, project, diagnostic)) {
        reportProjectDiagnostic(result, diagnostic);
        lastLoad_ = result;
        return result;
    }
    if (!project.editorMetadata.empty()) {
        std::filesystem::path path;
        if (!ProjectManifestCodec::resolvePath(
                root, project.editorMetadata, ProjectPathIntent::ExistingFile, path, diagnostic)) {
            reportProjectDiagnostic(result, diagnostic);
            lastLoad_ = result;
            return result;
        }
        const auto read = core::readTextFile(path, kMaxManifestBytes);
        if (!read.success) {
            result.diagnostics = "EDITOR-PROJECT-METADATA-READ: ";
            result.diagnostics += read.diagnostics;
            lastLoad_ = result;
            return result;
        }
        if (!ProjectMetadataCodec::decode(read.text, metadata, diagnostic)) {
            reportProjectDiagnostic(result, diagnostic);
            lastLoad_ = result;
            return result;
        }
        metadataBytes = read.text;
        if (!inputs.record(path, read.text, result.diagnostics)) {
            lastLoad_ = result;
            return result;
        }
    }
    // Source paths are explicit authoring references. Opening a project does not
    // compile modules or run constructors, but may not accept an escaping path.
    for (const auto& module : project.scriptModules) {
        std::filesystem::path source;
        if (!ProjectManifestCodec::resolvePath(
                root, module.source, ProjectPathIntent::ExistingFile, source, diagnostic)) {
            reportProjectDiagnostic(result, diagnostic);
            lastLoad_ = result;
            return result;
        }
        const auto read = core::readTextFile(source, ScriptDocumentSession::maxScriptBytes);
        if (!read.success || !inputs.record(source, read.text, result.diagnostics)) {
            if (result.diagnostics.empty())
                result.diagnostics = "EDITOR-PROJECT-MODULE-SOURCE-READ-OR-BUDGET";
            lastLoad_ = result;
            return result;
        }
    }

    auto candidate = core::makeShared<resource::ResourceManager>();
    core::SharedPtr<physics2d::PhysicsWorld2D> candidatePhysicsWorld;
    if (project.physics2d) {
        candidatePhysicsWorld = core::makeShared<physics2d::PhysicsWorld2D>(*project.physics2d);
        if (!candidatePhysicsWorld->valid()) {
            result.diagnostics = "EDITOR-PROJECT-PHYSICS2D-CREATE: ";
            result.diagnostics += candidatePhysicsWorld->diagnostic();
            lastLoad_ = result;
            return result;
        }
    }
    const auto sceneFactory =
        ProjectSceneAuthoring::factory(*candidate, candidatePhysicsWorld, result.diagnostics);
    const auto codecs = sceneFactory
        ? ProjectSceneAuthoring::packageCodecs(sceneFactory, result.diagnostics)
        : nullptr;
    if (!codecs) {
        lastLoad_ = result;
        return result;
    }
    // Capture before resource loading. An unsupported recovery profile remains
    // explicitly unbound; it cannot acquire provenance from an after-only read.
    core::pmr::Vector<ProjectPackageBinding> packageBindings;
    core::pmr::String packageDiagnostic;
    const bool packagesBound = ProjectPackageSnapshot::capture(
        root, project.assetPackages, codecs, packageBindings, packageDiagnostic);
    core::pmr::Vector<PackageResourceRoot> candidatePackageRoots{&core::defaultCpuMemoryResource()};
    ProjectInputSnapshot definitionInputs(root, ProjectResourceLocations::budget);
    for (const auto& relative : project.assetPackages) {
        std::filesystem::path packagePath;
        if (!resolveContainedPath(root,
                std::string_view(relative.data(), relative.size()),
                ProjectPathIntent::ExistingDirectory,
                packagePath)
            || !std::filesystem::is_directory(packagePath, error) || error) {
            result.diagnostics =
                "EDITOR-PROJECT-ASSET-PACKAGE-PATH: package must resolve to a directory inside project root";
            lastLoad_ = result;
            return result;
        }
        const auto reader = resource::InternalAssetPackageReader();
        if (!definitionInputs.read(packagePath / "manifest.arpkg.yaml", result.diagnostics)) {
            lastLoad_ = result;
            return result;
        }
        const auto package = reader.read(packagePath);
        if (!package.success) {
            result.diagnostics = "EDITOR-PROJECT-ASSET-PACKAGE-LOAD: ";
            result.diagnostics += package.diagnostics;
            lastLoad_ = result;
            return result;
        }
        for (const auto& entry : package.package.entries) {
            if (!definitionInputs.record(packagePath / std::filesystem::u8path(entry.envelopePath),
                    entry.envelopeYaml,
                    result.diagnostics)) {
                lastLoad_ = result;
                return result;
            }
        }
        const auto loaded = reader.load(packagePath, *candidate, codecs);
        if (!loaded.success) {
            result.diagnostics = "EDITOR-PROJECT-ASSET-PACKAGE-LOAD: ";
            result.diagnostics += loaded.diagnostics;
            lastLoad_ = result;
            return result;
        }
        for (const auto& entry : package.package.entries) {
            if (entry.resourceType != resource::PrefabResource::resourceTypeName()
                && entry.resourceType != "SceneAssetResource")
                continue;
            PackageResourceRoot mapping;
            mapping.resourceId = entry.resourceId;
            mapping.packageRoot = packagePath;
            candidatePackageRoots.push_back(std::move(mapping));
        }
        ++result.packageCount;
    }

    if (!project.entryScene.empty()
        && !candidate->find<scene::SceneAssetResource>(project.entryScene).ready()) {
        result.diagnostics = "EDITOR-PROJECT-ENTRY-SCENE-NOT-READY-OR-TYPE";
        lastLoad_ = result;
        return result;
    }
    for (const auto& preview : metadata.terrainPreviews) {
        if (!loadEditorTerrainPreview(root, preview, *candidate, result.diagnostics, &inputs)) {
            lastLoad_ = result;
            return result;
        }
    }
    if (!inputs.verify(result.diagnostics)) {
        lastLoad_ = result;
        return result;
    }
    if (packagesBound) {
        core::pmr::Vector<ProjectPackageBinding> current;
        if (!ProjectPackageSnapshot::capture(
                root, project.assetPackages, codecs, current, result.diagnostics)) {
            lastLoad_ = result;
            return result;
        }
        if (!ProjectPackageSnapshot::equal(packageBindings, current)) {
            result.diagnostics = "EDITOR-PROJECT-PACKAGE-PROVENANCE-CHANGED";
            lastLoad_ = result;
            return result;
        }
    }
    ProjectResourceLocations locations;
    if (const auto* issue =
            ProjectResourceLocations::capture(root, project.assetPackages, candidate, locations)) {
        result.diagnostics = issue;
        lastLoad_ = result;
        return result;
    }
    if (!definitionInputs.verify(result.diagnostics)) {
        lastLoad_ = result;
        return result;
    }
    core::pmr::String snapshot(yaml);
    result.success = true;
    result.physicsWorld2DReady = bool(candidatePhysicsWorld);
    result.diagnostics.clear();
    manager_ = std::move(candidate);
    physicsWorld2D_ = std::move(candidatePhysicsWorld);
    packageRoots_ = std::move(candidatePackageRoots);
    resourceLocations_ = std::move(locations);
    manifestSnapshot_ = std::move(snapshot);
    manifest_ = std::move(project);
    metadata_ = std::move(metadata);
    metadataSnapshot_ = std::move(metadataBytes);
    inputBindings_ = inputs.takeBindings();
    packageBindings_ = std::move(packageBindings);
    packageProvenanceDiagnostic_ = std::move(packageDiagnostic);
    lastLoad_ = result;
    return result;
}

void EditorProjectResources::publishAssetPackages(EditorProjectResources&& candidate)
{
    manager_ = std::move(candidate.manager_);
    packageRoots_ = std::move(candidate.packageRoots_);
    resourceLocations_ = std::move(candidate.resourceLocations_);
    lastLoad_ = std::move(candidate.lastLoad_);
    manifestSnapshot_ = std::move(candidate.manifestSnapshot_);
    manifest_ = std::move(candidate.manifest_);
    metadata_ = std::move(candidate.metadata_);
    metadataSnapshot_ = std::move(candidate.metadataSnapshot_);
    inputBindings_ = std::move(candidate.inputBindings_);
    packageBindings_ = std::move(candidate.packageBindings_);
    packageProvenanceDiagnostic_ = std::move(candidate.packageProvenanceDiagnostic_);
    lastLoad_.physicsWorld2DReady = bool(physicsWorld2D_);
}

void EditorProjectResources::publishSavedSettings(
    ProjectManifest manifest, core::pmr::String snapshot)
{
    manifest_ = std::move(manifest);
    manifestSnapshot_ = std::move(snapshot);
}

bool EditorProjectResources::packageRootForResource(
    resource::NameView resourceId, std::filesystem::path& packageRoot) const
{
    const auto found = std::find_if(packageRoots_.begin(),
        packageRoots_.end(),
        [resourceId](const PackageResourceRoot& mapping) {
            return std::string_view(mapping.resourceId.data(), mapping.resourceId.size())
                == resourceId;
        });
    if (found == packageRoots_.end())
        return false;
    packageRoot = found->packageRoot;
    return true;
}

} // namespace arp::editor
