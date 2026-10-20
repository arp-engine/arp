#pragma once
#include <cstdint>

namespace arp::rhi {

// Value intent only, never device/native authorization.
struct IndirectRecordingBudget {
    std::uint32_t contractVersion = 1u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxCalls = 0u, maxRecords = 0u;

    const char* validate() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-INDIRECT-RECORDING-VERSION";
        if (!prototypeOptIn)
            return "RHI-INDIRECT-RECORDING-OPT-IN-REQUIRED";
        return maxCommands >= 3u && maxCalls && maxRecords ? nullptr
                                                           : "RHI-INDIRECT-RECORDING-BUDGET";
    }
};

// One pass pair, finite argument work, no resource state cache or allocation.
class IndirectRecordingBudgetState final {
public:
    enum class Phase { Empty, PassOpen, PassEnded };

    explicit IndirectRecordingBudgetState(IndirectRecordingBudget budget) noexcept : budget_(budget)
    {}

    const char* canBegin() const noexcept
    {
        if (const auto* id = budget_.validate())
            return id;
        return phase_ == Phase::Empty ? capacity(1u) : "RHI-INDIRECT-RECORDING-PASS-BEGIN";
    }

    const char* begin() noexcept
    {
        if (const auto* id = canBegin())
            return id;
        phase_ = Phase::PassOpen;
        ++commands_;
        return nullptr;
    }

    const char* canStateCommand() const noexcept
    {
        if (phase_ != Phase::PassOpen)
            return "RHI-INDIRECT-RECORDING-PASS-REQUIRED";
        return capacity(2u); // state command plus the required end
    }

    const char* stateCommand() noexcept
    {
        if (const auto* id = canStateCommand())
            return id;
        ++commands_;
        return nullptr;
    }

    const char* canDraw(std::uint64_t records) const noexcept
    {
        if (phase_ != Phase::PassOpen)
            return "RHI-INDIRECT-RECORDING-PASS-REQUIRED";
        if (calls_ >= budget_.maxCalls)
            return "RHI-INDIRECT-RECORDING-CALL-BUDGET";
        if (!records || records > budget_.maxRecords - records_)
            return "RHI-INDIRECT-RECORDING-RECORD-BUDGET";
        return capacity(records + 1u); // native records plus the required end
    }

    const char* draw(std::uint64_t records) noexcept
    {
        if (const auto* id = canDraw(records))
            return id;
        ++calls_;
        records_ += static_cast<std::uint32_t>(records);
        commands_ += static_cast<std::uint32_t>(records);
        return nullptr;
    }

    const char* canEnd() const noexcept
    {
        if (phase_ != Phase::PassOpen)
            return "RHI-INDIRECT-RECORDING-PASS-END";
        if (!calls_)
            return "RHI-INDIRECT-RECORDING-DRAW-REQUIRED";
        return capacity(1u);
    }

    const char* end() noexcept
    {
        if (const auto* id = canEnd())
            return id;
        phase_ = Phase::PassEnded;
        ++commands_;
        return nullptr;
    }

    const char* finish() const noexcept
    {
        return phase_ == Phase::PassEnded ? nullptr : "RHI-INDIRECT-RECORDING-INCOMPLETE";
    }

    std::uint32_t commands() const noexcept { return commands_; }

    std::uint32_t calls() const noexcept { return calls_; }

    std::uint32_t records() const noexcept { return records_; }

    Phase phase() const noexcept { return phase_; }

private:
    const char* capacity(std::uint64_t units) const noexcept
    {
        return units <= budget_.maxCommands - commands_ ? nullptr
                                                        : "RHI-INDIRECT-RECORDING-COMMAND-BUDGET";
    }

    IndirectRecordingBudget budget_;
    Phase phase_ = Phase::Empty;
    std::uint32_t commands_ = 0u, calls_ = 0u, records_ = 0u;
};
} // namespace arp::rhi
