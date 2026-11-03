#pragma once

#include "core/io/file_service.h"
#include "records.h"

namespace arp::survivors {

enum class RecordPhase { Empty, Loading, Ready, Saving, Failed };

// Domain record ownership and optimistic conflict handling. FileService owns
// IO; callbacks retain only this session's state, never the game or host.
class RecordSession final {
public:
    static constexpr std::size_t maxFileBytes = 65536;
    // Separate explicit admission for the native writer's path/rename/chunk
    // storage. A candidate and a maximum-sized observed baseline remain distinct.
    static constexpr std::size_t maxBackendSaveScratchBytes = 65536;

    RecordSession(core::io::FileService&, core::PoolHandle mount);
    ~RecordSession();
    RecordSession(const RecordSession&) = delete;
    RecordSession& operator=(const RecordSession&) = delete;

    bool load() noexcept;
    bool save(const Record&) noexcept;
    RecordPhase phase() const noexcept;
    const Record& record() const noexcept;
    const char* diagnostic() const noexcept;
    const core::io::FileSaveResult& saveResult() const noexcept;

private:
    struct State;
    core::io::FileService& files_;
    core::PoolHandle mount_;
    core::SharedPtr<State> state_;
};
} // namespace arp::survivors
