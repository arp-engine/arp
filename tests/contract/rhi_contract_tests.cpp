#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_device.h"

#include <cassert>

int main()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    assert(device->backend() == arp::rhi::Backend::Null);

    const auto caps = device->capabilities();
    assert(caps.core.graphics);
    assert(caps.core.compute);
    assert(caps.explicitFeatures.pushConstants);
    assert(caps.swapchain.requestedImageCount == 2u);
    assert(caps.swapchain.maxFramesInFlight == 2u);
    return 0;
}
