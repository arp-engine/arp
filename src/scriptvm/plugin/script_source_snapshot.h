#pragma once

#include "core/api.h"
#include "core/io/file_request.h"
#include "core/io/file_result.h"

namespace arp::scriptvm {

struct ScriptSourceBudget {
    std::size_t maxFiles = 0;
    std::uint64_t maxFileBytes = 0;
    std::uint64_t maxSourceBytes = 0;
    std::uint64_t maxTextBytes = 0;
};

// Preparation is owner-thread work. Once sealed, original bytes and decoded
// source strings stay immutable and require no filesystem or backend lifetime.
class ARP_API ScriptSourceSnapshot final {
public:
    struct Entry {
        core::pmr::String moduleId;
        core::io::FileReadRequest binding;
        core::io::FileReadResult source;
        core::pmr::String text;
        core::pmr::String textHash;
    };

    explicit ScriptSourceSnapshot(ScriptSourceBudget budget) : budget_(budget) {}

    ScriptSourceSnapshot(const ScriptSourceSnapshot&) = delete;
    ScriptSourceSnapshot& operator=(const ScriptSourceSnapshot&) = delete;
    ScriptSourceSnapshot(ScriptSourceSnapshot&&) = default;
    ScriptSourceSnapshot& operator=(ScriptSourceSnapshot&&) = default;

    bool add(core::StringView moduleId,
        const core::io::FileReadRequest& binding,
        core::io::FileReadResult source,
        const char*& diagnosticId) noexcept;
    bool seal(const char*& diagnosticId) noexcept;
    const Entry* find(core::StringView moduleId) const noexcept;

    bool sealed() const noexcept { return sealed_; }

    std::size_t size() const noexcept { return entries_.size(); }

    std::uint64_t sourceBytes() const noexcept { return sourceBytes_; }

    std::uint64_t textBytes() const noexcept { return textBytes_; }

    const ScriptSourceBudget& budget() const noexcept { return budget_; }

    static bool validBudget(const ScriptSourceBudget& budget) noexcept;

private:
    ScriptSourceBudget budget_;
    core::pmr::Vector<Entry> entries_;
    std::uint64_t sourceBytes_ = 0;
    std::uint64_t textBytes_ = 0;
    bool sealed_ = false;
};

} // namespace arp::scriptvm
