#pragma once
#include "core/math/math.h"

#include <stdexcept>

namespace arp::core {
AFUNC(Name = "sqrt")

inline double scriptSqrt(double value)
{
    return std::sqrt(value);
}
AFUNC(Name = "sin")

inline double scriptSin(double value)
{
    return std::sin(value);
}
AFUNC(Name = "cos")

inline double scriptCos(double value)
{
    return std::cos(value);
}
AFUNC(Name = "atan2")

inline double scriptAtan2(double y, double x)
{
    return std::atan2(y, x);
}
AFUNC(Name = "clamp")

inline double scriptClamp(double value, double low, double high)
{
    if (!std::isfinite(value) || !std::isfinite(low) || !std::isfinite(high) || low > high)
        throw std::invalid_argument("ARP-MATH-CLAMP-RANGE");
    return std::clamp(value, low, high);
}
} // namespace arp::core
