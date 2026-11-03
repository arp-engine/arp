#pragma once

#include "core/ownership/monotonic_id_sequence.h"
#include "core/runtime/cancellation_token.h"

#include <cstdio>
#include <limits>
#include <string_view>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace arp::core::io::detail {

// Shared by the legacy host-path service and rooted native-directory saves.
// Ownership/rename policy stays with the caller; this class never opens paths.
class AtomicFileWriter final {
public:
    static constexpr std::size_t suffixBytes =
        sizeof(".arp-tmp--") + 2 * (std::numeric_limits<unsigned long long>::digits10 + 1);
    using Suffix = char[suffixBytes];

    static bool temporarySuffix(Suffix& suffix) noexcept
    {
        static core::MonotonicIdSequence sequence;
        const auto serial = sequence.tryAcquire();
        if (!serial)
            return false;
#if defined(_WIN32)
        const auto process = GetCurrentProcessId();
#else
        const auto process = getpid();
#endif
        const auto count = std::snprintf(suffix,
            sizeof(suffix),
            ".arp-tmp-%llu-%llu",
            static_cast<unsigned long long>(process),
            static_cast<unsigned long long>(serial));
        return count > 0 && static_cast<std::size_t>(count) < sizeof(suffix);
    }

#if defined(_WIN32)
    enum class WriteResult { Flushed, Failed, Cancelled };

    static WriteResult writeAndFlush(HANDLE file,
        std::string_view bytes,
        const runtime::CancellationToken& cancellation = {}) noexcept
    {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            if (cancellation.cancelled())
                return WriteResult::Cancelled;
            const auto remaining = bytes.size() - offset;
            const DWORD amount = remaining > MAXDWORD ? MAXDWORD : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, amount, &written, nullptr) || !written)
                return WriteResult::Failed;
            offset += written;
        }
        if (cancellation.cancelled())
            return WriteResult::Cancelled;
        return FlushFileBuffers(file) ? WriteResult::Flushed : WriteResult::Failed;
    }
#endif
};

} // namespace arp::core::io::detail
