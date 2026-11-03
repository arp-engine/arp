#pragma once

#include "native_startup_files.h"
#include "startup_resource_task.h"

#include <chrono>
#include <thread>

namespace arp::examples {

struct NativeStartupWaitDiagnostics {
    const char* timeout;
    const char* dispatch;
    const char* notRetired;
};

// Cold native startup only. Source tasks retain domain parsing/ownership;
// this common wait retires IO before the caller consumes their prepared owner.
template <class Task>
bool waitForNativeStartup(NativeStartupFiles& files,
    Task& task,
    std::chrono::milliseconds timeout,
    NativeStartupWaitDiagnostics ids,
    core::pmr::String& diagnostic)
{
    auto& runtime = files.runtime();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (task.phase() == StartupResourcePhase::Pending) {
        if (std::chrono::steady_clock::now() >= deadline) {
            diagnostic = ids.timeout;
            task.cancel();
            break;
        }
        if (!runtime.dispatch().success()) {
            diagnostic = ids.dispatch;
            task.cancel();
            break;
        }
        task.advance();
        std::this_thread::yield();
    }
    if (task.phase() != StartupResourcePhase::Ready && diagnostic.empty()) {
        if (task.diagnosticId()) {
            diagnostic = task.diagnosticId();
            diagnostic += ": ";
        }
        diagnostic += task.diagnostic();
    }
    runtime.shutdown();
    if (task.phase() != StartupResourcePhase::Ready)
        return false;
    if (runtime.completionQueue().pending() || runtime.completionQueue().reserved()) {
        diagnostic = ids.notRetired;
        return false;
    }
    return true;
}
} // namespace arp::examples
