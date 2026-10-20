#pragma once

namespace arp::rhi::semantics {
// RenderOutputObservation/1: queue/present counters prove operations only.
// A fingerprint of counters, descriptors or expected bytes is never an output
// observation. Legacy report fields remain source-compatible and false/zero;
// only a separately sealed original native-pixel source may certify output.
class RenderOutputObservationContract final {
public:
    inline static constexpr const char* unavailableDiagnostic =
        "RHI-OUTPUT-ORIGINAL-OBSERVATION-UNAVAILABLE";
};
} // namespace arp::rhi::semantics
