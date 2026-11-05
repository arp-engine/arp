#include "3d/scene_3d_spatial_index.h"

#include "core/math/conservative_clip_volume.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <utility>

namespace arp::scene3d {
namespace {
void validate(const Aabb& b)
{
    if (!std::isfinite(b.min.x) || !std::isfinite(b.min.y) || !std::isfinite(b.min.z)
        || !std::isfinite(b.max.x) || !std::isfinite(b.max.y) || !std::isfinite(b.max.z)
        || b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z)
        throw core::DiagnosticError("SCENE3D-SPATIAL-BOUNDS-INVALID");
}

bool same(const Aabb& a, const Aabb& b)
{
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x
        && a.max.y == b.max.y && a.max.z == b.max.z;
}

class ClipQuery final : public BvhQuery {
public:
    ClipQuery(const float* matrix, SceneSpatialTelemetry& telemetry)
        : volume_(matrix), telemetry_(telemetry)
    {}

    bool intersectsAabb(const Aabb& box) const override
    {
        ++telemetry_.nodeTests;
        const float low[]{box.min.x, box.min.y, box.min.z}, high[]{box.max.x, box.max.y, box.max.z};
        return volume_.intersects(low, high);
    }

    bool intersectsLeaf(const BvhLeaf&) const override
    {
        ++telemetry_.candidateLeaves;
        return true; // Both trees have exactly one entry per leaf node.
    }

private:
    core::ConservativeClipVolume volume_;
    SceneSpatialTelemetry& telemetry_;
};
} // namespace

class Scene3DSpatialIndex::Sink final : public BvhQuerySink {
public:
    explicit Sink(Scene3DSpatialIndex& index) : index_(index) {}

    void accept(const BvhLeaf& leaf) override { index_.accept(leaf.handle); }

private:
    Scene3DSpatialIndex& index_;
};

void Scene3DSpatialIndex::reserve(std::size_t count)
{
    if (count >= UINT32_MAX)
        throw core::DiagnosticError("SCENE3D-SPATIAL-CAPACITY");
    entries_.reserve(count);
    free_.reserve(count);
    visible_.reserve(count);
    previous_.reserve(count);
    changed_.reserve(count);
    alwaysExtract_.reserve(count);
    extraction_.reserve(count);
    dynamic_.reserve(count);
}

bool Scene3DSpatialIndex::live(SceneSpatialHandle h) const
{
    return h.slot < entries_.size() && entries_[h.slot].active
        && entries_[h.slot].generation == h.generation;
}

const Scene3DSpatialIndex::Entry& Scene3DSpatialIndex::require(SceneSpatialHandle h) const
{
    if (!live(h))
        throw core::DiagnosticError("SCENE3D-SPATIAL-STALE-HANDLE");
    return entries_[h.slot];
}

Scene3DSpatialIndex::Entry& Scene3DSpatialIndex::require(SceneSpatialHandle h)
{
    return const_cast<Entry&>(std::as_const(*this).require(h));
}

SceneSpatialHandle Scene3DSpatialIndex::insert(
    SceneSpatialOwner owner, const Aabb& bounds, SpatialMobility mobility)
{
    validate(bounds);
    if (!owner.object.valid())
        throw core::DiagnosticError("SCENE3D-SPATIAL-OWNER-INVALID");
    std::uint32_t slot;
    if (!free_.empty()) {
        slot = free_.back();
        free_.pop_back();
    } else {
        if (entries_.size() >= UINT32_MAX)
            throw core::DiagnosticError("SCENE3D-SPATIAL-CAPACITY");
        slot = std::uint32_t(entries_.size());
        entries_.push_back({});
    }
    auto& e = entries_[slot];
    e.owner = owner;
    e.bounds = bounds;
    e.mobility = mobility;
    e.visible = true;
    e.active = true;
    e.stamp = 0;
    if (mobility == SpatialMobility::Static)
        staticDirty_ = true;
    else
        e.proxy = dynamic_.insert({bounds, slot, 0});
    const SceneSpatialHandle h{slot, e.generation};
    visible_.push_back(h);
    if (owner.extractOutsideView)
        alwaysExtract_.push_back(h);
    ++revision_;
    ++telemetry_.objects;
    return h;
}

bool Scene3DSpatialIndex::update(SceneSpatialHandle h, const Aabb& bounds)
{
    validate(bounds);
    auto& e = require(h);
    if (same(e.bounds, bounds))
        return false;
    if (e.mobility == SpatialMobility::Static)
        staticDirty_ = true;
    else {
        dynamic_.update(e.proxy, bounds);
        ++telemetry_.dynamicUpdates;
    }
    e.bounds = bounds;
    ++revision_;
    return true;
}

void Scene3DSpatialIndex::erase(SceneSpatialHandle h)
{
    auto& e = require(h);
    if (e.generation == UINT32_MAX)
        throw core::DiagnosticError("SCENE3D-SPATIAL-GENERATION-EXHAUSTED");
    if (e.mobility == SpatialMobility::Static)
        staticDirty_ = true;
    else
        dynamic_.remove(e.proxy);
    e.active = false;
    if (e.owner.extractOutsideView)
        alwaysExtract_.erase(
            std::remove(alwaysExtract_.begin(), alwaysExtract_.end(), h), alwaysExtract_.end());
    ++e.generation;
    free_.push_back(h.slot);
    ++revision_;
    --telemetry_.objects;
}

void Scene3DSpatialIndex::accept(std::uint32_t slot)
{
    visible_.push_back({slot, entries_[slot].generation});
}

void Scene3DSpatialIndex::query(const std::array<float, 16>* matrix)
{
    changed_.clear();
    telemetry_.nodeTests = telemetry_.candidateLeaves = 0;
    const bool enabled = matrix != nullptr;
    if (queriedRevision_ == revision_ && enabled == enabled_
        && (!enabled || std::memcmp(camera_.data(), matrix->data(), sizeof(camera_)) == 0)) {
        ++telemetry_.cacheHits;
        return;
    }
    // Validate before touching published visibility state.
    if (enabled)
        (void)core::ConservativeClipVolume(matrix->data());
    if (staticDirty_) {
        static_.clear();
        for (std::uint32_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].active && entries_[i].mobility == SpatialMobility::Static)
                static_.insert(entries_[i].bounds, i, 0);
        static_.build();
        staticDirty_ = false;
        ++telemetry_.staticBuilds;
    }
    previous_.clear();
    previous_.swap(visible_);
    ++stamp_;
    try {
        if (enabled) {
            ClipQuery query(matrix->data(), telemetry_);
            Sink sink(*this);
            static_.query(query, sink);
            dynamic_.query(query, sink);
        } else
            for (std::uint32_t i = 0; i < entries_.size(); ++i)
                if (entries_[i].active)
                    accept(i);
    } catch (...) {
        visible_.clear();
        visible_.swap(previous_);
        throw;
    }
    for (const auto h : visible_) {
        auto& e = entries_[h.slot];
        if (!e.visible)
            changed_.push_back(h);
        e.visible = true;
        e.stamp = stamp_;
    }
    for (const auto h : previous_)
        if (live(h) && entries_[h.slot].stamp != stamp_) {
            entries_[h.slot].visible = false;
            changed_.push_back(h);
        }
    // Stable source identities, independent of tree shape or rotations.
    const auto sourceOrder = [&](auto a, auto b) {
        const auto x = entries_[a.slot].owner, y = entries_[b.slot].owner;
        return std::tuple{x.object.value(), x.component.value()}
        < std::tuple{y.object.value(), y.component.value()};
    };
    std::sort(visible_.begin(), visible_.end(), sourceOrder);
    extraction_.assign(visible_.begin(), visible_.end());
    for (const auto h : alwaysExtract_)
        if (!entries_[h.slot].visible)
            extraction_.push_back(h);
    std::sort(extraction_.begin(), extraction_.end(), sourceOrder);
    if (enabled)
        camera_ = *matrix;
    enabled_ = enabled;
    queriedRevision_ = revision_;
    telemetry_.visible = visible_.size();
    ++telemetry_.queries;
}

bool Scene3DSpatialIndex::visible(SceneSpatialHandle h) const
{
    return require(h).visible;
}

SceneSpatialOwner Scene3DSpatialIndex::owner(SceneSpatialHandle h) const
{
    return require(h).owner;
}

void Scene3DSpatialIndex::extractVisible(
    const scene::Scene& scene, scene::RenderExtractSink& sink) const
{
    extractVisible(scene, sink, {});
}

void Scene3DSpatialIndex::extractVisible(const scene::Scene& scene,
    scene::RenderExtractSink& sink,
    core::ArrayView<SceneSpatialOwner> unbounded) const
{
    if (queriedRevision_ != revision_)
        throw core::DiagnosticError("SCENE3D-SPATIAL-QUERY-STALE");
    const auto key = [](const SceneSpatialOwner& owner) {
        return std::tuple{owner.object.value(), owner.component.value()};
    };
    for (std::size_t i = 1; i < unbounded.size(); ++i)
        if (!(key(unbounded[i - 1]) < key(unbounded[i])))
            throw core::DiagnosticError("SCENE3D-SPATIAL-UNBOUNDED-ORDER");
    const auto dispatch = [&](const SceneSpatialOwner& identity) {
        const auto* object = scene.findObject(identity.object);
        const auto* component = object ? object->findComponent(identity.component) : nullptr;
        if (!component)
            throw core::DiagnosticError("SCENE3D-SPATIAL-COMPONENT-RETIRED");
        if (!object->activeInHierarchy() || !component->enabled() || component->pendingDestroy())
            return;
        const auto world = object->world_transform();
        component->extract({object, &world, true}, sink);
    };
    std::size_t next = 0;
    for (const auto handle : extraction_) {
        const auto identity = require(handle).owner;
        while (next < unbounded.size() && key(unbounded[next]) < key(identity))
            dispatch(unbounded[next++]);
        if (next < unbounded.size() && key(unbounded[next]) == key(identity))
            throw core::DiagnosticError("SCENE3D-SPATIAL-DUPLICATE-UNBOUNDED");
        dispatch(identity);
    }
    while (next < unbounded.size())
        dispatch(unbounded[next++]);
}
} // namespace arp::scene3d
