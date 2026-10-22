#include "core/math/geometry.h"

#include <cassert>

int main()
{
    using arp::core::Rect2;

    const Rect2<float> a{0.0f, 0.0f, 10.0f, 10.0f};
    const Rect2<float> b{5.0f, 4.0f, 10.0f, 3.0f};
    assert(arp::core::rectsIntersect(a, b));

    const auto clipped = arp::core::intersectRect(a, b);
    assert(clipped.x == 5.0f);
    assert(clipped.y == 4.0f);
    assert(clipped.width == 5.0f);
    assert(clipped.height == 3.0f);

    const Rect2<float> separated{11.0f, 0.0f, 2.0f, 2.0f};
    assert(!arp::core::rectsIntersect(a, separated));
    assert(arp::core::intersectRect(a, separated).width == 0.0f);

    const Rect2<float> edgeTouch{10.0f, 2.0f, 4.0f, 4.0f};
    assert(!arp::core::rectsIntersect(a, edgeTouch));
    assert(arp::core::intersectRect(a, edgeTouch).width == 0.0f);

    assert(arp::core::rectContainsPoint(a, 0.0f, 0.0f));
    assert(arp::core::rectContainsPoint(a, 10.0f, 10.0f));
    assert(!arp::core::rectContainsPoint(a, 10.1f, 5.0f));

    const Rect2<int> ia{-2, -1, 8, 6};
    const Rect2<int> ib{1, -4, 3, 5};
    const auto ic = arp::core::intersectRect(ia, ib);
    assert(arp::core::rectsIntersect(ia, ib));
    assert(ic.x == 1);
    assert(ic.y == -1);
    assert(ic.width == 3);
    assert(ic.height == 2);

    const Rect2<int> empty{0, 0, 0, 10};
    assert(!arp::core::rectsIntersect(empty, ia));

    return 0;
}
