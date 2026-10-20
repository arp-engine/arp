#pragma once
#include "rhi/gpu_commands.h"

namespace arp::rhi::semantics {
// Whole-attachment definedness. Draws alone do not prove complete coverage.
// This logical result grants neither native content nor CPU byte initialization.
class AttachmentContentContract final {
public:
    static constexpr bool afterStore(LoadOp load, StoreOp store, bool previousWhole) noexcept
    {
        return store == StoreOp::Store
            && (load == LoadOp::Clear || (load == LoadOp::Load && previousWhole));
    }
};
} // namespace arp::rhi::semantics
