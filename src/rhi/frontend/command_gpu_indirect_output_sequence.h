#pragma once
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/command_graphics_storage_write_sequence.h"
#include "rhi/frontend/command_indirect_recording_budget.h"

namespace arp::rhi {
// Structural lifecycle validation only. The backend binds each transition to
// real command admission/commit/completion; this value creates no GPU ticket.
class GpuIndirectOutputLifetimeContract final {
public:
    static constexpr std::uint32_t version = 1u;
    enum class Status { Cold, Recorded, Submitted, Completed, Failed, Abandoned };

    static const char* validateTransition(
        Status before, Status after, bool nativeCompletionProven = false) noexcept
    {
        if (before == Status::Cold && after == Status::Recorded)
            return nullptr;
        if (before == Status::Recorded
            && (after == Status::Submitted || after == Status::Failed
                || after == Status::Abandoned))
            return nullptr;
        if (before == Status::Submitted && (after == Status::Completed || after == Status::Failed))
            return nativeCompletionProven ? nullptr
                                          : "RHI-GPU-INDIRECT-LIFETIME-NATIVE-COMPLETION-REQUIRED";
        return "RHI-GPU-INDIRECT-LIFETIME-TRANSITION";
    }
};

// Value intent, not an execution ticket. The backend supplies the issued
// identity and independently proves encoder order, resource state and owners.
struct GpuIndirectOutputRecordingIdentity {
    GpuResourceId resource;
    std::uint64_t generation = 0u, recording = 0u;

    bool operator==(const GpuIndirectOutputRecordingIdentity& b) const noexcept
    {
        return resource == b.resource && generation == b.generation && recording == b.recording;
    }

    bool valid() const noexcept { return resource.isValid() && generation && recording; }
};

struct GpuIndirectOutputRecordingBudget {
    static constexpr std::uint32_t version = 1u;
    // Begin, dispatch, end, dependency: four distinct command units, followed
    // by the existing consumer begin/record/end and any stream state commands.
    static constexpr std::uint32_t producerCommandUnits = 4u;
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u;

    const char* validate() const noexcept
    {
        if (contractVersion != version || !prototypeOptIn)
            return "RHI-GPU-INDIRECT-SEQUENCE-VERSION-OPT-IN";
        return maxCommands >= producerCommandUnits + 3u ? nullptr
                                                        : "RHI-GPU-INDIRECT-SEQUENCE-BUDGET";
    }

    IndirectRecordingBudget consumerBudget() const noexcept
    {
        return {1u,
            prototypeOptIn,
            maxCommands >= producerCommandUnits ? maxCommands - producerCommandUnits : 0u,
            1u,
            1u};
    }
};

// Composition preserves existing producer order and indirect budget semantics.
// Admission is transactional: failed identity/order/budget checks change nothing.
// This class caches no ResourceState and provides no native completion signal.
class GpuIndirectOutputSequence final {
public:
    static constexpr std::uint32_t version = 1u;
    enum class Command {
        ProducerBegin,
        Dispatch,
        ProducerEnd,
        Dependency,
        ConsumerBegin,
        StreamBinding,
        Indirect,
        ConsumerEnd
    };

    GpuIndirectOutputSequence(GpuIndirectOutputRecordingBudget budget,
        GpuIndirectOutputRecordingIdentity identity) noexcept
        : budget_(budget), identity_(identity), consumer_(budget.consumerBudget())
    {}

    const char* canRecord(
        Command command, const GpuIndirectOutputRecordingIdentity& identity) const noexcept
    {
        auto prospective = *this;
        return prospective.apply(command, identity);
    }

    const char* record(Command command, const GpuIndirectOutputRecordingIdentity& identity) noexcept
    {
        auto prospective = *this;
        if (const auto* id = prospective.apply(command, identity))
            return id;
        *this = prospective;
        return nullptr;
    }

    const char* finish() const noexcept
    {
        if (const auto* id = budget_.validate())
            return id;
        if (!identity_.valid())
            return "RHI-GPU-INDIRECT-OUTPUT-RECORDING-MISMATCH";
        if (const auto* id = producer_.finish())
            return id;
        return consumer_.finish();
    }

    auto phase() const noexcept { return producer_.phase(); }

    std::uint32_t commands() const noexcept { return producerCommands_ + consumer_.commands(); }

    std::uint32_t calls() const noexcept { return consumer_.calls(); }

    std::uint32_t records() const noexcept { return consumer_.records(); }

    const auto& identity() const noexcept { return identity_; }

private:
    const char* apply(Command command, const GpuIndirectOutputRecordingIdentity& identity) noexcept
    {
        if (const auto* id = budget_.validate())
            return id;
        if (!identity_.valid() || !identity.valid() || !(identity == identity_))
            return "RHI-GPU-INDIRECT-OUTPUT-RECORDING-MISMATCH";
        const auto resource = identity_.resource;
        const char* id = nullptr;
        switch (command) {
        case Command::ProducerBegin:
            id = producer_.begin(resource);
            break;
        case Command::Dispatch:
            if (dispatched_)
                return "RHI-GPU-INDIRECT-SEQUENCE-DISPATCH-ALREADY-RECORDED";
            id = producer_.draw(resource);
            if (!id)
                dispatched_ = true;
            break;
        case Command::ProducerEnd:
            id = producer_.end(resource);
            break;
        case Command::Dependency:
            id = producer_.depend(resource);
            break;
        case Command::ConsumerBegin:
            if ((id = producer_.beginConsumer(resource)))
                return id;
            return consumer_.begin();
        case Command::StreamBinding:
            if (producer_.phase() != GraphicsStorageWriteSequence::Phase::ConsumerOpen)
                return "RHI-INDIRECT-RECORDING-PASS-REQUIRED";
            return consumer_.stateCommand();
        case Command::Indirect:
            if ((id = producer_.drawConsumer(resource)))
                return id;
            return consumer_.draw(1u);
        case Command::ConsumerEnd:
            if ((id = producer_.endConsumer(resource)))
                return id;
            return consumer_.end();
        default:
            return "RHI-GPU-INDIRECT-SEQUENCE-COMMAND-DOMAIN";
        }
        if (!id)
            ++producerCommands_;
        return id;
    }

    GpuIndirectOutputRecordingBudget budget_;
    GpuIndirectOutputRecordingIdentity identity_;
    GraphicsStorageWriteSequence producer_;
    IndirectRecordingBudgetState consumer_;
    std::uint32_t producerCommands_ = 0u;
    bool dispatched_ = false;
};

namespace semantics {
class GpuIndirectOutputRecordingLowering final {
public:
    static constexpr GpuIndirectProducerLoweringPlan current(Backend backend) noexcept
    {
        return CommandDrawLowering::indirectSourceDisposition(backend,
            GpuIndirectProducerLowering::current(backend),
            GpuIndirectOutputSequence::version,
            "RHI-GPU-INDIRECT-SEQUENCE-BACKEND-UNKNOWN",
            "RHI-GPU-INDIRECT-SEQUENCE-VALIDATOR-ONLY",
            "RHI-GPU-INDIRECT-SEQUENCE-PAYLOAD-MISSING");
    }
};
} // namespace semantics
} // namespace arp::rhi
