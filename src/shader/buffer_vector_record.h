#pragma once
#include "shader/buffer_declaration_type_layout.h"

namespace arp::shader {
// VectorRecord/1 is a portable, explicit declaration ABI. These checks consume
// existing cold type graphs; they do not issue resource or execution authority.
class BufferVectorRecordContract final {
    using Graph = BufferDeclarationTypeLayouts;

public:
    struct Field {
        Graph::Scalar scalar = Graph::Scalar::None;
    };

    static const char* inspect(const Graph& graph,
        std::size_t root,
        bool uniform,
        bool spirv,
        const Field* fields,
        std::size_t count) noexcept
    {
        if (!fields || !count || count > UINT32_MAX / 16u || root >= graph.roots().size())
            return "SHADER-VECTOR-RECORD-FRAMING";
        const auto& binding = graph.roots()[root];
        if ((binding.storage == Graph::Storage::Uniform) != uniform)
            return "SHADER-VECTOR-RECORD-STORAGE";
        auto type = binding.type;
        if (type >= graph.nodes().size())
            return "SHADER-VECTOR-RECORD-TYPE";
        if (!uniform) {
            if (spirv) {
                const auto& block = graph.nodes()[type];
                if (block.kind != Graph::Kind::Struct || block.memberCount != 1u)
                    return "SHADER-VECTOR-RECORD-BLOCK";
                const auto& member = graph.members()[block.memberBegin];
                if (member.offsetBytes)
                    return "SHADER-VECTOR-RECORD-OFFSET";
                type = member.type;
            }
            if (type >= graph.nodes().size())
                return "SHADER-VECTOR-RECORD-TYPE";
            const auto& array = graph.nodes()[type];
            if (array.kind != Graph::Kind::RuntimeArray || array.strideBytes != count * 16u)
                return "SHADER-VECTOR-RECORD-STRIDE";
            type = array.child;
        }
        if (type >= graph.nodes().size())
            return "SHADER-VECTOR-RECORD-TYPE";
        const auto& record = graph.nodes()[type];
        if (record.kind != Graph::Kind::Struct || record.memberCount != count
            || record.occupiedExtentBytes != count * 16u
            || (!spirv
                && (!record.alignmentKnown || record.alignmentBytes != 16u
                    || !record.targetSizeKnown || record.targetSizeBytes != count * 16u)))
            return "SHADER-VECTOR-RECORD-SIZE";
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& member = graph.members()[record.memberBegin + i];
            if (member.offsetBytes != i * 16u || member.type >= graph.nodes().size())
                return "SHADER-VECTOR-RECORD-OFFSET";
            const auto& vector = graph.nodes()[member.type];
            if (vector.kind != Graph::Kind::Vector || vector.count != 4u
                || vector.occupiedExtentBytes != 16u || vector.child >= graph.nodes().size())
                return "SHADER-VECTOR-RECORD-VECTOR";
            const auto& scalar = graph.nodes()[vector.child];
            if (scalar.kind != Graph::Kind::Scalar || scalar.scalar != fields[i].scalar
                || scalar.scalarBits != 32u || scalar.occupiedExtentBytes != 4u)
                return "SHADER-VECTOR-RECORD-SCALAR";
        }
        return nullptr;
    }
};
} // namespace arp::shader
