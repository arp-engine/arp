#pragma once

#include "../common/startup_resource_task.h"
#include "gameplay_text.h"

namespace arp::survivors {

// CPU-only startup result. No ScriptVM execution, scene or GPU lifetime is
// attached; the owner remains usable after the source task and IO retire.
struct PreparedGameplay {
    GameConfig config;
    Rules rules;
};

class GameplaySourceTask final {
public:
    GameplaySourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        core::PoolHandle,
        core::runtime::CancellationToken = {});

    void advance() noexcept { task_.advance(); }

    void cancel() noexcept { task_.cancel(); }

    examples::StartupResourcePhase phase() const noexcept { return task_.phase(); }

    std::string_view diagnostic() const noexcept { return task_.diagnostic(); }

    const char* diagnosticId() const noexcept { return task_.diagnosticId(); }

    core::io::FileIoStatus fileStatus() const noexcept { return task_.fileStatus(); }

    std::uint64_t readCount() const noexcept { return task_.readCount(); }

    core::SharedPtr<const PreparedGameplay> preparation() const;

private:
    examples::StartupResourceTask task_;
};
} // namespace arp::survivors
