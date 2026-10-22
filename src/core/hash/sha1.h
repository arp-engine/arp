#pragma once

#include "core/encoding/base64.h"

namespace arp::core {

class ARP_API Sha1 {
public:
    static ByteBuffer digest(StringView text);
};

} // namespace arp::core
