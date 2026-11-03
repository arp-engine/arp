#pragma once

#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"

#include <filesystem>

namespace arp::tools::io {

struct ColdToolFileProfile {
    static constexpr std::size_t pathBytes = 512, ioChunkBytes = 16384;
    static constexpr std::uint64_t saveScratchBytes = 65536, httpControlBytes = 1024 * 1024;
};

enum class ColdToolFileScope { MountedResources, NativeHostFilesystem };

// CLI embedding policy only. FileService owns request semantics and backends;
// this host provides finite mounts, dispatch and cold native waits.
class ColdToolFiles final {
public:
    ColdToolFiles(std::uint64_t maxFileBytes,
        std::uint32_t maxMounts,
        ColdToolFileScope scope = ColdToolFileScope::MountedResources);
    ~ColdToolFiles();
    ColdToolFiles(const ColdToolFiles&) = delete;
    ColdToolFiles& operator=(const ColdToolFiles&) = delete;

    core::PoolHandle mountDirectory(
        core::StringView name, const std::filesystem::path& directory, bool writable = false);
    core::PoolHandle mountHttp(core::StringView name, core::StringView baseUrl);

    core::io::FileService& service() noexcept { return runtime_->fileService(); }

    bool dispatch() { return runtime_->dispatch().success(); }

    void retire();
    core::io::FileReadResult read(const core::io::FileReadRequest&);
    core::io::FileTextReadResult readText(const core::io::FileTextReadRequest&);
    core::io::FileSaveResult save(core::io::FileSaveRequest);
    static bool validBudget(std::uint64_t bytes) noexcept;

private:
    template <class Result, class Submit> Result await(Submit&& submit);
    std::uint64_t bytes_ = 0;
    ColdToolFileScope scope_;
    core::SharedPtr<core::io::MountedFileBackend> mounts_;
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
};
} // namespace arp::tools::io
