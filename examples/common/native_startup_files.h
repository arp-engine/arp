#pragma once

#include "core/runtime/async_runtime.h"

#include <filesystem>

namespace arp::examples {

enum class NativeStartupSource { Directory, Http };

struct NativeStartupFileDesc {
    NativeStartupSource source = NativeStartupSource::Directory;
    std::filesystem::path directory;
    core::pmr::String baseUrl;
    core::pmr::String mountName;
    std::size_t maxFileBytes = 0;
    // Explicit local record-storage profile; HTTP sources stay read-only.
    bool allowObservedBaselineReplace = false;
    std::size_t maxBackendSaveScratchBytes = 0;
};

// IDs must be nonempty static-lifetime strings. Hosts retain their existing
// diagnostics; this class owns only the shared sequential startup IO profile.
struct NativeStartupFileDiagnostics {
    const char* configuration = nullptr;
    const char* mount = nullptr;
    const char* runtime = nullptr;
};

class NativeStartupFiles final {
public:
    NativeStartupFiles(NativeStartupFileDesc, NativeStartupFileDiagnostics);

    core::runtime::AsyncRuntime& runtime() noexcept { return *runtime_; }

    core::PoolHandle mount() const noexcept { return mount_; }

private:
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
    core::PoolHandle mount_;
};
} // namespace arp::examples
