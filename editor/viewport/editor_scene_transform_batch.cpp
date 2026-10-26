#include "editor/viewport/editor_scene_transform_batch.h"

#include <algorithm>
#include <new>
#include <utility>

namespace arp::editor {
namespace {
using Frame = EditorSceneTransformFrame;
using Tool = EditorSceneTransformTool;
using Space = EditorSceneTransformSpace;
using Pivot = EditorSceneTransformPivot;

float component(core::Vec3 value, unsigned axis) noexcept
{
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}

bool validWorld(const scene::Transform& value) noexcept
{
    Frame frame;
    return std::isfinite(value.translation.x) && std::isfinite(value.translation.y)
        && std::isfinite(value.translation.z) && std::isfinite(value.scale.x)
        && std::isfinite(value.scale.y) && std::isfinite(value.scale.z)
        && Frame::build(value.rotation, Space::World, frame);
}

const char* validateSnapshot(const scene::SceneTraversalSnapshot& snapshot) noexcept
{
    if (!snapshot.ok() || snapshot.nodes().empty())
        return "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
    std::size_t roots = 0;
    for (const auto& node : snapshot.nodes()) {
        if (!node.objectId.valid() || !Frame::validAuthoring(node.localTransform)
            || !validWorld(node.worldTransform))
            return "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
        unsigned occurrences = 0;
        for (const auto& other : snapshot.nodes())
            occurrences += other.objectId == node.objectId;
        if (occurrences != 1)
            return "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
        const auto* parent = snapshot.findNode(node.parentId);
        if (node.parentId.valid() && !parent)
            return "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
        roots += !node.parentId.valid();
        const auto expected = parent
            ? scene::composeTransforms(parent->worldTransform, node.localTransform)
            : node.localTransform;
        if (!Frame::same(expected, node.worldTransform))
            return "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
        auto ancestor = node.parentId;
        std::size_t hops = 0;
        while (ancestor.valid()) {
            if (++hops > snapshot.nodes().size())
                return "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
            const auto* found = snapshot.findNode(ancestor);
            if (!found)
                return "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
            ancestor = found->parentId;
        }
    }
    return roots == 1 ? nullptr : "EDITOR-TRANSFORM-BATCH-SNAPSHOT";
}
} // namespace

const char* EditorSceneTransformBatch::capture(const scene::SceneTraversalSnapshot& snapshot,
    scene::GameObjectId primary,
    core::ArrayView<scene::GameObjectId> selected,
    Pivot pivot,
    Budget budget,
    EditorSceneTransformBatch& destination) noexcept
{
    if (selected.empty() || !selected.data() || !primary.valid()
        || (pivot != Pivot::Active && pivot != Pivot::Center && pivot != Pivot::Individual))
        return "EDITOR-TRANSFORM-BATCH-SELECTION";
    if (!budget.maxSelectedObjects || !budget.maxSnapshotNodes
        || selected.size() > budget.maxSelectedObjects
        || selected.size() > destination.members_.max_size()
        || snapshot.nodes().size() > budget.maxSnapshotNodes)
        return "EDITOR-TRANSFORM-BATCH-BUDGET";
    if (const auto* diagnostic = validateSnapshot(snapshot))
        return diagnostic;
    try {
        EditorSceneTransformBatch candidate(destination.members_.get_allocator().resource());
        candidate.members_.reserve(selected.size());
        bool hasPrimary = false;
        for (const auto id : selected) {
            const auto* node = snapshot.findNode(id);
            if (!id.valid() || !node)
                return "EDITOR-TRANSFORM-BATCH-SELECTION";
            if (std::any_of(
                    candidate.members_.begin(), candidate.members_.end(), [&](const auto& member) {
                        return member.objectId == id;
                    }))
                return "EDITOR-TRANSFORM-BATCH-SELECTION";
            const auto* parent = snapshot.findNode(node->parentId);
            candidate.members_.push_back({id,
                node->parentId,
                node->localTransform,
                node->worldTransform,
                parent ? parent->worldTransform : scene::Transform::identity(),
                true});
            if (id == primary) {
                hasPrimary = true;
                candidate.primaryLocal_ = node->localTransform;
                candidate.primaryWorld_ = node->worldTransform;
            }
        }
        if (!hasPrimary)
            return "EDITOR-TRANSFORM-BATCH-SELECTION";
        std::sort(
            candidate.members_.begin(), candidate.members_.end(), [](const auto& a, const auto& b) {
                return a.objectId.value() < b.objectId.value();
            });
        core::Vec3 minimum{}, maximum{};
        for (auto& member : candidate.members_) {
            auto ancestor = member.parentId;
            while (ancestor.valid()) {
                if (std::find(selected.begin(), selected.end(), ancestor) != selected.end()) {
                    member.effective = false;
                    break;
                }
                ancestor = snapshot.findNode(ancestor)->parentId;
            }
            if (!member.effective)
                continue;
            const auto position = member.world.translation;
            if (candidate.effectiveCount_++ == 0) {
                minimum = maximum = position;
            } else {
                minimum = {std::min(minimum.x, position.x),
                    std::min(minimum.y, position.y),
                    std::min(minimum.z, position.z)};
                maximum = {std::max(maximum.x, position.x),
                    std::max(maximum.y, position.y),
                    std::max(maximum.z, position.z)};
            }
        }
        candidate.origin_ = pivot == Pivot::Center
            ? core::Vec3{float((double(minimum.x) + maximum.x) * 0.5),
                  float((double(minimum.y) + maximum.y) * 0.5),
                  float((double(minimum.z) + maximum.z) * 0.5)}
            : candidate.primaryWorld_.translation;
        candidate.primary_ = primary;
        candidate.pivot_ = pivot;
        destination.members_.swap(candidate.members_);
        destination.primary_ = candidate.primary_;
        destination.primaryLocal_ = candidate.primaryLocal_;
        destination.primaryWorld_ = candidate.primaryWorld_;
        destination.pivot_ = candidate.pivot_;
        destination.origin_ = candidate.origin_;
        destination.effectiveCount_ = candidate.effectiveCount_;
        destination.budget_ = budget;
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-TRANSFORM-BATCH-ALLOCATION";
    }
}

const char* EditorSceneTransformBatch::prepare(Tool tool,
    Space space,
    unsigned axis,
    double amount,
    core::pmr::Vector<Change>& destination) const noexcept
{
    Frame common;
    if (!effectiveCount_ || axis >= 3 || !std::isfinite(amount)
        || std::abs(amount) > std::numeric_limits<float>::max()
        || (tool != Tool::Move && tool != Tool::Rotate && tool != Tool::Scale)
        || !Frame::build(primaryWorld_.rotation, space, common))
        return "EDITOR-TRANSFORM-BATCH-INVALID";
    if (effectiveCount_ > destination.max_size())
        return "EDITOR-TRANSFORM-BATCH-BUDGET";
    double factor = 1;
    if (pivot_ != Pivot::Individual && tool == Tool::Scale && amount != 0) {
        unsigned referenceAxis = axis;
        if (space == Space::World
            && !Frame::alignedAxis(primaryWorld_.rotation, common.axes[axis], referenceAxis))
            return "EDITOR-TRANSFORM-WORLD-SCALE-SHEAR";
        const double reference = component(
            space == Space::Local ? primaryLocal_.scale : primaryWorld_.scale, referenceAxis);
        if (reference == 0)
            return "EDITOR-TRANSFORM-BATCH-SCALE-ZERO-REFERENCE";
        factor = (reference + amount) / reference;
        if (!std::isfinite(factor) || std::abs(factor) > std::numeric_limits<float>::max())
            return "EDITOR-TRANSFORM-BATCH-RANGE";
    }
    try {
        core::pmr::Vector<Change> candidate(destination.get_allocator().resource());
        candidate.reserve(effectiveCount_);
        for (const auto& member : members_) {
            if (!member.effective)
                continue;
            scene::Transform local;
            const char* diagnostic = nullptr;
            if (pivot_ == Pivot::Individual) {
                diagnostic = Frame::prepare(
                    tool, space, axis, amount, member.local, member.parentWorld, local);
            } else {
                double individualAmount = amount;
                if (tool == Tool::Scale && amount != 0) {
                    unsigned aligned = 0;
                    if (!Frame::alignedAxis(member.world.rotation, common.axes[axis], aligned))
                        return "EDITOR-TRANSFORM-WORLD-SCALE-SHEAR";
                    individualAmount =
                        double(component(member.world.scale, aligned)) * (factor - 1);
                }
                diagnostic = Frame::prepareWorld(tool,
                    common.axes[axis],
                    individualAmount,
                    member.local,
                    member.parentWorld,
                    local);
                if (!diagnostic && amount != 0 && (tool == Tool::Rotate || tool == Tool::Scale)) {
                    const auto offset = member.world.translation - origin_;
                    const auto worldPosition = tool == Tool::Rotate ? origin_
                            + core::rotate(core::Quat::fromAxisAngle(
                                               common.axes[axis], static_cast<float>(amount)),
                                offset)
                                                                    : member.world.translation
                            + core::scale(common.axes[axis],
                                static_cast<float>(
                                    (factor - 1) * core::dot(offset, common.axes[axis])));
                    auto world = member.world;
                    world.translation = worldPosition;
                    local.translation =
                        scene::deriveLocalTransform(member.parentWorld, world).translation;
                }
            }
            if (diagnostic)
                return diagnostic;
            scene::Transform canonical;
            if (!Frame::canonicalize(local, canonical))
                return "EDITOR-TRANSFORM-BATCH-RANGE";
            // Zero is an exact pose no-op, including its accepted quaternion.
            candidate.push_back({member.objectId,
                member.parentId,
                member.local,
                amount == 0 ? member.local : canonical,
                member.parentWorld});
        }
        destination.swap(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-TRANSFORM-BATCH-ALLOCATION";
    }
}

bool EditorSceneTransformBatch::matchesStart(const scene::SceneTraversalSnapshot& snapshot,
    scene::GameObjectId primary,
    core::ArrayView<scene::GameObjectId> selected) const noexcept
{
    if (!effectiveCount_ || primary != primary_ || selected.size() != members_.size()
        || !selected.data() || snapshot.nodes().size() > budget_.maxSnapshotNodes
        || validateSnapshot(snapshot))
        return false;
    for (const auto& member : members_) {
        if (std::count(selected.begin(), selected.end(), member.objectId) != 1)
            return false;
        const auto* node = snapshot.findNode(member.objectId);
        if (!node || node->parentId != member.parentId
            || !Frame::same(node->localTransform, member.local)
            || !Frame::same(node->worldTransform, member.world))
            return false;
        const auto* parent = snapshot.findNode(node->parentId);
        if (node->parentId.valid() && !parent)
            return false;
        if (!Frame::same(
                parent ? parent->worldTransform : scene::Transform::identity(), member.parentWorld))
            return false;
    }
    return true;
}
} // namespace arp::editor
