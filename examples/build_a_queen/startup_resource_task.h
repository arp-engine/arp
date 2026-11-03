#pragma once

#include "../common/startup_resource_task.h"

namespace arp::build_a_queen {
using StartupResourcePhase = examples::StartupResourcePhase;
using StartupResourceTask = examples::StartupResourceTask;
// Preserve the established Queen diagnostics while sharing only coordination.
inline constexpr examples::StartupResourceDiagnostics queenStartupDiagnostics{
    "QUEEN-STARTUP-RESOURCE-CANCELLED",
    "QUEEN-STARTUP-GRAPH-NOT-ACCEPTED",
    "QUEEN-STARTUP-RESOURCE-PREPARATION"};
} // namespace arp::build_a_queen
