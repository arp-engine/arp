#include "core/runtime/work_service.h"

#include <cassert>
#include <stdexcept>

namespace {

using namespace arp::core::runtime;

struct Probe {
    unsigned executed = 0;
    unsigned callbacks = 0;
    WorkStatus status = WorkStatus::Completed;
};

WorkStatus run(void* context)
{
    ++static_cast<Probe*>(context)->executed;
    return WorkStatus::Completed;
}

WorkStatus fail(void*)
{
    throw std::runtime_error("fixture execution failure");
}

void complete(void* context, WorkResult result)
{
    auto& probe = *static_cast<Probe*>(context);
    ++probe.callbacks;
    probe.status = result.status;
}

void checkCapacity(bool inlineWork)
{
    CompletionQueue queue({1});
    WorkService work({&queue, 1, 4, inlineWork});
    Probe probe;
    const WorkItem item{run, &probe, complete, &probe};
    assert(work.submit(item).accepted());
    // Whether the worker has executed or not, its terminal slot is protected.
    assert(work.submit(item).status == WorkServiceStatus::QueueFull);
    assert(queue.post({[](void*) {}, nullptr}).status == CompletionStatus::QueueFull);
    queue.shutdown();
    work.shutdown(WorkShutdownPolicy::Drain);
    assert(probe.callbacks == 0 && probe.executed == 1);
    assert(queue.dispatch(1).dispatched == 1);
    assert(probe.callbacks == 1 && probe.status == WorkStatus::Completed);
    assert(queue.reserved() == 0);
}

void checkFailure(bool inlineWork)
{
    CompletionQueue queue({1});
    WorkService work({&queue, 1, 1, inlineWork});
    Probe probe;
    assert(work.submit({fail, nullptr, complete, &probe}).accepted());
    work.shutdown(WorkShutdownPolicy::Drain);
    assert(queue.dispatch(1).dispatched == 1);
    assert(probe.callbacks == 1 && probe.status == WorkStatus::Failed);
}

} // namespace

int main()
{
    checkCapacity(false);
    checkCapacity(true);
    checkFailure(false);
    checkFailure(true);
}
