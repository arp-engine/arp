#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

#include <filesystem>

namespace arp::render_graph {

class FrameGraph;
class CompiledFrameGraph;

/// Result of a render graph topology HTML dump.
struct TopologyDumpResult {
    /// True when the HTML file was written successfully.
    bool success = false;
    /// Human-readable diagnostic when success is false (e.g. path not writable).
    core::pmr::String diagnostics;
    /// Absolute path of the HTML file that was (attempted to be) written.
    core::pmr::String htmlPath;
};

/// Export the compiled render graph topology as a self-contained, offline-capable
/// HTML document with an embedded SVG dependency graph and diagnostic tables.
///
/// The HTML is fully self-contained (no external CSS/JS/CDN references) and can
/// be opened directly in any browser without a network connection.
///
/// @param graph        The FrameGraph whose pass/resource declarations are
///                     inspected. Must be the same graph that produced @p compiled.
/// @param compiled     The CompiledFrameGraph returned by @c graph.compile().
///                     Provides execution order, lifetimes, barriers, alias plan.
/// @param outputPath   Destination .html file path. Parent directories are
///                     created automatically.
/// @return Result with success flag, diagnostics, and the absolute output path.
ARP_API TopologyDumpResult dumpTopologyHtml(const FrameGraph& graph,
    const CompiledFrameGraph& compiled,
    const std::filesystem::path& outputPath);

/// Convenience overload: compiles @p graph internally, then dumps the topology.
/// Useful for one-shot diagnostic dumps where the caller does not need the
/// CompiledFrameGraph for anything else.
ARP_API TopologyDumpResult dumpTopologyHtml(
    FrameGraph& graph, const std::filesystem::path& outputPath);

} // namespace arp::render_graph
