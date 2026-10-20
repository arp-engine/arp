#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

#include <filesystem>

namespace arp::core {

class ImmutableBuildInputSnapshot;

struct CompilerToolchainLayoutResult final {
    bool success = false;
    std::filesystem::path programSearchDirectory;
    pmr::Vector<std::filesystem::path> auxiliaryProgramSearchDirectories{
        &defaultCpuMemoryResource()};
    pmr::Vector<std::filesystem::path> systemIncludeDirectories{&defaultCpuMemoryResource()};
    pmr::Vector<std::filesystem::path> libraryDirectories{&defaultCpuMemoryResource()};
    pmr::String diagnostics{&defaultCpuMemoryResource()};
};

struct CompilerToolchainSnapshotResult final {
    bool success = false;
    std::filesystem::path compilerExecutable;
    CompilerToolchainLayoutResult layout;
    pmr::String diagnostics{&defaultCpuMemoryResource()};
};

// Resolves compiler support programs from a versioned installation layout.
// GCC installations must expose exactly one cc1plus directory below
// <installation>/libexec/gcc. Other self-contained compiler drivers need no
// external program search directory and return an empty path.
class ARP_API CompilerToolchainLayout final {
public:
    CompilerToolchainLayoutResult inspect(const std::filesystem::path& compilerExecutable) const;

    // Captures every compiler-controlled program, system include, and library
    // directory consumed by inspect(). Returned paths point exclusively into
    // the immutable snapshot.
    CompilerToolchainSnapshotResult captureSnapshot(ImmutableBuildInputSnapshot& snapshot,
        std::string_view logicalRoot,
        const std::filesystem::path& compilerExecutable) const;
};

} // namespace arp::core
