#include "native_startup_files.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/filesystem_path.h"
#include "core/io/http_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/native_directory_file_backend.h"

#include <cstdio>

namespace arp::examples {

NativeStartupFiles::NativeStartupFiles(
    NativeStartupFileDesc config, NativeStartupFileDiagnostics diagnostics)
{
    if (!diagnostics.configuration || !*diagnostics.configuration || !diagnostics.mount
        || !*diagnostics.mount || !diagnostics.runtime || !*diagnostics.runtime)
        throw core::DiagnosticError("EXAMPLE-STARTUP-FILE-DIAGNOSTICS-INVALID");
    if (!config.maxFileBytes || config.maxFileBytes > SIZE_MAX / 2 || config.mountName.empty())
        throw core::DiagnosticError(diagnostics.configuration);
    if (config.allowObservedBaselineReplace
            ? (!config.maxBackendSaveScratchBytes
                  || config.maxBackendSaveScratchBytes > SIZE_MAX - 2 * config.maxFileBytes)
            : config.maxBackendSaveScratchBytes != 0)
        throw core::DiagnosticError(diagnostics.configuration);

    using namespace core::io;
    core::SharedPtr<ManagedFileBackend> backend;
    // One operation at a time. The original Queen read profile is unchanged;
    // record saves explicitly reserve both candidate and baseline payloads.
    const auto reservationBytes =
        config.allowObservedBaselineReplace ? 2 * config.maxFileBytes : config.maxFileBytes;
    if (config.source == NativeStartupSource::Directory) {
        if (config.directory.empty() || !config.baseUrl.empty())
            throw core::DiagnosticError(diagnostics.configuration);
        NativeDirectoryFileBackendDesc desc;
        desc.rootPath = core::FilePath::utf8(config.directory);
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = reservationBytes + config.maxBackendSaveScratchBytes;
        desc.maxPathBytes = 512;
        desc.ioChunkBytes = 16384;
        desc.directoryBufferBytes = 4096;
        desc.maxScanEntries = 16;
        desc.maxScanNameBytes = 4096;
        desc.allowObservedBaselineReplace = config.allowObservedBaselineReplace;
        backend = core::makeShared<NativeDirectoryFileBackend>(std::move(desc));
    } else if (config.source == NativeStartupSource::Http) {
        if (!config.directory.empty() || config.baseUrl.empty()
            || config.allowObservedBaselineReplace)
            throw core::DiagnosticError(diagnostics.configuration);
        HttpFileBackendDesc desc;
        desc.baseUrl = std::move(config.baseUrl);
        desc.maxInFlight = 1;
        // Reserve payload and the existing transport/header/task envelope
        // separately. The HTTP backend enforces its actual admission budget.
        desc.maxBytesInFlight = 2 * config.maxFileBytes;
        desc.maxPathBytes = 512;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 16384;
        desc.timeoutMs = 5000;
        desc.cancellationPollMs = 10;
        desc.maxDnsAddresses = 16;
        backend = core::makeShared<HttpFileBackend>(std::move(desc));
    } else
        throw core::DiagnosticError(diagnostics.configuration);

    auto mounts = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, reservationBytes, 32, 512});
    mount_ = mounts->mount(config.mountName, std::move(backend));
    if (!mount_) {
        std::fprintf(stderr, "%s\n", mounts->diagnostic());
        throw core::DiagnosticError(diagnostics.mount);
    }
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 2;
    desc.completionBudget = 2;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = std::move(mounts);
    runtime_ = core::makeUnique<core::runtime::AsyncRuntime>(std::move(desc));
    if (!runtime_->valid())
        throw core::DiagnosticError(diagnostics.runtime);
}
} // namespace arp::examples
