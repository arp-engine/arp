#pragma once

#include "reflection/property.h"
#include "resource/resource_manager.h"

namespace arp::resource {
// Resources retain ownership. Persistence stores stable UUIDs and validates
// the declared type against the resource manager during candidate preparation.
class SerializationReferenceResolver final {
public:
    static bool validate(const ResourceManager* resources,
        const reflection::ReferenceMetadata& metadata,
        const core::pmr::String& id,
        core::pmr::String& diagnostic)
    {
        if (id.empty() && !metadata.required)
            return true;
        if (isResourceUuid(id)) {
            // Saving unloaded authoring documents checks identity; loading
            // requires an injected resource owner and a ready typed resource.
            if (!resources)
                return true;
            const auto handle = resources->find<ResourceBase>(id);
            if (handle.ready() && handle.get()->reference().type == metadata.targetType)
                return true;
        }
        diagnostic = "ARP-RESOURCE-REFERENCE-UUID-TYPE-OR-READINESS";
        return false;
    }
};
} // namespace arp::resource
