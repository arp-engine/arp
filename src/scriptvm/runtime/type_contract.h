#pragma once

// Backend-neutral runtime value categories used at public call boundaries.

#include <cstdint>

namespace arp::scriptvm {

enum class RuntimeType : std::uint8_t {
    Any,
    Int,
    Int64,
    Float,
    Bool,
    String,
    Bytes,
    Object,
    List,
    Dict,
    Function,
    Class,
};

} // namespace arp::scriptvm
