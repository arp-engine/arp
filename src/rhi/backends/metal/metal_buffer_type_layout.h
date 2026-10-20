#pragma once
#include "shader/spirv_msl_buffer_memory_lowering.h"

#import <Metal/Metal.h>

namespace arp::rhi_metal {
// Cold independent native reflection. The caller owns the queried function and
// reflection throughout collection. No names, total-size inference or authority.
class MetalRhiBufferTypeLayoutCollector final {
public:
    using Graph = shader::BufferDeclarationTypeLayouts;

    static const char* collect(
        MTLStructType* root, Graph::Budget budget, Graph& output, std::size_t& type)
    {
        return collectKey({MTLDataTypeStruct, root}, budget, output, type);
    }

    static const char* collect(MTLPointerType* pointer,
        Graph::Budget budget,
        Graph& output,
        std::size_t& type,
        shader::SpirvMslBufferMemoryLowering::TargetElement& element)
    {
        output.clear();
        type = Graph::absent;
        element = {};
        if (!pointer || pointer.elementIsArgumentBuffer || !pointer.dataSize || !pointer.alignment
            || (pointer.alignment & (pointer.alignment - 1u))
            || pointer.dataSize % pointer.alignment)
            return "RHI-METAL-BUFFER-TYPE-POINTER";
        if (const auto* id = collectKey(
                {pointer.elementType, pointer.elementStructType, pointer.elementArrayType},
                budget,
                output,
                type))
            return id;
        if (!output.nodes()[type].occupiedExtentBytes
            || output.nodes()[type].occupiedExtentBytes > pointer.dataSize) {
            output.clear();
            type = Graph::absent;
            return "RHI-METAL-BUFFER-TYPE-POINTER-EXTENT";
        }
        element = {pointer.dataSize, pointer.alignment};
        return nullptr;
    }

private:
    struct Rejection {
        const char* id;
    };

    struct Key {
        MTLDataType type;
        MTLStructType* structure = nil;
        MTLArrayType* array = nil;

        bool matches(const Key& other) const noexcept
        {
            return type == other.type && structure == other.structure && array == other.array;
        }
    };

    static const char* collectKey(Key root, Graph::Budget budget, Graph& output, std::size_t& type)
    {
        type = Graph::absent;
        output.clear();
        try {
            MetalRhiBufferTypeLayoutCollector collector(output, budget);
            collector.run(root, type);
            return nullptr;
        } catch (const Rejection& rejected) {
            output.clear();
            type = Graph::absent;
            return rejected.id;
        } catch (const std::bad_alloc&) {
            output.clear();
            type = Graph::absent;
            return "RHI-METAL-BUFFER-TYPE-ALLOCATION";
        }
    }

    struct Pending {
        Key key;
        std::uint32_t state = 0u;
        std::size_t type = Graph::absent;
    };

    struct Frame {
        std::size_t pending, nextChild = 0u;
    };

    struct Numeric {
        Graph::Scalar scalar = Graph::Scalar::None;
        std::uint32_t bits = 0u, rows = 0u, columns = 0u;
    };

    static void require(bool valid, const char* id)
    {
        if (!valid)
            throw Rejection{id};
    }

    static std::uint64_t add(std::uint64_t a, std::uint64_t b)
    {
        require(b <= UINT64_MAX - a, "RHI-METAL-BUFFER-TYPE-OVERFLOW");
        return a + b;
    }

    static std::uint64_t multiply(std::uint64_t a, std::uint64_t b)
    {
        require(core::canMultiplyUnsigned(a, b), "RHI-METAL-BUFFER-TYPE-OVERFLOW");
        return a * b;
    }

    // The explicit MSL scalar/vector/matrix ABI; struct offsets and array strides
    // are always queried. Packed/unknown/pointer/atomic/resource types refuse.
    static Numeric numeric(MTLDataType type)
    {
        switch (type) {
#define ARP_METAL_NUMERIC(name, scalar, bits)                                                      \
    case MTLDataType##name:                                                                        \
        return {Graph::Scalar::scalar, bits, 1u, 1u};                                              \
    case MTLDataType##name##2:                                                                     \
        return {Graph::Scalar::scalar, bits, 2u, 1u};                                              \
    case MTLDataType##name##3:                                                                     \
        return {Graph::Scalar::scalar, bits, 3u, 1u};                                              \
    case MTLDataType##name##4:                                                                     \
        return {Graph::Scalar::scalar, bits, 4u, 1u}
            ARP_METAL_NUMERIC(Float, FloatingPoint, 32u);
            ARP_METAL_NUMERIC(Half, FloatingPoint, 16u);
            ARP_METAL_NUMERIC(Int, SignedInteger, 32u);
            ARP_METAL_NUMERIC(UInt, UnsignedInteger, 32u);
#undef ARP_METAL_NUMERIC
#define ARP_METAL_MATRIX(name, bits, columns, rows)                                                \
    case MTLDataType##name##columns##x##rows:                                                      \
        return {Graph::Scalar::FloatingPoint, bits, rows, columns}
            ARP_METAL_MATRIX(Float, 32u, 2, 2);
            ARP_METAL_MATRIX(Float, 32u, 2, 3);
            ARP_METAL_MATRIX(Float, 32u, 2, 4);
            ARP_METAL_MATRIX(Float, 32u, 3, 2);
            ARP_METAL_MATRIX(Float, 32u, 3, 3);
            ARP_METAL_MATRIX(Float, 32u, 3, 4);
            ARP_METAL_MATRIX(Float, 32u, 4, 2);
            ARP_METAL_MATRIX(Float, 32u, 4, 3);
            ARP_METAL_MATRIX(Float, 32u, 4, 4);
            ARP_METAL_MATRIX(Half, 16u, 2, 2);
            ARP_METAL_MATRIX(Half, 16u, 2, 3);
            ARP_METAL_MATRIX(Half, 16u, 2, 4);
            ARP_METAL_MATRIX(Half, 16u, 3, 2);
            ARP_METAL_MATRIX(Half, 16u, 3, 3);
            ARP_METAL_MATRIX(Half, 16u, 3, 4);
            ARP_METAL_MATRIX(Half, 16u, 4, 2);
            ARP_METAL_MATRIX(Half, 16u, 4, 3);
            ARP_METAL_MATRIX(Half, 16u, 4, 4);
#undef ARP_METAL_MATRIX
        default:
            return {};
        }
    }

    explicit MetalRhiBufferTypeLayoutCollector(Graph& graph, Graph::Budget budget) : graph_(graph)
    {
        graph_.begin(budget);
    }

    std::size_t pending(Key key)
    {
        for (std::size_t i = 0u; i < pending_.size(); ++i)
            if (pending_[i].key.matches(key))
                return i;
        require(pending_.size() < graph_.budget().maxNodes, "RHI-METAL-BUFFER-TYPE-NODE-BUDGET");
        pending_.push_back({key});
        return pending_.size() - 1u;
    }

    std::size_t append(
        Graph::Node node, const Graph::Member* members = nullptr, std::size_t count = 0u)
    {
        std::size_t type;
        if (const auto* id = graph_.append(node, members, count, type))
            throw Rejection{id};
        return type;
    }

    static Key memberKey(MTLStructMember* member)
    {
        return {member.dataType, member.structType, member.arrayType};
    }

    static Key elementKey(MTLArrayType* array)
    {
        return {array.elementType, array.elementStructType, array.elementArrayType};
    }

    std::size_t complete(Key key)
    {
        const auto index = pending(key);
        require(pending_[index].state == 2u, "RHI-METAL-BUFFER-TYPE-DEPENDENCY");
        return pending_[index].type;
    }

    std::size_t emitNumeric(Numeric value)
    {
        require(value.bits != 0u, "RHI-METAL-BUFFER-TYPE-DOMAIN-UNSUPPORTED");
        Graph::Node node;
        node.scalar = value.scalar;
        node.scalarBits = value.bits;
        node.occupiedExtentBytes = value.bits / 8u;
        auto child = append(node);
        if (value.rows == 1u)
            return child;
        node = {};
        node.kind = Graph::Kind::Vector;
        node.child = child;
        node.count = value.rows;
        node.occupiedExtentBytes = multiply(value.rows, value.bits / 8u);
        child = append(node);
        if (value.columns == 1u)
            return child;
        node = {};
        node.kind = Graph::Kind::Matrix;
        node.child = child;
        node.rows = value.rows;
        node.columns = value.columns;
        node.matrixEncoding = Graph::MatrixEncoding::MslColumnVectors;
        node.strideBytes = multiply(value.rows == 2u ? 2u : 4u, value.bits / 8u);
        node.occupiedExtentBytes = add(multiply(value.columns - 1u, node.strideBytes),
            graph_.nodes()[child].occupiedExtentBytes);
        return append(node);
    }

    std::size_t emit(Key key)
    {
        if (key.type == MTLDataTypeStruct) {
            require(key.structure && key.structure.members.count,
                "RHI-METAL-BUFFER-TYPE-STRUCT-MISSING");
            Graph::Node node;
            node.kind = Graph::Kind::Struct;
            core::pmr::Vector<Graph::Member> members{&core::defaultCpuMemoryResource()};
            for (MTLStructMember* member in key.structure.members) {
                require(members.size() < graph_.budget().maxMembers,
                    "RHI-METAL-BUFFER-TYPE-MEMBER-BUDGET");
                const auto type = complete(memberKey(member));
                const auto extent = graph_.nodes()[type].occupiedExtentBytes;
                // Standard nonoverlapping MSL value members, with actual offsets.
                require(member.offset >= node.occupiedExtentBytes,
                    "RHI-METAL-BUFFER-TYPE-MEMBER-OVERLAP");
                node.occupiedExtentBytes = add(member.offset, extent);
                members.push_back({type, member.offset});
            }
            return append(node, members.data(), members.size());
        }
        if (key.type == MTLDataTypeArray) {
            require(key.array && key.array.arrayLength && key.array.stride,
                "RHI-METAL-BUFFER-TYPE-ARRAY-MISSING");
            Graph::Node node;
            node.kind = Graph::Kind::FixedArray;
            node.child = complete(elementKey(key.array));
            node.count = key.array.arrayLength;
            node.strideBytes = key.array.stride;
            const auto extent = graph_.nodes()[node.child].occupiedExtentBytes;
            require(node.strideBytes >= extent, "RHI-METAL-BUFFER-TYPE-ARRAY-STRIDE");
            node.occupiedExtentBytes = add(multiply(node.count - 1u, node.strideBytes), extent);
            return append(node);
        }
        return emitNumeric(numeric(key.type));
    }

    void run(Key root, std::size_t& output)
    {
        require(root.type != MTLDataTypeStruct || root.structure != nil,
            "RHI-METAL-BUFFER-TYPE-ROOT-MISSING");
        work_.push_back({pending(root)});
        while (!work_.empty()) {
            const auto index = work_.back().pending;
            if (pending_[index].state == 2u) {
                work_.pop_back();
                continue;
            }
            pending_[index].state = 1u;
            const auto key = pending_[index].key;
            const auto next = work_.back().nextChild++;
            Key child{MTLDataTypeNone};
            bool hasChild = false;
            if (key.type == MTLDataTypeStruct && key.structure
                && next < key.structure.members.count) {
                require(key.structure.members.count <= graph_.budget().maxMembers,
                    "RHI-METAL-BUFFER-TYPE-MEMBER-BUDGET");
                child = memberKey(key.structure.members[next]);
                hasChild = true;
            } else if (key.type == MTLDataTypeArray && key.array && next == 0u) {
                child = elementKey(key.array);
                hasChild = true;
            }
            if (hasChild) {
                const auto dependency = pending(child);
                require(pending_[dependency].state != 1u, "RHI-METAL-BUFFER-TYPE-CYCLIC");
                if (pending_[dependency].state == 0u)
                    work_.push_back({dependency});
                continue;
            }
            pending_[index].type = emit(key);
            pending_[index].state = 2u;
            work_.pop_back();
        }
        output = pending_.front().type;
    }

    Graph& graph_;
    core::pmr::Vector<Pending> pending_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Frame> work_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::rhi_metal
