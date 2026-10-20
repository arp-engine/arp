#include "reflection/reflection.h"

#include <cassert>

namespace {

struct TransformDebugComponent {
    explicit TransformDebugComponent(float initialX) : x(initialX) {}

    float translate(float delta)
    {
        x += delta;
        return x;
    }

    float x = 0.0f;
};

} // namespace

int main()
{
    using namespace arp::reflection;

    registration::class_<TransformDebugComponent>("TransformDebugComponent")
        .construct<float>()
        .property("x", &TransformDebugComponent::x, "", 1)
        .method("translate", &TransformDebugComponent::translate);

    object created = class_of<TransformDebugComponent>()->new_object(variant(4.0f));
    assert(!created.is_undefined());
    assert(created["x"].cast<float>() > 3.99f);

    object moved = created.call("translate", 1.5f);
    assert(moved.cast<float>() > 5.49f);
    assert(created["x"].cast<float>() > 5.49f);

    object missing = created.call("missing");
    assert(missing.is_undefined());

    return 0;
}
