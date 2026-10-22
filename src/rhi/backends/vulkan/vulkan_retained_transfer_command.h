#pragma once

#include "rhi/backends/vulkan/vulkan_retained_command.h"

namespace arp::rhi_vulkan {
// Compatibility entry for the previously published private transfer slice.
using VulkanRhiRetainedTransferCommand = VulkanRhiRetainedCommand;
} // namespace arp::rhi_vulkan
