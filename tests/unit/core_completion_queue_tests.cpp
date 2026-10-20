#include "core/runtime/completion_queue.h"

#include <cassert>
#include <cstdint>

namespace {

struct OrderProbe {
    int values[4]{};
    std::uint32_t count = 0;
};

void recordOne(void* userData)
{
    auto* probe = static_cast<OrderProbe*>(userData);
    probe->values[probe->count++] = 1;
}

void recordTwo(void* userData)
{
    auto* probe = static_cast<OrderProbe*>(userData);
    probe->values[probe->count++] = 2;
}

void recordThree(void* userData)
{
    auto* probe = static_cast<OrderProbe*>(userData);
    probe->values[probe->count++] = 3;
}

} // namespace

int main()
{
    using namespace arp::core::runtime;

    CompletionQueue queue(CompletionQueueDesc{4});
    OrderProbe probe;

    assert(queue.post(CompletionCallback{&recordOne, &probe}).status == CompletionStatus::Accepted);
    assert(queue.post(CompletionCallback{&recordTwo, &probe}).status == CompletionStatus::Accepted);
    assert(
        queue.post(CompletionCallback{&recordThree, &probe}).status == CompletionStatus::Accepted);
    assert(queue.telemetry().submitted == 3);
    assert(queue.telemetry().maxQueueDepth == 3);

    assert(queue.dispatch(2).dispatched == 2);
    assert(probe.count == 2);
    assert(probe.values[0] == 1);
    assert(probe.values[1] == 2);
    assert(queue.telemetry().dispatched == 2);

    assert(queue.dispatch(2).dispatched == 1);
    assert(probe.count == 3);
    assert(probe.values[2] == 3);

    assert(queue.dispatch(1).dispatched == 0);
    assert(probe.count == 3);

    assert(queue.post(CompletionCallback{&recordOne, &probe}).status == CompletionStatus::Accepted);
    queue.shutdown();
    assert(queue.post(CompletionCallback{&recordTwo, &probe}).status
        == CompletionStatus::ServiceClosed);
    assert(queue.telemetry().rejected == 1);
    assert(queue.dispatch(1).dispatched == 1);
    assert(probe.count == 4);

    CompletionQueue tiny(CompletionQueueDesc{1});
    assert(tiny.post(CompletionCallback{&recordOne, &probe}).status == CompletionStatus::Accepted);
    assert(tiny.post(CompletionCallback{&recordTwo, &probe}).status == CompletionStatus::QueueFull);
    assert(tiny.telemetry().rejected == 1);
    assert(tiny.dispatch(0).status == CompletionStatus::InvalidDispatchBudget);

    return 0;
}
