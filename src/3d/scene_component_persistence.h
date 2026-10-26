#pragma once

#include "scene/component_factory.h"

namespace arp::scene3d {
// Only Mesh/Light/procedural Sky are supported. Other native components must
// register their own domain contracts before scene export is permitted.
ARP_API bool registerScene3DPersistence(scene::SceneComponentFactory&);
} // namespace arp::scene3d
