#pragma once

#include <cstdint>

namespace arp::rhi_webgpu {

struct WebGpuRhiCommandSliceReport {
    bool backendDescriptorVocabularyHiddenFromUpperLayer = true;
    bool nativeCommandHandleExposedToUpperLayer = false;
    bool pushConstantUploadRingSupported = true;
    bool backendOwnedUploadStagingSupported = true;
    bool softSecondaryCommandReplaySupported = true;
    bool canvasLifecycleDiagnosticsRecorded = true;
    std::uint32_t logicalFrameSlots = 2;
};

WebGpuRhiCommandSliceReport describeWebGpuRhiCommandSlice();

} // namespace arp::rhi_webgpu
