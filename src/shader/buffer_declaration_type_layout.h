#pragma once
#include "core/cpu_allocator.h"
#include "core/unsigned_range.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace arp::shader {
// A bounded cold declaration DAG. This is data, never an original proof issuer.
class BufferDeclarationTypeLayouts final {
public:
    BufferDeclarationTypeLayouts() = default;
    BufferDeclarationTypeLayouts(const BufferDeclarationTypeLayouts&) = delete;
    BufferDeclarationTypeLayouts& operator=(const BufferDeclarationTypeLayouts&) = delete;
    BufferDeclarationTypeLayouts(BufferDeclarationTypeLayouts&&) noexcept = default;
    BufferDeclarationTypeLayouts& operator=(BufferDeclarationTypeLayouts&&) = default;
    static constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();
    enum class Kind : std::uint32_t {
        Scalar = 1,
        Vector,
        Matrix,
        FixedArray,
        RuntimeArray,
        Struct,
        Atomic,
        DescriptorArray
    };
    enum class Scalar : std::uint32_t { None, UnsignedInteger, SignedInteger, FloatingPoint };
    enum class MatrixEncoding : std::uint32_t {
        None,
        SpirvColMajorDecoration,
        WgslColumnVectors,
        MslColumnVectors
    };
    enum class Storage : std::uint32_t {
        Uniform = 1,
        StorageAccessUnspecified,
        StorageRead,
        StorageReadWrite,
        PushConstant
    };
    enum class Comparison : std::uint32_t { Exact, MemoryRepresentation };

    struct Budget {
        std::size_t maxNodes = 0u, maxMembers = 0u, maxRoots = 0u;
    };

    struct Member {
        std::size_t type = absent;
        std::uint64_t offsetBytes = 0u;
        bool alignmentKnown = false, reservedSizeKnown = false;
        std::uint64_t alignmentBytes = 0u, reservedSizeBytes = 0u;
    };

    struct Node {
        Kind kind = Kind::Scalar;
        Scalar scalar = Scalar::None;
        MatrixEncoding matrixEncoding = MatrixEncoding::None;
        std::uint32_t scalarBits = 0u, rows = 0u, columns = 0u;
        std::size_t child = absent, memberBegin = 0u, memberCount = 0u;
        std::uint64_t count = 0u, strideBytes = 0u, occupiedExtentBytes = 0u;
        bool alignmentKnown = false, targetSizeKnown = false;
        std::uint64_t alignmentBytes = 0u, targetSizeBytes = 0u;
    };

    struct Root {
        std::uint32_t set = 0u, binding = 0u;
        Storage storage = Storage::Uniform;
        std::size_t type = absent;
        std::uint64_t descriptorCount = 0u;
        std::size_t descriptorType = absent;
    };

    void clear() noexcept
    {
        nodes_.clear();
        members_.clear();
        roots_.clear();
        budget_ = {};
    }

    void begin(Budget budget) noexcept
    {
        clear();
        budget_ = budget;
    }

    const auto& nodes() const noexcept { return nodes_; }

    const auto& members() const noexcept { return members_; }

    const auto& roots() const noexcept { return roots_; }

    const Budget& budget() const noexcept { return budget_; }

    // Append only topologically complete nodes, with an ordered borrowed member
    // span. Parser-owned semantic checks precede this framing/budget check.
    const char* append(Node node, const Member* members, std::size_t count, std::size_t& output)
    {
        output = absent;
        if (nodes_.size() >= budget_.maxNodes)
            return "SHADER-BUFFER-TYPE-NODE-BUDGET";
        if (members_.size() > budget_.maxMembers || count > budget_.maxMembers - members_.size())
            return "SHADER-BUFFER-TYPE-MEMBER-BUDGET";
        if ((count && !members) || (node.kind == Kind::Struct) != bool(count)
            || (node.kind == Kind::Scalar || node.kind == Kind::Struct
                    ? node.child != absent
                    : node.child >= nodes_.size())
            || (!node.alignmentKnown && node.alignmentBytes)
            || (node.alignmentKnown && !node.alignmentBytes)
            || (!node.targetSizeKnown && node.targetSizeBytes))
            return "SHADER-BUFFER-TYPE-FRAMING";
        for (std::size_t i = 0u; i < count; ++i)
            if (members[i].type >= nodes_.size()
                || (!members[i].alignmentKnown && members[i].alignmentBytes)
                || (members[i].alignmentKnown && !members[i].alignmentBytes)
                || (!members[i].reservedSizeKnown && members[i].reservedSizeBytes))
                return "SHADER-BUFFER-TYPE-MEMBER-FRAMING";
        node.memberBegin = members_.size();
        node.memberCount = count;
        for (std::size_t i = 0u; i < count; ++i)
            members_.push_back(members[i]);
        nodes_.push_back(node);
        output = nodes_.size() - 1u;
        return nullptr;
    }

    const char* appendRoot(Root root)
    {
        if (roots_.size() >= budget_.maxRoots)
            return "SHADER-BUFFER-TYPE-ROOT-BUDGET";
        const bool push = root.storage == Storage::PushConstant;
        if (root.type >= nodes_.size()
            || (push ? root.descriptorCount || root.set || root.binding
                        || root.descriptorType != absent
                     : !root.descriptorCount)
            || (root.descriptorType != absent && root.descriptorType >= nodes_.size())
            || (!push && root.descriptorCount != 1u && root.descriptorType == absent))
            return "SHADER-BUFFER-TYPE-ROOT-FRAMING";
        for (const auto& previous : roots_)
            if ((previous.storage == Storage::PushConstant) == push && previous.set == root.set
                && previous.binding == root.binding)
                return "SHADER-BUFFER-TYPE-ROOT-DUPLICATE";
        roots_.push_back(root);
        return nullptr;
    }

    // Exact cold comparison. Independent of node IDs, names and sharing.
    // Cross-target compatibility must use a separately approved lowering.
    static const char* compare(const BufferDeclarationTypeLayouts& lhs,
        std::size_t left,
        const BufferDeclarationTypeLayouts& rhs,
        std::size_t right,
        std::size_t maxPairs,
        bool& equal,
        Comparison policy = Comparison::Exact)
    {
        if (policy != Comparison::Exact && policy != Comparison::MemoryRepresentation) {
            equal = false;
            return "SHADER-BUFFER-TYPE-COMPARE-POLICY";
        }
        std::size_t count = 0u;
        return visitPairs(
            lhs,
            left,
            rhs,
            right,
            maxPairs,
            [&](const Node& a, const Node& b, bool& matches) -> const char* {
                matches = a.kind == b.kind && a.scalar == b.scalar
                    && a.matrixEncoding == b.matrixEncoding && a.scalarBits == b.scalarBits
                    && a.rows == b.rows && a.columns == b.columns && a.count == b.count
                    && a.strideBytes == b.strideBytes
                    && a.occupiedExtentBytes == b.occupiedExtentBytes
                    && (policy == Comparison::MemoryRepresentation
                        || (a.alignmentKnown == b.alignmentKnown
                            && a.targetSizeKnown == b.targetSizeKnown
                            && a.alignmentBytes == b.alignmentBytes
                            && a.targetSizeBytes == b.targetSizeBytes))
                    && a.memberCount == b.memberCount && (a.child == absent) == (b.child == absent);
                for (std::size_t i = 0u; matches && i < a.memberCount; ++i) {
                    const auto& m = lhs.members_[a.memberBegin + i];
                    const auto& n = rhs.members_[b.memberBegin + i];
                    matches = m.offsetBytes == n.offsetBytes
                        && (policy == Comparison::MemoryRepresentation
                            || (m.alignmentKnown == n.alignmentKnown
                                && m.reservedSizeKnown == n.reservedSizeKnown
                                && m.alignmentBytes == n.alignmentBytes
                                && m.reservedSizeBytes == n.reservedSizeBytes));
                }
                return nullptr;
            },
            equal,
            count);
    }

    // Shared bounded traversal for exact comparison and approved relations.
    // A visitor compares node/member facts; this owns framing, topology and
    // pair deduplication. A relation can supply an explicit pair resolver;
    // exact comparison always uses identity resolution.
    struct IdentityPairResolver {
        const char* operator()(std::size_t&, std::size_t&) const noexcept { return nullptr; }
    };

    template <class Visitor, class Resolver = IdentityPairResolver>
    static const char* visitPairs(const BufferDeclarationTypeLayouts& lhs,
        std::size_t left,
        const BufferDeclarationTypeLayouts& rhs,
        std::size_t right,
        std::size_t maxPairs,
        Visitor&& visitor,
        bool& equal,
        std::size_t& comparedPairs,
        Resolver resolver = {})
    {
        equal = false;
        comparedPairs = 0u;

        struct Pair {
            std::size_t left, right;
        };

        core::pmr::Vector<Pair> pairs{&core::defaultCpuMemoryResource()};
        try {
            if (!maxPairs)
                return "SHADER-BUFFER-TYPE-PAIR-BUDGET";
            if (left >= lhs.nodes_.size() || right >= rhs.nodes_.size())
                return "SHADER-BUFFER-TYPE-COMPARE-ROOT";
            if (const auto* id = resolver(left, right))
                return id;
            if (left >= lhs.nodes_.size() || right >= rhs.nodes_.size())
                return "SHADER-BUFFER-TYPE-COMPARE-FRAMING";
            pairs.push_back({left, right});
            const auto enqueue = [&](std::size_t l, std::size_t r) -> const char* {
                if (l >= lhs.nodes_.size() || r >= rhs.nodes_.size())
                    return "SHADER-BUFFER-TYPE-COMPARE-FRAMING";
                if (const auto* id = resolver(l, r))
                    return id;
                if (l >= lhs.nodes_.size() || r >= rhs.nodes_.size())
                    return "SHADER-BUFFER-TYPE-COMPARE-FRAMING";
                for (const auto& old : pairs)
                    if (old.left == l && old.right == r)
                        return nullptr;
                if (pairs.size() >= maxPairs)
                    return "SHADER-BUFFER-TYPE-PAIR-BUDGET";
                pairs.push_back({l, r});
                return nullptr;
            };
            for (std::size_t index = 0u; index < pairs.size(); ++index) {
                const auto pair = pairs[index];
                const auto& a = lhs.nodes_[pair.left];
                const auto& b = rhs.nodes_[pair.right];
                if (a.memberBegin > lhs.members_.size()
                    || a.memberCount > lhs.members_.size() - a.memberBegin
                    || b.memberBegin > rhs.members_.size()
                    || b.memberCount > rhs.members_.size() - b.memberBegin)
                    return "SHADER-BUFFER-TYPE-COMPARE-FRAMING";
                bool matches = false;
                if (const auto* id = visitor(a, b, matches))
                    return id;
                if (!matches)
                    return nullptr;
                if (a.memberCount != b.memberCount || (a.child == absent) != (b.child == absent))
                    return nullptr;
                if (a.child != absent)
                    if (const auto* id = enqueue(a.child, b.child))
                        return id;
                for (std::size_t i = 0u; i < a.memberCount; ++i) {
                    const auto& m = lhs.members_[a.memberBegin + i];
                    const auto& n = rhs.members_[b.memberBegin + i];
                    if (const auto* id = enqueue(m.type, n.type))
                        return id;
                }
            }
            comparedPairs = pairs.size();
            equal = true;
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "SHADER-BUFFER-TYPE-ALLOCATION";
        }
    }

private:
    Budget budget_;
    core::pmr::Vector<Node> nodes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Member> members_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Root> roots_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::shader
