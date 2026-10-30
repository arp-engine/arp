#pragma once

#if ARP_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H

namespace arp::ui::detail {
bool initializeFreeTypeLibrary(FT_Library& library) noexcept;
} // namespace arp::ui::detail
#endif
