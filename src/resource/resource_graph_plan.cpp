#include "resource/resource_graph_plan.h"

namespace arp::resource {

namespace {

core::pmr::String text(const char* value)
{
    return core::pmr::String(value, &core::defaultCpuMemoryResource());
}

ResourceGraphPlanResult failure(
    const ResourceGraphRequest& request, const char* message, NameView id = {}, NameView type = {})
{
    ResourceGraphPlanResult result;
    result.resourceId = id.empty() ? request.rootResourceId
                                   : core::pmr::String(id, &core::defaultCpuMemoryResource());
    result.resourceType = core::pmr::String(type, &core::defaultCpuMemoryResource());
    result.diagnostics = text(message);
    if (!id.empty() && result.diagnostics.back() == ' ') {
        result.diagnostics += id;
    }
    return result;
}

} // namespace

ResourceGraphPlanResult buildResourceGraphPlan(const ResourceGraphRequest& request)
{
    if (request.rootResourceId.empty()) {
        return failure(request, "resource graph requires rootResourceId");
    }
    if (!isResourceUuid(request.rootResourceId)) {
        return failure(request, "resource graph root id must be UUID: ", request.rootResourceId);
    }
    if (request.nodes.empty()) {
        return failure(request, "resource graph requires at least one node");
    }

    core::pmr::UnorderedMap<core::pmr::String, std::size_t> indexById{
        &core::defaultCpuMemoryResource()};
    core::pmr::UnorderedMap<core::pmr::String, core::pmr::String> aliasOwners{
        &core::defaultCpuMemoryResource()};
    bool rootFound = false;
    for (std::size_t index = 0; index < request.nodes.size(); ++index) {
        const auto& node = request.nodes[index];
        if (!node.resource || !node.resource->reference().valid()) {
            return failure(request, "resource graph node has invalid ResourceBase descriptor");
        }
        const auto& reference = node.resource->reference();
        if (!isResourceUuid(reference.id)) {
            return failure(
                request, "resource graph node id must be UUID: ", reference.id, reference.type);
        }
        if (reference.status == "Legacy fallback") {
            return failure(request,
                "resource graph rejects legacy dependency status: ",
                reference.id,
                reference.type);
        }
        if (indexById.find(reference.id) != indexById.end()) {
            return failure(
                request, "resource graph duplicate resource id: ", reference.id, reference.type);
        }
        indexById[reference.id] = index;
        if (!reference.alias.empty()) {
            if (aliasOwners.find(reference.alias) != aliasOwners.end()) {
                ResourceGraphPlanResult result =
                    failure(request, "resource graph duplicate alias: ");
                result.resourceId = reference.id;
                result.resourceType = reference.type;
                result.diagnostics += reference.alias;
                return result;
            }
            aliasOwners[reference.alias] = reference.id;
        }
        rootFound = rootFound || reference.id == request.rootResourceId;
    }
    if (!rootFound) {
        return failure(request, "resource graph root node is missing: ", request.rootResourceId);
    }

    for (const auto& node : request.nodes) {
        for (const auto& dependency : node.dependencies) {
            if (!isResourceUuid(dependency.id)) {
                return failure(request,
                    "resource graph dependency id must be UUID: ",
                    dependency.id,
                    node.resource->reference().type);
            }
            if (indexById.find(dependency.id) == indexById.end()) {
                return failure(request,
                    "resource graph missing dependency node: ",
                    dependency.id,
                    node.resource->reference().type);
            }
        }
    }

    struct Frame {
        std::size_t index = 0u;
        std::size_t nextDependency = 0u;
    };

    core::pmr::Vector<std::uint8_t> states(
        request.nodes.size(), 0u, &core::defaultCpuMemoryResource());
    core::pmr::Vector<Frame> stack{&core::defaultCpuMemoryResource()};
    ResourceGraphPlanResult result;
    for (std::size_t start = 0; start < request.nodes.size(); ++start) {
        if (states[start] != 0u) {
            continue;
        }
        stack.push_back({start, 0u});
        states[start] = 1u;
        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto& dependencies = request.nodes[frame.index].dependencies;
            if (frame.nextDependency >= dependencies.size()) {
                states[frame.index] = 2u;
                result.order.push_back(frame.index);
                stack.pop_back();
                continue;
            }
            const auto& dependency = dependencies[frame.nextDependency++];
            const auto dependencyIndex = indexById.find(dependency.id)->second;
            if (states[dependencyIndex] == 1u) {
                return failure(request,
                    "resource graph dependency cycle detected at: ",
                    dependency.id,
                    request.nodes[frame.index].resource->reference().type);
            }
            if (states[dependencyIndex] == 0u) {
                states[dependencyIndex] = 1u;
                stack.push_back({dependencyIndex, 0u});
            }
        }
    }
    result.success = true;
    result.resourceId = request.rootResourceId;
    return result;
}

} // namespace arp::resource
