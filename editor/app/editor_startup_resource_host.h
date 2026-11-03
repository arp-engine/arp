#pragma once

#include "core/container/array_view.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"

#include <optional>

namespace arp::editor {

struct EditorStartupMountDesc {
    core::pmr::String alias;
    std::filesystem::path directory;
    std::optional<std::string_view> baseUrl;
    std::size_t maxFileBytes = 0;
};

// Shared native cold-start IO policy for the real viewport and UI consumers.
// Graph execution/draining completes before borrowed callback state retires.
// One-shot: execute closes the runtime on success, rejection or cancellation.
class EditorStartupResourceHost final {
public:
    explicit EditorStartupResourceHost(core::ArrayView<const EditorStartupMountDesc>);

    core::PoolHandle mount(std::size_t index) const { return mounts_.at(index); }

    bool execute(resource::ResourceManager&,
        resource::ResourceGraphExecutionRequest,
        resource::ResourceGraphExecutionResult&,
        core::pmr::String& diagnostic);

private:
    core::pmr::Vector<core::PoolHandle> mounts_;
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
};
} // namespace arp::editor
