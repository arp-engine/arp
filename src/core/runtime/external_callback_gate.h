#pragma once

namespace arp::core::runtime {
// Externally serialized, one-shot foreign callback lifetime. No completion
// success, GPU proof, rearming or cross-thread synchronization is implied.
class ExternalCallbackGate final {
public:
    enum class Phase { Unarmed, Armed, Delivering, Delivered, Detached };
    ExternalCallbackGate() = default;
    ExternalCallbackGate(const ExternalCallbackGate&) = delete;
    ExternalCallbackGate& operator=(const ExternalCallbackGate&) = delete;

    Phase phase() const noexcept { return phase_; }

    bool arm() noexcept
    {
        if (phase_ != Phase::Unarmed)
            return false;
        phase_ = Phase::Armed;
        return true;
    }

    bool beginDelivery() noexcept
    {
        if (phase_ != Phase::Armed)
            return false;
        phase_ = Phase::Delivering;
        return true;
    }

    bool finishDelivery() noexcept
    {
        if (phase_ != Phase::Delivering)
            return false;
        phase_ = Phase::Delivered;
        return true;
    }

    // The foreign runtime must first suppress late use of userdata. This gate
    // only records that fact; it cannot detach an external subscription itself.
    bool detach() noexcept
    {
        if (phase_ != Phase::Unarmed && phase_ != Phase::Armed)
            return false;
        phase_ = Phase::Detached;
        return true;
    }

private:
    Phase phase_ = Phase::Unarmed;
};
} // namespace arp::core::runtime
