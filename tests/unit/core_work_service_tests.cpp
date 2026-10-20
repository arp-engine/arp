#include "core/runtime/completion_queue.h"
#include "core/runtime/work_service.h"

#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool open = false;
};

struct Probe {
    std::uint32_t executed = 0;
    std::uint32_t completed = 0;
    std::uint32_t cancelled = 0;
    std::uint32_t failed = 0;
    std::thread::id completionThread{};
};

arp::core::runtime::WorkStatus waitForGate(void* userData)
{
    auto* gate = static_cast<Gate*>(userData);
    std::unique_lock<std::mutex> lock(gate->mutex);
    gate->cv.wait(lock, [gate] {
        return gate->open;
    });
    return arp::core::runtime::WorkStatus::Completed;
}

arp::core::runtime::WorkStatus recordWork(void* userData)
{
    auto* probe = static_cast<Probe*>(userData);
    ++probe->executed;
    return arp::core::runtime::WorkStatus::Completed;
}

void recordCompletion(void* userData, arp::core::runtime::WorkResult result)
{
    auto* probe = static_cast<Probe*>(userData);
    probe->completionThread = std::this_thread::get_id();
    if (result.status == arp::core::runtime::WorkStatus::Completed) {
        ++probe->completed;
    } else if (result.status == arp::core::runtime::WorkStatus::Cancelled) {
        ++probe->cancelled;
    } else if (result.status == arp::core::runtime::WorkStatus::Failed) {
        ++probe->failed;
    }
}

} // namespace

int main()
{
    using namespace arp::core::runtime;

    CompletionQueue completions(CompletionQueueDesc{8});
    WorkService invalid(WorkServiceDesc{&completions, 0, 1});
    assert(!invalid.valid());
    assert(invalid.status() == WorkServiceStatus::InvalidWorkerCount);

    WorkService service(WorkServiceDesc{&completions, 1, 2});
    assert(service.valid());
    assert(service.workerCount() == 1);

    Gate gate;
    Probe probe;
    const auto blocking = service.submit(WorkItem{
        &waitForGate,
        &gate,
        &recordCompletion,
        &probe,
    });
    assert(blocking.status == WorkServiceStatus::Accepted);

    const auto pending = service.submit(WorkItem{
        &recordWork,
        &probe,
        &recordCompletion,
        &probe,
    });
    assert(pending.status == WorkServiceStatus::Accepted);
    assert(service.telemetry().submitted == 2);

    const auto full = service.submit(WorkItem{
        &recordWork,
        &probe,
        &recordCompletion,
        &probe,
    });
    assert(full.status == WorkServiceStatus::QueueFull);

    assert(service.cancelPending(pending.handle).status == WorkServiceStatus::Cancelled);

    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.open = true;
    }
    gate.cv.notify_all();

    service.shutdown(WorkShutdownPolicy::Drain);
    assert(completions.dispatch(8).dispatched == 2);
    assert(probe.completed == 1);
    assert(probe.cancelled == 1);
    assert(probe.executed == 0);
    assert(probe.completionThread == std::this_thread::get_id());
    assert(service.telemetry().started == 1);
    assert(service.telemetry().completed == 1);
    assert(service.telemetry().cancelled == 1);
    assert(service.telemetry().rejected == 1);
    assert(service.telemetry().maxQueueDepth == 2);

    const auto afterShutdown = service.submit(WorkItem{
        &recordWork,
        &probe,
        &recordCompletion,
        &probe,
    });
    assert(afterShutdown.status == WorkServiceStatus::ServiceClosed);

    return 0;
}
