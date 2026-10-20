#pragma once
#include "core/runtime/completion_evidence_batch.h"

namespace arp::core::runtime {
// Serialized, allocation-free lifecycle for an operation accepted only after
// every independent receipt and a final immutable-input validator succeed.
class CreationReceiptLifecycle final {
public:
    enum class Phase { Prepared, Pending, Ready, Rejected };

    explicit CreationReceiptLifecycle(std::uint32_t participants) noexcept : evidence_(participants)
    {}

    Phase phase() const noexcept { return phase_; }

    const char* failure() const noexcept { return failure_; }

    const CompletionEvidenceBatch& evidence() const noexcept { return evidence_; }

    bool begin() noexcept
    {
        if (phase_ != Phase::Prepared)
            return false;
        phase_ = Phase::Pending;
        return true;
    }

    void reject(const char* diagnostic) noexcept
    {
        if (!failure_)
            failure_ = diagnostic;
        phase_ = Phase::Rejected;
    }

    template <class Validate>
    void observe(std::uint32_t participant,
        bool successful,
        const char* receiptDiagnostic,
        Validate&& validate) noexcept
    {
        static_assert(noexcept(validate()), "Receipt validator must not throw");
        const bool accepted = evidence_.observe(participant, successful);
        if (phase_ == Phase::Prepared || phase_ == Phase::Ready) {
            reject(receiptDiagnostic);
            return;
        }
        if (phase_ != Phase::Pending)
            return;
        if (!accepted || !successful) {
            reject(receiptDiagnostic);
            return;
        }
        if (!evidence_.allReceived())
            return;
        if (const auto* id = validate()) {
            reject(id);
            return;
        }
        phase_ = Phase::Ready;
    }

private:
    CompletionEvidenceBatch evidence_;
    Phase phase_ = Phase::Prepared;
    const char* failure_ = nullptr;
};
} // namespace arp::core::runtime
