#include "rhi/backends/webgpu/webgpu_query_pool.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace arp::rhi_webgpu {

WebGpuRhiQueryPoolPayload::WebGpuRhiQueryPoolPayload(
#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    WGPUQuerySet querySet,
#endif
    core::pmr::Vector<std::uint64_t> scopeIds,
    std::uint32_t count)
#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    : querySet_(querySet), scopeIds_(std::move(scopeIds))
#else
    : scopeIds_(std::move(scopeIds))
#endif
{
    states_.assign(count, rhi::QueryExecutionState::NotRecorded);
}

WebGpuRhiQueryPoolPayload::~WebGpuRhiQueryPoolPayload()
{
#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    if (querySet_) {
        if (!webGpuNativeDeviceLost()) {
            wgpuQuerySetDestroy(querySet_);
            wgpuQuerySetRelease(querySet_);
        }
        querySet_ = nullptr;
    }
#endif
}

WebGpuRhiQueryPool::WebGpuRhiQueryPool(
    std::shared_ptr<WebGpuRhiQueryPoolPayload> payload, rhi::QueryPoolDesc desc)
    : payload_(std::move(payload)), desc_(std::move(desc))
{}

rhi::QueryExecutionStatus WebGpuRhiQueryPool::executionStatus() const
{
    return payload_ ? payload_->executionStatus() : rhi::QueryExecutionStatus{};
}

rhi::CommandValidationResult WebGpuRhiQueryPool::reset()
{
    return payload_
        ? payload_->reset()
        : rhi::CommandValidationResult{false, "RHI-WEBGPU-QUERY-POOL-PAYLOAD-MISSING", 0u};
}

bool WebGpuRhiQueryPoolPayload::markRecorded(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size()) {
        diagnostics = "RHI-WEBGPU-QUERY-INDEX-OUT-OF-RANGE";
        return false;
    }
    if (states_[queryIndex] != rhi::QueryExecutionState::NotRecorded) {
        diagnostics = "RHI-WEBGPU-QUERY-MUST-BE-RESET-FIRST";
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::Recorded;
    return true;
}

bool WebGpuRhiQueryPoolPayload::markSubmitted(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size() || states_[queryIndex] != rhi::QueryExecutionState::Recorded) {
        diagnostics = "RHI-WEBGPU-QUERY-SUBMISSION-STATE-INVALID";
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::Pending;
    return true;
}

bool WebGpuRhiQueryPoolPayload::rollbackSubmitted(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size() || states_[queryIndex] != rhi::QueryExecutionState::Pending) {
        diagnostics = "RHI-WEBGPU-QUERY-ROLLBACK-STATE-INVALID";
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::Recorded;
    return true;
}

bool WebGpuRhiQueryPoolPayload::rollbackRecorded(const std::uint32_t queryIndex)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size() || states_[queryIndex] != rhi::QueryExecutionState::Recorded) {
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::NotRecorded;
    return true;
}

void WebGpuRhiQueryPoolPayload::markAvailable(const std::uint32_t queryIndex)
{
    std::lock_guard lock(mutex_);
    if (queryIndex < states_.size() && states_[queryIndex] == rhi::QueryExecutionState::Pending) {
        states_[queryIndex] = rhi::QueryExecutionState::Available;
    }
}

void WebGpuRhiQueryPoolPayload::markDiagnosticFailure(const std::uint32_t queryIndex)
{
    std::lock_guard lock(mutex_);
    if (queryIndex < states_.size()) {
        states_[queryIndex] = rhi::QueryExecutionState::DiagnosticFailure;
    }
}

rhi::CommandValidationResult WebGpuRhiQueryPoolPayload::reset()
{
    std::lock_guard lock(mutex_);
    for (const auto state : states_) {
        if (state == rhi::QueryExecutionState::Recorded
            || state == rhi::QueryExecutionState::Pending) {
            return {
                false,
                "RHI-WEBGPU-QUERY-POOL-RESET-WAITING-FOR-COMPLETION",
                0,
            };
        }
    }
    std::fill(states_.begin(), states_.end(), rhi::QueryExecutionState::NotRecorded);
    return {true, "", 0};
}

rhi::QueryExecutionStatus WebGpuRhiQueryPoolPayload::executionStatus() const
{
    rhi::QueryExecutionStatus status;
    status.type = rhi::QueryType::Timestamp;
    status.count = static_cast<std::uint32_t>(states_.size());
#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    status.creationSupported = querySet_ != nullptr;
#else
    status.creationSupported = false;
#endif
    status.resetSupported = status.creationSupported;
    status.asynchronousReadbackSupported = status.creationSupported;
    status.completionGatedLifetimeRequired = status.creationSupported;
    if (!status.creationSupported) {
        status.state = rhi::QueryExecutionState::Unsupported;
        status.diagnostics = "RHI-WEBGPU-QUERY-POOL-NATIVE-PAYLOAD-MISSING";
        return status;
    }
    std::lock_guard lock(mutex_);
    bool hasDiagnosticFailure = false;
    bool hasPending = false;
    bool hasRecorded = false;
    bool hasAvailable = false;
    for (const auto state : states_) {
        hasDiagnosticFailure =
            hasDiagnosticFailure || state == rhi::QueryExecutionState::DiagnosticFailure;
        hasPending = hasPending || state == rhi::QueryExecutionState::Pending;
        hasRecorded = hasRecorded || state == rhi::QueryExecutionState::Recorded;
        hasAvailable = hasAvailable || state == rhi::QueryExecutionState::Available;
    }
    if (hasDiagnosticFailure) {
        status.state = rhi::QueryExecutionState::DiagnosticFailure;
        status.diagnostics = "RHI-WEBGPU-QUERY-POOL-READBACK-FAILED";
    } else if (hasPending) {
        status.state = rhi::QueryExecutionState::Pending;
        status.diagnostics = "RHI-WEBGPU-QUERY-POOL-WAITING-FOR-COMPLETION";
    } else if (hasRecorded) {
        status.state = rhi::QueryExecutionState::Recorded;
        status.diagnostics = "RHI-WEBGPU-QUERY-POOL-RECORDED-BEFORE-SUBMISSION";
    } else if (hasAvailable) {
        status.state = rhi::QueryExecutionState::Available;
        status.diagnostics = "RHI-WEBGPU-QUERY-POOL-READBACK-AVAILABLE";
    } else {
        status.state = rhi::QueryExecutionState::NotRecorded;
        status.diagnostics = "RHI-WEBGPU-QUERY-POOL-NOT-RECORDED";
    }
    return status;
}

arp::core::UniquePtr<rhi::QueryPool> createWebGpuRhiQueryPool(const rhi::QueryPoolDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
{
    if (!rhi::validateQueryPoolDesc(desc).success || desc.type != rhi::QueryType::Timestamp
        || !rhi::semantics::hasExecutableOptionalNativeFeature(
            capabilityContract, rhi::semantics::NativeBackendFeature::TimestampQueries)) {
        return {};
    }

#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    if (!webGpuNativeTimestampQuerySupported()) {
        return {};
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        return {};
    }
    auto nativeDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    nativeDesc.label = {desc.label.c_str(), desc.label.size()};
    nativeDesc.type = WGPUQueryType_Timestamp;
    nativeDesc.count = desc.count;
    auto querySet = wgpuDeviceCreateQuerySet(device, &nativeDesc);
    wgpuDeviceRelease(device);
    if (!querySet) {
        return {};
    }
#else
    return {};
#endif

    core::pmr::Vector<std::uint64_t> scopeIds(&core::defaultCpuMemoryResource());
    if (desc.timingScopeIds && desc.timingScopeIdCount) {
        scopeIds.assign(desc.timingScopeIds, desc.timingScopeIds + desc.timingScopeIdCount);
    }
    return arp::core::makeUnique<WebGpuRhiQueryPool>(
        arp::core::makeShared<WebGpuRhiQueryPoolPayload>(
#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
            querySet,
#endif
            std::move(scopeIds),
            desc.count),
        desc);
}

const WebGpuRhiQueryPool* asWebGpuRhiQueryPool(const rhi::QueryPool& pool)
{
    return dynamic_cast<const WebGpuRhiQueryPool*>(&pool);
}

} // namespace arp::rhi_webgpu
