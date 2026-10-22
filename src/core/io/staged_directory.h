#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <filesystem>

namespace arp::core::io {

// Preparation/tooling-only publication of a complete new directory. Never
// replaces an existing destination and never falls back to copying files.
class ARP_API StagedDirectory final {
public:
    ~StagedDirectory();
    StagedDirectory() = default;
    StagedDirectory(const StagedDirectory&) = delete;
    StagedDirectory& operator=(const StagedDirectory&) = delete;

    bool begin(const std::filesystem::path& destination);
    bool publish();

    const std::filesystem::path& path() const noexcept { return staging_; }

    // Stable diagnostic ID; native publication failures append
    // " native=<error-category>:<numeric-code>" without automatic retry.
    const pmr::String& diagnostic() const noexcept { return diagnostic_; }

private:
    std::filesystem::path parent_, destination_, staging_;
    pmr::String diagnostic_{&defaultCpuMemoryResource()};
    bool owned_ = false;
};

} // namespace arp::core::io
