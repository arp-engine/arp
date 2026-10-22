#pragma once

// Engine-internal animation retargeting — retargets a source AnimationClip to a
// target Skeleton using humanoid, generic, or aliased bone mapping strategies.
//
// Lives in arp_assets (not tools/) because the retargeting algorithms operate
// on general engine types (animation::AnimationClip, animation::Skeleton,
// animation::LocalTransform).  The AvatarAsset provides the bone mapping
// contract but the math itself is format-agnostic.

#include "animation/animation.h"
#include "animation/skeleton.h"
#include "assets/imported_asset_types.h"
#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <vector>

namespace arp::assets {

ARP_API animation::AnimationClip retargetClipToSkeleton(animation::AnimationClip clip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose,
    const AvatarAsset& avatar);

} // namespace arp::assets

// Backward-compatible alias — prefer retargetClipToSkeleton.
namespace arp::assets {
inline animation::AnimationClip retargetUnityClipToSkeleton(animation::AnimationClip clip,
    const animation::Skeleton& targetSkeleton,
    const std::pmr::vector<animation::LocalTransform>& targetBindPose,
    const animation::Skeleton& sourceSkeleton,
    const std::pmr::vector<animation::LocalTransform>& sourceBindPose,
    const AvatarAsset& avatar)
{
    return retargetClipToSkeleton(
        std::move(clip), targetSkeleton, targetBindPose, sourceSkeleton, sourceBindPose, avatar);
}
} // namespace arp::assets
