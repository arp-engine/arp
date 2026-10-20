#pragma once

#include <cstdint>

namespace arp::rhi_vulkan {

struct VulkanRhiCommandSliceReport {
    bool backendVocabularyHiddenFromUpperLayer = true;
    bool nativeCommandHandleExposedToUpperLayer = false;
    bool transferUploadReadbackSupported = true;
    bool stageAccessBarriersSupported = true;
    std::uint32_t logicalFrameSlots = 2;
};

VulkanRhiCommandSliceReport describeVulkanRhiCommandSlice();

} // namespace arp::rhi_vulkan
