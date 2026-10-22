#pragma once

#include "core/memory/cpu_allocator.h"
#include "scriptvm/runtime/diagnostics.h"

#include <cstdint>
#include <stdexcept>

namespace arp::scriptvm {

// Source-module exports cross the HostCallable ABI as C++ calls. This marker
// preserves a script diagnostic across that boundary without weakening the
// generic HostError handling for ordinary host exceptions.
class ScriptHostException final : public std::runtime_error {
public:
    explicit ScriptHostException(const char* message)
        : std::runtime_error(message != nullptr ? message : "script host exception")
    {}
};

struct VmError {
    ScriptDiagnosticCode code = ScriptDiagnosticCode::None;
    core::pmr::String message{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String traceback{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String moduleId{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0;
    core::pmr::String functionId{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String requestedBackend{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String selectedBackend{&arp::core::defaultCpuMemoryResource()};
    std::uint64_t policyEpoch = 0;
    core::pmr::String diagnosticId{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm
