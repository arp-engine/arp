#pragma once

#include "network/network_module.h"
#include "scriptvm/aot/script_value_memory_accountant.h"

namespace arp::scriptvm {

// Install on the host's DirectNative context/dispatcher. Native references
// retain their invalidation control, not their runtime-owned referents. Owned
// response/result Values expose the exact core allocations of their payloads.
inline bool describeNetworkScriptValue(
    void*, const Value& value, ScriptValueMemoryDescriptor& descriptor) noexcept
{
    descriptor.allocationIdentity = value.storageIdentity();
    descriptor.retainedBytes = 0;
    descriptor.externallyLeased = false;
    if (value.is_native_reference()) {
        const auto* type = value.class_();
        return type
            && (type == reflection::class_of<NetworkRuntime>()
                || type == reflection::class_of<HttpScriptServer>()
                || type == reflection::class_of<HttpScriptIncomingRequest>());
    }
    const auto quota = core::currentCpuMemoryQuota();
    const auto own = core::inspectCpuAllocationContaining(value.storageIdentity());
    auto include = [&](const void* address) {
        const auto allocation = core::inspectCpuAllocationContaining(address);
        if (allocation.found && allocation.allocationIdentity != own.allocationIdentity
            && (!quota || allocation.quota != quota.get()))
            descriptor.retainedBytes += allocation.sizeBytes;
    };
    if (value.is<NetworkResult>()) {
        const auto& result = *value.ptr<NetworkResult>();
        include(result.message.data());
        include(result.data.data());
        include(result.peer.data());
        include(result.headers.data());
        return true;
    }
    if (value.is<HttpScriptResponse>()) {
        const auto& response = *value.ptr<HttpScriptResponse>();
        include(response.reason.data());
        include(response.body.data());
        include(response.content_type.data());
        if (response.headers.size())
            include(&response.headers.at(0));
        const auto headerAllocation = response.headers.size()
            ? core::inspectCpuAllocationContaining(&response.headers.at(0))
            : core::CpuAllocationChargeInfo{};
        for (std::size_t i = 0; i < response.headers.size(); ++i) {
            const auto& header = response.headers.at(i);
            for (const auto text : {header.name(), header.value()}) {
                const auto allocation = core::inspectCpuAllocationContaining(text.data());
                if (allocation.found
                    && allocation.allocationIdentity != headerAllocation.allocationIdentity
                    && allocation.allocationIdentity != own.allocationIdentity
                    && (!quota || allocation.quota != quota.get()))
                    descriptor.retainedBytes += allocation.sizeBytes;
            }
        }
        return true;
    }
    return value.is<HttpScriptServerConfig>();
}

} // namespace arp::scriptvm
