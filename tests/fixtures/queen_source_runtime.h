#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/http_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"

namespace queen_source_fixture {
struct Runtime {
    arp::core::UniquePtr<arp::core::runtime::AsyncRuntime> owner;
    arp::core::PoolHandle mount;
    arp::core::SharedPtr<arp::core::io::MountedFileBackend> mounts;
};

// Memory and HTTP probes share the same finite mount/completion setup. The
// caller declares payload and transport budgets; these are fixture limits.
inline Runtime create(
    arp::core::SharedPtr<arp::core::io::ManagedFileBackend> backend, std::size_t fileBytes)
{
    using namespace arp;
    using namespace core::io;
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, fileBytes, 32, 512});
    const auto mount = mounts->mount("queen-probe", std::move(backend));
    if (!mount)
        throw core::DiagnosticError("QUEEN-PROBE-MOUNT");
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = desc.completionBudget = 2;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    auto runtime = core::makeUnique<core::runtime::AsyncRuntime>(std::move(desc));
    if (!runtime->valid())
        throw core::DiagnosticError("QUEEN-PROBE-RUNTIME");
    return {std::move(runtime), mount, std::move(mounts)};
}

inline Runtime create(std::string_view url, std::size_t fileBytes)
{
    using namespace arp;
    using namespace core::io;
    HttpFileBackendDesc http;
    http.baseUrl = url;
    http.maxInFlight = 1;
    http.maxBytesInFlight = 2 * fileBytes;
    http.maxPathBytes = 512;
    http.maxHeaderBytes = 8192;
    http.ioChunkBytes = 16384;
    http.timeoutMs = 5000;
    http.cancellationPollMs = 10;
    return create(core::makeShared<HttpFileBackend>(std::move(http)), fileBytes);
}
} // namespace queen_source_fixture
