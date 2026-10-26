#include "editor/workspace/editor_scene_publication_preparation.h"

#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "editor/project/project_scene_authoring.h"
#include "resource/asset_package_closure.h"
#include "resource/resource_contract.h"

namespace arp::editor {
namespace {
using Preparation = EditorScenePublicationPreparation;

bool samePackage(const Preparation::PackageSnapshot& lhs, const Preparation::PackageSnapshot& rhs)
{
    return lhs.directory == rhs.directory && lhs.resourceId == rhs.resourceId
        && lhs.contentHash == rhs.contentHash && lhs.fingerprint == rhs.fingerprint;
}

bool validSnapshot(const Preparation::PackageSnapshot& value)
{
    return resource::isResourceUuid(value.resourceId)
        && EditorSceneAuthoringCodec::isContentHash(value.contentHash)
        && EditorSceneAuthoringCodec::isContentHash(value.fingerprint)
        && !value.relativeDirectory.empty()
        && value.relativeDirectory.size() <= Preparation::maxRelativeDirectoryBytes;
}

bool manifestUnchanged(const Preparation::Request& request, core::pmr::String& diagnostic)
{
    std::filesystem::path path;
    ProjectDiagnostic issue;
    if (!ProjectManifestCodec::resolvePath(
            request.root, "project.arp.yaml", ProjectPathIntent::ExistingFile, path, issue)) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-MANIFEST-PATH";
        return false;
    }
    const auto current = core::readTextFile(path, EditorProjectResources::maxManifestBytes);
    if (!current.success || current.text != request.manifestSnapshot) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-MANIFEST-CONFLICT";
        return false;
    }
    return true;
}

bool packagesUnchanged(const Preparation::Request& request,
    const scene::SceneComponentFactory& factory,
    core::pmr::String& diagnostic)
{
    const auto check = [&](const Preparation::PackageSnapshot& expected) {
        Preparation::PackageSnapshot current;
        if (!Preparation::inspectPackage(request.root,
                expected.relativeDirectory,
                expected.resourceId,
                factory,
                current,
                diagnostic))
            return false;
        if (!samePackage(expected, current)) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-PACKAGE-CONFLICT";
            return false;
        }
        return true;
    };
    return check(request.target) && (!request.source || check(*request.source));
}
} // namespace

bool EditorScenePublicationPreparation::inspectPackage(const std::filesystem::path& root,
    std::string_view relativeDirectory,
    std::string_view resourceId,
    const scene::SceneComponentFactory& factory,
    PackageSnapshot& destination,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!resource::isResourceUuid(resourceId) || relativeDirectory.empty()
        || relativeDirectory.size() > maxRelativeDirectoryBytes) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-PACKAGE-IDENTITY-OR-BUDGET";
        return false;
    }
    try {
        PackageSnapshot candidate;
        ProjectDiagnostic pathDiagnostic;
        if (!ProjectManifestCodec::resolvePath(root,
                relativeDirectory,
                ProjectPathIntent::ExistingDirectory,
                candidate.directory,
                pathDiagnostic)) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-PACKAGE-PATH";
            return false;
        }
        const auto codecs = ProjectSceneAuthoring::packageCodecs(
            core::makeShared<scene::SceneComponentFactory>(factory), diagnostic);
        if (!codecs)
            return false;
        const auto closure =
            resource::AssetPackageClosureService(codecs).inspect(candidate.directory,
                {1u,
                    1u,
                    ProjectSceneAuthoring::maxSceneBytes,
                    ProjectSceneAuthoring::maxSceneBytes,
                    ProjectSceneAuthoring::maxSceneBytes});
        if (!closure.success) {
            diagnostic = closure.diagnostics;
            return false;
        }
        if (closure.package.entries.size() != 1u || closure.files.size() != 1u
            || closure.package.entries.front().resourceId != resourceId
            || closure.package.entries.front().resourceType != "SceneAssetResource"
            || !closure.package.entries.front().dependencies.empty()) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-SCENE-ONLY-PROFILE";
            return false;
        }
        const auto& entry = closure.package.entries.front();
        // These paths are contract metadata/bindings, never inferred filenames.
        const auto readBound =
            [&](std::string_view relative, std::size_t limit, core::pmr::String& value) {
                std::filesystem::path path;
                if (!ProjectManifestCodec::resolvePath(candidate.directory,
                        relative,
                        ProjectPathIntent::ExistingFile,
                        path,
                        pathDiagnostic))
                    return false;
                const auto read = core::readTextFile(path, limit);
                if (!read.success)
                    return false;
                value = read.text;
                return true;
            };
        core::pmr::String manifest, envelope, index, document;
        if (!readBound("manifest.arpkg.yaml",
                resource::AssetPackageClosureService::maxMetadataBytes,
                manifest)
            || !readBound(entry.envelopePath,
                resource::AssetPackageClosureService::maxMetadataBytes,
                envelope)
            || !readBound(closure.package.closurePath,
                resource::AssetPackageClosureService::maxMetadataBytes,
                index)
            || !readBound(
                closure.files.front().path, ProjectSceneAuthoring::maxSceneBytes, document)) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-PACKAGE-SNAPSHOT-READ";
            return false;
        }
        using Y = core::YamlContract;
        auto identity = Y::Node::mapping();
        identity["type"] = "arp.editor-scene-package-snapshot";
        identity["contractVersion"] = 1;
        identity["resourceId"] = core::pmr::String(resourceId);
        identity["manifestHash"] = core::Sha256::hexDigest(manifest);
        identity["envelopeHash"] = core::Sha256::hexDigest(envelope);
        identity["closureHash"] = core::Sha256::hexDigest(index);
        candidate.contentHash = core::Sha256::hexDigest(document);
        identity["documentHash"] = candidate.contentHash;
        candidate.fingerprint = core::Sha256::hexDigest(Y::serialize(identity));
        candidate.resourceId = resourceId;
        candidate.relativeDirectory = relativeDirectory;
        destination = std::move(candidate);
        diagnostic.clear();
        return true;
    } catch (...) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-PACKAGE-INSPECTION-FAILED";
        return false;
    }
}

bool EditorScenePublicationPreparation::captureDraft(const EditorSceneDocument& document,
    std::uint64_t documentEpoch,
    DraftSnapshot& destination,
    core::pmr::String& diagnostic)
{
    if (!documentEpoch) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-DOCUMENT-EPOCH";
        return false;
    }
    DraftSnapshot candidate;
    candidate.documentEpoch = documentEpoch;
    if (!EditorSceneAuthoringCodec::encode(document, candidate.yaml, diagnostic)
        || !EditorSceneAuthoringCodec::contentHash(document, candidate.contentHash, diagnostic))
        return false;
    destination = std::move(candidate);
    diagnostic.clear();
    return true;
}

core::UniquePtr<EditorScenePublicationPreparation::Candidate>
EditorScenePublicationPreparation::prepareResources(
    const Request& request, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if ((request.intent != Intent::Register && request.intent != Intent::Update)
        || !resource::isResourceUuid(request.projectId) || !validSnapshot(request.target)
        || (request.intent == Intent::Update) != bool(request.source)
        || (request.source
            && (!validSnapshot(*request.source)
                || request.source->resourceId != request.target.resourceId
                || request.source->directory == request.target.directory))
        || !request.draft.documentEpoch || request.draft.yaml.empty()
        || request.draft.yaml.size() > EditorSceneAuthoringCodec::maxDocumentBytes
        || !EditorSceneAuthoringCodec::isContentHash(request.draft.contentHash)
        || request.manifestSnapshot.empty()
        || request.manifestSnapshot.size() > EditorProjectResources::maxManifestBytes) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-REQUEST-INVALID";
        return {};
    }
    try {
        std::error_code error;
        if (std::filesystem::canonical(request.root, error) != request.root || error) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-ROOT-INVALID";
            return {};
        }
        if (!manifestUnchanged(request, diagnostic))
            return {};
        EditorProjectResources previous;
        const auto original = previous.loadManifest(request.root, request.manifestSnapshot);
        if (!original.success || !previous.manifest()) {
            diagnostic = original.diagnostics;
            return {};
        }
        if (previous.manifest()->id != request.projectId) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-PROJECT-IDENTITY";
            return {};
        }
        const auto factory = ProjectSceneAuthoring::factory(
            previous.manager(), previous.physicsWorld2D(), diagnostic);
        if (!factory || !packagesUnchanged(request, *factory, diagnostic))
            return {};
        const auto* existing = previous.manager().recordSnapshot(request.target.resourceId);
        auto manifest = *previous.manifest();
        if (request.intent == Intent::Register) {
            if (existing) {
                diagnostic = "EDITOR-SCENE-PUBLICATION-IDENTITY-CONFLICT";
                return {};
            }
        } else {
            std::filesystem::path source;
            if (!existing || existing->state != resource::ResourceState::Ready
                || existing->type != "SceneAssetResource"
                || !previous.packageRootForResource(request.target.resourceId, source)
                || source != request.source->directory) {
                diagnostic = "EDITOR-SCENE-PUBLICATION-SOURCE-NOT-REGISTERED";
                return {};
            }
        }
        // Reject aliases of every registered directory, not only equal strings.
        std::size_t sourceIndex = manifest.assetPackages.size();
        for (std::size_t i = 0; i < manifest.assetPackages.size(); ++i) {
            std::filesystem::path path;
            ProjectDiagnostic issue;
            if (!ProjectManifestCodec::resolvePath(request.root,
                    manifest.assetPackages[i],
                    ProjectPathIntent::ExistingDirectory,
                    path,
                    issue)) {
                diagnostic = "EDITOR-SCENE-PUBLICATION-REGISTERED-PATH";
                return {};
            }
            if (path == request.target.directory) {
                diagnostic = "EDITOR-SCENE-PUBLICATION-TARGET-ALREADY-REGISTERED";
                return {};
            }
            if (request.source && path == request.source->directory) {
                if (sourceIndex != manifest.assetPackages.size()) {
                    diagnostic = "EDITOR-SCENE-PUBLICATION-SOURCE-AMBIGUOUS";
                    return {};
                }
                sourceIndex = i;
            }
        }
        if (request.source) {
            if (sourceIndex == manifest.assetPackages.size()) {
                diagnostic = "EDITOR-SCENE-PUBLICATION-SOURCE-NOT-REGISTERED";
                return {};
            }
            manifest.assetPackages[sourceIndex] = request.target.relativeDirectory;
        } else {
            manifest.assetPackages.push_back(request.target.relativeDirectory);
        }
        auto candidate = core::makeUnique<Candidate>();
        candidate->submitted = request;
        ProjectDiagnostic issue;
        if (!ProjectManifestCodec::encode(manifest, candidate->manifestYaml, issue)) {
            diagnostic = issue.code;
            return {};
        }
        const auto loaded =
            candidate->resources.loadManifest(request.root, candidate->manifestYaml);
        if (!loaded.success) {
            diagnostic = loaded.diagnostics;
            return {};
        }
        const auto scene = candidate->resources.manager().find<scene::SceneAssetResource>(
            request.target.resourceId);
        if (!scene.ready()
            || core::Sha256::hexDigest(scene.get()->yaml()) != request.target.contentHash) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-CANDIDATE-CONTENT";
            return {};
        }
        if (!manifestUnchanged(request, diagnostic)
            || !packagesUnchanged(request, *factory, diagnostic))
            return {};
        return candidate;
    } catch (...) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-RESOURCE-PREPARATION-FAILED";
        return {};
    }
}

bool EditorScenePublicationPreparation::prepareDocument(
    Candidate& candidate, core::pmr::String& diagnostic)
{
    if (candidate.document) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-DOCUMENT-ALREADY-PREPARED";
        return false;
    }
    try {
        auto document = core::makeUnique<EditorSceneDocument>(false);
        auto* manager = &candidate.resources.manager();
        if (!candidate.resources.bindSceneServices(*document, diagnostic))
            return false;
        core::pmr::String hash, yaml;
        if (!EditorSceneAuthoringCodec::decode(candidate.submitted.draft.yaml,
                *document,
                &EditorSceneAuthoringCodec::isResourceReady,
                manager,
                diagnostic)
            || !EditorSceneAuthoringCodec::contentHash(*document, hash, diagnostic)
            || !EditorSceneAuthoringCodec::encode(*document, yaml, diagnostic))
            return false;
        if (document->runtimeSceneId() != candidate.submitted.target.resourceId
            || hash != candidate.submitted.draft.contentHash
            || yaml != candidate.submitted.draft.yaml) {
            diagnostic = "EDITOR-SCENE-PUBLICATION-DRAFT-MISMATCH";
            return false;
        }
        candidate.document = std::move(document);
        diagnostic.clear();
        return true;
    } catch (...) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-DOCUMENT-PREPARATION-FAILED";
        return false;
    }
}

bool EditorScenePublicationPreparation::validateCurrent(Candidate& candidate,
    const std::filesystem::path& currentRoot,
    std::string_view currentProjectId,
    const DraftSnapshot& currentDraft,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    std::error_code error;
    if (!candidate.document || currentProjectId != candidate.submitted.projectId
        || currentDraft.documentEpoch != candidate.submitted.draft.documentEpoch
        || currentDraft.contentHash != candidate.submitted.draft.contentHash
        || currentDraft.yaml != candidate.submitted.draft.yaml
        || std::filesystem::canonical(currentRoot, error) != candidate.submitted.root || error) {
        diagnostic = "EDITOR-SCENE-PUBLICATION-CANDIDATE-STALE";
        return false;
    }
    const auto factory = ProjectSceneAuthoring::factory(
        candidate.resources.manager(), candidate.resources.physicsWorld2D(), diagnostic);
    return factory && manifestUnchanged(candidate.submitted, diagnostic)
        && packagesUnchanged(candidate.submitted, *factory, diagnostic);
}

} // namespace arp::editor
