#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <string_view>

namespace arp::core {

using StringView = std::basic_string_view<char>;
using ByteBuffer = pmr::Vector<std::uint8_t>;

class ARP_API Base64 {
public:
    static pmr::String encode(const ByteBuffer& bytes);
    static bool decode(StringView text, ByteBuffer& out);
};

} // namespace arp::core
