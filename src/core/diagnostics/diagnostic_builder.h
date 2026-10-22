#pragma once

#include "core/memory/cpu_allocator.h"

#include <memory_resource>
#include <string>
#include <string_view>

namespace arp::core {

inline void appendDiagnostic(
    pmr::String& target, std::string_view entry, std::string_view separator = "; ")
{
    if (entry.empty()) {
        return;
    }
    if (!target.empty()) {
        target.append(separator.data(), separator.size());
    }
    target.append(entry.data(), entry.size());
}

inline void appendDiagnostic(
    pmr::String& target, const pmr::String& entry, std::string_view separator = "; ")
{
    appendDiagnostic(target, std::string_view(entry.data(), entry.size()), separator);
}

inline void appendDiagnostic(
    pmr::String& target, const std::string& entry, std::string_view separator = "; ")
{
    appendDiagnostic(target, std::string_view(entry.data(), entry.size()), separator);
}

inline void appendDiagnostic(
    pmr::String& target, const char* entry, std::string_view separator = "; ")
{
    appendDiagnostic(target, std::string_view(entry != nullptr ? entry : ""), separator);
}

class DiagnosticBuilder {
public:
    explicit DiagnosticBuilder(pmr::String& target) noexcept : target_(target) {}

    void append(std::string_view entry) { appendDiagnostic(target_, entry); }

    void append(const pmr::String& entry) { appendDiagnostic(target_, entry); }

    void append(const char* entry) { appendDiagnostic(target_, entry); }

private:
    pmr::String& target_;
};

} // namespace arp::core
