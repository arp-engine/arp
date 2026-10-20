#pragma once

#include <string>
#include <string_view>

namespace arp::reflection {

template <typename String>
void appendRegistrationContractField(String& output, std::string_view name, std::string_view value)
{
    output.append(name.data(), name.size());
    output.push_back(':');
    const auto sizeText = std::to_string(value.size());
    output.append(sizeText.data(), sizeText.size());
    output.push_back(':');
    output.append(value.data(), value.size());
    output.push_back('\n');
}

} // namespace arp::reflection
