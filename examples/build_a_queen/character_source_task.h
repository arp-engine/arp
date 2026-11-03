#pragma once

#include "character_sources.h"
#include "core/io/file_service.h"

namespace arp::build_a_queen {

enum class CharacterSourcePhase { Pending, Ready, Failed, Cancelled, Consumed };

// Owner-thread coordinator. advance() never waits or dispatches callbacks.
// FileService and its queue must outlive pending callbacks; destroying this
// task cancels its work while retained callback state safely retires later.
class CharacterSourceTask final {
public:
    CharacterSourceTask(core::io::FileService& files, core::io::FileReadRequest manifest);
    ~CharacterSourceTask();
    CharacterSourceTask(const CharacterSourceTask&) = delete;
    CharacterSourceTask& operator=(const CharacterSourceTask&) = delete;

    void advance() noexcept;
    void cancel() noexcept;
    CharacterSourcePhase phase() const noexcept;
    const char* diagnostic() const noexcept;
    core::io::FileIoStatus ioStatus() const noexcept;
    core::UniquePtr<CharacterSources> takeSources() noexcept;

private:
    struct State;
    static void completed(void*, core::io::FileReadResult) noexcept;
    core::SharedPtr<State> state_;
};

} // namespace arp::build_a_queen
