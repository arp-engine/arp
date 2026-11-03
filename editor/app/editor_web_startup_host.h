#pragma once

#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "editor/web/editor_web_startup_sources.h"

namespace arp::editor {
// Browser cold entry. poll() dispatches a finite batch and returns to the host;
// it never busy-waits for Fetch. No Shell or GPU exists during preparation.
class EditorWebStartupHost final {
public:
    explicit EditorWebStartupHost(
        const std::array<core::SharedPtr<core::io::ManagedFileBackend>, 3>& backends);
    explicit EditorWebStartupHost(const std::array<core::pmr::String, 3>& baseUrls);
    ~EditorWebStartupHost();
    EditorWebStartupHost(const EditorWebStartupHost&) = delete;
    EditorWebStartupHost& operator=(const EditorWebStartupHost&) = delete;
    void poll();
    void cancel() noexcept;

    bool done() const noexcept { return done_; }

    bool ready() const noexcept { return done_ && result_.resource.success; }

    const core::pmr::String& diagnostic() const noexcept { return diagnostic_; }

    const resource::ResourceGraphExecutionResult& result() const noexcept { return result_; }

    core::SharedPtr<const EditorWebStartupSources> sources() const;

private:
    // Reverse destruction: runtime retires callbacks before the manager/result.
    resource::ResourceManager manager_;
    core::runtime::CancellationSource cancellation_;
    resource::ResourceGraphExecutionResult result_;
    core::pmr::String diagnostic_;
    bool done_ = false;
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
};
} // namespace arp::editor
