#pragma once

#include <cstring>

namespace arp::platform {

struct VulkanValidationPolicy {
    bool valid = true;
    bool enabled = false;
    bool explicitlyRequired = false;

    static VulkanValidationPolicy resolve(bool debugBuild, const char* value)
    {
        if (value == nullptr)
            return {true, debugBuild, false};
        if (std::strcmp(value, "0") == 0)
            return {true, false, false};
        if (std::strcmp(value, "1") == 0)
            return {true, true, true};
        return {false, false, false};
    }
};

} // namespace arp::platform
