#pragma once

#include "core/memory/cpu_allocator.h"

#include <exception>
#include <string_view>

namespace arp::core {

// Owned diagnostic for exception-based execution boundaries. Its message uses
// the runtime allocator, remains valid during unwinding, and carries no native
// payload. Existing std::exception boundaries can report it unchanged.
class DiagnosticError final : public std::exception {
public:
    explicit DiagnosticError(std::string_view message)
        : message_(message, &defaultCpuMemoryResource())
    {}

    const char* what() const noexcept override { return message_.c_str(); }

private:
    pmr::String message_;
};

// Adapt a typed success/diagnostics result to an exception-based boundary.
// This utility owns the first diagnostic before the result can be destroyed;
// successful calls allocate nothing. The caller supplies a stable fallback
// only for a failing result whose producer did not provide a diagnostic.
template <class Result>
void requireSuccessfulResult(const Result& result, std::string_view fallbackDiagnostic)
{
    if (!result.success) {
        throw DiagnosticError(
            result.diagnostics.empty() ? fallbackDiagnostic : std::string_view(result.diagnostics));
    }
}

} // namespace arp::core
