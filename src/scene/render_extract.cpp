#include "scene/render_extract.h"

// render_extract.h is currently header-only: the tagged-union payload and the
// abstract sink carry no out-of-line symbols (ASTRUCT/APROP/AENUM are codegen
// markers with empty definitions). This .cpp exists to:
//   - anchor the translation unit for future domain-neutral extraction helpers
//     (e.g. deterministic payload-equality diagnostics, typeId validation);
//   - keep the module source layout symmetric with sibling scene headers that
//     ship a .cpp (component.cpp, transform.cpp, diagnostics.cpp).
//
// No magic numbers, no guessing, no silent fallback: any helper added here must
// operate on explicit payload fields and fail with a diagnostic on unknown
// typeId contracts.

namespace arp::scene {
// Intentionally empty for now.
} // namespace arp::scene
