#pragma once

#include "core/diagnostic_error.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::core {
// Bounds finite float arithmetic, rounding both endpoints outwards. Products
// of two float endpoints are exact in double before conversion to float.
class ConservativeFloatInterval final {
public:
    float low = 0, high = 0;

    static ConservativeFloatInterval range(float a, float b)
    {
        if (!std::isfinite(a) || !std::isfinite(b) || a > b)
            throw DiagnosticError("FLOAT-INTERVAL-INVALID");
        return {a, b};
    }

    static ConservativeFloatInterval point(float value) { return range(value, value); }

    float absoluteMaximum() const { return std::max(std::abs(low), std::abs(high)); }

    friend ConservativeFloatInterval operator+(
        ConservativeFloatInterval a, ConservativeFloatInterval b)
    {
        return enclose(double(a.low) + b.low, double(a.high) + b.high);
    }

    friend ConservativeFloatInterval operator-(
        ConservativeFloatInterval a, ConservativeFloatInterval b)
    {
        return enclose(double(a.low) - b.high, double(a.high) - b.low);
    }

    friend ConservativeFloatInterval operator*(
        ConservativeFloatInterval a, ConservativeFloatInterval b)
    {
        const double products[] = {double(a.low) * b.low,
            double(a.low) * b.high,
            double(a.high) * b.low,
            double(a.high) * b.high};
        return enclose(
            *std::min_element(products, products + 4), *std::max_element(products, products + 4));
    }

private:
    static ConservativeFloatInterval enclose(double a, double b)
    {
        const auto low = std::nextafter(float(a), -std::numeric_limits<float>::infinity());
        const auto high = std::nextafter(float(b), std::numeric_limits<float>::infinity());
        if (!std::isfinite(low) || !std::isfinite(high))
            throw DiagnosticError("FLOAT-INTERVAL-OVERFLOW");
        return {low, high};
    }
};
} // namespace arp::core
