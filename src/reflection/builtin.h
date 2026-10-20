#pragma once

// builtin -- mirrors ae::hybrid/builtin.h, adapted for arp::reflection.
// Declares registerBuiltinReflectionTypes() which registers hdict, hlist,
// String, int, int64, bool, double with their magic methods.

#include "core/api.h"
#include "reflection/def.h"

namespace arp::reflection {

ARP_API void registerBuiltinReflectionTypes();

} // namespace arp::reflection
