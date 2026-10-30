#include "2d/sprite_resources.h"
#include "editor/project/project_resource_sources.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_terrain_preview.h"
#include "editor/workspace/script_document_session.h"
#include "resource/prefab_asset_resources.h"
#include "scene/scene_asset_resource.h"

namespace arp::editor {

EditorProjectResourceLoadResult EditorProjectResources::loadSources(
    core::SharedPtr<const ProjectSourceSnapshot> snapshot,
    core::io::FileService& files,
    std::uint64_t epoch)
{
    EditorProjectResourceLoadResult result;
    const auto fail = [&](const char* id = nullptr) {
        result.success = false;
        if (id)
            result.diagnostics = id;
        lastLoad_ = result;
        return result;
    };
    try {
        scene2d::registerSpriteFontResourceType();
        ProjectResourceSources sources;
        if (!sources.capture(snapshot,
                files,
                epoch,
                ProjectPackageSnapshot::maxAllPackageBytes,
                result.diagnostics))
            return fail();
        const auto yaml = sources.text("project.arp.yaml", maxManifestBytes);
        result.manifestFound = bool(sources.find("project.arp.yaml"));
        if (!yaml.success) {
            result.diagnostics = yaml.diagnostics;
            return fail();
        }
        ProjectManifest project;
        ProjectDiagnostic issue;
        if (!ProjectManifestCodec::decode(yaml.text, project, issue)) {
            result.diagnostics = issue.code;
            result.diagnostics += ": ";
            result.diagnostics += issue.field;
            return fail();
        }
        EditorProjectResources candidate;
        ProjectInputSnapshot inputs({});
        const auto record = [&](core::StringView path) {
            const auto* input = sources.find(path);
            if (!input)
                return false;
            return inputs.recordLogical(path,
                {input->bytes.empty() ? "" : reinterpret_cast<const char*>(input->bytes.data()),
                    input->bytes.size()},
                result.diagnostics);
        };
        if (!project.editorMetadata.empty()) {
            const auto metadata = sources.text(project.editorMetadata, maxManifestBytes);
            if (!metadata.success) {
                result.diagnostics = metadata.diagnostics;
                return fail();
            }
            if (!ProjectMetadataCodec::decode(metadata.text, candidate.metadata_, issue)) {
                result.diagnostics = issue.code;
                return fail();
            }
            candidate.metadataSnapshot_ = metadata.text;
            if (!record(project.editorMetadata))
                return fail("EDITOR-PROJECT-METADATA-SOURCE-NOT-ACQUIRED");
        }
        for (const auto& module : project.scriptModules) {
            const auto source = sources.text(module.source, ScriptDocumentSession::maxScriptBytes);
            if (!source.success) {
                result.diagnostics = source.diagnostics;
                return fail();
            }
            if (!record(module.source))
                return fail("EDITOR-PROJECT-MODULE-SOURCE-NOT-ACQUIRED");
        }
        core::SharedPtr<const resource::AssetPackageCodecRegistry> codecs;
        if (!prepareManager(
                project, candidate.manager_, candidate.physicsWorld2D_, codecs, result.diagnostics))
            return fail();
        auto context = sources.context(ProjectPackageSnapshot::maxPackageBytes);
        if (!ProjectPackageSnapshot::captureSources(sources.root(),
                project.assetPackages,
                codecs,
                context,
                candidate.packageBindings_,
                result.diagnostics))
            return fail();
        for (const auto& relative : project.assetPackages) {
            std::filesystem::path path;
            if (!sources.path(relative, path))
                return fail("EDITOR-PROJECT-ASSET-PACKAGE-PATH");
            const auto package = resource::InternalAssetPackageReader().readSources(
                path, context, maxManifestBytes, ProjectPackageSnapshot::maxPackageFiles);
            if (!package.success) {
                result.diagnostics = package.diagnostics;
                return fail();
            }
            const auto loaded = resource::InternalAssetPackageReader().loadSources(
                path, *candidate.manager_, context, codecs);
            if (!loaded.success) {
                result.diagnostics = loaded.diagnostics;
                return fail();
            }
            for (const auto& entry : package.package.entries) {
                if (entry.resourceType != resource::PrefabResource::resourceTypeName()
                    && entry.resourceType != "SceneAssetResource")
                    continue;
                PackageResourceRoot mapping;
                mapping.resourceId = entry.resourceId;
                mapping.packageRoot = path;
                candidate.packageRoots_.push_back(std::move(mapping));
            }
            ++result.packageCount;
        }
        if (!project.entryScene.empty()
            && !candidate.manager_->find<scene::SceneAssetResource>(project.entryScene).ready())
            return fail("EDITOR-PROJECT-ENTRY-SCENE-NOT-READY-OR-TYPE");
        for (const auto& preview : candidate.metadata_.terrainPreviews)
            if (!loadEditorTerrainPreviewSources(
                    sources, preview, *candidate.manager_, result.diagnostics, &inputs))
                return fail();
        const char* id = nullptr;
        if (const auto* error = ProjectResourceLocations::captureSources(sources,
                files,
                epoch,
                project.assetPackages,
                candidate.manager_,
                candidate.resourceLocations_))
            return fail(error);
        candidate.manifest_ = std::move(project);
        candidate.manifestSnapshot_ = yaml.text;
        candidate.inputBindings_ = inputs.takeBindings();
        candidate.sourceSnapshot_ = std::move(snapshot);
        candidate.packageProvenanceDiagnostic_.clear();
        result.success = true;
        result.physicsWorld2DReady = bool(candidate.physicsWorld2D_);
        result.diagnostics.clear();
        candidate.lastLoad_ = result;
        if (!sources.validate(files, epoch, id)) {
            result.success = false;
            return fail(id);
        }
        *this = std::move(candidate);
        return result;
    } catch (const std::bad_alloc&) {
        return fail("EDITOR-PROJECT-SOURCE-PREPARATION-ALLOCATION");
    } catch (const std::exception& error) {
        result.diagnostics = "EDITOR-PROJECT-SOURCE-PREPARATION: ";
        result.diagnostics += error.what();
        return fail();
    }
}

} // namespace arp::editor
