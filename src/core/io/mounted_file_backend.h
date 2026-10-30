#pragma once

#include "core/container/generation_pool.h"
#include "core/io/file_backend.h"

#include <string_view>

namespace arp::core::io {

struct MountedFileBackendDesc {
    std::uint32_t maxMounts = 0;
    std::uint32_t maxInFlight = 0;
    std::uint64_t maxReadBytesInFlight = 0;
    std::size_t maxNameBytes = 0;
    std::size_t maxPathBytes = 0;
};

struct MountedFileBackendTelemetry {
    std::uint32_t mounts = 0;
    std::uint32_t inFlight = 0;
    std::uint64_t reservedReadBytes = 0;
};

// Single dispatch-owner thread. Routing is explicit by generation handle;
// mount names are only resolved when the host deliberately requests lookup.
class ARP_API MountedFileBackend final : public ManagedFileBackend {
public:
    explicit MountedFileBackend(MountedFileBackendDesc desc);
    ~MountedFileBackend() override;
    MountedFileBackend(const MountedFileBackend&) = delete;
    MountedFileBackend& operator=(const MountedFileBackend&) = delete;

    core::PoolHandle mount(std::string_view name, core::SharedPtr<ManagedFileBackend> backend);
    bool unmount(core::PoolHandle handle) noexcept;
    core::PoolHandle findMount(std::string_view name) const noexcept;
    FileBackendCapability capability() const noexcept override;
    FileReadResult validateRead(const FileReadRequest& request) const override;
    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
    FileIoStatus queryAsync(const FileQueryRequest& request,
        FileQueryCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
    void shutdown() noexcept override;
    MountedFileBackendTelemetry telemetry() const noexcept;

    const char* diagnostic() const noexcept { return diagnostic_; }

private:
    struct State;
    struct Completion;
    static void complete(void* context, FileReadResult result);
    static void completeQuery(void* context, FileQueryResult result);
    MountedFileBackendDesc desc_;
    core::SharedPtr<State> state_;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::core::io
