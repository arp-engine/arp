#include "rhi/backends/vulkan/vulkan_query_pool.h"

#include "core/cpu_allocator.h"

#include <algorithm>

namespace arp::rhi_vulkan {

VulkanRhiQueryPoolPayload::VulkanRhiQueryPoolPayload(std::shared_ptr<VulkanRhiRuntime> runtime,
    VkQueryPool queryPool,
    core::pmr::Vector<std::uint64_t> scopeIds,
    std::uint32_t count)
    : runtime_(std::move(runtime)), queryPool_(queryPool), scopeIds_(std::move(scopeIds))
{
    states_.assign(count, rhi::QueryExecutionState::NotRecorded);
}

VulkanRhiQueryPoolPayload::~VulkanRhiQueryPoolPayload()
{
    if (runtime_ && queryPool_) {
        if (auto fn = reinterpret_cast<PFN_vkDestroyQueryPool>(
                runtime_->getDeviceProcAddr("vkDestroyQueryPool"))) {
            fn(runtime_->logicalDevice(), queryPool_, nullptr);
        }
    }
}

VkQueryPool VulkanRhiQueryPoolPayload::nativeHandle() const
{
    return queryPool_;
}

const core::pmr::Vector<std::uint64_t>& VulkanRhiQueryPoolPayload::scopeIds() const
{
    return scopeIds_;
}

bool VulkanRhiQueryPoolPayload::markRecorded(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size()) {
        diagnostics = "RHI-VULKAN-QUERY-INDEX-OUT-OF-RANGE";
        return false;
    }
    if (states_[queryIndex] != rhi::QueryExecutionState::NotRecorded) {
        diagnostics = "RHI-VULKAN-QUERY-MUST-BE-RESET-FIRST";
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::Recorded;
    return true;
}

bool VulkanRhiQueryPoolPayload::markSubmitted(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size() || states_[queryIndex] != rhi::QueryExecutionState::Recorded) {
        diagnostics = "RHI-VULKAN-QUERY-SUBMISSION-STATE-INVALID";
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::Pending;
    return true;
}

bool VulkanRhiQueryPoolPayload::rollbackSubmitted(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size() || states_[queryIndex] != rhi::QueryExecutionState::Pending) {
        diagnostics = "RHI-VULKAN-QUERY-ROLLBACK-STATE-INVALID";
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::Recorded;
    return true;
}

bool VulkanRhiQueryPoolPayload::rollbackRecorded(const std::uint32_t queryIndex)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size() || states_[queryIndex] != rhi::QueryExecutionState::Recorded) {
        return false;
    }
    states_[queryIndex] = rhi::QueryExecutionState::NotRecorded;
    return true;
}

void VulkanRhiQueryPoolPayload::markDiagnosticFailure(const std::uint32_t queryIndex, const char*)
{
    std::lock_guard lock(mutex_);
    if (queryIndex < states_.size()) {
        states_[queryIndex] = rhi::QueryExecutionState::DiagnosticFailure;
    }
}

void VulkanRhiQueryPoolPayload::markCompleted(
    const VulkanRhiRuntime& runtime, const std::uint32_t queryIndex)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= states_.size() || states_[queryIndex] != rhi::QueryExecutionState::Pending) {
        return;
    }
    const auto getResults = reinterpret_cast<PFN_vkGetQueryPoolResults>(
        runtime.getDeviceProcAddr("vkGetQueryPoolResults"));
    std::uint64_t value = 0u;
    const auto result = getResults ? getResults(runtime.logicalDevice(),
                                         queryPool_,
                                         queryIndex,
                                         1u,
                                         sizeof(value),
                                         &value,
                                         sizeof(value),
                                         VK_QUERY_RESULT_64_BIT)
                                   : VK_ERROR_INITIALIZATION_FAILED;
    states_[queryIndex] = result == VK_SUCCESS ? rhi::QueryExecutionState::Available
                                               : rhi::QueryExecutionState::DiagnosticFailure;
}

rhi::CommandValidationResult VulkanRhiQueryPoolPayload::reset()
{
    std::lock_guard lock(mutex_);
    for (const auto state : states_) {
        if (state == rhi::QueryExecutionState::Recorded
            || state == rhi::QueryExecutionState::Pending) {
            return {
                false,
                "RHI-VULKAN-QUERY-POOL-RESET-WAITING-FOR-COMPLETION",
                0,
            };
        }
    }
    const auto resetQueryPool = reinterpret_cast<PFN_vkResetQueryPool>(
        runtime_ ? runtime_->getDeviceProcAddr("vkResetQueryPool") : nullptr);
    if (!resetQueryPool || !runtime_ || queryPool_ == VK_NULL_HANDLE) {
        return {
            false,
            "RHI-VULKAN-QUERY-POOL-RESET-ENTRYPOINT-MISSING",
            0,
        };
    }
    resetQueryPool(
        runtime_->logicalDevice(), queryPool_, 0u, static_cast<std::uint32_t>(states_.size()));
    std::fill(states_.begin(), states_.end(), rhi::QueryExecutionState::NotRecorded);
    return {true, "", 0};
}

rhi::QueryExecutionStatus VulkanRhiQueryPoolPayload::executionStatus() const
{
    rhi::QueryExecutionStatus status;
    status.type = rhi::QueryType::Timestamp;
    status.count = static_cast<std::uint32_t>(states_.size());
    status.creationSupported = runtime_ && queryPool_ != VK_NULL_HANDLE;
    status.resetSupported = status.creationSupported;
    status.asynchronousReadbackSupported = status.creationSupported;
    status.completionGatedLifetimeRequired = status.creationSupported;
    if (!status.creationSupported) {
        status.state = rhi::QueryExecutionState::Unsupported;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-NATIVE-PAYLOAD-MISSING";
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
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-READBACK-FAILED";
    } else if (hasPending) {
        status.state = rhi::QueryExecutionState::Pending;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-WAITING-FOR-COMPLETION";
    } else if (hasRecorded) {
        status.state = rhi::QueryExecutionState::Recorded;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-RECORDED-BEFORE-SUBMISSION";
    } else if (hasAvailable) {
        status.state = rhi::QueryExecutionState::Available;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-READBACK-AVAILABLE";
    } else {
        status.state = rhi::QueryExecutionState::NotRecorded;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-NOT-RECORDED";
    }
    return status;
}

VulkanRhiQueryPool::VulkanRhiQueryPool(
    std::shared_ptr<VulkanRhiQueryPoolPayload> payload, rhi::QueryPoolDesc desc)
    : payload_(std::move(payload)), desc_(std::move(desc))
{}

std::uint32_t VulkanRhiQueryPool::count() const
{
    return desc_.count;
}

rhi::QueryType VulkanRhiQueryPool::type() const
{
    return desc_.type;
}

VkQueryPool VulkanRhiQueryPool::nativeHandle() const
{
    return payload_ ? payload_->nativeHandle() : VK_NULL_HANDLE;
}

const std::shared_ptr<VulkanRhiQueryPoolPayload>& VulkanRhiQueryPool::payload() const
{
    return payload_;
}

arp::core::UniquePtr<rhi::QueryPool> createVulkanRhiQueryPool(
    const std::shared_ptr<VulkanRhiRuntime>& runtime, const rhi::QueryPoolDesc& desc)
{
    if (!runtime || runtime->syntheticRuntime() || !rhi::validateQueryPoolDesc(desc).success
        || desc.type != rhi::QueryType::Timestamp)
        return {};
    const auto create =
        reinterpret_cast<PFN_vkCreateQueryPool>(runtime->getDeviceProcAddr("vkCreateQueryPool"));
    if (!create)
        return {};
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = desc.count;
    VkQueryPool pool = VK_NULL_HANDLE;
    if (create(runtime->logicalDevice(), &info, nullptr, &pool) != VK_SUCCESS)
        return {};
    core::pmr::Vector<std::uint64_t> scopeIds;
    if (desc.timingScopeIds && desc.timingScopeIdCount) {
        scopeIds.assign(desc.timingScopeIds, desc.timingScopeIds + desc.timingScopeIdCount);
    }
    auto created = arp::core::makeUnique<VulkanRhiQueryPool>(
        arp::core::makeShared<VulkanRhiQueryPoolPayload>(
            runtime, pool, std::move(scopeIds), desc.count),
        desc);
    // Timestamp writes commonly occur inside a render pass. Vulkan forbids
    // vkCmdResetQueryPool there, so initialize this new, not-yet-submitted
    // pool with the Vulkan 1.2 host reset API before it is exposed.
    if (!created->reset().success) {
        return {};
    }
    return created;
}

const VulkanRhiQueryPool* asVulkanRhiQueryPool(const rhi::QueryPool& pool)
{
    return dynamic_cast<const VulkanRhiQueryPool*>(&pool);
}

rhi::CommandValidationResult VulkanRhiQueryPool::reset()
{
    return payload_ ? payload_->reset()
                    : rhi::CommandValidationResult{
                          false,
                          "RHI-VULKAN-QUERY-POOL-PAYLOAD-MISSING",
                          0,
                      };
}

rhi::QueryExecutionStatus VulkanRhiQueryPool::executionStatus() const
{
    if (payload_) {
        return payload_->executionStatus();
    }
    rhi::QueryExecutionStatus status;
    status.type = desc_.type;
    status.state = rhi::QueryExecutionState::DiagnosticFailure;
    status.count = desc_.count;
    status.diagnostics = "RHI-VULKAN-QUERY-POOL-PAYLOAD-MISSING";
    return status;
}

} // namespace arp::rhi_vulkan
