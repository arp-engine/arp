#pragma once
#include "core/memory/cpu_allocator.h"

#include <cstdint>

namespace arp::rhi_vulkan {
class VulkanRhiNativeCommandPayload;

// One backend-owned queue/fence custody protocol. Only actual native command
// events enter it; implementations retain their exact original seal and graph.
class VulkanOriginalCommandCustody {
    friend class VulkanRhiNativeCommandPayload;

public:
    virtual ~VulkanOriginalCommandCustody() = default;
    virtual std::uint32_t lifecycleVersion() const noexcept = 0;

private:
    virtual bool beforeSubmit(
        const VulkanRhiNativeCommandPayload&, core::pmr::String&) noexcept = 0;
    virtual const char* maySubmit() noexcept = 0;
    virtual void submissionUnproven(const VulkanRhiNativeCommandPayload&) noexcept = 0;
    virtual void fenceCompleted(const VulkanRhiNativeCommandPayload&) noexcept = 0;
};
} // namespace arp::rhi_vulkan
