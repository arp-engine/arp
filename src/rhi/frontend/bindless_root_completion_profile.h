#pragma once
#include <cstdint>

namespace arp::rhi {
// Completion is selected by the original sealed lifecycle, never by the
// number of retained Root bindings or by a backend's encoder arrangement.
// This value-only projection grants no issuer or completion authority.
class OriginalBindlessRootCompletionProfile final {
public:
    struct Decision {
        std::uint32_t submissionVersion = 0u;
        bool callbacks = false;
        const char* diagnostic = "RHI-BINDLESS-ROOT-COMPLETION-LIFECYCLE";
    };

    static constexpr Decision inspect(std::uint32_t lifecycle) noexcept
    {
        switch (lifecycle) {
        case 6u:
            return {0u, false, nullptr};
        case 8u:
            return {5u, false, nullptr};
        case 9u:
            return {6u, false, nullptr};
        case 11u:
            return {0u, true, nullptr};
        case 12u:
            return {6u, true, nullptr};
        case 13u:
            return {7u, true, nullptr};
        case 14u:
            return {8u, false, nullptr};
        case 15u:
            return {9u, false, nullptr};
        case 16u:
            return {10u, true, nullptr};
        case 17u:
            return {11u, false, nullptr};
        case 18u:
            return {12u, true, nullptr};
        case 19u:
            return {13u, false, nullptr};
        case 20u:
            return {14u, true, nullptr};
        case 21u:
            return {15u, false, nullptr};
        case 22u:
            return {15u, true, nullptr};
        default:
            return {};
        }
    }
};
} // namespace arp::rhi
