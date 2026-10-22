#pragma once

#include "core/memory/cpu_allocator.h"

#include <string>
#include <string_view>

namespace arp::renderer {

enum class RendererResultDisposition {
    DiagnosticFailure,
    Deferred,
};

struct RendererResult {
    bool success = false;
    std::pmr::string diagnostics;
    RendererResultDisposition disposition = RendererResultDisposition::DiagnosticFailure;
    std::pmr::string diagnosticId;

    bool isDeferred() const noexcept
    {
        return !success && disposition == RendererResultDisposition::Deferred;
    }

    static RendererResult deferred(std::string_view message, std::string_view stableDiagnosticId)
    {
        RendererResult result;
        result.diagnostics = message;
        result.disposition = RendererResultDisposition::Deferred;
        result.diagnosticId = stableDiagnosticId;
        return result;
    }
};

} // namespace arp::renderer
