#pragma once
#include "resource/cooked_resource.h"

namespace arp::ui {
// CPU-only UI codecs; renderer-owned uploads are constructed from these resources.
ARP_API core::SharedPtr<const resource::CookedResourceCodecRegistry> makeUiCookedResourceCodecs();
} // namespace arp::ui
