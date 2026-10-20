#pragma once
#include "shader/fragment_storage_read_proof.h"
#include "shader/graphics_triangle_storage_wgsl_proof.h"
#include "shader/vertex_index_triangle_proof.h"

namespace arp::shader {
class GraphicsTriangleStorageCrossTargetProof final {
public:
    struct Facts {
        VertexIndexTriangleProof::Facts vertex;
        FragmentStorageReadProof::Facts fragment;
        GraphicsTriangleStorageWgslProof::Facts target;
    };

    static const char* verify(const void* vertex,
        std::size_t vertexBytes,
        const void* fragment,
        std::size_t fragmentBytes,
        core::StringView wgsl,
        core::StringView vertexEntry,
        core::StringView fragmentEntry,
        Facts& output) noexcept
    {
        output = {};
        Facts facts;
        if (const auto* id =
                VertexIndexTriangleProof::verify(vertex, vertexBytes, vertexEntry, facts.vertex))
            return id;
        if (const auto* id = FragmentStorageReadProof::verify(
                fragment, fragmentBytes, fragmentEntry, facts.fragment))
            return id;
        if (const auto* id = GraphicsTriangleStorageWgslProof::verify(
                wgsl, vertexEntry, fragmentEntry, facts.target))
            return id;
        if (facts.vertex.positions != facts.target.positions
            || facts.fragment.replacement != facts.target.comparison
            || facts.fragment.byteOffset != facts.target.byteOffset
            || facts.fragment.byteCount != facts.target.byteCount)
            return "SHADER-GRAPHICS-TRIANGLE-TARGET-MISMATCH";
        output = facts;
        return nullptr;
    }

    // Explicit equivalence precondition for VertexIndex-BaseVertex vs WGSL
    // vertex_index. Topology/vertex-stream gates belong to the lowering caller.
    static const char* validateDraw(std::uint32_t vertices,
        std::uint32_t instances,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance,
        bool indexed = false) noexcept
    {
        return !indexed && vertices == 3u && instances == 1u && !firstVertex && !firstInstance
            ? nullptr
            : "SHADER-GRAPHICS-TRIANGLE-DRAW-DOMAIN";
    }
};
} // namespace arp::shader
