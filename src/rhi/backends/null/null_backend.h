#pragma once

#include "core/api.h"
#include "rhi/gpu_device.h"

#include <memory>

namespace arp::rhi {

ARP_API arp::core::UniquePtr<GpuDevice> createNullDevice();

} // namespace arp::rhi
