#pragma once

#include <cstdint>

namespace arp::rhi_metal {

struct MetalRhiCommandSliceReport {
    bool backendVocabularyHiddenFromUpperLayer = true;
    bool nativeCommandHandleExposedToUpperLayer = false;
    // Compatibility field: true means executable push-constant upload via
    // Metal setBytes; this backend intentionally does not allocate an upload
    // buffer for push constants.
    bool pushConstantUploadBufferSupported = true;
    bool argumentBindingSupported = true;
    bool drawableLifecycleGuarded = true;
    std::uint32_t logicalFrameSlots = 2;
};

MetalRhiCommandSliceReport describeMetalRhiCommandSlice();

} // namespace arp::rhi_metal
