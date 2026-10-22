#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/gpu_device.h"

#include <mutex>

namespace arp::rhi_vulkan {

class VulkanRhiQueryPoolPayload final {
public:
    VulkanRhiQueryPoolPayload(std::shared_ptr<VulkanRhiRuntime> runtime,
        VkQueryPool queryPool,
        core::pmr::Vector<std::uint64_t> scopeIds,
        std::uint32_t count);
    ~VulkanRhiQueryPoolPayload();
    VkQueryPool nativeHandle() const;
    const core::pmr::Vector<std::uint64_t>& scopeIds() const;

    bool markRecorded(std::uint32_t queryIndex, std::pmr::string& diagnostics);
    bool markSubmitted(std::uint32_t queryIndex, std::pmr::string& diagnostics);
    bool rollbackSubmitted(std::uint32_t queryIndex, std::pmr::string& diagnostics);
    void markCompleted(const VulkanRhiRuntime& runtime, std::uint32_t queryIndex);
    bool rollbackRecorded(std::uint32_t queryIndex);
    void markDiagnosticFailure(std::uint32_t queryIndex, const char* diagnostic);
    rhi::CommandValidationResult reset();
    rhi::QueryExecutionStatus executionStatus() const;

private:
    std::shared_ptr<VulkanRhiRuntime> runtime_;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    core::pmr::Vector<std::uint64_t> scopeIds_;
    core::pmr::Vector<rhi::QueryExecutionState> states_{
        &core::defaultCpuMemoryResource(),
    };
    mutable std::mutex mutex_;
};

class VulkanRhiQueryPool final : public rhi::QueryPool {
public:
    VulkanRhiQueryPool(std::shared_ptr<VulkanRhiQueryPoolPayload> payload, rhi::QueryPoolDesc desc);
    std::uint32_t count() const override;
    rhi::QueryType type() const override;
    rhi::CommandValidationResult reset() override;
    rhi::QueryExecutionStatus executionStatus() const override;
    VkQueryPool nativeHandle() const;
    const std::shared_ptr<VulkanRhiQueryPoolPayload>& payload() const;

private:
    std::shared_ptr<VulkanRhiQueryPoolPayload> payload_;
    rhi::QueryPoolDesc desc_;
};

arp::core::UniquePtr<rhi::QueryPool> createVulkanRhiQueryPool(
    const std::shared_ptr<VulkanRhiRuntime>& runtime, const rhi::QueryPoolDesc& desc);

const VulkanRhiQueryPool* asVulkanRhiQueryPool(const rhi::QueryPool& pool);

} // namespace arp::rhi_vulkan
