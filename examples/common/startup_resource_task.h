#pragma once

#include "resource/resource_graph_executor.h"

namespace arp::examples {

enum class StartupResourcePhase { Pending, Ready, Failed, Cancelled };

// Host diagnostics must be nonempty static-lifetime IDs. Each example owns
// their meaning; the shared coordinator only manages a bounded resource graph.
struct StartupResourceDiagnostics {
    const char* cancelled = nullptr;
    const char* notAccepted = nullptr;
    const char* preparationFailed = nullptr;
};

// ResourceGraphExecutor owns acquisition/admission; callbacks retain State.
// The task does not decode assets or create GPU work.
class StartupResourceTask final {
public:
    StartupResourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        resource::ResourceGraphExecutionRequest,
        StartupResourceDiagnostics,
        core::runtime::CancellationToken = {});
    ~StartupResourceTask();
    StartupResourceTask(const StartupResourceTask&) = delete;
    StartupResourceTask& operator=(const StartupResourceTask&) = delete;

    void advance() noexcept;
    void cancel() noexcept;
    StartupResourcePhase phase() const noexcept;
    std::string_view diagnostic() const noexcept;
    const char* diagnosticId() const noexcept;
    core::io::FileIoStatus fileStatus() const noexcept;
    const resource::ResourceManager& resources() const noexcept;
    std::uint64_t readCount() const noexcept;
    std::uint64_t bytesRead() const noexcept;

private:
    struct State;
    core::SharedPtr<State> state_;
};
} // namespace arp::examples
