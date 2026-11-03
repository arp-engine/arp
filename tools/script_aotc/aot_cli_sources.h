#pragma once

#include "core/io/file_service.h"
#include "scriptvm/aot/aot_package.h"

namespace arp::tools::script_aotc {

struct AotCliPreparedSources {
    scriptvm::ScriptAotPackageLoadResult loaded;
    core::UniquePtr<scriptvm::ScriptSourceSnapshot> sources;
    core::io::FileBackendCapability backend;
    core::io::FileServiceTelemetry telemetry;

    bool ok() const noexcept { return loaded.ok() && sources && sources->sealed(); }
};

// Only prepares input through FileService. The original builder owns all
// compilation, identity, generated contents and output publication semantics.
class AotCliSourcePreparation final {
public:
    static core::UniquePtr<AotCliPreparedSources> load(const std::filesystem::path& manifest,
        std::uint64_t maxManifestBytes,
        scriptvm::ScriptSourceBudget sourceBudget);
};

} // namespace arp::tools::script_aotc
