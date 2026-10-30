#pragma once

#include "core/io/native_directory_file_backend.h"

namespace arp::core::io::detail {

// Root-relative native operations. Called exclusively by the backend IO worker.
class NativeDirectoryAccess final {
public:
    explicit NativeDirectoryAccess(const NativeDirectoryFileBackendDesc& desc);
    ~NativeDirectoryAccess();
    bool supported() const noexcept;
    FileReadResult read(const FileReadRequest& request);
    FileQueryResult query(const FileQueryRequest& request);
    void shutdown() noexcept;
    std::uint64_t openObjectCount() const noexcept;

private:
    struct Impl;
    core::UniquePtr<Impl> impl_;
};

} // namespace arp::core::io::detail
