#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <filesystem>

namespace arp::core {

class ARP_API ImmutableBuildInputContract final {
public:
    bool add(std::string_view logicalName, std::string_view value, pmr::String& diagnostics);
    pmr::String canonicalText() const;
    pmr::String fingerprint() const;

private:
    pmr::Map<pmr::String, pmr::String> entries_{&defaultCpuMemoryResource()};
};

// Captures build inputs as immutable bytes under one staging root. Build
// tools consume paths returned by path(); verifySourcesUnchanged() closes the
// publication race against edits made while those tools were running.
class ARP_API ImmutableBuildInputSnapshot final {
public:
    explicit ImmutableBuildInputSnapshot(std::filesystem::path root);

    bool captureFile(std::string_view logicalName,
        const std::filesystem::path& source,
        pmr::String& diagnostics);
    bool captureExecutable(std::string_view logicalName,
        const std::filesystem::path& source,
        pmr::String& diagnostics);
    bool captureDirectory(std::string_view logicalName,
        const std::filesystem::path& source,
        pmr::String& diagnostics);

    // Explicit metadata-bound resolution; the strict APIs above still reject
    // symlinks. Captured files are regular bytes, never mutable host links.
    bool captureResolvedFile(std::string_view logicalName,
        const std::filesystem::path& source,
        const std::filesystem::path& allowedRoot,
        pmr::String& diagnostics);
    bool captureResolvedDirectory(std::string_view logicalName,
        const std::filesystem::path& source,
        pmr::String& diagnostics);

    std::filesystem::path path(std::string_view logicalName) const;
    pmr::String fingerprint() const;
    bool verifySourcesUnchanged(pmr::String& diagnostics) const;

private:
    struct FileRecord {
        pmr::String logicalName{&defaultCpuMemoryResource()};
        std::filesystem::path source;
        pmr::String fingerprint{&defaultCpuMemoryResource()};
        pmr::String versionOutput{&defaultCpuMemoryResource()};
        std::filesystem::perms permissions = std::filesystem::perms::unknown;
        bool tracksPermissions = false;
        std::filesystem::path resolutionSource;
        pmr::String resolutionBinding{&defaultCpuMemoryResource()};
    };

    struct DirectoryRecord {
        pmr::String logicalName{&defaultCpuMemoryResource()};
        std::filesystem::path source;
        pmr::Vector<pmr::String> relativeFiles{&defaultCpuMemoryResource()};
        pmr::Map<pmr::String, pmr::String> resolvedEntries{&defaultCpuMemoryResource()};
        pmr::Map<pmr::String, pmr::String> directoryLinks{&defaultCpuMemoryResource()};
        bool resolvesLinks = false;
    };

    bool captureFileInternal(std::string_view logicalName,
        const std::filesystem::path& source,
        pmr::String& diagnostics);

    std::filesystem::path root_;
    pmr::Vector<FileRecord> files_{&defaultCpuMemoryResource()};
    pmr::Vector<DirectoryRecord> directories_{&defaultCpuMemoryResource()};
};

} // namespace arp::core
