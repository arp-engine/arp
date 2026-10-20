# Async FileService Contract

Status: Contract Complete.

FileService defines typed async file request/result semantics independent of
WorkService implementation. This phase does not claim a runnable file IO
backend beyond NullFileBackend validation.

## Request Contract

FileReadRequest requires:

- explicit path scope: HostFilesystem, MountedResource, or Package
- non-empty path
- range policy: read-whole-file or exact-range
- exact-range offset and size, where size == 0 is invalid
- partial read policy: DenyPartial or AllowPartial

Path scopes must not be mixed silently. Resource packages, mounted paths, and
host filesystem paths require explicit metadata or request fields.

## Result Contract

FileReadResult contains FileIoStatus, FileBackendKind, allocator-aware
`core::pmr::Vector<std::uint8_t>` bytes, and allocator-aware diagnostics.

Deterministic statuses include missing file, permission denied, invalid path
scope, invalid range, partial read denied, backend unavailable, cancelled,
queue full, service closed, and invalid request.

## Completion And Cancellation

Callbacks complete through CompletionQueue. FileService validates before
posting when possible and rejects shutdown submissions deterministically.

Cancellation before scheduling, while queued, while running, and after
completion is defined as a typed FileIoStatus contract for later phases. Late
completion after cancellation must not call back twice.

## Backends

Backend kinds are NullFileBackend, ThreadPoolFallbackFileBackend,
WindowsOverlappedFileBackend, LinuxIoUringFileBackend, and DarwinFileBackend.
ThreadPoolFallbackFileBackend must report nativeAsync = false.

NullFileBackend request validation smoke: submit invalid path scope and valid
host path requests, dispatch through CompletionQueue, verify deterministic
diagnostics, then reject shutdown submissions.
