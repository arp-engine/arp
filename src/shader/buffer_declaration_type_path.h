#pragma once
#include "shader/buffer_declaration_type_layout.h"

namespace arp::shader {
// A path names authored declaration structure, never parser or driver node IDs.
// Selection performs no search, inference, allocation or mutation.
class BufferDeclarationTypePath final {
public:
    using Graph = BufferDeclarationTypeLayouts;
    enum class Kind : std::uint32_t { Member = 1u, ArrayElement };

    struct Step {
        Kind kind = Kind::Member;
        std::size_t member = 0u;
    };

    struct Input {
        const Step* steps = nullptr;
        std::size_t count = 0u;
    };

    static const char* resolve(const Graph& graph,
        std::size_t root,
        Input path,
        std::size_t maxSteps,
        std::size_t& output) noexcept
    {
        output = Graph::absent;
        if (path.count > maxSteps || (path.count && !path.steps))
            return "SHADER-BUFFER-TYPE-PATH-BUDGET";
        auto type = root;
        for (std::size_t i = 0u; i < path.count; ++i) {
            if (type >= graph.nodes().size())
                return "SHADER-BUFFER-TYPE-PATH-ROOT";
            const auto& node = graph.nodes()[type];
            const auto& step = path.steps[i];
            if (step.kind == Kind::Member) {
                if (node.kind != Graph::Kind::Struct || step.member >= node.memberCount
                    || node.memberBegin > graph.members().size()
                    || node.memberCount > graph.members().size() - node.memberBegin)
                    return "SHADER-BUFFER-TYPE-PATH-MEMBER";
                type = graph.members()[node.memberBegin + step.member].type;
            } else if (step.kind == Kind::ArrayElement) {
                if (step.member
                    || (node.kind != Graph::Kind::FixedArray
                        && node.kind != Graph::Kind::RuntimeArray))
                    return "SHADER-BUFFER-TYPE-PATH-ARRAY";
                type = node.child;
            } else
                return "SHADER-BUFFER-TYPE-PATH-KIND";
        }
        if (type >= graph.nodes().size())
            return "SHADER-BUFFER-TYPE-PATH-ROOT";
        output = type;
        return nullptr;
    }
};
} // namespace arp::shader
