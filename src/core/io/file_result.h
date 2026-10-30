#pragma once

#include "core/io/file_version.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>

namespace arp::core::io {

enum class FileIoStatus : std::uint8_t {
    Accepted,
    Success,
    MissingFile,
    PermissionDenied,
    InvalidPathScope,
    InvalidRange,
    PartialReadDenied,
    BackendUnavailable,
    Cancelled,
    QueueFull,
    ServiceClosed,
    InvalidRequest,
    BudgetExceeded,
    Unsupported,
    VersionMismatch,
    IntegrityMismatch,
    Timeout,
    StaleLocation,
    OutcomeUnknown,
};

enum class FileBackendKind : std::uint8_t {
    NullFileBackend,
    ThreadPoolFallbackFileBackend,
    WindowsOverlappedFileBackend,
    LinuxIoUringFileBackend,
    DarwinFileBackend,
    MemoryFileBackend,
    MountedFileBackend,
    HttpFileBackend,
    FetchFileBackend,
    NativeDirectoryFileBackend,
};

enum class FileBackendStatus : std::uint8_t {
    ContractComplete,
    PrototypeComplete,
    BackendValidated,
    PlatformValidated,
    ProductionReady,
};

struct FileReadResult {
    FileIoStatus status = FileIoStatus::Success;
    FileBackendKind backendKind = FileBackendKind::NullFileBackend;
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    // Static stable identifier; never points into transient backend storage.
    // Allows cancellation/failure delivery without allocating a message.
    const char* diagnosticId = nullptr;
    FileVersionToken version;
};

using FileReadCompleteFn = void (*)(void*, FileReadResult);

struct FileReadCompletion {
    FileReadCompleteFn complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

} // namespace arp::core::io
