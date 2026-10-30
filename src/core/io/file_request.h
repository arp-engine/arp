#pragma once

#include "core/container/generation_pool.h"
#include "core/io/file_version.h"
#include "core/memory/cpu_allocator.h"
#include "core/runtime/cancellation_token.h"

#include <cstdint>

namespace arp::core::io {

enum class FilePathScope : std::uint8_t {
    Unspecified,
    HostFilesystem,
    MountedResource,
    Package,
};

enum class FileReadRangeMode : std::uint8_t {
    WholeFile,
    ExactRange,
};

enum class FilePartialReadPolicy : std::uint8_t {
    DenyPartial,
    AllowPartial,
};

enum class FileRequestPriority : std::uint8_t {
    Normal,
    Background,
    Interactive,
};

struct FileReadRange {
    FileReadRangeMode mode = FileReadRangeMode::WholeFile;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;

    static FileReadRange wholeFile() noexcept { return {}; }

    static FileReadRange exactRange(std::uint64_t offset, std::uint64_t size) noexcept
    {
        return {FileReadRangeMode::ExactRange, offset, size};
    }
};

struct FileReadRequest {
    FilePathScope pathScope = FilePathScope::Unspecified;
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    FileReadRange range{};
    FilePartialReadPolicy partialPolicy = FilePartialReadPolicy::DenyPartial;
    FileRequestPriority priority = FileRequestPriority::Normal;
    runtime::CancellationToken cancellation;
    // Version0 preserves existing callers. Version1 requires an explicit,
    // finite positive byte budget; both versions enforce any supplied bound.
    std::uint64_t maxBytes = UINT64_MAX;
    std::uint32_t contractVersion = 0;
    // Routed requests bind a particular mount generation, not its display name.
    core::PoolHandle mount;
    FileVersionToken expectedVersion;
    std::uint64_t maxVersionBytes = 0;
};

} // namespace arp::core::io
