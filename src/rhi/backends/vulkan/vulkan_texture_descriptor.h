#pragma once

#include "rhi/gpu_resources.h"

#include <vulkan/vulkan.h>

namespace arp::rhi_vulkan {

// Shared backend-private descriptor mapping. Callers supply explicit create
// flags and sample policy; placement input validation precedes this mapping.
class VulkanTextureDescriptor final {
public:
    static VkImageUsageFlags usage(rhi::TextureUsage usage, rhi::TextureFormat format)
    {
        VkImageUsageFlags flags = 0;
        if ((usage & rhi::TextureUsage::RenderAttachment) != rhi::TextureUsage::None) {
            if (format == rhi::TextureFormat::D32Float
                || format == rhi::TextureFormat::Depth24Stencil8) {
                flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            } else {
                flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
        }
        if ((usage & rhi::TextureUsage::Sampled) != rhi::TextureUsage::None) {
            flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if ((usage & rhi::TextureUsage::Storage) != rhi::TextureUsage::None) {
            flags |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        if ((usage & rhi::TextureUsage::CopySrc) != rhi::TextureUsage::None) {
            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        if ((usage & rhi::TextureUsage::CopyDst) != rhi::TextureUsage::None) {
            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        return flags;
    }

    static VkFormat format(rhi::TextureFormat format)
    {
        switch (format) {
        case rhi::TextureFormat::Rgba8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case rhi::TextureFormat::Rgba8UnormSrgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case rhi::TextureFormat::Bgra8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case rhi::TextureFormat::R8Unorm:
            return VK_FORMAT_R8_UNORM;
        case rhi::TextureFormat::Rgba16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case rhi::TextureFormat::Rg16Float:
            return VK_FORMAT_R16G16_SFLOAT;
        case rhi::TextureFormat::R32Float:
            return VK_FORMAT_R32_SFLOAT;
        case rhi::TextureFormat::D32Float:
            return VK_FORMAT_D32_SFLOAT;
        case rhi::TextureFormat::Depth24Stencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case rhi::TextureFormat::Bc1Unorm:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case rhi::TextureFormat::Bc3Unorm:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case rhi::TextureFormat::Bc7Unorm:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case rhi::TextureFormat::Bc7UnormSrgb:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case rhi::TextureFormat::Unknown:
            break;
        }
        return VK_FORMAT_UNDEFINED;
    }

    static VkImageCreateInfo imageInfo(
        const rhi::TextureDesc& desc, VkImageCreateFlags extraFlags, VkSampleCountFlagBits samples)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = desc.extent.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        imageInfo.extent = {desc.extent.width, desc.extent.height, desc.extent.depth};
        imageInfo.mipLevels = desc.mipLevelCount;
        imageInfo.arrayLayers = desc.arrayLayerCount;
        imageInfo.format = format(desc.format);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage(desc.usage, desc.format);
        imageInfo.flags =
            extraFlags | (desc.cubeCompatible ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);
        imageInfo.samples = samples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        return imageInfo;
    }
};

} // namespace arp::rhi_vulkan
