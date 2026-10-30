#pragma once
#include "core/io/file_io.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/text_decoder.h"
#include "resource/resource_manager.h"

namespace arp::resource {
enum class ResourceSourcePlanStatus { Valid, InvalidRequest, Ambiguous, Conflict };

struct ResourceSourcePlanValidation {
    ResourceSourcePlanStatus status = ResourceSourcePlanStatus::Valid;
    core::io::FileReadValidation request;
};

// Shared by graph and package acquisition. Repeated constraints on a cached
// source must agree; batch-local duplicates are ambiguous, never a retry policy.
inline ResourceSourcePlanValidation pendingResourceReads(
    core::pmr::Vector<ResourceFileReadIntent>& reads,
    const ResourceSourceSet& sources,
    std::size_t inheritedBudget)
{
    for (std::size_t i = 0; i < reads.size(); ++i) {
        const auto& intent = reads[i];
        core::io::FileReadRequest request;
        request.contractVersion = 1;
        request.path = intent.path;
        request.pathScope = intent.pathScope;
        request.mount = intent.mount;
        request.expectedVersion = intent.expectedVersion;
        request.maxVersionBytes = intent.maxVersionBytes;
        request.maxBytes = intent.maxBytes ? intent.maxBytes : inheritedBudget;
        const auto valid = core::io::FileReadContract::validate(request);
        if (valid.status != core::io::FileIoStatus::Success)
            return {ResourceSourcePlanStatus::InvalidRequest, valid};
        for (std::size_t j = 0; j < i; ++j)
            if (reads[j].path == intent.path)
                return {ResourceSourcePlanStatus::Ambiguous, {}};
    }
    for (auto intent = reads.begin(); intent != reads.end();) {
        const auto* source = sources.find(intent->path);
        if (!source) {
            ++intent;
            continue;
        }
        if (source->pathScope != intent->pathScope || !(source->mount == intent->mount)
            || (intent->maxBytes && source->bytes.size() > intent->maxBytes)
            || (intent->expectedVersion.kind != core::io::FileVersionKind::None
                && source->version != intent->expectedVersion))
            return {ResourceSourcePlanStatus::Conflict, {}};
        intent = reads.erase(intent);
    }
    return {};
}

// Lexical resource binding, not an OS containment check. Rooted backends own
// physical containment; acquired-source consumers must never reopen host paths.
inline bool resolveResourceSourcePath(const std::filesystem::path& root,
    core::StringView relative,
    ResourceFileOrigin origin,
    std::filesystem::path& output)
{
    if (relative.find('\0') != core::StringView::npos
        || !core::FilePath::isSafeRelativePath(relative))
        return false;
    if (origin.pathScope != core::io::FilePathScope::HostFilesystem
        && origin.pathScope != core::io::FilePathScope::MountedResource)
        return false;
    if (origin.pathScope == core::io::FilePathScope::MountedResource) {
        const auto rootText = core::FilePath::utf8(root);
        if (!origin.mount || (!rootText.empty() && !core::FilePath::isSafeRelativePath(rootText)))
            return false;
    } else if (origin.mount)
        return false;
    output = root / std::filesystem::u8path(relative.begin(), relative.end());
    return true;
}

// Shared bounded source boundary. Async loaders must provide every source;
// synchronous preparation may explicitly allow host IO through the context.
inline core::BinaryFileReadResult readResourceSource(const core::pmr::String& path,
    const ResourceLoadContext& context,
    std::size_t budget,
    bool allowEmpty = false,
    const ResourceFileOrigin* origin = nullptr)
{
    if (context.sources)
        if (auto* source = context.sources->find(path)) {
            core::BinaryFileReadResult result;
            if (origin
                && (source->pathScope != origin->pathScope || !(source->mount == origin->mount))) {
                result.diagnostics = "ARP-RESOURCE-SOURCE-LOCATION-MISMATCH";
                return result;
            }
            if ((!allowEmpty && source->bytes.empty()) || source->bytes.size() > budget) {
                result.diagnostics = "ARP-RESOURCE-SOURCE-BUDGET";
                return result;
            }
            result.success = true;
            result.bytes = source->bytes;
            return result;
        }
    if (context.requirePreloadedSources
        || (origin
            && (origin->pathScope != core::io::FilePathScope::HostFilesystem || origin->mount))) {
        core::BinaryFileReadResult result;
        result.diagnostics = "ARP-RESOURCE-SOURCE-NOT-PRELOADED";
        return result;
    }
    return core::readBinaryFile(std::filesystem::u8path(path.begin(), path.end()), budget);
}

// Domain YAML acquisition keeps raw bytes in ResourceSourceSet for identity.
// Decoding has an explicit UTF-8/BOM/NUL policy and preserves newline bytes.
inline core::TextFileReadResult readResourceText(const core::pmr::String& path,
    const ResourceLoadContext& context,
    std::size_t budget,
    const ResourceFileOrigin* origin = nullptr)
{
    auto source = readResourceSource(path, context, budget, false, origin);
    core::TextFileReadResult result;
    if (!source.success) {
        result.diagnostics = std::move(source.diagnostics);
        return result;
    }
    core::TextDecodeDesc desc;
    desc.bom = core::TextBomPolicy::Strip;
    desc.nul = core::TextNulPolicy::Reject;
    desc.maxOutputBytes = budget;
    auto decoded = core::TextDecoder::decode(
        {reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()}, desc);
    result.success = decoded.status == core::TextDecodeStatus::Success;
    result.text = std::move(decoded.text);
    if (!result.success)
        result.diagnostics = decoded.diagnosticId;
    return result;
}

inline ResourceReference authoredResourceReference(core::pmr::String id, const char* type)
{
    return {std::move(id), core::pmr::String(type), {}, "Contract Complete", {}};
}

inline ResourceLoadResult resourceLoadFailure(const ResourceBase& resource, const char* diagnostic)
{
    ResourceLoadResult result;
    result.resourceId = resource.reference().id;
    result.resourceType = resource.reference().type;
    result.success = false;
    result.state = ResourceState::Failed;
    result.diagnostics = diagnostic;
    return result;
}

inline bool appendResourceReadIntent(const core::pmr::String& path,
    std::size_t budget,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic,
    ResourceFileOrigin origin = {})
{
    if (path.empty() || !budget || budget == SIZE_MAX
        || (origin.pathScope != core::io::FilePathScope::HostFilesystem
            && origin.pathScope != core::io::FilePathScope::MountedResource)
        || (origin.pathScope == core::io::FilePathScope::HostFilesystem && origin.mount)
        || (origin.pathScope == core::io::FilePathScope::MountedResource
            && (!origin.mount || !core::FilePath::isSafeRelativePath(path, path.size())))) {
        diagnostic = "ARP-RESOURCE-SOURCE-CONFIG";
        return false;
    }
    core::io::FileReadRequest request;
    request.contractVersion = 1;
    request.path = path;
    request.pathScope = origin.pathScope;
    request.mount = origin.mount;
    request.maxBytes = budget;
    const auto validation = core::io::FileReadContract::validate(request);
    if (validation.status != core::io::FileIoStatus::Success) {
        diagnostic = validation.diagnostic;
        return false;
    }
    ResourceFileReadIntent intent;
    intent.path = path;
    intent.role = "yaml-resource";
    intent.maxBytes = budget;
    intent.pathScope = origin.pathScope;
    intent.mount = origin.mount;
    intents.push_back(std::move(intent));
    return true;
}
} // namespace arp::resource
