#include "core/io/http_file_backend.h"

#include "network/http/byte_read_transport.h"
#include "network/http/fetch_byte_transfer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <new>

namespace arp::core::io {

namespace {

FileIoStatus fileStatus(network::http::ByteReadStatus status) noexcept
{
    using network::http::ByteReadStatus;
    switch (status) {
    case ByteReadStatus::Accepted:
        return FileIoStatus::Accepted;
    case ByteReadStatus::Success:
        return FileIoStatus::Success;
    case ByteReadStatus::Unsupported:
        return FileIoStatus::Unsupported;
    case ByteReadStatus::Cancelled:
        return FileIoStatus::Cancelled;
    case ByteReadStatus::Timeout:
        return FileIoStatus::Timeout;
    case ByteReadStatus::BudgetExceeded:
        return FileIoStatus::BudgetExceeded;
    case ByteReadStatus::InvalidRequest:
        return FileIoStatus::InvalidRequest;
    case ByteReadStatus::ProtocolError:
        return FileIoStatus::IntegrityMismatch;
    case ByteReadStatus::TransportError:
        return FileIoStatus::BackendUnavailable;
    }
    return FileIoStatus::BackendUnavailable;
}

network::http::ByteReadDesc transportDesc(const HttpFileBackendDesc& desc) noexcept
{
    return {desc.maxHeaderBytes,
        desc.ioChunkBytes,
        desc.timeoutMs,
        desc.cancellationPollMs,
        desc.maxDnsAddresses};
}

class ResponseObserver final : public network::http::ByteReadObserver {
public:
    explicit ResponseObserver(HttpFileBodyReceiver& receiver) : receiver_(receiver) {}

    bool onHeaders(unsigned status, std::string_view block) noexcept override
    {
        try {
            HttpFileResponseHeaders response;
            const auto validation = HttpFileContract::responseHeaders(status, block, response);
            if (validation.diagnostic) {
                receiver_.fail(validation.status, validation.diagnostic);
                return false;
            }
            return receiver_.headers(response);
        } catch (...) {
            receiver_.fail(FileIoStatus::BudgetExceeded, "IO-HTTP-HEADER-ALLOCATION");
            return false;
        }
    }

    bool onBody(const std::uint8_t* bytes, std::size_t count) noexcept override
    {
        try {
            return receiver_.append(bytes, count);
        } catch (...) {
            receiver_.fail(FileIoStatus::BudgetExceeded, "IO-HTTP-BODY-ALLOCATION");
            return false;
        }
    }

    void onEnd() noexcept override
    {
        try {
            receiver_.finish();
        } catch (...) {
            receiver_.fail(FileIoStatus::BudgetExceeded, "IO-HTTP-VERSION-ALLOCATION");
        }
    }

    void onFailure(network::http::ByteReadStatus status, const char* diagnostic) noexcept override
    {
        receiver_.fail(fileStatus(status), diagnostic);
    }

private:
    HttpFileBodyReceiver& receiver_;
};

} // namespace

struct HttpFileBackend::State {
    explicit State(HttpFileBackendDesc config) : desc(std::move(config)) {}

    HttpFileBackendDesc desc;
    std::atomic_bool closed{false};
    std::atomic_uint64_t activeNativeTransfers{0};
    std::atomic_uint64_t receivedBytes{0};
    std::uint32_t inFlight = 0;
    std::uint64_t reservedBytes = 0;
    core::pmr::Vector<Pending*> active{&core::defaultCpuMemoryResource()};
};

struct HttpFileBackend::Pending {
    Pending(const HttpFileBackendDesc& desc, const FileReadRequest& read)
        : request(read), receiver(request), observer(receiver)
    {
        transferRequest.url = HttpFileContract::url(desc, read.path);
        transferRequest.cancellation = read.cancellation;
        transferRequest.maxBodyBytes = HttpFileContract::wireBudget(read);
        if (HttpFileContract::usesWireRange(read)) {
            char range[sizeof("bytes=-") + 2 * std::numeric_limits<std::uint64_t>::digits10 + 2];
            std::snprintf(range,
                sizeof(range),
                "bytes=%llu-%llu",
                static_cast<unsigned long long>(read.range.offset),
                static_cast<unsigned long long>(read.range.offset + read.range.size - 1));
            transferRequest.range = range;
        }
    }

    core::SharedPtr<State> owner;
    FileReadRequest request;
    HttpFileBodyReceiver receiver;
    ResponseObserver observer;
    network::http::ByteReadRequest transferRequest;
    FileReadCompletion completion;
    std::uint64_t budget = 0;
    runtime::CompletionReservation reservation;
    runtime::CompletionQueue* queue = nullptr;
    network::http::FetchByteTransfer fetch;
};

HttpFileBackend::HttpFileBackend(HttpFileBackendDesc desc)
    : state_(core::makeShared<State>(std::move(desc)))
{
    diagnostic_ = HttpFileContract::validate(state_->desc).diagnostic;
    if (diagnostic_)
        state_->closed = true;
    else
        state_->active.reserve(state_->desc.maxInFlight);
}

HttpFileBackend::~HttpFileBackend()
{
    shutdown();
}

FileBackendCapability HttpFileBackend::capability() const noexcept
{
    const bool browser = network::http::FetchByteTransfer::supported();
    return {browser ? FileBackendKind::FetchFileBackend : FileBackendKind::HttpFileBackend,
        FileBackendStatus::PrototypeComplete,
        browser,
        true,
        true,
        false,
        false,
        browser || network::http::ByteReadTransport::supported()};
}

FileReadResult HttpFileBackend::validateRead(const FileReadRequest& request) const
{
    FileReadResult result;
    result.backendKind = capability().kind;
    if (state_->closed) {
        result.status = FileIoStatus::ServiceClosed;
        result.diagnosticId = "IO-HTTP-CLOSED";
        return result;
    }
    const auto validation = HttpFileContract::validate(state_->desc, request);
    result.status = validation.status;
    result.diagnosticId = validation.diagnostic;
    return result;
}

FileIoStatus HttpFileBackend::start(runtime::CompletionQueue& queue)
{
    if (queue_ && queue_ != &queue)
        return FileIoStatus::InvalidRequest;
    if (network::http::FetchByteTransfer::supported()) {
        queue_ = &queue;
        return FileIoStatus::Success;
    }
    if (!work_) {
        auto work = core::makeUnique<runtime::WorkService>(
            runtime::WorkServiceDesc{&queue, 1, state_->desc.maxInFlight, false, true});
        if (!work->valid())
            return FileIoStatus::BackendUnavailable;
        work_ = std::move(work);
        queue_ = &queue;
    }
    return FileIoStatus::Success;
}

FileIoStatus HttpFileBackend::readAsync(const FileReadRequest& request,
    FileReadCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    if (state_->closed)
        return FileIoStatus::ServiceClosed;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (!network::http::ByteReadTransport::supported()
        && !network::http::FetchByteTransfer::supported())
        return FileIoStatus::Unsupported;
    if (request.cancellation.cancelled())
        return FileIoStatus::Cancelled;
    try {
        const auto validation = HttpFileContract::validate(state_->desc, request);
        if (validation.diagnostic)
            return validation.status;
        if (state_->inFlight >= state_->desc.maxInFlight)
            return FileIoStatus::QueueFull;
        // Reservation covers output, native header storage + ASCII copy,
        // chunk, copied path, encoded URL and wide URL/host, plus task storage.
        // Allocator bookkeeping and socket/browser internals remain separate.
        const auto& desc = state_->desc;
        const auto urlBytes =
            static_cast<std::uint64_t>(desc.baseUrl.size()) + 3 * desc.maxPathBytes + 1;
        auto fixed = sizeof(Pending) + 2 * static_cast<std::uint64_t>(desc.maxHeaderBytes)
            + desc.ioChunkBytes + desc.maxPathBytes + 4 * urlBytes + 2 * (desc.baseUrl.size() + 1);
        const auto dns =
            network::http::ByteReadTransport::resolverBudget(transportDesc(desc), urlBytes);
        if (dns > UINT64_MAX - fixed)
            return FileIoStatus::BudgetExceeded;
        fixed += dns;
        if (request.maxBytes > UINT64_MAX - fixed)
            return FileIoStatus::BudgetExceeded;
        const auto budget = request.maxBytes + fixed;
        if (budget > desc.maxBytesInFlight - state_->reservedBytes || budget > SIZE_MAX)
            return FileIoStatus::BudgetExceeded;
        const auto started = start(queue);
        if (started != FileIoStatus::Success)
            return started;
        auto pending = core::makeUnique<Pending>(desc, request);
        pending->owner = state_;
        pending->completion = completion;
        pending->budget = budget;
        if (network::http::FetchByteTransfer::supported()) {
            pending->reservation = queue.reserve();
            if (!pending->reservation.valid())
                return pending->reservation.status() == runtime::CompletionStatus::QueueFull
                    ? FileIoStatus::QueueFull
                    : FileIoStatus::ServiceClosed;
            pending->queue = &queue;
            const auto started = pending->fetch.start(transportDesc(desc),
                pending->transferRequest,
                pending->observer,
                state_->closed,
                browserComplete,
                pending.get());
            if (started != network::http::ByteReadStatus::Accepted)
                return fileStatus(started);
            state_->active.push_back(pending.get());
            ++state_->inFlight;
            state_->reservedBytes += budget;
            pending.release();
            return FileIoStatus::Accepted;
        }
        const auto submitted =
            work_->submit({execute, pending.get(), complete, pending.get(), request.cancellation});
        if (!submitted.accepted())
            return submitted.status == runtime::WorkServiceStatus::QueueFull
                ? FileIoStatus::QueueFull
                : submitted.status == runtime::WorkServiceStatus::Cancelled
                ? FileIoStatus::Cancelled
                : FileIoStatus::ServiceClosed;
        ++state_->inFlight;
        state_->reservedBytes += budget;
        pending.release();
        return FileIoStatus::Accepted;
    } catch (const std::bad_alloc&) {
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        return FileIoStatus::BackendUnavailable;
    }
}

runtime::WorkStatus HttpFileBackend::execute(void* context)
{
    auto& pending = *static_cast<Pending*>(context);
    try {
        network::http::ByteReadTransport::read(transportDesc(pending.owner->desc),
            pending.transferRequest,
            pending.observer,
            pending.owner->closed,
            pending.owner->activeNativeTransfers);
    } catch (const std::bad_alloc&) {
        pending.receiver.fail(FileIoStatus::BudgetExceeded, "IO-HTTP-ALLOCATION");
    } catch (...) {
        pending.receiver.fail(FileIoStatus::BackendUnavailable, "IO-HTTP-TRANSPORT-EXCEPTION");
    }
    pending.owner->receivedBytes += pending.receiver.receivedBytes();
    return pending.receiver.status() == FileIoStatus::Success ? runtime::WorkStatus::Completed
                                                              : runtime::WorkStatus::Failed;
}

void HttpFileBackend::complete(void* context, runtime::WorkResult result)
{
    auto& pending = *static_cast<Pending*>(context);
    if (result.status == runtime::WorkStatus::Cancelled)
        pending.receiver.fail(FileIoStatus::Cancelled, "IO-HTTP-WORK-CANCELLED");
    else if (result.status == runtime::WorkStatus::Failed)
        pending.receiver.fail(FileIoStatus::BackendUnavailable, "IO-HTTP-WORK-FAILED");
    deliver(context);
}

void HttpFileBackend::deliver(void* context)
{
    core::UniquePtr<Pending> pending{static_cast<Pending*>(context),
        core::CpuAllocatedDeleter<Pending>{context, &core::destroyCpuAllocatedUnique<Pending>}};
    auto& state = *pending->owner;
    --state.inFlight;
    state.reservedBytes -= pending->budget;
    auto result = pending->receiver.take();
    if (pending->request.cancellation.cancelled() || state.closed) {
        result.status = pending->request.cancellation.cancelled() ? FileIoStatus::Cancelled
                                                                  : FileIoStatus::ServiceClosed;
        result.diagnosticId = "IO-HTTP-TERMINATED-BEFORE-DISPATCH";
        result.bytes.clear();
        result.version = {};
    }
    result.backendKind =
        pending->queue ? FileBackendKind::FetchFileBackend : FileBackendKind::HttpFileBackend;
    if (pending->queue) {
        const auto found = std::find(state.active.begin(), state.active.end(), pending.get());
        if (found != state.active.end())
            state.active.erase(found);
    }
    pending->completion.complete(pending->completion.userData, std::move(result));
}

void HttpFileBackend::browserComplete(void* context)
{
    auto& pending = *static_cast<Pending*>(context);
    pending.owner->receivedBytes += pending.receiver.receivedBytes();
    const auto posted =
        pending.queue->postReserved(std::move(pending.reservation), {deliver, context});
    if (!posted.accepted())
        std::terminate(); // An accepted terminal reservation must stay dispatchable.
}

void HttpFileBackend::shutdown() noexcept
{
    if (state_->closed.exchange(true))
        return;
    if (work_)
        work_->shutdown(runtime::WorkShutdownPolicy::CancelPending);
    for (auto* pending : state_->active)
        pending->fetch.cancel();
}

HttpFileBackendTelemetry HttpFileBackend::telemetry() const noexcept
{
    return {state_->inFlight,
        state_->reservedBytes,
        state_->activeNativeTransfers.load(),
        state_->receivedBytes.load()};
}

} // namespace arp::core::io
