#pragma once
#include <memory>
#include <utility>

namespace arp::core::runtime {
// Observer lifetime only. The actual callback owner supplies the drain fact;
// neither receipt nor this observer grants success or execution authority.
class CompletionDrainConsumer {
public:
    virtual ~CompletionDrainConsumer() = default;
    virtual void consumeOriginalCallbackDrain() noexcept = 0;

    static bool consumeOnce(
        bool drained, std::shared_ptr<CompletionDrainConsumer>& original) noexcept
    {
        if (!drained || !original)
            return false;
        auto consumer = std::move(original);
        consumer->consumeOriginalCallbackDrain();
        return true;
    }
};
} // namespace arp::core::runtime
