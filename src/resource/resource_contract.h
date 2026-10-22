#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace arp::resource {

using ResourceContractNameView = std::string_view;

enum class ResourceStatus {
    Invalid,
    ContractComplete,
    PrototypeComplete,
    BackendValidated,
    PlatformValidated,
    ProductionReady,
    LegacyFallback,
};

struct ResourceValidationResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct ResourceStatusParseResult : ResourceValidationResult {
    ResourceStatus status = ResourceStatus::Invalid;
};

struct ResourceContractDescriptor {
    ResourceContractNameView type;
    ResourceContractNameView contractVersion;
};

struct ResourceContractRedirect {
    ResourceContractDescriptor source;
    ResourceContractDescriptor target;
    ResourceStatus status = ResourceStatus::Invalid;
};

ARP_API ResourceContractNameView resourceStatusName(ResourceStatus status) noexcept;
ARP_API ResourceStatusParseResult parseResourceStatus(ResourceContractNameView text);
ARP_API ResourceValidationResult validateResourceStatus(ResourceContractNameView text,
    std::initializer_list<ResourceStatus> allowed,
    ResourceContractNameView subject);
ARP_API ResourceValidationResult validateResourceStatusPromotion(
    ResourceStatus from, ResourceStatus to);
ARP_API ResourceValidationResult validateResourceContract(ResourceContractNameView actualType,
    ResourceContractNameView actualContractVersion,
    const ResourceContractDescriptor& expected);
ARP_API ResourceValidationResult validateResourceDependencyUsage(
    ResourceContractNameView actualUsage, ResourceContractNameView expectedUsage);
ARP_API ResourceValidationResult validateResourceContractRedirect(
    const ResourceContractRedirect& redirect);

ARP_API core::pmr::String deriveResourceUuid(std::string_view seed, std::string_view role);
ARP_API core::pmr::String deriveResourceUuid(
    std::string_view seed, std::string_view role, std::uint64_t ordinal);

} // namespace arp::resource
