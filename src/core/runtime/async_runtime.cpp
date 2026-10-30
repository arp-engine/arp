#include "core/runtime/async_runtime.h"

namespace arp::core::runtime {

namespace {

WorkShutdownPolicy mapShutdownPolicy(AsyncRuntimeShutdownPolicy policy) noexcept
{
    return policy == AsyncRuntimeShutdownPolicy::Drain ? WorkShutdownPolicy::Drain
                                                       : WorkShutdownPolicy::CancelPending;
}

} // namespace

AsyncRuntime::AsyncRuntime(AsyncRuntimeDesc desc)
    : desc_(desc), completions_(CompletionQueueDesc{desc.completionQueueCapacity}),
      work_(WorkServiceDesc{
          &completions_,
          desc.workerCount,
          desc.workQueueCapacity,
#if defined(__EMSCRIPTEN__)
          true,
#else
          false,
#endif
          desc.enableWork,
      }),
      fileBackend_(work_),
      fileService_(completions_,
          desc_.fileBackend                      ? static_cast<io::FileBackend&>(*desc_.fileBackend)
              : desc.enableThreadPoolFileBackend ? static_cast<io::FileBackend&>(fileBackend_)
                                                 : static_cast<io::FileBackend&>(nullFileBackend_))
{
    diagnostic_ = validateDesc(desc_);
    if (diagnostic_ != AsyncRuntimeDiagnostic::None || (desc.enableWork && !work_.valid())) {
        status_ = AsyncRuntimeStatus::InvalidConfiguration;
        fileService_.shutdown();
        work_.shutdown(WorkShutdownPolicy::CancelPending);
    }
}

AsyncRuntime::~AsyncRuntime()
{
    shutdown(AsyncRuntimeShutdownPolicy::CancelPending);
}

bool AsyncRuntime::valid() const noexcept
{
    return status_ == AsyncRuntimeStatus::Running;
}

AsyncRuntimeStatus AsyncRuntime::status() const noexcept
{
    return status_;
}

AsyncRuntimeDiagnostic AsyncRuntime::startupDiagnostics() const noexcept
{
    return diagnostic_;
}

CompletionQueue& AsyncRuntime::completionQueue() noexcept
{
    return completions_;
}

WorkService& AsyncRuntime::workService() noexcept
{
    return work_;
}

io::FileService& AsyncRuntime::fileService() noexcept
{
    return fileService_;
}

CompletionDispatchResult AsyncRuntime::dispatch() noexcept
{
    return dispatch(desc_.completionBudget);
}

CompletionDispatchResult AsyncRuntime::dispatch(std::size_t maxCallbacks) noexcept
{
    ++dispatchCalls_;
    return completions_.dispatch(maxCallbacks);
}

void AsyncRuntime::shutdown(AsyncRuntimeShutdownPolicy policy) noexcept
{
    if (status_ == AsyncRuntimeStatus::Shutdown) {
        return;
    }

    fileService_.shutdown();
    if (desc_.fileBackend && status_ != AsyncRuntimeStatus::InvalidConfiguration)
        desc_.fileBackend->shutdown();
    work_.shutdown(mapShutdownPolicy(policy));
    while (completions_.pending() > 0) {
        const auto dispatchResult = completions_.dispatch(desc_.completionBudget);
        if (!dispatchResult.success() || dispatchResult.dispatched == 0) {
            break;
        }
    }
    completions_.shutdown();
    status_ = AsyncRuntimeStatus::Shutdown;
}

AsyncRuntimeTelemetry AsyncRuntime::telemetry() const noexcept
{
    return {
        completions_.telemetry(),
        work_.telemetry(),
        fileService_.capability(),
        fileService_.telemetry(),
        desc_.completionBudget,
        dispatchCalls_,
        desc_.enableExternalNetworkValidation,
    };
}

AsyncRuntimeDiagnostic AsyncRuntime::validateDesc(const AsyncRuntimeDesc& desc) const noexcept
{
    if (desc.completionQueueCapacity == 0) {
        return AsyncRuntimeDiagnostic::InvalidCompletionQueueCapacity;
    }
    if (desc.completionBudget == 0) {
        return AsyncRuntimeDiagnostic::InvalidCompletionBudget;
    }
    if (desc.enableWork && (desc.workerCount == 0 || desc.workQueueCapacity == 0)) {
        return AsyncRuntimeDiagnostic::MissingWorkBackend;
    }
    if (desc.enableThreadPoolFileBackend && !desc.enableWork) {
        return AsyncRuntimeDiagnostic::MissingFileBackend;
    }
    if (desc.fileBackend && desc.enableThreadPoolFileBackend)
        return AsyncRuntimeDiagnostic::AmbiguousFileBackend;
    if (!desc.fileBackend && !desc.enableThreadPoolFileBackend)
        return AsyncRuntimeDiagnostic::MissingFileBackend;
    if (!desc.enableExternalNetworkValidation) {
        return AsyncRuntimeDiagnostic::MissingNetworkValidationSurface;
    }
    return AsyncRuntimeDiagnostic::None;
}

} // namespace arp::core::runtime
