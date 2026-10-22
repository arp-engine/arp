#include "core/math/curve.h"

#include <cassert>
#include <cmath>

namespace {

void float_curve_empty_is_invalid()
{
    arp::core::FloatCurve curve;
    assert(!curve.valid());
    assert(curve.sample(0.5f) == 0.0f);
}

void float_curve_single_key_returns_constant()
{
    arp::core::FloatCurve curve;
    curve.keys.push_back({0.0f, 3.0f});
    assert(curve.valid());
    assert(curve.sample(0.0f) == 3.0f);
    assert(curve.sample(0.5f) == 3.0f);
    assert(curve.sample(1.0f) == 3.0f);
}

void float_curve_linear_interpolation()
{
    arp::core::FloatCurve curve;
    curve.keys.push_back({0.0f, 0.0f});
    curve.keys.push_back({1.0f, 10.0f});
    assert(curve.valid());
    assert(curve.sample(0.0f) == 0.0f);
    assert(curve.sample(0.5f) == 5.0f);
    assert(curve.sample(1.0f) == 10.0f);
}

void float_curve_clamps_before_first_and_after_last()
{
    arp::core::FloatCurve curve;
    curve.keys.push_back({0.2f, 1.0f});
    curve.keys.push_back({0.8f, 9.0f});
    assert(curve.sample(0.0f) == 1.0f);
    assert(curve.sample(1.0f) == 9.0f);
}

void float_curve_rejects_unsorted_keys()
{
    arp::core::FloatCurve curve;
    curve.keys.push_back({0.5f, 1.0f});
    curve.keys.push_back({0.2f, 2.0f});
    assert(!curve.valid());
}

void float_curve_uses_core_pmr_allocator()
{
    arp::core::FloatCurve curve;
    curve.keys.push_back({0.0f, 1.0f});
    curve.keys.push_back({1.0f, 2.0f});
    assert(curve.valid());
    assert(curve.sample(0.5f) == 1.5f);
}

} // namespace

int main()
{
    float_curve_empty_is_invalid();
    float_curve_single_key_returns_constant();
    float_curve_linear_interpolation();
    float_curve_clamps_before_first_and_after_last();
    float_curve_rejects_unsorted_keys();
    float_curve_uses_core_pmr_allocator();
    return 0;
}
