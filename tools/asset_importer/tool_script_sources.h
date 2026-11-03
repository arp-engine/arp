#pragma once

#include "core/io/file_service.h"
#include "scriptvm/plugin/script_source_snapshot.h"

namespace arp::tools::asset_importer {
struct ToolScriptSourceLimits {
    static constexpr const char* entryModule = "@entry";
    static constexpr std::size_t maxFiles = 64, maxNames = 128;
    static constexpr std::size_t maxPathBytes = 512, maxModuleBytes = 128;
    static constexpr std::uint64_t maxFileBytes = 2u * 1024u * 1024u;
    static constexpr std::uint64_t maxSourceBytes = 16u * 1024u * 1024u;

    static constexpr scriptvm::ScriptSourceBudget budget()
    {
        return {maxFiles, maxFileBytes, maxSourceBytes, maxSourceBytes};
    }
};

struct ToolScriptSourceDesc {
    ToolScriptSourceDesc() = default;

    ToolScriptSourceDesc(const ToolScriptSourceDesc& other) : ToolScriptSourceDesc()
    {
        *this = other;
    }

    ToolScriptSourceDesc& operator=(const ToolScriptSourceDesc&) = default;
    ToolScriptSourceDesc(ToolScriptSourceDesc&&) = default;
    ToolScriptSourceDesc& operator=(ToolScriptSourceDesc&&) = default;

    std::uint32_t contractVersion = 1;
    core::io::FileReadRequest entry;
    core::io::FilePathScope moduleScope = core::io::FilePathScope::MountedResource;
    core::PoolHandle moduleMount;
    core::pmr::String modulePrefix{&core::defaultCpuMemoryResource()};
    // Diagnostic labels are provenance only, never alternate read locations.
    core::pmr::String entryDisplayName{&core::defaultCpuMemoryResource()};
    core::pmr::String moduleDisplayPrefix{&core::defaultCpuMemoryResource()};
    scriptvm::ScriptSourceBudget budget = ToolScriptSourceLimits::budget();
    std::size_t maxNames = ToolScriptSourceLimits::maxNames;
};

class ToolScriptSourceTask;

class ToolScriptSources final {
public:
    ToolScriptSources() = default;
    ToolScriptSources(const ToolScriptSources&) = delete;
    ToolScriptSources& operator=(const ToolScriptSources&) = delete;

    struct Module {
        Module(core::StringView moduleName,
            core::StringView modulePath,
            core::StringView label,
            core::SharedPtr<const scriptvm::ScriptSourceSnapshot> input,
            bool missing)
            : name(moduleName, &core::defaultCpuMemoryResource()),
              path(modulePath, &core::defaultCpuMemoryResource()),
              displayName(label, &core::defaultCpuMemoryResource()), snapshot(std::move(input)),
              absent(missing)
        {}

        core::pmr::String name, path, displayName;
        core::SharedPtr<const scriptvm::ScriptSourceSnapshot> snapshot;
        bool absent = false;

        const scriptvm::ScriptSourceSnapshot::Entry* source() const noexcept
        {
            return snapshot ? snapshot->find(name) : nullptr;
        }
    };

    const Module* find(core::StringView name) const noexcept;

    const Module* entry() const noexcept { return find(ToolScriptSourceLimits::entryModule); }

    const core::pmr::Vector<Module>& modules() const noexcept { return modules_; }

    bool ready() const noexcept { return ready_; }

    std::uint64_t sourceBytes() const noexcept { return sourceBytes_; }

    std::uint64_t textBytes() const noexcept { return textBytes_; }

    std::size_t fileCount() const noexcept { return fileCount_; }

private:
    friend class ToolScriptSourceTask;
    core::pmr::Vector<Module> modules_{&core::defaultCpuMemoryResource()};
    std::uint64_t sourceBytes_ = 0, textBytes_ = 0;
    std::size_t fileCount_ = 0;
    bool ready_ = false;
};

enum class ToolScriptSourcePhase { Pending, Ready, Failed, Cancelled };

// A single in-flight source read; advance() yields after a bounded stage. The
// FileService/queue outlive accepted callbacks. Destruction cancels safely and
// does not require a browser busy-wait or retain the Task's own stack address.
class ToolScriptSourceTask final {
public:
    ToolScriptSourceTask(core::io::FileService&, const ToolScriptSourceDesc&);
    ~ToolScriptSourceTask();
    ToolScriptSourceTask(const ToolScriptSourceTask&) = delete;
    ToolScriptSourceTask& operator=(const ToolScriptSourceTask&) = delete;
    void advance() noexcept;
    void cancel() noexcept;
    ToolScriptSourcePhase phase() const noexcept;
    const char* diagnosticId() const noexcept;
    core::StringView diagnostic() const noexcept;
    core::SharedPtr<const ToolScriptSources> sources() const noexcept;
    std::size_t readCount() const noexcept;

private:
    struct State;
    static bool consume(State&);
    static void fail(State&, const char* diagnostic);
    core::SharedPtr<State> state_;
};
} // namespace arp::tools::asset_importer
