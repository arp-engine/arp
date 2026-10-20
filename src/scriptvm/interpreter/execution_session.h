#pragma once

#include <cstdint>

namespace arp::scriptvm {

// One dispatcher entry owns this state. VM and AOT both consume it, so a
// backend bridge cannot renew instruction or recursion budgets.
struct ScriptExecutionSession {
    std::int64_t remainingInstructions = -1;
    int recursionLimit = 1024;
    int recursionDepth = 0;

    void begin(std::int64_t instructionBudget, int limit)
    {
        remainingInstructions = instructionBudget >= 0 ? instructionBudget : -1;
        recursionLimit = limit;
        recursionDepth = 0;
    }

    bool tryEnterFrame()
    {
        if (recursionLimit >= 0 && recursionDepth >= recursionLimit)
            return false;
        ++recursionDepth;
        return true;
    }

    void leaveFrame()
    {
        if (recursionDepth > 0)
            --recursionDepth;
    }
};

class ScriptExecutionFrameScope final {
public:
    explicit ScriptExecutionFrameScope(ScriptExecutionSession* session)
        : session_(session), active_(session_ == nullptr || session_->tryEnterFrame())
    {}

    ~ScriptExecutionFrameScope()
    {
        if (active_ && session_ != nullptr)
            session_->leaveFrame();
    }

    bool active() const { return active_; }

private:
    ScriptExecutionSession* session_ = nullptr;
    bool active_ = false;
};

} // namespace arp::scriptvm
