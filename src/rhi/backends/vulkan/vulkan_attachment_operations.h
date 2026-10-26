#pragma once

#include "rhi/gpu_commands.h"

#include <vulkan/vulkan.h>

namespace arp::rhi_vulkan {

// Callers validate closed L2 enums before native creation/recording. Invalid
// values map to a sentinel, never a native default load/store operation.
inline VkAttachmentLoadOp vulkanRhiAttachmentLoadOp(rhi::LoadOp op) noexcept
{
    switch (op) {
    case rhi::LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case rhi::LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case rhi::LoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_MAX_ENUM;
}

inline VkAttachmentStoreOp vulkanRhiAttachmentStoreOp(rhi::StoreOp op) noexcept
{
    switch (op) {
    case rhi::StoreOp::Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case rhi::StoreOp::DontCare:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_MAX_ENUM;
}

} // namespace arp::rhi_vulkan
