#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_manifest.h"
#include "editor/project/project_package_snapshot.h"
#include "editor/project/project_resource_locations.h"
#include "editor/project/project_resource_sources.h"

#include <algorithm>

namespace arp::editor {

const char* ProjectResourceLocations::captureSources(const ProjectResourceSources& sources,
    core::io::FileService& files,
    std::uint64_t epoch,
    const core::pmr::Vector<core::pmr::String>& packages,
    core::SharedPtr<resource::ResourceManager> owner,
    ProjectResourceLocations& destination) noexcept
{
    try {
        const char* issue = nullptr;
        if (!sources.validate(files, epoch, issue))
            return issue;
        if (!owner || packages.size() > ProjectManifestBudget{}.maxAssetPackages)
            return "EDITOR-RESOURCE-LOCATION-OWNER-OR-BUDGET";
        ProjectResourceLocations candidate;
        candidate.root_ = sources.root();
        candidate.owner_ = std::move(owner);
        candidate.location_ =
            core::makeUnique<ProjectSourceLocation>(sources.snapshot().location());
        const auto context = sources.context(ProjectPackageSnapshot::maxPackageBytes);
        ProjectInputSnapshot identities({}, budget);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        for (const auto& directory : packages) {
            std::filesystem::path path;
            if (!sources.path(directory, path))
                return "EDITOR-RESOURCE-LOCATION-PACKAGE-PATH";
            const auto manifest = directory + "/manifest.arpkg.yaml";
            const auto* rawManifest = sources.find(manifest);
            if (!rawManifest
                || !identities.recordLogical(manifest,
                    {reinterpret_cast<const char*>(rawManifest->bytes.data()),
                        rawManifest->bytes.size()},
                    diagnostic))
                return "EDITOR-RESOURCE-LOCATION-MANIFEST-READ-OR-BUDGET";
            const auto package = resource::InternalAssetPackageReader().readSources(
                path, context, budget.maxFileBytes, budget.maxFiles);
            if (!package.success)
                return "EDITOR-RESOURCE-LOCATION-PACKAGE-INVALID-OR-BUDGET";
            for (const auto& member : package.package.entries) {
                const auto envelope = directory + "/" + member.envelopePath;
                if (const auto* error = candidate.addDefinition(member, manifest, envelope))
                    return error;
                const auto* raw = sources.find(envelope);
                if (!raw
                    || !identities.recordLogical(envelope,
                        {reinterpret_cast<const char*>(raw->bytes.data()), raw->bytes.size()},
                        diagnostic))
                    return "EDITOR-RESOURCE-LOCATION-ENVELOPE-READ-OR-BUDGET";
            }
        }
        if (!sources.validate(files, epoch, issue))
            return issue;
        candidate.inputs_ = identities.takeBindings();
        destination.swap(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED";
    } catch (...) {
        return "EDITOR-RESOURCE-LOCATION-PATH";
    }
}

const char* ProjectResourceLocations::resolveSource(std::string_view id,
    std::string_view type,
    const resource::ResourceManager& owner,
    const ProjectResourceSources& sources,
    core::io::FileService& files,
    std::uint64_t epoch,
    core::io::FileReadRequest& destination) const noexcept
{
    try {
        const char* issue = nullptr;
        if (!sources.validate(files, epoch, issue))
            return issue;
        if (!location_ || !(location_->mount == sources.snapshot().location().mount)
            || location_->root != sources.snapshot().location().root || location_->epoch != epoch)
            return "EDITOR-RESOURCE-LOCATION-SOURCE-ORIGIN";
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
        std::size_t selected = 0;
        for (const auto& input : inputs_) {
            if (input.path != found->manifestPath && input.path != found->envelopePath)
                continue;
            ++selected;
            const auto& fresh = sources.snapshot().identities();
            const auto match = std::find_if(fresh.begin(), fresh.end(), [&](const auto& item) {
                return item.path == input.path;
            });
            if (match == fresh.end() || match->byteCount != input.byteCount
                || match->fingerprint != input.fingerprint)
                return "EDITOR-RESOURCE-LOCATION-CONTENT-CHANGED";
        }
        if (selected != 2)
            return "EDITOR-RESOURCE-LOCATION-CONTENT-CHANGED";
        const auto* source = sources.snapshot().find(found->envelopePath);
        if (!source)
            return "EDITOR-RESOURCE-LOCATION-CONTENT-CHANGED";
        auto candidate = source->binding;
        candidate.expectedVersion.kind = core::io::FileVersionKind::ContentSha256;
        const auto& bytes = source->receipt.bytes;
        const auto digest =
            core::Sha256::digest({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        std::copy(digest.begin(), digest.end(), candidate.expectedVersion.digest.begin());
        candidate.maxVersionBytes = candidate.maxBytes;
        if (!sources.validate(files, epoch, issue))
            return issue;
        destination = std::move(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED";
    } catch (...) {
        return "EDITOR-RESOURCE-LOCATION-PATH";
    }
}

const char* ProjectResourceLocations::identifySource(const core::io::FileReadRequest& file,
    const resource::ResourceManager& owner,
    const ProjectResourceSources& sources,
    core::io::FileService& files,
    std::uint64_t epoch,
    Definition& destination) const noexcept
{
    try {
        const auto valid = core::io::FileReadContract::validate(file);
        if (valid.status != core::io::FileIoStatus::Success || file.contractVersion != 1
            || file.range.mode != core::io::FileReadRangeMode::WholeFile
            || file.partialPolicy != core::io::FilePartialReadPolicy::DenyPartial)
            return "EDITOR-RESOURCE-LOCATION-PATH";
        for (const auto& entry : entries_) {
            std::filesystem::path path;
            if (!sources.path(entry.envelopePath, path) || core::FilePath::utf8(path) != file.path)
                continue;
            core::io::FileReadRequest verified;
            if (const auto* issue =
                    resolveSource(entry.id, entry.type, owner, sources, files, epoch, verified))
                return issue;
            if (file.pathScope != verified.pathScope || !(file.mount == verified.mount)
                || (file.expectedVersion.kind != core::io::FileVersionKind::None
                    && file.expectedVersion != verified.expectedVersion))
                return "EDITOR-RESOURCE-LOCATION-SOURCE-ORIGIN";
            Definition candidate;
            candidate.resourceId = entry.id;
            candidate.resourceType = entry.type;
            destination.swap(candidate);
            return nullptr;
        }
        return "EDITOR-RESOURCE-LOCATION-UNMAPPED";
    } catch (...) {
        return "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED";
    }
}

} // namespace arp::editor
