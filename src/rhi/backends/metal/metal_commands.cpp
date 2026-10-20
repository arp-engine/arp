#include "rhi/backends/metal/metal_commands.h"

namespace arp::rhi_metal {

MetalRhiCommandSliceReport describeMetalRhiCommandSlice()
{
    MetalRhiCommandSliceReport report;
    // The public command slice exposes only logical RHI objects. Metal's
    // setBytes and argument-buffer lowering stay entirely inside the native
    // payload and no native command handle crosses this boundary.
    report.backendVocabularyHiddenFromUpperLayer = true;
    report.nativeCommandHandleExposedToUpperLayer = false;
    report.pushConstantUploadBufferSupported = true;
    report.argumentBindingSupported = true;
    report.drawableLifecycleGuarded = true;
    report.logicalFrameSlots = 2u;
    return report;
}

} // namespace arp::rhi_metal
