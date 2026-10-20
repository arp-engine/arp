#pragma once
#include <cstddef>

namespace arp::rhi {
// Cross-representation ABI comparison for shader reflection and L2 metadata.
// Member order is part of the versioned reflected layout, not a set union.
class ShaderPushConstantAbiIdentity final {
public:
    template <class Left, class Right>
    static bool matchesMembers(const Left& left, const Right& right) noexcept
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t i = 0u; i < left.size(); ++i) {
            const auto& a = left[i];
            const auto& b = right[i];
            if (a.name != b.name || a.scalarType != b.scalarType || a.scalarWidth != b.scalarWidth
                || a.vecSize != b.vecSize || a.matrixRows != b.matrixRows
                || a.matrixCols != b.matrixCols || a.matrixStride != b.matrixStride
                || a.rowMajor != b.rowMajor || a.arrayDims != b.arrayDims
                || a.arrayStride != b.arrayStride || a.offset != b.offset || a.size != b.size
                || a.decorations != b.decorations)
                return false;
        }
        return true;
    }
};
} // namespace arp::rhi
