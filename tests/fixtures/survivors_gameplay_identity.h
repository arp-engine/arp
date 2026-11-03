#pragma once

#include <cstddef>

namespace arp::tests {
// Explicit golden input identity after the pickup body/visual separation.
// The original IO-migration baseline remains in its historical evidence file.
struct SurvivorsGameplayIdentity {
    static constexpr const char* sourceSha256 =
        "sha256:7fafc7c75c52b5ad41f35084974ce904bf77c83f213b07bc7e1461dacdf149a7";
    static constexpr const char* configSha256 =
        "sha256:ce69fdd833b5063de648d1ef8d896034ceb46bdd13297f7b42926f01fa10143d";
    static constexpr std::size_t sourceBytes = 46364;
};
} // namespace arp::tests
