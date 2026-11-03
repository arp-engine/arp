#include "core/io/http_file_backend.h"

#include "core/io/detail/http_transport_status.h"
#include "network/http/byte_read_transport.h"
#include "network/http/byte_transfer_executor.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <new>

namespace arp::core::io {

namespace {

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
        receiver_.fail(detail::httpFileStatus(status), diagnostic);
    }

private:
    HttpFileBodyReceiver& receiver_;
};

} // namespace

struct HttpFileBackend::State {
    explicit State(HttpFileBackendDesc config) : desc(std::move(config)) {}

    HttpFileBackendDesc desc;
    std::atomic_bool closed{false};
    std::atomic_uint64_t receivedBytes{0};
    std::uint32_t inFlight = 0;
    std::uint64_t reservedBytes = 0;
    core::UniquePtr<network::http::ByteTransferExecutor> executor;
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
    runtime::CompletionQueue* queue = nullptr;
};

HttpFileBackend::HttpFileBackend(HttpFileBackendDesc desc)
    : state_(core::makeShared<State>(std::move(desc)))
{
    diagnostic_ = HttpFileContract::validate(state_->desc).diagnostic;
    if (diagnostic_)
        state_->closed = true;
}

HttpFileBackend::~HttpFileBackend()
{
    shutdown();
}

FileBackendCapability HttpFileBackend::capability() const noexcept
{
    const bool browser = network::http::ByteTransferExecutor::browserSupported();
    return {browser ? FileBackendKind::FetchFileBackend : FileBackendKind::HttpFileBackend,
        FileBackendStatus::PrototypeComplete,
        browser,
        true,
        true,
        false,
        false,
        !state_->closed && (browser || network::http::ByteReadTransport::supported()),
        !state_->closed && (browser || network::http::ByteReadTransport::supported())};
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
    if (!state_->executor)
        state_->executor = core::makeUnique<network::http::ByteTransferExecutor>(
            network::http::ByteTransferExecutorDesc{
                transportDesc(state_->desc), state_->desc.maxInFlight});
    queue_ = &queue;
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
        && !network::http::ByteTransferExecutor::browserSupported())
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
        pending->queue = network::http::ByteTransferExecutor::browserSupported() ? &queue : nullptr;
        const auto submitted = state_->executor->submit(
            pending->transferRequest, pending->observer, deliver, pending.get(), queue);
        if (submitted != network::http::ByteReadStatus::Accepted)
            return detail::httpFileStatus(submitted);
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

void HttpFileBackend::deliver(void* context)
{
    core::UniquePtr<Pending> pending{static_cast<Pending*>(context),
        core::CpuAllocatedDeleter<Pending>{context, &core::destroyCpuAllocatedUnique<Pending>}};
    auto& state = *pending->owner;
    --state.inFlight;
    state.reservedBytes -= pending->budget;
    state.receivedBytes += pending->receiver.receivedBytes();
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
    pending->completion.complete(pending->completion.userData, std::move(result));
}

void HttpFileBackend::shutdown() noexcept
{
    if (state_->closed.exchange(true))
        return;
    if (state_->executor)
        state_->executor->shutdown();
}

HttpFileBackendTelemetry HttpFileBackend::telemetry() const noexcept
{
    return {state_->inFlight,
        state_->reservedBytes,
        state_->executor ? state_->executor->telemetry().activeNativeTransfers : 0,
        state_->receivedBytes.load()};
}

} // namespace arp::core::io
