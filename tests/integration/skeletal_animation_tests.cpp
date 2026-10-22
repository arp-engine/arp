#include "animation/skeleton.h"
#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cmath>
#include <vector>

namespace {

bool near(const float a, const float b)
{
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main()
{
    animation::Skeleton skeleton;
    skeleton.joints.push_back(animation::Joint{
        "root",
        -1,
        animation::Mat4::translation({-10.0f, 0.0f, 0.0f}),
    });
    skeleton.joints.push_back(animation::Joint{
        "hand",
        0,
        animation::Mat4::translation({0.0f, -2.0f, 0.0f}),
    });

    assert(skeleton.valid());
    assert(skeleton.jointCount() == 2);

    std::pmr::vector<animation::LocalTransform> localPose = {
        animation::LocalTransform{
            {10.0f, 0.0f, 0.0f},
            animation::Quat::identity(),
            {1.0f, 1.0f, 1.0f},
        },
        animation::LocalTransform{
            {0.0f, 2.0f, 0.0f},
            animation::Quat::identity(),
            {1.0f, 1.0f, 1.0f},
        },
    };

    const auto modelPose = animation::buildModelPose(skeleton, localPose);
    assert(modelPose.size() == 2);
    assert(near(modelPose[0].m[12], 10.0f));
    assert(near(modelPose[1].m[12], 10.0f));
    assert(near(modelPose[1].m[13], 2.0f));

    const auto skinMatrices = animation::buildSkinMatrices(skeleton, modelPose);
    assert(skinMatrices.size() == 2);
    assert(near(skinMatrices[0].m[12], 0.0f));
    assert(near(skinMatrices[1].m[12], 10.0f));
    assert(near(skinMatrices[1].m[13], 0.0f));

    const auto blended = animation::lerp(
        animation::LocalTransform{
            {0.0f, 0.0f, 0.0f},
            animation::Quat::identity(),
            {1.0f, 1.0f, 1.0f},
        },
        animation::LocalTransform{
            {2.0f, 4.0f, 6.0f},
            animation::Quat::identity(),
            {3.0f, 3.0f, 3.0f},
        },
        0.5f);
    assert(near(blended.translation.x, 1.0f));
    assert(near(blended.translation.y, 2.0f));
    assert(near(blended.translation.z, 3.0f));
    assert(near(blended.scale.x, 2.0f));

    return 0;
}
