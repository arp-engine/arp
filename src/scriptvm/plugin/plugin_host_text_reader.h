#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <filesystem>

namespace arp::scriptvm {

// PluginHostText/1: finite, caller-adjustable limits for native tool outputs.
// Diagnostic excerpts read at most maxDiagnosticBytes + 1 for truncation detection.
struct PluginHostTextBudget {
    std::uint32_t contractVersion = 1;
    std::uint64_t maxMetadataBytes = 16u * 1024u * 1024u;
    std::uint64_t maxAuditBytes = 64u * 1024u * 1024u;
    std::uint64_t maxDiagnosticBytes = 8192u;
};

enum class PluginHostTextKind { Metadata, Audit };

struct PluginHostTextResult {
    bool success = false;
    bool truncated = false;
    const char* diagnosticId = nullptr;
    core::pmr::String text{&core::defaultCpuMemoryResource()};
};

// Generated metadata and audits require complete UTF-8 (BOM/newlines preserved,
// NUL rejected). Process diagnostics are byte excerpts: non-ASCII/control bytes
// are escaped, so an unknown process encoding or split scalar cannot corrupt UI.
// This is a native compiler boundary; it never treats a URL as an OS path.
class ARP_API PluginHostTextReader final {
public:
    explicit PluginHostTextReader(PluginHostTextBudget budget) : budget_(budget) {}

    static bool validBudget(const PluginHostTextBudget& budget) noexcept;
    PluginHostTextResult read(
        const std::filesystem::path& path, PluginHostTextKind kind) const noexcept;
    PluginHostTextResult diagnostic(const std::filesystem::path& path) const noexcept;

private:
    PluginHostTextBudget budget_;
};

} // namespace arp::scriptvm
