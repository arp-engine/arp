#pragma once

#include <string>
#include <string_view>

namespace arp::core {

inline std::string quoteShellArgument(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 2u);
    result.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            result += "\\\"";
#if defined(_WIN32) || defined(__CYGWIN__)
        } else if (ch == '/') {
            result.push_back('\\');
#endif
        } else {
            result.push_back(ch);
        }
    }
    result.push_back('"');
    return result;
}

} // namespace arp::core
