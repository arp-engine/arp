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

    CompletionQueue reservedQueue({2});
    OrderProbe reservedProbe;
    auto first = reservedQueue.reserve();
    assert(first.valid() && reservedQueue.reserved() == 1);
    {
        auto abandoned = reservedQueue.reserve();
        assert(abandoned.valid());
        assert(reservedQueue.reserve().status() == CompletionStatus::QueueFull);
        assert(
            reservedQueue.post({recordTwo, &reservedProbe}).status == CompletionStatus::QueueFull);
    }
    assert(reservedQueue.reserved() == 1);
    auto moved = std::move(first);
    assert(!first.valid() && moved.valid());
    assert(reservedQueue.post({recordOne, &reservedProbe}).accepted());
    reservedQueue.shutdown();
    assert(reservedQueue.reserve().status() == CompletionStatus::ServiceClosed);
    assert(reservedQueue.postReserved(std::move(moved), {recordTwo, &reservedProbe}).accepted());
    assert(!moved.valid() && reservedQueue.reserved() == 0);
    assert(reservedQueue.postReserved(std::move(moved), {recordThree, &reservedProbe}).status
        == CompletionStatus::InvalidReservation);
    assert(reservedQueue.dispatch(2).dispatched == 2);
    assert(
        reservedProbe.count == 2 && reservedProbe.values[0] == 1 && reservedProbe.values[1] == 2);

    CompletionQueue other({1});
    auto otherToken = other.reserve();
    assert(reservedQueue.postReserved(std::move(otherToken), {recordOne, &reservedProbe}).status
        == CompletionStatus::InvalidReservation);
    assert(otherToken.valid());
    assert(
        other.postReserved(std::move(otherToken), {}).status == CompletionStatus::InvalidCallback);
    assert(otherToken.valid());
    otherToken = {};
    assert(other.reserved() == 0);

    return 0;
}
