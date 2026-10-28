#pragma once

#include "rhi/gpu_device.h"

#include <cassert>
#include <string_view>

namespace arp::tests {
class DeviceIdleFailureFixture final {
public:
    static void verify(rhi::GpuDevice& device, std::string_view expected)
    {
        bool rejected = false;
        try {
            device.waitIdle();
        } catch (const core::DiagnosticError& error) {
            rejected = std::string_view(error.what()) == expected;
        }
        assert(rejected);
    }
};
} // namespace arp::tests
