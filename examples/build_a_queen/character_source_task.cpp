#include "character_source_task.h"

#include <optional>

namespace arp::build_a_queen {
using namespace core::io;

struct CharacterSourceTask::State {
    FileService* files = nullptr;
    FileReadRequest binding;
    core::runtime::CancellationToken externalCancellation;
    core::runtime::CancellationSource cancellation;
    core::UniquePtr<CharacterSources> sources = core::makeUnique<CharacterSources>();
    std::optional<FileReadResult> delivered;
    CharacterSourcePhase phase = CharacterSourcePhase::Pending;
    const char* diagnostic = nullptr;
    FileIoStatus ioStatus = FileIoStatus::Success;
    core::pmr::String diagnosticText{&core::defaultCpuMemoryResource()};
    std::size_t stage = 0;
    bool submitted = false;

    void fail(const char* issue) noexcept
    {
        cancellation.cancel();
        phase = CharacterSourcePhase::Failed;
        diagnostic = issue ? issue : "ARP-QUEEN-SOURCE-TASK-FAILED";
        delivered.reset();
        sources.reset();
    }

    void failResult(FileReadResult result) noexcept
    {
        ioStatus = result.status;
        diagnosticText = std::move(result.diagnostics);
        fail(result.diagnosticId         ? result.diagnosticId
                : diagnosticText.empty() ? "ARP-QUEEN-SOURCE-TASK-IO"
                                         : diagnosticText.c_str());
    }
};

CharacterSourceTask::CharacterSourceTask(FileService& files, FileReadRequest manifest)
    : state_(core::makeShared<State>())
{
    state_->files = &files;
    state_->externalCancellation = manifest.cancellation;
    state_->binding = std::move(manifest);
    state_->binding.cancellation = state_->cancellation.token();
}

CharacterSourceTask::~CharacterSourceTask()
{
    cancel();
}

CharacterSourcePhase CharacterSourceTask::phase() const noexcept
{
    return state_->phase;
}

const char* CharacterSourceTask::diagnostic() const noexcept
{
    return state_->diagnostic;
}

FileIoStatus CharacterSourceTask::ioStatus() const noexcept
{
    return state_->ioStatus;
}

void CharacterSourceTask::cancel() noexcept
{
    if (state_->phase == CharacterSourcePhase::Pending) {
        state_->ioStatus = FileIoStatus::Cancelled;
        state_->fail("ARP-QUEEN-SOURCE-TASK-CANCELLED");
        state_->phase = CharacterSourcePhase::Cancelled;
    }
}

core::UniquePtr<CharacterSources> CharacterSourceTask::takeSources() noexcept
{
    if (state_->phase != CharacterSourcePhase::Ready)
        return {};
    state_->phase = CharacterSourcePhase::Consumed;
    return std::move(state_->sources);
}

void CharacterSourceTask::completed(void* context, FileReadResult result) noexcept
{
    using Retained = core::SharedPtr<State>;
    core::UniquePtr<Retained> retained{static_cast<Retained*>(context),
        core::CpuAllocatedDeleter<Retained>{context, &core::destroyCpuAllocatedUnique<Retained>}};
    auto& state = **retained;
    if (state.phase == CharacterSourcePhase::Pending)
        state.delivered.emplace(std::move(result));
}

void CharacterSourceTask::advance() noexcept
{
    auto& s = *state_;
    if (s.phase != CharacterSourcePhase::Pending)
        return;
    try {
        if (s.externalCancellation.cancelled()) {
            cancel();
            return;
        }
        if (s.stage == 0
            && (s.binding.contractVersion != 1
                || s.binding.pathScope != FilePathScope::MountedResource || !s.binding.mount
                || s.binding.range.mode != FileReadRangeMode::WholeFile
                || s.binding.partialPolicy != FilePartialReadPolicy::DenyPartial
                || s.binding.maxBytes == 0
                || s.binding.maxBytes > CharacterSources::maxManifestBytes)) {
            s.fail("ARP-QUEEN-SOURCE-TASK-MANIFEST-REQUEST");
            return;
        }
        auto admission = s.files->validateRead(s.binding);
        if (admission.status != FileIoStatus::Success) {
            s.failResult(std::move(admission));
            return;
        }
        if (s.delivered) {
            if (s.delivered->status != FileIoStatus::Success) {
                s.failResult(std::move(*s.delivered));
                return;
            }
            const bool accepted = s.stage == 0
                ? s.sources->setManifest(s.binding, std::move(*s.delivered))
                : s.sources->addCharacter(s.binding, std::move(*s.delivered));
            s.delivered.reset();
            if (!accepted) {
                s.fail(s.sources->diagnostic());
                return;
            }
            ++s.stage;
            if (s.sources->ready()) {
                s.phase = CharacterSourcePhase::Ready;
                return;
            }
            s.binding.path = s.sources->characterPath(s.stage - 1);
            s.binding.maxBytes = CharacterSources::maxCharacterBytes;
            // The manifest's observed version is not a GLB version condition.
            s.binding.expectedVersion = {};
            s.binding.maxVersionBytes = 0;
            s.submitted = false;
            return;
        }
        if (s.submitted)
            return;
        auto retained = core::makeUnique<core::SharedPtr<State>>(state_);
        const auto submitted = s.files->read(s.binding, {completed, retained.get()});
        if (submitted != FileIoStatus::Accepted) {
            s.ioStatus = submitted;
            s.fail("ARP-QUEEN-SOURCE-TASK-NOT-ACCEPTED");
            return;
        }
        retained.release();
        s.submitted = true;
    } catch (...) {
        s.fail("ARP-QUEEN-SOURCE-TASK-PREPARATION");
    }
}

} // namespace arp::build_a_queen
