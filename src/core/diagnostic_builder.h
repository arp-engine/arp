#pragma once

#include <memory_resource>
#include <string>
#include <string_view>

namespace arp::core {

inline void appendDiagnostic(
    std::pmr::string& target, std::string_view entry, std::string_view separator = "; ")
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
    std::pmr::string& target, const std::pmr::string& entry, std::string_view separator = "; ")
{
    appendDiagnostic(target, std::string_view(entry.data(), entry.size()), separator);
}

inline void appendDiagnostic(
    std::pmr::string& target, const std::string& entry, std::string_view separator = "; ")
{
    appendDiagnostic(target, std::string_view(entry.data(), entry.size()), separator);
}

inline void appendDiagnostic(
    std::pmr::string& target, const char* entry, std::string_view separator = "; ")
{
    appendDiagnostic(target, std::string_view(entry != nullptr ? entry : ""), separator);
}

class DiagnosticBuilder {
public:
    explicit DiagnosticBuilder(std::pmr::string& target) noexcept : target_(target) {}

    void append(std::string_view entry) { appendDiagnostic(target_, entry); }

    void append(const std::pmr::string& entry) { appendDiagnostic(target_, entry); }

    void append(const char* entry) { appendDiagnostic(target_, entry); }

private:
    std::pmr::string& target_;
};

} // namespace arp::core
