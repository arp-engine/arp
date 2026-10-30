#include "network/http/fetch_byte_transfer.h"

#include <cstdio>
#include <limits>
#include <new>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

EM_JS_DEPS(arpFetchStart, "$UTF8ToString,$lengthBytesUTF8,$stringToUTF8");

// FetchByteTransfer/1 private bridge terminal reasons: 0 preserves a recorded
// contract failure; 1 EOF, 2 cancellation, 3 deadline, 4 transport, 5 headers.
EM_JS(int, arpFetchSupported, (), {
    // clang-format off
    return typeof window !== 'undefined' && typeof document !== 'undefined' &&
        typeof fetch === 'function' && typeof AbortController === 'function' &&
        typeof ReadableStream === 'function';
    // clang-format on
});

EM_JS(int,
    arpFetchStart,
    (void* context,
        const char* urlBytes,
        const char* rangeBytes,
        char* header,
        int headerCapacity,
        unsigned char* scratch,
        int scratchCapacity,
        int timeoutMs,
        int pollMs),
    {
        // clang-format off
    const url = UTF8ToString(urlBytes);
    if (new URL(url).origin !== location.origin) return 0;
    const transfers = Module.arpCoreFileTransfers || (Module.arpCoreFileTransfers = new Map());
    if (transfers.has(context)) return 0;
    const entry = { controller: new AbortController(), reader: null, timer: null, poll: null };
    const live = () => transfers.get(context) === entry;
    const finish = reason => {
        if (!live()) return;
        transfers.delete(context);
        clearTimeout(entry.timer);
        clearInterval(entry.poll);
        if (entry.reader) entry.reader.cancel().catch(() => {});
        entry.controller.abort();
        Module._arpCoreHttpFetchTerminal(context, reason);
    };
    transfers.set(context, entry);
    entry.timer = setTimeout(() => finish(3), timeoutMs);
    entry.poll = setInterval(() => {
        if (live() && Module._arpCoreHttpFetchCancelled(context)) finish(2);
    }, pollMs);
    const range = UTF8ToString(rangeBytes);
    const requestHeaders = range ? {Range: range} : {};
    // Accept-Encoding is a forbidden browser request header. V1 refuses
    // cross-origin reads and observable non-identity response encoding.
    Promise.resolve().then(async () => {
        if (!live()) return;
        const response = await fetch(url, {method: 'GET', headers: requestHeaders,
            signal: entry.controller.signal, mode: 'same-origin', credentials: 'omit',
            redirect: 'error', cache: 'no-store', referrerPolicy: 'no-referrer'});
        if (!live()) {
            if (response.body) response.body.cancel().catch(() => {});
            return;
        }
        let block = '';
        let overflow = false;
        response.headers.forEach((value, name) => {
            if (overflow) return;
            const line = name + ':' + value + '\n';
            if (lengthBytesUTF8(block) + lengthBytesUTF8(line) + 1 > headerCapacity) {
                overflow = true;
                return;
            }
            block += line;
        });
        if (overflow) { finish(5); return; }
        const size = lengthBytesUTF8(block);
        stringToUTF8(block, header, headerCapacity);
        if (!Module._arpCoreHttpFetchHeaders(context, response.status, size)) { finish(0); return; }
        if (!response.body) { finish(1); return; }
        entry.reader = response.body.getReader();
        for (;;) {
            const chunk = await entry.reader.read();
            if (!live()) return;
            if (chunk.done) { finish(1); return; }
            for (let offset = 0; offset < chunk.value.byteLength; offset += scratchCapacity) {
                const part = chunk.value.subarray(offset, Math.min(offset + scratchCapacity, chunk.value.byteLength));
                // Resolve current wasm heap after every await; do not retain
                // a stale HEAPU8 view across memory growth.
                HEAPU8.set(part, scratch);
                if (!Module._arpCoreHttpFetchAppend(context, part.byteLength)) { finish(0); return; }
            }
        }
    }).catch(() => finish(4));
    return 1;
        // clang-format on
    });

EM_JS(void, arpFetchDetach, (void* context), {
    // clang-format off
    const transfers = Module.arpCoreFileTransfers;
    const entry = transfers && transfers.get(context);
    if (!entry) return;
    transfers.delete(context);
    clearTimeout(entry.timer);
    clearInterval(entry.poll);
    if (entry.reader) entry.reader.cancel().catch(() => {});
    entry.controller.abort();
    // cancel() publishes the terminal callback. Detached continuations must
    // pass the registry identity check before touching any C++ memory.
    // clang-format on
});

extern "C" {
EMSCRIPTEN_KEEPALIVE int arpCoreHttpFetchCancelled(void* context)
{
    return static_cast<arp::network::http::FetchByteTransfer*>(context)->cancelled();
}

EMSCRIPTEN_KEEPALIVE int arpCoreHttpFetchHeaders(void* context, unsigned status, unsigned length)
{
    return static_cast<arp::network::http::FetchByteTransfer*>(context)->headers(status, length);
}

EMSCRIPTEN_KEEPALIVE int arpCoreHttpFetchAppend(void* context, unsigned length)
{
    return static_cast<arp::network::http::FetchByteTransfer*>(context)->append(length);
}

EMSCRIPTEN_KEEPALIVE void arpCoreHttpFetchTerminal(void* context, unsigned reason)
{
    static_cast<arp::network::http::FetchByteTransfer*>(context)->terminal(reason);
}
}
#endif

namespace arp::network::http {

bool FetchByteTransfer::supported() noexcept
{
#if defined(__EMSCRIPTEN__)
    return arpFetchSupported();
#else
    return false;
#endif
}

ByteReadStatus FetchByteTransfer::start(const ByteReadDesc& desc,
    const ByteReadRequest& request,
    ByteReadObserver& receiver,
    const std::atomic_bool& closed,
    CompleteFn complete,
    void* context)
{
    if (!supported())
        return ByteReadStatus::Unsupported;
    if (const auto status = ByteReadTransport::validate(desc, request);
        status != ByteReadStatus::Success)
        return status;
    request_ = &request;
    receiver_ = &receiver;
    closed_ = &closed;
    complete_ = complete;
    context_ = context;
    scratch_.resize(desc.ioChunkBytes);
    header_.resize(desc.maxHeaderBytes);
#if defined(__EMSCRIPTEN__)
    active_ = arpFetchStart(this,
        request.url.c_str(),
        request.range.c_str(),
        header_.data(),
        static_cast<int>(header_.size()),
        scratch_.data(),
        static_cast<int>(scratch_.size()),
        static_cast<int>(desc.timeoutMs),
        static_cast<int>(desc.cancellationPollMs));
    return active_ ? ByteReadStatus::Accepted : ByteReadStatus::Unsupported;
#else
    return ByteReadStatus::Unsupported;
#endif
}

void FetchByteTransfer::cancel() noexcept
{
    if (!active_)
        return;
#if defined(__EMSCRIPTEN__)
    arpFetchDetach(this);
#endif
    terminal(2);
}

bool FetchByteTransfer::cancelled() const noexcept
{
    return closed_->load() || request_->cancellation.cancelled();
}

bool FetchByteTransfer::headers(unsigned status, std::size_t length) noexcept
{
    try {
        if (length >= header_.size()) {
            receiver_->onFailure(ByteReadStatus::BudgetExceeded, "IO-HTTP-FETCH-HEADER-BUDGET");
            return false;
        }
        return receiver_->onHeaders(status, {header_.data(), length});
    } catch (...) {
        receiver_->onFailure(ByteReadStatus::BudgetExceeded, "IO-HTTP-FETCH-HEADER-ALLOCATION");
        return false;
    }
}

bool FetchByteTransfer::append(std::size_t length) noexcept
{
    try {
        if (length > scratch_.size()) {
            receiver_->onFailure(ByteReadStatus::BudgetExceeded, "IO-HTTP-FETCH-CHUNK-BUDGET");
            return false;
        }
        return receiver_->onBody(scratch_.data(), length);
    } catch (...) {
        receiver_->onFailure(ByteReadStatus::BudgetExceeded, "IO-HTTP-FETCH-BODY-ALLOCATION");
        return false;
    }
}

void FetchByteTransfer::terminal(unsigned reason) noexcept
{
    if (!active_)
        return;
    active_ = false;
    try {
        if (reason == 1)
            receiver_->onEnd();
        else if (reason == 2)
            receiver_->onFailure(ByteReadStatus::Cancelled, "IO-HTTP-FETCH-CANCELLED");
        else if (reason == 3)
            receiver_->onFailure(ByteReadStatus::Timeout, "IO-HTTP-FETCH-DEADLINE");
        else if (reason == 4)
            receiver_->onFailure(ByteReadStatus::TransportError, "IO-HTTP-FETCH-TRANSPORT");
        else if (reason == 5)
            receiver_->onFailure(ByteReadStatus::BudgetExceeded, "IO-HTTP-FETCH-HEADER-BUDGET");
    } catch (...) {
        receiver_->onFailure(ByteReadStatus::BudgetExceeded, "IO-HTTP-FETCH-FINALIZE-ALLOCATION");
    }
    complete_(context_);
}

} // namespace arp::network::http
