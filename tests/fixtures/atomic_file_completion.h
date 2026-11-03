#pragma once

#include "core/io/atomic_file_service.h"

#include <cassert>
#include <chrono>
#include <thread>

namespace arp::tests::atomic_file {

// Owner-thread completion for the real bounded AtomicFileService worker.
inline core::io::AtomicFileResult finish(
    core::io::AtomicFileService& service, core::PoolHandle handle)
{
    assert(handle);
    core::io::AtomicFileResult result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (service.take(handle, result))
            return result;
        std::this_thread::yield();
    }
    assert(false && "file completion deadline");
    return result;
}
} // namespace arp::tests::atomic_file
