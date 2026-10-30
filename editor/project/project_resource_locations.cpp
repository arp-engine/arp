#include "editor/project/project_resource_locations.h"

#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_manifest.h"
#include "resource/internal_asset_package.h"

#include <algorithm>
#include <new>

namespace arp::editor {
void ProjectResourceLocations::swap(ProjectResourceLocations& other) noexcept
{
    root_.swap(other.root_);
    owner_.swap(other.owner_);
    entries_.swap(other.entries_);
    inputs_.swap(other.inputs_);
    location_.swap(other.location_);
}

const char* ProjectResourceLocations::addDefinition(
    const resource::InternalAssetPackageEntry& member,
    core::pmr::String manifest,
    core::pmr::String envelope)
{
    if (entries_.size() >= budget.maxFiles)
        return "EDITOR-RESOURCE-LOCATION-BUDGET";
    for (const auto& entry : entries_)
        if (entry.id == member.resourceId || entry.envelopePath == envelope)
            return "EDITOR-RESOURCE-LOCATION-AMBIGUOUS";
    const auto* record = owner_->recordSnapshot(member.resourceId);
    if (!record || !record->metadataAdmitted || record->type != member.resourceType
        || record->alias != member.alias || record->status != member.status
        || record->reloadInProgress)
        return "EDITOR-RESOURCE-LOCATION-METADATA-CHANGED";
    Entry entry;
    entry.id = member.resourceId;
    entry.type = member.resourceType;
    entry.manifestPath = std::move(manifest);
    entry.envelopePath = std::move(envelope);
    entry.generation = record->generation;
    entry.contentHash = record->contentHash;
    entries_.push_back(std::move(entry));
    return nullptr;
}

const char* ProjectResourceLocations::capture(const std::filesystem::path& root,
    const core::pmr::Vector<core::pmr::String>& packages,
    core::SharedPtr<resource::ResourceManager> owner,
    ProjectResourceLocations& destination) noexcept
{
    try {
        if (!owner || packages.size() > ProjectManifestBudget{}.maxAssetPackages)
            return "EDITOR-RESOURCE-LOCATION-OWNER-OR-BUDGET";
        ProjectResourceLocations candidate;
        std::error_code error;
        candidate.root_ = std::filesystem::canonical(root, error);
        if (error || !std::filesystem::is_directory(candidate.root_, error) || error)
            return "EDITOR-RESOURCE-LOCATION-ROOT";
        candidate.owner_ = std::move(owner);
        ProjectInputSnapshot inputs(candidate.root_, budget);
        core::pmr::String diagnostic;
        for (const auto& directory : packages) {
            std::filesystem::path packagePath;
            ProjectDiagnostic pathDiagnostic;
            if (!ProjectManifestCodec::resolvePath(candidate.root_,
                    directory,
                    ProjectPathIntent::ExistingDirectory,
                    packagePath,
                    pathDiagnostic))
                return "EDITOR-RESOURCE-LOCATION-PACKAGE-PATH";
            if (packagePath
                != (candidate.root_ / std::filesystem::u8path(directory)).lexically_normal())
                return "EDITOR-RESOURCE-LOCATION-PATH-ALIAS";
            const auto manifest = packagePath / "manifest.arpkg.yaml";
            // Capture before parsing. Verify after all reads to reject mixed inputs.
            if (!inputs.read(manifest, diagnostic))
                return "EDITOR-RESOURCE-LOCATION-MANIFEST-READ-OR-BUDGET";
            const auto package = resource::InternalAssetPackageReader().read(
                packagePath, budget.maxFileBytes, budget.maxFiles);
            if (!package.success)
                return "EDITOR-RESOURCE-LOCATION-PACKAGE-INVALID-OR-BUDGET";
            for (const auto& member : package.package.entries) {
                const auto envelope = packagePath / std::filesystem::u8path(member.envelopePath);
                if (const auto* issue = candidate.addDefinition(member,
                        core::FilePath::utf8(manifest.lexically_relative(candidate.root_)),
                        core::FilePath::utf8(envelope.lexically_relative(candidate.root_))))
                    return issue;
                if (std::filesystem::canonical(manifest) != manifest
                    || std::filesystem::canonical(envelope) != envelope.lexically_normal())
                    return "EDITOR-RESOURCE-LOCATION-PATH-ALIAS";
                if (!inputs.recordDecodedText(envelope, member.envelopeYaml, diagnostic))
                    return "EDITOR-RESOURCE-LOCATION-ENVELOPE-READ-OR-BUDGET";
            }
        }
        if (!inputs.verify(diagnostic))
            return "EDITOR-RESOURCE-LOCATION-CONTENT-CHANGED";
        candidate.inputs_ = inputs.takeBindings();
        // All owned storage uses the same core resource; publication only swaps.
        destination.swap(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED";
    } catch (const std::filesystem::filesystem_error&) {
        return "EDITOR-RESOURCE-LOCATION-PATH";
    }
}

bool ProjectResourceLocations::contains(std::string_view id, std::string_view type) const noexcept
{
    return std::any_of(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry.id == id && entry.type == type;
    });
}

const char* ProjectResourceLocations::identify(const std::filesystem::path& envelope,
    const resource::ResourceManager& owner,
    Definition& destination) const noexcept
{
    if (location_)
        return "EDITOR-RESOURCE-LOCATION-MOUNTED-SOURCE-REQUIRED";
    try {
        if (!envelope.is_absolute() || envelope != envelope.lexically_normal()
            || !core::FilePath::contains(root_, envelope))
            return "EDITOR-RESOURCE-LOCATION-PATH";
        const auto relative = core::FilePath::utf8(envelope.lexically_relative(root_));
        if (!core::FilePath::isSafeRelativePath(relative, ProjectInputBudget::maxPathBytes))
            return "EDITOR-RESOURCE-LOCATION-PATH";
        const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
            return entry.envelopePath == relative;
        });
        if (found == entries_.end())
            return "EDITOR-RESOURCE-LOCATION-UNMAPPED";
        std::filesystem::path verified;
        if (const auto* issue = resolve(found->id, found->type, owner, verified))
            return issue;
        if (verified != envelope)
            return "EDITOR-RESOURCE-LOCATION-PATH-ALIAS";
        Definition candidate;
        candidate.resourceId = found->id;
        candidate.resourceType = found->type;
        destination.swap(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED";
    } catch (const std::filesystem::filesystem_error&) {
        return "EDITOR-RESOURCE-LOCATION-PATH";
    }
}

const char* ProjectResourceLocations::resolve(std::string_view id,
    std::string_view type,
    const resource::ResourceManager& owner,
    std::filesystem::path& destination) const noexcept
{
    if (location_)
        return "EDITOR-RESOURCE-LOCATION-MOUNTED-SOURCE-REQUIRED";
    try {
        if (owner_.get() != &owner)
            return "EDITOR-RESOURCE-LOCATION-OWNER-CHANGED";
        const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
            return entry.id == id;
        });
        if (found == entries_.end())
            return "EDITOR-RESOURCE-LOCATION-UNMAPPED";
        const auto* record = owner.recordSnapshot(id);
        if (found->type != type || !record || !record->metadataAdmitted || record->type != type
            || found->generation != record->generation || found->contentHash != record->contentHash
            || record->reloadInProgress)
            return "EDITOR-RESOURCE-LOCATION-METADATA-CHANGED";
        core::pmr::Vector<ProjectInputBinding> selected;
        for (const auto& binding : inputs_)
            if (binding.path == found->manifestPath || binding.path == found->envelopePath)
                selected.push_back(binding);
        core::pmr::String diagnostic;
        if (selected.size() != 2
            || !ProjectInputSnapshot::verify(root_, selected, diagnostic, budget))
            return "EDITOR-RESOURCE-LOCATION-CONTENT-CHANGED";
        auto path = root_ / std::filesystem::u8path(found->envelopePath);
        destination.swap(path);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED";
    } catch (const std::filesystem::filesystem_error&) {
        return "EDITOR-RESOURCE-LOCATION-PATH";
    }
}
} // namespace arp::editor
