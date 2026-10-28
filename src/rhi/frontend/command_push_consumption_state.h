#pragma once

namespace arp::rhi::semantics {
// Pass termination closes the encoder-local lifecycle without consuming it.
struct PushConstantConsumptionState {
    bool pending = false;

    bool hasUnconsumedUpdate() const { return pending; }

    void onPush() { pending = true; }

    void onConsume() { pending = false; }

    void onPassEnd() { pending = false; }
};
} // namespace arp::rhi::semantics
