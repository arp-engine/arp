#include "editor/workspace/editor_project_resource_planner.h"

#include "editor/project/project_manifest.h"
#include "editor/project/project_metadata.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_terrain_preview.h"
#include "editor/workspace/script_document_session.h"
#include "resource/asset_package_closure.h"
#include "resource/asset_package_payload.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::editor {

resource::AssetPackageReadPlan EditorProjectResourcePlanner::plan(const std::filesystem::path& root,
    const resource::AssetPackageSourceContext& context,
    ProjectInputBudget budget,
    std::size_t maxDecodedBytes,
    core::SharedPtr<const resource::AssetPackageCodecRegistry> codecs)
{
    resource::AssetPackageReadPlan result;
    const auto fail = [&](const char* id) {
        result.success = false;
        result.diagnostics = id;
        result.reads.clear();
        return std::move(result);
    };
    try {
        if (!ProjectInputSnapshot::validBudget(budget) || !maxDecodedBytes
            || maxDecodedBytes == SIZE_MAX || !context.resources.sources
            || !context.resources.requirePreloadedSources
            || context.origin.pathScope != core::io::FilePathScope::MountedResource
            || !context.origin.mount || (codecs && !codecs->frozen())
            || context.resources.maxDecodedBytes != maxDecodedBytes)
            return fail("EDITOR-PROJECT-READ-PLAN-CONFIG");
        const auto metadataLimit =
            std::min(budget.maxFileBytes, EditorProjectResources::maxManifestBytes);
        const auto admit = [&](resource::ResourceFileReadIntent read) {
            read.maxBytes =
                read.maxBytes ? std::min(read.maxBytes, budget.maxFileBytes) : budget.maxFileBytes;
            for (auto& previous : result.reads)
                if (previous.path == read.path) {
                    if (previous.pathScope != read.pathScope || !(previous.mount == read.mount)
                        || previous.expectedVersion != read.expectedVersion
                        || previous.maxVersionBytes != read.maxVersionBytes) {
                        result.diagnostics = "EDITOR-PROJECT-READ-PLAN-CONFLICT";
                        return false;
                    }
                    previous.maxBytes = std::min(previous.maxBytes, read.maxBytes);
                    return true;
                }
            if (result.reads.size() >= budget.maxFiles) {
                result.diagnostics = "EDITOR-PROJECT-READ-PLAN-BUDGET";
                return false;
            }
            result.reads.push_back(std::move(read));
            return true;
        };
        const auto append = [&](std::string_view relative, std::size_t bytes) {
            std::filesystem::path path;
            if (!ProjectInputSnapshot::validLogicalPath(relative)
                || !resource::resolveResourceSourcePath(root, relative, context.origin, path)) {
                result.diagnostics = "EDITOR-PROJECT-READ-PLAN-PATH";
                return false;
            }
            resource::ResourceFileReadIntent read;
            read.path = core::FilePath::utf8(path);
            read.pathScope = context.origin.pathScope;
            read.mount = context.origin.mount;
            read.maxBytes = std::min(bytes, budget.maxFileBytes);
            read.role = "editor-project-input/v1";
            return admit(std::move(read));
        };
        const auto merge = [&](resource::AssetPackageReadPlan&& plan) {
            if (!plan.success) {
                result.diagnostics = std::move(plan.diagnostics);
                return false;
            }
            for (auto& read : plan.reads)
                if (!admit(std::move(read)))
                    return false;
            return true;
        };
        if (!append("project.arp.yaml", metadataLimit))
            return fail(result.diagnostics.c_str());
        const auto manifestPath = core::FilePath::utf8(root / "project.arp.yaml");
        if (context.resources.sources->find(manifestPath)) {
            const auto text = resource::readResourceText(
                manifestPath, context.resources, metadataLimit, &context.origin);
            if (!text.success)
                return fail(text.diagnostics.c_str());
            ProjectManifest manifest;
            ProjectDiagnostic issue;
            if (!ProjectManifestCodec::decode(text.text, manifest, issue))
                return fail(issue.code);
            if (!manifest.editorMetadata.empty()) {
                if (!append(manifest.editorMetadata, metadataLimit))
                    return fail(result.diagnostics.c_str());
                const auto metadataPath =
                    core::FilePath::utf8(root / std::filesystem::u8path(manifest.editorMetadata));
                if (context.resources.sources->find(metadataPath)) {
                    const auto metadataText = resource::readResourceText(
                        metadataPath, context.resources, metadataLimit, &context.origin);
                    if (!metadataText.success)
                        return fail(metadataText.diagnostics.c_str());
                    ProjectMetadata metadata;
                    if (!ProjectMetadataCodec::decode(metadataText.text, metadata, issue))
                        return fail(issue.code);
                    const resource::AssetPackageReadBudget terrainLimits{metadataLimit,
                        resource::AssetPackageClosureService::maxResources,
                        budget.maxFiles,
                        budget.maxFileBytes,
                        budget.maxTotalBytes,
                        maxDecodedBytes};
                    for (const auto& preview : metadata.terrainPreviews)
                        if (!merge(planEditorTerrainPreviewSources(
                                root, preview, context, terrainLimits)))
                            return fail(result.diagnostics.c_str());
                }
            }
            for (const auto& module : manifest.scriptModules)
                if (!append(module.source, ScriptDocumentSession::maxScriptBytes))
                    return fail(result.diagnostics.c_str());
            const resource::AssetPackageReadBudget limits{metadataLimit,
                resource::AssetPackageClosureService::maxResources,
                budget.maxFiles,
                budget.maxFileBytes,
                budget.maxTotalBytes,
                maxDecodedBytes};
            for (const auto& package : manifest.assetPackages) {
                std::filesystem::path path;
                if (!ProjectInputSnapshot::validLogicalPath(package)
                    || !resource::resolveResourceSourcePath(root, package, context.origin, path))
                    return fail("EDITOR-PROJECT-READ-PLAN-PATH");
                if (!merge(resource::InternalAssetPackageReader().planSourceReads(
                        path, context, limits, codecs)))
                    return fail(result.diagnostics.c_str());
            }
        }
        const auto valid = resource::pendingResourceReads(
            result.reads, *context.resources.sources, budget.maxFileBytes);
        if (valid.status != resource::ResourceSourcePlanStatus::Valid)
            return fail("EDITOR-PROJECT-READ-PLAN-SOURCE-CONFLICT");
        result.success = true;
        return result;
    } catch (const std::bad_alloc&) {
        return fail("EDITOR-PROJECT-READ-PLAN-ALLOCATION");
    } catch (const std::exception&) {
        return fail("EDITOR-PROJECT-READ-PLAN-PREPARATION");
    }
}

} // namespace arp::editor
