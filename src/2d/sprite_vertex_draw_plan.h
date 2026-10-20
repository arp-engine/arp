#pragma once

#include "2d/sprite_draw_plan.h"
#include "core/diagnostic_error.h"
#include "core/quad_geometry.h"

#include <limits>

namespace arp::scene2d {

struct SpriteVertex {
    core::Vec2 position, uv;
    core::Vec4 color;
};

static_assert(sizeof(SpriteVertex) == 32);

struct SpriteVertexBatch {
    resource::ResourceHandle<SpriteAtlasResource> atlas;
    std::uint32_t firstVertex = 0, vertexCount = 0;
};

struct SpriteVertexDrawPlanDesc {
    std::uint32_t contractVersion = 0;
    std::uint32_t maxSprites = 0;
    std::uint64_t maxVertexBytes = 0;
};

// Explicit caller-selected vertex path. It consumes the original sorted plan;
// it does not reorder, merge batches or silently replace an instanced profile.
class SpriteVertexDrawPlan final {
public:
    explicit SpriteVertexDrawPlan(const SpriteVertexDrawPlanDesc& desc) : desc_(desc)
    {
        if (desc.contractVersion != 1)
            throw core::DiagnosticError("ARP-2D-VERTEX-PLAN-VERSION");
        constexpr auto verticesPerSprite = core::QuadTriangleTopology::vertexCount;
        const auto vertices = std::uint64_t{desc.maxSprites} * verticesPerSprite;
        if (!desc.maxSprites || vertices > UINT32_MAX
            || vertices > std::numeric_limits<std::size_t>::max() / sizeof(SpriteVertex)
            || vertices * sizeof(SpriteVertex) > desc.maxVertexBytes)
            throw core::DiagnosticError("ARP-2D-VERTEX-PLAN-BUDGET");
        vertices_.reserve(static_cast<std::size_t>(vertices));
        batches_.reserve(desc.maxSprites);
    }

    bool build(const SpriteDrawPlan& source)
    {
        vertices_.clear();
        batches_.clear();
        diagnostic_ = nullptr;
        if (source.diagnostic())
            return fail("ARP-2D-VERTEX-SOURCE-INVALID");
        if (source.instances().size() > desc_.maxSprites)
            return fail("ARP-2D-VERTEX-PLAN-CAPACITY");
        std::size_t next = 0;
        for (const auto& batch : source.batches()) {
            if (!batch.atlas.ready())
                return fail("ARP-2D-VERTEX-ATLAS-STALE");
            if (batch.first != next || !batch.count
                || batch.count > source.instances().size() - next)
                return fail("ARP-2D-VERTEX-BATCH-COVERAGE");
            next += batch.count;
        }
        if (next != source.instances().size())
            return fail("ARP-2D-VERTEX-BATCH-COVERAGE");
        for (const auto& batch : source.batches()) {
            batches_.push_back({batch.atlas,
                static_cast<std::uint32_t>(vertices_.size()),
                batch.count * core::QuadTriangleTopology::vertexCount});
            for (std::size_t i = batch.first; i < std::size_t{batch.first} + batch.count; ++i) {
                const auto& instance = source.instances()[i];
                for (const auto corner : core::QuadTriangleTopology::corners) {
                    SpriteVertex vertex;
                    vertex.position = core::QuadTriangleTopology::affinePoint(
                        {instance.origin.x, instance.origin.y},
                        {instance.axisX.x, instance.axisX.y},
                        {instance.axisY.x, instance.axisY.y},
                        corner);
                    const core::Vec2 textureCorner{
                        instance.parameters.x > 0 ? 1 - corner.x : corner.x,
                        instance.parameters.y > 0 ? corner.y : 1 - corner.y};
                    vertex.uv =
                        core::QuadTriangleTopology::affinePoint({instance.uv.x, instance.uv.y},
                            {instance.uv.z, 0},
                            {0, instance.uv.w},
                            textureCorner);
                    vertex.color = instance.color;
                    if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y)
                        || !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y))
                        return fail("ARP-2D-VERTEX-NONFINITE");
                    vertices_.push_back(vertex);
                }
            }
        }
        return true;
    }

    const auto& vertices() const noexcept { return vertices_; }

    const auto& batches() const noexcept { return batches_; }

    const char* diagnostic() const noexcept { return diagnostic_; }

private:
    bool fail(const char* diagnostic) noexcept
    {
        vertices_.clear();
        batches_.clear();
        diagnostic_ = diagnostic;
        return false;
    }

    const SpriteVertexDrawPlanDesc desc_;
    core::pmr::Vector<SpriteVertex> vertices_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SpriteVertexBatch> batches_{&core::defaultCpuMemoryResource()};
    const char* diagnostic_ = nullptr;
};
} // namespace arp::scene2d
