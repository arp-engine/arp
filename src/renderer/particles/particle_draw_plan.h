#pragma once

#include "core/cpu_allocator.h"
#include "renderer/particles/particle_system.h"
#include "renderer/scene/scene.h"

#include <algorithm>
#include <cstdint>

namespace arp::renderer {

struct ParticlePushConstants {
    float viewProjection[16];
    std::uint32_t visualFamily = 0;
    std::uint32_t isTextured = 0;
    std::uint32_t flipbookTilesX = 1;
    std::uint32_t flipbookTilesY = 1;
    std::uint32_t billboardMode = 0; // 0=ViewAligned, 1=LocalAligned, 2=WorldAligned
    std::uint32_t depthFadeEnabled = 0;
    float depthFadeDistance = 0.5f;
    float pad2 = 0;
    // Keep the tail explicitly 16-byte wide. The WebGPU push-constant
    // lowering binds this as a uniform block, whose final member alignment
    // would otherwise make the native minimum binding size exceed this C++
    // payload.
    float pad3[4]{};
};

struct ParticleVertex {
    float worldPosition[3]{};
    float corner[2]{};
    float color[4]{};
    float size = 1.0f;
    float normalizedAge = 0.0f;
    float flipbookFrame = 0.0f;
    float rotation = 0.0f;
};

struct ParticleDrawCommand {
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
};

struct ParticleDrawPlan {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ParticleVertex> vertices{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ParticleDrawCommand> commands{&core::defaultCpuMemoryResource()};
    ParticlePushConstants pushConstants{};
    ParticleVisualFamily visualFamily = ParticleVisualFamily::SpriteAlpha;

    uint64_t pushConstantSizeBytes() const { return sizeof(ParticlePushConstants); }

    uint64_t verticesSizeBytes() const
    {
        return static_cast<uint64_t>(vertices.size() * sizeof(ParticleVertex));
    }
};

inline float particleSortDistanceSq(const ParticleInstance& particle, const SceneConstants& scene)
{
    const auto dx = particle.position.x - scene.cameraPosition[0];
    const auto dy = particle.position.y - scene.cameraPosition[1];
    const auto dz = particle.position.z - scene.cameraPosition[2];
    return dx * dx + dy * dy + dz * dz;
}

inline void appendParticleBillboardVertices(
    core::pmr::Vector<ParticleVertex>& vertices, const ParticleInstance& particle)
{
    const float corners[6][2]{
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f},
    };
    for (const auto& corner : corners) {
        ParticleVertex vertex;
        vertex.worldPosition[0] = particle.position.x;
        vertex.worldPosition[1] = particle.position.y;
        vertex.worldPosition[2] = particle.position.z;
        vertex.corner[0] = corner[0];
        vertex.corner[1] = corner[1];
        vertex.color[0] = particle.color.r;
        vertex.color[1] = particle.color.g;
        vertex.color[2] = particle.color.b;
        vertex.color[3] = particle.color.a;
        vertex.size = particle.size;
        vertex.normalizedAge = particle.normalizedAge();
        vertex.flipbookFrame = static_cast<float>(particle.flipbookFrame);
        vertex.rotation = particle.rotation;
        vertices.push_back(vertex);
    }
}

inline ParticleDrawPlan buildParticleDrawPlan(const core::pmr::Vector<ParticleInstance>& particles,
    const SceneConstants& scene,
    uint32_t maxParticles,
    bool sortBackToFront)
{
    if (particles.empty()) {
        ParticleDrawPlan plan;
        plan.diagnostics = "particle draw data is empty";
        return plan;
    }
    if (particles.size() > maxParticles) {
        ParticleDrawPlan plan;
        plan.diagnostics = "particle count exceeds draw plan budget";
        return plan;
    }

    core::pmr::Vector<const ParticleInstance*> ordered{&core::defaultCpuMemoryResource()};
    ordered.reserve(particles.size());
    for (const auto& particle : particles) {
        ordered.push_back(&particle);
    }
    if (sortBackToFront) {
        std::stable_sort(ordered.begin(),
            ordered.end(),
            [&scene](const ParticleInstance* lhs, const ParticleInstance* rhs) {
                return particleSortDistanceSq(*lhs, scene) > particleSortDistanceSq(*rhs, scene);
            });
    }

    ParticleDrawPlan plan;
    plan.success = true;
    plan.visualFamily = ordered.front()->visualFamily;
    plan.pushConstants.visualFamily = static_cast<std::uint32_t>(plan.visualFamily);
    for (uint32_t index = 0; index < 16; ++index) {
        plan.pushConstants.viewProjection[index] = scene.viewProjection[index];
    }
    plan.pushConstants.isTextured = 0;
    plan.vertices.reserve(particles.size() * 6);
    for (const auto* particle : ordered) {
        appendParticleBillboardVertices(plan.vertices, *particle);
        if (particle->isTextured) {
            plan.pushConstants.isTextured = 1;
        }
    }
    for (const auto* particle : ordered) {
        if (particle->isTextured
            && (particle->flipbookTilesX > 1 || particle->flipbookTilesY > 1)) {
            plan.pushConstants.flipbookTilesX = particle->flipbookTilesX;
            plan.pushConstants.flipbookTilesY = particle->flipbookTilesY;
            break;
        }
    }
    if (!ordered.empty()) {
        plan.pushConstants.billboardMode = ordered.front()->billboardMode;
    }
    if (!ordered.empty() && ordered.front()->depthFadeEnabled) {
        plan.pushConstants.depthFadeEnabled = 1;
        plan.pushConstants.depthFadeDistance = ordered.front()->depthFadeDistance;
    }
    plan.commands.push_back(ParticleDrawCommand{
        static_cast<uint32_t>(plan.vertices.size()),
        1,
        0,
        0,
    });
    return plan;
}

struct ParticleDrawPlanSet {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ParticleDrawPlan> plans{&core::defaultCpuMemoryResource()};
};

// Build draw plans grouped by visual family.
// One plan per family that has particles; preserves back-to-front sorting
// within each family.
inline ParticleDrawPlanSet buildParticleDrawPlanSet(
    const core::pmr::Vector<ParticleInstance>& particles,
    const SceneConstants& scene,
    uint32_t maxParticles,
    bool sortBackToFront)
{
    ParticleDrawPlanSet set;
    if (particles.empty()) {
        set.diagnostics = "particle draw data is empty";
        return set;
    }
    if (particles.size() > maxParticles) {
        set.diagnostics = "particle count exceeds draw plan budget";
        return set;
    }

    // Group particle pointers by visual family
    core::pmr::Vector<const ParticleInstance*>
        byFamily[static_cast<size_t>(ParticleVisualFamily::Count)];
    for (size_t i = 0; i < static_cast<size_t>(ParticleVisualFamily::Count); ++i) {
        byFamily[i].reserve(particles.size());
    }

    for (const auto& particle : particles) {
        const auto index = static_cast<size_t>(particle.visualFamily);
        if (index < static_cast<size_t>(ParticleVisualFamily::Count)) {
            byFamily[index].push_back(&particle);
        }
    }

    for (size_t i = 0; i < static_cast<size_t>(ParticleVisualFamily::Count); ++i) {
        auto& group = byFamily[i];
        if (group.empty()) {
            continue;
        }
        // MeshParticle, Trail, and Beam are handled by separate plan builders.
        const auto family = static_cast<ParticleVisualFamily>(i);
        if (family == ParticleVisualFamily::MeshParticle || family == ParticleVisualFamily::Trail
            || family == ParticleVisualFamily::Beam) {
            continue;
        }
        if (sortBackToFront) {
            std::stable_sort(group.begin(),
                group.end(),
                [&scene](const ParticleInstance* lhs, const ParticleInstance* rhs) {
                    return particleSortDistanceSq(*lhs, scene)
                        > particleSortDistanceSq(*rhs, scene);
                });
        }

        ParticleDrawPlan plan;
        plan.success = true;
        plan.visualFamily = static_cast<ParticleVisualFamily>(i);
        plan.pushConstants.visualFamily = static_cast<std::uint32_t>(plan.visualFamily);
        for (uint32_t idx = 0; idx < 16; ++idx) {
            plan.pushConstants.viewProjection[idx] = scene.viewProjection[idx];
        }
        plan.pushConstants.isTextured = 0;
        plan.vertices.reserve(group.size() * 6);
        for (const auto* particle : group) {
            appendParticleBillboardVertices(plan.vertices, *particle);
            if (particle->isTextured) {
                plan.pushConstants.isTextured = 1;
            }
        }
        // Propagate flipbook tiles from first textured particle
        for (const auto* particle : group) {
            if (particle->isTextured
                && (particle->flipbookTilesX > 1 || particle->flipbookTilesY > 1)) {
                plan.pushConstants.flipbookTilesX = particle->flipbookTilesX;
                plan.pushConstants.flipbookTilesY = particle->flipbookTilesY;
                break;
            }
        }
        // Propagate billboard mode from first particle in group
        if (!group.empty()) {
            plan.pushConstants.billboardMode = group.front()->billboardMode;
        }
        // Propagate depth fade settings from first particle in group
        if (!group.empty() && group.front()->depthFadeEnabled) {
            plan.pushConstants.depthFadeEnabled = 1;
            plan.pushConstants.depthFadeDistance = group.front()->depthFadeDistance;
        }
        plan.commands.push_back(ParticleDrawCommand{
            static_cast<uint32_t>(plan.vertices.size()),
            1,
            0,
            0,
        });
        set.plans.push_back(std::move(plan));
    }

    set.success = !set.plans.empty();
    if (!set.success) {
        set.diagnostics = "no drawable particle families found";
    }
    return set;
}

// ---------------------------------------------------------------------------
// Trail / Beam non-billboard draw structures and plan builders.
//
// Trail: each particle is a point in an ordered chain. Adjacent particles
// form strip segments; the renderer emits quad geometry per segment pair.
//
// Beam: two endpoint particles produce a single oriented quad.
// ---------------------------------------------------------------------------

struct TrailBeamPushConstants {
    float viewProjection[16];
    std::uint32_t visualFamily = 0; // 5=Trail, 6=Beam (ParticleVisualFamily)
    std::uint32_t isTextured = 0;
    float trailWidth = 0.1f;
    float beamLength = 1.0f;
};

struct TrailBeamVertex {
    float worldPosition[3]{};
    float corner[2]{}; // ±1 strip offset for trail; quad corner for beam
    float color[4]{};
    float size = 1.0f;
    float trailIndex = 0.0f; // ordering index within emitter
    float normalizedAge = 0.0f;
};

struct TrailBeamDrawCommand {
    std::uint32_t vertexCount = 0;
    std::uint32_t instanceCount = 1;
    std::uint32_t firstVertex = 0;
    std::uint32_t firstInstance = 0;
};

struct TrailBeamDrawPlan {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TrailBeamVertex> vertices{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TrailBeamDrawCommand> commands{&core::defaultCpuMemoryResource()};
    TrailBeamPushConstants pushConstants{};
    ParticleVisualFamily visualFamily = ParticleVisualFamily::Trail;

    std::uint64_t pushConstantSizeBytes() const { return sizeof(TrailBeamPushConstants); }

    std::uint64_t verticesSizeBytes() const
    {
        return static_cast<std::uint64_t>(vertices.size() * sizeof(TrailBeamVertex));
    }
};

// Emit a trail segment quad between two adjacent trail points.
// Each pair produces 6 vertices (2 triangles) forming a quad along the strip.
inline void appendTrailSegmentVertices(core::pmr::Vector<TrailBeamVertex>& vertices,
    const ParticleInstance& prev,
    const ParticleInstance& next)
{
    // Emit 6 vertices for a quad segment (2 triangles)
    // left/right offset is perpendicular to view in the vertex shader
    const float corners[6][2] = {
        {-1.0f, 0.0f}, // left,  prev side
        {1.0f, 0.0f},  // right, prev side
        {1.0f, 1.0f},  // right, next side
        {-1.0f, 0.0f}, // left,  prev side
        {1.0f, 1.0f},  // right, next side
        {-1.0f, 1.0f}, // left,  next side
    };

    const ParticleInstance* points[2] = {&prev, &next};
    for (int side = 0; side < 2; ++side) {
        for (int tri = 0; tri < 3; ++tri) {
            const int ci = side * 3 + tri;
            const int pointIdx = (corners[ci][1] > 0.5f) ? 1 : 0;
            const auto& pt = *points[pointIdx];

            TrailBeamVertex v;
            v.worldPosition[0] = pt.position.x;
            v.worldPosition[1] = pt.position.y;
            v.worldPosition[2] = pt.position.z;
            v.corner[0] = corners[ci][0];
            v.corner[1] = corners[ci][1];
            v.color[0] = pt.color.r;
            v.color[1] = pt.color.g;
            v.color[2] = pt.color.b;
            v.color[3] = pt.color.a;
            v.size = pt.size;
            v.trailIndex = static_cast<float>(pt.trailIndex);
            v.normalizedAge = pt.normalizedAge();
            vertices.push_back(v);
        }
    }
}

// Emit beam quad vertices from two beam endpoints.
// Produces 6 vertices (2 triangles) forming a quad between start and end.
inline void appendBeamSegmentVertices(core::pmr::Vector<TrailBeamVertex>& vertices,
    const ParticleInstance& start,
    const ParticleInstance& end)
{
    const float corners[6][2] = {
        {-1.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {-1.0f, 0.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f},
    };

    const ParticleInstance* points[2] = {&start, &end};
    for (int i = 0; i < 6; ++i) {
        const int pointIdx = (corners[i][1] > 0.5f) ? 1 : 0;
        const auto& pt = *points[pointIdx];

        TrailBeamVertex v;
        v.worldPosition[0] = pt.position.x;
        v.worldPosition[1] = pt.position.y;
        v.worldPosition[2] = pt.position.z;
        v.corner[0] = corners[i][0];
        v.corner[1] = corners[i][1];
        v.color[0] = pt.color.r;
        v.color[1] = pt.color.g;
        v.color[2] = pt.color.b;
        v.color[3] = pt.color.a;
        v.size = pt.size;
        v.trailIndex = static_cast<float>(pt.trailIndex);
        v.normalizedAge = pt.normalizedAge();
        vertices.push_back(v);
    }
}

// Build a single draw plan for trail particles grouped by emitter.
// Sorts by emitter + trailIndex, then emits segment quads between
// adjacent particles. Beam particles produce a single quad.
inline TrailBeamDrawPlan buildTrailBeamDrawPlan(
    const core::pmr::Vector<ParticleInstance>& particles,
    const SceneConstants& scene,
    ParticleVisualFamily family)
{
    TrailBeamDrawPlan plan;
    plan.visualFamily = family;

    if (particles.empty()) {
        plan.diagnostics = "trail/beam draw data is empty";
        return plan;
    }

    // Only include particles matching the requested family
    core::pmr::Vector<const ParticleInstance*> filtered{&core::defaultCpuMemoryResource()};
    filtered.reserve(particles.size());
    for (const auto& p : particles) {
        if (p.visualFamily == family) {
            filtered.push_back(&p);
        }
    }
    if (filtered.empty()) {
        plan.diagnostics = "no particles for trail/beam family";
        return plan;
    }

    for (std::uint32_t i = 0; i < 16; ++i) {
        plan.pushConstants.viewProjection[i] = scene.viewProjection[i];
    }
    plan.pushConstants.visualFamily = static_cast<std::uint32_t>(family);
    plan.pushConstants.isTextured = 0;

    if (family == ParticleVisualFamily::Beam) {
        // Beam: render quad from first two endpoints
        if (filtered.size() < 2) {
            plan.diagnostics = "beam requires at least 2 endpoint particles";
            return plan;
        }
        const auto* start = filtered[0];
        const auto* end = filtered[1];
        plan.pushConstants.beamLength = std::max(start->beamLength, end->beamLength);
        appendBeamSegmentVertices(plan.vertices, *start, *end);
    } else {
        // Trail: order by emitterIndex + trailIndex, emit segment quads
        std::stable_sort(filtered.begin(),
            filtered.end(),
            [](const ParticleInstance* a, const ParticleInstance* b) {
                if (a->emitterIndex != b->emitterIndex) {
                    return a->emitterIndex < b->emitterIndex;
                }
                return a->trailIndex < b->trailIndex;
            });

        float maxTrailWidth = 0.1f;
        for (const auto* particle : filtered) {
            maxTrailWidth = std::max(maxTrailWidth, particle->trailWidth);
        }
        plan.pushConstants.trailWidth = maxTrailWidth;
        for (size_t i = 1; i < filtered.size(); ++i) {
            // Only connect particles from the same emitter
            if (filtered[i]->emitterIndex == filtered[i - 1]->emitterIndex) {
                appendTrailSegmentVertices(plan.vertices, *filtered[i - 1], *filtered[i]);
            }
        }
    }

    if (plan.vertices.empty()) {
        plan.diagnostics = "trail/beam produced no geometry";
        return plan;
    }

    plan.commands.push_back(TrailBeamDrawCommand{
        static_cast<std::uint32_t>(plan.vertices.size()),
        1,
        0,
        0,
    });
    plan.success = true;
    return plan;
}

// ---------------------------------------------------------------------------
// Mesh Particle draw structures and plan builder.
//
// MeshParticle: instanced 3D mesh rendering (non-sprite). Each particle becomes
// a mesh instance at its world-space position with its rotation and scale.
// The plan stores per-instance transform data and a mesh asset reference.
//
// Status: Prototype Complete — data path and plan builder exist. GPU instanced
// mesh draw path is deferred (renderer needs to consume these plans).
// ---------------------------------------------------------------------------

struct MeshParticleInstanceData {
    float position[3]{};
    float rotation = 0.0f;
    float scale = 1.0f;
    float color[4]{};
    float normalizedAge = 0.0f;
};

struct MeshParticleDrawCommand {
    std::uint32_t instanceCount = 0;
    std::uint32_t firstInstance = 0;
};

struct MeshParticleDrawPlan {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::String meshAssetId{&core::defaultCpuMemoryResource()};
    core::pmr::String meshMaterialId{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<MeshParticleInstanceData> instances{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<MeshParticleDrawCommand> commands{&core::defaultCpuMemoryResource()};

    // Push constants share the same layout as sprite particles for the
    // view-projection matrix, then mesh-specific data follows.
    ParticlePushConstants pushConstants{};
    ParticleVisualFamily visualFamily = ParticleVisualFamily::MeshParticle;

    bool empty() const { return instances.empty(); }
};

inline MeshParticleDrawPlan buildMeshParticleDrawPlan(
    const core::pmr::Vector<const ParticleInstance*>& particles, const SceneConstants& scene)
{
    MeshParticleDrawPlan plan;
    plan.visualFamily = ParticleVisualFamily::MeshParticle;

    if (particles.empty()) {
        plan.diagnostics = "mesh particle draw data is empty";
        return plan;
    }

    plan.meshAssetId = particles.front()->meshAssetId;
    plan.meshMaterialId = particles.front()->meshMaterialId;

    // Copy mesh asset reference from first particle's emitter desc.
    // MeshParticle particles from the same visual family group are assumed
    // to share the same mesh — the builder trusts the caller to group them.
    // (The current `buildParticleDrawPlanSet` groups by visual family,
    //  so all MeshParticle particles arrive together regardless of emitter.)
    for (std::uint32_t i = 0; i < 16; ++i) {
        plan.pushConstants.viewProjection[i] = scene.viewProjection[i];
    }
    plan.pushConstants.visualFamily =
        static_cast<std::uint32_t>(ParticleVisualFamily::MeshParticle);

    plan.instances.reserve(particles.size());
    for (const auto* p : particles) {
        MeshParticleInstanceData inst;
        inst.position[0] = p->position.x;
        inst.position[1] = p->position.y;
        inst.position[2] = p->position.z;
        inst.rotation = p->rotation;
        inst.scale = p->size; // reuse size as mesh scale
        inst.color[0] = p->color.r;
        inst.color[1] = p->color.g;
        inst.color[2] = p->color.b;
        inst.color[3] = p->color.a;
        inst.normalizedAge = p->normalizedAge();
        plan.instances.push_back(inst);
    }

    plan.commands.push_back(MeshParticleDrawCommand{
        static_cast<std::uint32_t>(plan.instances.size()),
        0,
    });
    plan.success = true;
    return plan;
}

} // namespace arp::renderer
