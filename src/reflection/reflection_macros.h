#pragma once

#include "core/meta/annotations.h"
#include "reflection/reflection.h"

#define ARP_REFLECTION_CONCAT_IMPL(lhs, rhs) lhs##rhs
#define ARP_REFLECTION_CONCAT(lhs, rhs) ARP_REFLECTION_CONCAT_IMPL(lhs, rhs)
#define AGENERATED_BODY(TypeName)                                                                  \
    OBJECT_RTTI_IMPL(TypeName);                                                                    \
    friend struct ARP_REFLECTION_CONCAT(Access_, TypeName);
