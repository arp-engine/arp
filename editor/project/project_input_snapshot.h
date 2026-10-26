#pragma once

#include "core/memory/cpu_allocator.h"

#include <filesystem>
#include <string_view>

namespace arp::editor {

struct ProjectInputBinding {
    core::pmr::String path;
    std::size_t byteCount = 0;
    core::pmr::String fingerprint;
};

struct ProjectInputBudget {
    static constexpr std::size_t maxPathBytes = 512u;
    std::size_t maxFiles = 4096u;
    std::size_t maxFileBytes = 64u * 1024u * 1024u;
    std::size_t maxTotalBytes = 512u * 1024u * 1024u;
};

// Editor source provenance, recorded at existing domain read boundaries. The
// snapshot never discovers files or supplies substitute resource contents.
class ProjectInputSnapshot final {
public:
    explicit ProjectInputSnapshot(std::filesystem::path root, ProjectInputBudget budget = {});
    bool record(
        const std::filesystem::path& file, std::string_view bytes, core::pmr::String& diagnostic);
    bool read(const std::filesystem::path& file, core::pmr::String& diagnostic);
    bool verify(core::pmr::String& diagnostic) const;

    const core::pmr::Vector<ProjectInputBinding>& bindings() const noexcept { return bindings_; }

    core::pmr::Vector<ProjectInputBinding> takeBindings();

    static bool validateBindings(const core::pmr::Vector<ProjectInputBinding>& bindings,
        core::pmr::String& diagnostic,
        ProjectInputBudget budget = {});
    static bool verify(const std::filesystem::path& root,
        const core::pmr::Vector<ProjectInputBinding>& bindings,
        core::pmr::String& diagnostic,
        ProjectInputBudget budget = {});
    static bool equal(const core::pmr::Vector<ProjectInputBinding>& left,
        const core::pmr::Vector<ProjectInputBinding>& right) noexcept;

private:
    std::filesystem::path root_;
    ProjectInputBudget budget_;
    std::size_t usedBytes_ = 0;
    core::pmr::Vector<ProjectInputBinding> bindings_;
};

} // namespace arp::editor
