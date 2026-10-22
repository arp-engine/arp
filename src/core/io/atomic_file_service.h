#pragma once
#include "core/container/generation_pool.h"
#include "core/runtime/work_service.h"

namespace arp::core::io {
enum class AtomicFileOperation { Read, Replace };
using FileContentValidator = bool (*)(const pmr::Vector<std::uint8_t>&, pmr::String&);

struct AtomicFileResult {
    bool success = false, missing = false, committed = false;
    pmr::Vector<std::uint8_t> bytes;
    pmr::String diagnostic;
};

// Preparation/tooling-only synchronous boundary; runtime callers use the
// bounded worker service. Shares the same OS replacement implementation.
ARP_API AtomicFileResult replaceFileAtomically(
    const pmr::String& path, const pmr::Vector<std::uint8_t>& bytes);

// Bounded, single worker, ordered requests. Poll/take on the owner thread.
// Validation occurs before replacement; invalid existing files are preserved.
// No promise of directory/power-loss durability beyond OS atomic replacement.
class AtomicFileService final {
public:
    AtomicFileService(std::uint32_t maxRequests, std::size_t maxBytes);
    ~AtomicFileService();
    AtomicFileService(const AtomicFileService&) = delete;
    AtomicFileService& operator=(const AtomicFileService&) = delete;
    PoolHandle read(pmr::String path, FileContentValidator validator = nullptr);
    PoolHandle replace(pmr::String path,
        pmr::Vector<std::uint8_t> bytes,
        FileContentValidator validator = nullptr);
    void poll();
    bool take(PoolHandle, AtomicFileResult&);

    const char* diagnostic() const { return diagnostic_; }

private:
    struct Request {
        AtomicFileOperation operation;
        pmr::String path;
        std::size_t maxBytes = 0;
        FileContentValidator validator = nullptr;
        AtomicFileResult result;
        bool complete = false;
    };

    PoolHandle submit(
        AtomicFileOperation, pmr::String, pmr::Vector<std::uint8_t>, FileContentValidator);
    static runtime::WorkStatus execute(void*);
    static void complete(void*, runtime::WorkResult);
    std::size_t maxBytes_;
    GenerationPool<Request> requests_;
    runtime::CompletionQueue completions_;
    runtime::WorkService worker_;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::core::io
