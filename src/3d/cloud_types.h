#pragma once

#include "core/api.h"
#include "core/math/math.h"

namespace arp::scene3d {

// All lengths are world metres. Zero-initialized authoring is deliberately
// invalid until the caller supplies the physical layer and density scales.
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CloudLayerDesc {
    APROP(Serialize, Editor)
    core::Vec3 planetCenter{};
    APROP(Serialize, Editor)
    float planetRadius = 0.0f;
    APROP(Serialize, Editor)
    float baseAltitude = 0.0f;
    APROP(Serialize, Editor)
    float thickness = 0.0f;
    APROP(Serialize, Editor)
    float extinction = 0.0f;
    APROP(Serialize, Editor)
    core::Vec3 scatteringAlbedo{};
    APROP(Serialize, Editor)
    float anisotropy = 0.0f;
    APROP(Serialize, Editor)
    core::Vec3 windVelocity{};
    APROP(Serialize, Editor)
    float weatherPeriod = 0.0f;
    APROP(Serialize, Editor)
    float shapePeriod = 0.0f;
    APROP(Serialize, Editor)
    float detailPeriod = 0.0f;
    APROP(Serialize, Editor)
    float coverage = 0.0f;
    APROP(Serialize, Editor)
    float erosion = 0.0f;
};

class ARP_API CloudLayerValidator final {
public:
    static const char* diagnostic(const CloudLayerDesc& layer) noexcept;
};

} // namespace arp::scene3d
