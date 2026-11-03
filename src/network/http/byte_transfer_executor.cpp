#include "network/http/byte_transfer_executor.h"

#include "core/container/generation_pool.h"
#include "core/runtime/work_service.h"
#include "network/http/fetch_byte_transfer.h"

namespace arp::network::http {
struct ByteTransferExecutor::State {
    struct Pending {
        const ByteReadRequest* request = nullptr;
        ByteReadObserver* observer = nullptr;
        Complete complete = nullptr;
        void* context = nullptr;
        core::SharedPtr<core::CpuMemoryQuota> quota;
        core::runtime::CompletionReservation reservation;
        FetchByteTransfer fetch;
    };

    explicit State(ByteTransferExecutorDesc config) : desc(config), pending(config.maxInFlight) {}

    ByteTransferExecutorDesc desc;
    core::GenerationPool<Pending> pending;
    core::UniquePtr<core::runtime::WorkService> work;
    core::runtime::CompletionQueue* queue = nullptr;
    std::atomic_bool closed{false};
    std::atomic_uint64_t nativeTransfers{0};
};

struct ByteTransferExecutor::Cookie {
    core::SharedPtr<State> state;
    core::PoolHandle handle;
};

ByteTransferExecutor::ByteTransferExecutor(ByteTransferExecutorDesc desc)
    : state_(core::makeShared<State>(desc))
{
    if (!desc.maxInFlight)
        state_->closed = true;
}

ByteTransferExecutor::~ByteTransferExecutor()
{
    shutdown();
}

bool ByteTransferExecutor::browserSupported() noexcept
{
    return FetchByteTransfer::supported();
}

bool ByteTransferExecutor::supported() noexcept
{
    return browserSupported() || ByteReadTransport::supported();
}

ByteReadStatus ByteTransferExecutor::submit(const ByteReadRequest& request,
    ByteReadObserver& observer,
    Complete complete,
    void* context,
    core::runtime::CompletionQueue& queue,
    core::SharedPtr<core::CpuMemoryQuota> quota) noexcept
{
    auto& s = *state_;
    if (s.closed)
        return ByteReadStatus::TransportError;
    if (!complete || (s.queue && s.queue != &queue))
        return ByteReadStatus::InvalidRequest;
    if (!supported())
        return ByteReadStatus::Unsupported;
    if (request.cancellation.cancelled())
        return ByteReadStatus::Cancelled;
    if (const auto validation = ByteReadTransport::validate(s.desc.transport, request);
        validation != ByteReadStatus::Success)
        return validation;
    if (s.pending.liveCount() >= s.desc.maxInFlight)
        return ByteReadStatus::QueueFull;
    core::PoolHandle handle;
    try {
        auto cookie = core::makeUnique<Cookie>();
        cookie->state = state_;
        handle = s.pending.emplace();
        if (!handle)
            return ByteReadStatus::BudgetExceeded;
        cookie->handle = handle;
        auto& pending = *s.pending.get(handle);
        pending.request = &request;
        pending.observer = &observer;
        pending.complete = complete;
        pending.context = context;
        pending.quota = std::move(quota);
        s.queue = &queue;
        if (browserSupported()) {
            pending.reservation = queue.reserve();
            if (!pending.reservation.valid()) {
                s.pending.erase(handle);
                return ByteReadStatus::QueueFull;
            }
            const auto status = pending.fetch.start(
                s.desc.transport, request, observer, s.closed, browserComplete, cookie.get());
            if (status != ByteReadStatus::Accepted) {
                s.pending.erase(handle);
                return status;
            }
        } else {
            if (!s.work)
                s.work = core::makeUnique<core::runtime::WorkService>(
                    core::runtime::WorkServiceDesc{&queue, 1, s.desc.maxInFlight, false, true});
            if (!s.work->valid()) {
                s.pending.erase(handle);
                return ByteReadStatus::TransportError;
            }
            const auto accepted = s.work->submit(
                {execute, cookie.get(), workerComplete, cookie.get(), request.cancellation});
            if (!accepted.accepted()) {
                s.pending.erase(handle);
                return accepted.status == core::runtime::WorkServiceStatus::Cancelled
                    ? ByteReadStatus::Cancelled
                    : ByteReadStatus::QueueFull;
            }
        }
        cookie.release();
        return ByteReadStatus::Accepted;
    } catch (...) {
        if (handle)
            s.pending.erase(handle);
        return ByteReadStatus::BudgetExceeded;
    }
}

core::runtime::WorkStatus ByteTransferExecutor::execute(void* value)
{
    auto& cookie = *static_cast<Cookie*>(value);
    auto& s = *cookie.state;
    auto& p = *s.pending.get(cookie.handle);
    try {
        core::CpuMemoryQuotaScope quota(p.quota);
        ByteReadTransport::read(
            s.desc.transport, *p.request, *p.observer, s.closed, s.nativeTransfers);
    } catch (const std::bad_alloc&) {
        p.observer->onFailure(ByteReadStatus::BudgetExceeded, "NET-HTTP-EXECUTOR-ALLOCATION");
    } catch (...) {
        p.observer->onFailure(ByteReadStatus::TransportError, "NET-HTTP-EXECUTOR-EXCEPTION");
    }
    // The observer owns the transfer result. Work completion only retires the producer.
    return core::runtime::WorkStatus::Completed;
}

void ByteTransferExecutor::workerComplete(void* value, core::runtime::WorkResult result)
{
    auto& cookie = *static_cast<Cookie*>(value);
    auto& p = *cookie.state->pending.get(cookie.handle);
    if (result.status == core::runtime::WorkStatus::Cancelled)
        p.observer->onFailure(ByteReadStatus::Cancelled, "NET-HTTP-EXECUTOR-CANCELLED");
    else if (result.status == core::runtime::WorkStatus::Failed)
        p.observer->onFailure(ByteReadStatus::TransportError, "NET-HTTP-EXECUTOR-WORK-FAILED");
    deliver(value);
}

void ByteTransferExecutor::browserComplete(void* value)
{
    auto& cookie = *static_cast<Cookie*>(value);
    auto& s = *cookie.state;
    auto& p = *s.pending.get(cookie.handle);
    const auto posted = s.queue->postReserved(std::move(p.reservation), {deliver, value});
    if (!posted.accepted())
        std::terminate(); // Accepted reservations remain dispatchable through shutdown.
}

void ByteTransferExecutor::deliver(void* value)
{
    core::UniquePtr<Cookie> cookie{static_cast<Cookie*>(value),
        core::CpuAllocatedDeleter<Cookie>{value, &core::destroyCpuAllocatedUnique<Cookie>}};
    auto& s = *cookie->state;
    const auto* p = s.pending.get(cookie->handle);
    const auto complete = p->complete;
    void* context = p->context;
    s.pending.erase(cookie->handle);
    try {
        complete(context);
    } catch (...) {
    }
}

void ByteTransferExecutor::shutdown() noexcept
{
    auto& s = *state_;
    if (s.closed.exchange(true))
        return;
    if (s.work)
        s.work->shutdown(core::runtime::WorkShutdownPolicy::CancelPending);
    s.pending.forEach([](core::PoolHandle, State::Pending& p) {
        p.fetch.cancel();
    });
}

ByteTransferExecutorTelemetry ByteTransferExecutor::telemetry() const noexcept
{
    return {state_->pending.liveCount(), state_->nativeTransfers.load()};
}
} // namespace arp::network::http
