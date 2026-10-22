#pragma once

#include "core/diagnostics/cpu_work_measurement.h"

namespace arp::render_graph {

// CPU observation only; no execution/completion/publication authority.
struct OrderedGraphicsMeasurements {
    core::CpuWorkMeasurement total, build, compile, prepare, record, submit, presentRetire;
    core::CpuWorkMeasurementObserver* preparationObserver = nullptr;
};

} // namespace arp::render_graph
