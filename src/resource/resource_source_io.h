#pragma once
#include "core/io/file_io.h"
#include "resource/resource_manager.h"

namespace arp::resource {
// Shared bounded source boundary. Async loaders must provide every source;
// synchronous preparation may explicitly allow host IO through the context.
inline core::BinaryFileReadResult readResourceSource(const core::pmr::String& path,
    const ResourceLoadContext& context,
    std::size_t budget,
    bool allowEmpty = false)
{
    if (context.sources)
        if (auto* source = context.sources->find(path)) {
            core::BinaryFileReadResult result;
            if ((!allowEmpty && source->bytes.empty()) || source->bytes.size() > budget) {
                result.diagnostics = "ARP-RESOURCE-SOURCE-BUDGET";
                return result;
            }
            result.success = true;
            result.bytes = source->bytes;
            return result;
        }
    if (context.requirePreloadedSources) {
        core::BinaryFileReadResult result;
        result.diagnostics = "ARP-RESOURCE-SOURCE-NOT-PRELOADED";
        return result;
    }
    return core::readBinaryFile(std::filesystem::path(path.c_str()), budget);
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
    core::pmr::String& diagnostic)
{
    if (path.empty() || !budget) {
        diagnostic = "ARP-RESOURCE-SOURCE-CONFIG";
        return false;
    }
    ResourceFileReadIntent intent;
    intent.path = path;
    intent.role = "yaml-resource";
    intent.maxBytes = budget;
    intent.pathScope = core::io::FilePathScope::HostFilesystem;
    intents.push_back(std::move(intent));
    return true;
}
} // namespace arp::resource
