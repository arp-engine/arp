#include "rhi/gpu_common.h"

#include <cstdio>
#include <cstdlib>

namespace arp::rhi {

// -- RhiErrorHandler -------------------------------------------

RhiErrorHandler& RhiErrorHandler::get()
{
    static RhiErrorHandler instance;
    return instance;
}

void RhiErrorHandler::setCallback(RhiErrorCallback callback, void* userData)
{
    callback_ = callback;
    userData_ = userData;
}

void RhiErrorHandler::report(RhiSeverity severity, const char* message)
{
    if (callback_) {
        callback_(severity, message, userData_);
    } else {
        defaultReport(severity, message);
    }
    if (severity == RhiSeverity::Fatal) {
        std::abort();
    }
}

void RhiErrorHandler::fatal(const char* message)
{
    report(RhiSeverity::Fatal, message);
    std::abort();
}

void RhiErrorHandler::defaultReport(RhiSeverity severity, const char* message)
{
    const char* label = "info";
    switch (severity) {
    case RhiSeverity::Info:
        label = "RHI info";
        break;
    case RhiSeverity::Warning:
        label = "RHI warning";
        break;
    case RhiSeverity::Error:
        label = "RHI error";
        break;
    case RhiSeverity::Fatal:
        label = "RHI fatal";
        break;
    }
    std::fprintf(stderr, "[%s] %s\n", label, message ? message : "(no message)");
}

} // namespace arp::rhi
