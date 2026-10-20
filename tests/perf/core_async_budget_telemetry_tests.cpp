#include "core/runtime/async_runtime.h"

#include <cassert>

namespace {

struct Probe {
    int calls = 0;
};

void recordCompletion(void* userData)
{
    ++static_cast<Probe*>(userData)->calls;
}

} // namespace

int main()
{
    using namespace arp::core::runtime;

    AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8;
    desc.workerCount = 1;
    desc.workQueueCapacity = 4;
    desc.completionBudget = 2;
    desc.enableWork = true;
    desc.enableThreadPoolFileBackend = true;
    desc.enableExternalNetworkValidation = true;

    AsyncRuntime runtime(desc);
    assert(runtime.valid());

    Probe probe;
    for (int i = 0; i < 5; ++i) {
        assert(runtime.completionQueue()
                .post(CompletionCallback{&recordCompletion, &probe})
                .accepted());
    }

    auto dispatch = runtime.dispatch();
    assert(dispatch.status == CompletionStatus::Accepted);
    assert(dispatch.dispatched == 2);
    assert(probe.calls == 2);

    dispatch = runtime.dispatch();
    assert(dispatch.dispatched == 2);
    assert(probe.calls == 4);

    dispatch = runtime.dispatch();
    assert(dispatch.dispatched == 1);
    assert(probe.calls == 5);

    const auto telemetry = runtime.telemetry();
    assert(telemetry.completion.submitted == 5);
    assert(telemetry.completion.dispatched == 5);
    assert(telemetry.completion.maxQueueDepth == 5);
    assert(telemetry.completionBudget == 2);
    assert(telemetry.dispatchCalls == 3);
    assert(telemetry.externalNetworkValidationEnabled);

    runtime.shutdown(AsyncRuntimeShutdownPolicy::Drain);
    return 0;
}
