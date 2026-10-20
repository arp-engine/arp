#pragma once

// Runtime-only parser for the generated AOT v5 package manifest.  This
// header deliberately depends on the stable descriptor contract, not on the
// source compiler, parser, semantic analyzer, typed IR, or AOT code generator.

#include "scriptvm/aot/aot_v5.h"

namespace arp::scriptvm {

ARP_API ScriptResult parseAotPackageManifestV5(
    const core::pmr::String& yaml, AotPackageManifestV5& output, AotPlatform requiredPlatform);

} // namespace arp::scriptvm
