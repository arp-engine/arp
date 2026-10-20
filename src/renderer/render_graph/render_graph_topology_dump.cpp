#include "renderer/render_graph/render_graph_topology_dump.h"

#include "core/cpu_allocator.h"
#include "core/log.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace arp::render_graph {

namespace {

// ===========================================================================
// Enum stringification helpers
// ===========================================================================

const char* passKindName(PassKind kind)
{
    switch (kind) {
    case PassKind::Graphics:
        return "Graphics";
    case PassKind::Compute:
        return "Compute";
    case PassKind::Copy:
        return "Copy";
    case PassKind::Present:
        return "Present";
    }
    return "Unknown";
}

const char* queueRoleName(QueueRole role)
{
    switch (role) {
    case QueueRole::Graphics:
        return "Graphics";
    case QueueRole::Compute:
        return "Compute";
    case QueueRole::Transfer:
        return "Transfer";
    case QueueRole::Present:
        return "Present";
    }
    return "Unknown";
}

const char* resourceKindName(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Texture:
        return "Texture";
    case ResourceKind::Buffer:
        return "Buffer";
    }
    return "Unknown";
}

const char* resourceStateName(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::Undefined:
        return "Undefined";
    case rhi::ResourceState::CopySrc:
        return "CopySrc";
    case rhi::ResourceState::CopyDst:
        return "CopyDst";
    case rhi::ResourceState::ShaderRead:
        return "ShaderRead";
    case rhi::ResourceState::ShaderWrite:
        return "ShaderWrite";
    case rhi::ResourceState::ColorAttachment:
        return "ColorAttachment";
    case rhi::ResourceState::DepthAttachment:
        return "DepthAttachment";
    case rhi::ResourceState::Present:
        return "Present";
    }
    return "Unknown";
}

const char* loadOpName(rhi::LoadOp op)
{
    switch (op) {
    case rhi::LoadOp::Load:
        return "Load";
    case rhi::LoadOp::Clear:
        return "Clear";
    case rhi::LoadOp::DontCare:
        return "DontCare";
    }
    return "Unknown";
}

const char* storeOpName(rhi::StoreOp op)
{
    switch (op) {
    case rhi::StoreOp::Store:
        return "Store";
    case rhi::StoreOp::DontCare:
        return "DontCare";
    }
    return "Unknown";
}

const char* dependencyKindName(TopologyDependencyKind kind)
{
    switch (kind) {
    case TopologyDependencyKind::Raw:
        return "RAW";
    case TopologyDependencyKind::War:
        return "WAR";
    case TopologyDependencyKind::Waw:
        return "WAW";
    }
    return "Unknown";
}

const char* dependencyKindColor(TopologyDependencyKind kind)
{
    switch (kind) {
    case TopologyDependencyKind::Raw:
        return "#2563eb"; // blue
    case TopologyDependencyKind::War:
        return "#ea580c"; // orange
    case TopologyDependencyKind::Waw:
        return "#dc2626"; // red
    }
    return "#6b7280";
}

/// Format a bitflag PipelineStage into a compact "+ "-separated label.
std::string pipelineStageLabel(rhi::PipelineStage stage)
{
    const auto value = static_cast<std::uint32_t>(stage);
    if (value == 0) {
        return "None";
    }
    std::string result;
    auto append = [&](const char* name, std::uint32_t bit) {
        if ((value & bit) != 0) {
            if (!result.empty())
                result += "+";
            result += name;
        }
    };
    append("TopOfPipe", 1u << 0);
    append("Transfer", 1u << 1);
    append("VertexShader", 1u << 2);
    append("FragmentShader", 1u << 3);
    append("ComputeShader", 1u << 4);
    append("ColorAttachmentOutput", 1u << 5);
    append("DepthStencil", 1u << 6);
    append("Present", 1u << 7);
    return result;
}

/// Format a bitflag ResourceAccess into a compact "+ "-separated label.
std::string resourceAccessLabel(rhi::ResourceAccess access)
{
    const auto value = static_cast<std::uint32_t>(access);
    if (value == 0) {
        return "None";
    }
    std::string result;
    auto append = [&](const char* name, std::uint32_t bit) {
        if ((value & bit) != 0) {
            if (!result.empty())
                result += "+";
            result += name;
        }
    };
    append("TransferRead", 1u << 0);
    append("TransferWrite", 1u << 1);
    append("ShaderRead", 1u << 2);
    append("ShaderWrite", 1u << 3);
    append("ColorAttachmentRead", 1u << 4);
    append("ColorAttachmentWrite", 1u << 5);
    append("DepthStencilRead", 1u << 6);
    append("DepthStencilWrite", 1u << 7);
    append("PresentRead", 1u << 8);
    return result;
}

// ===========================================================================
// HTML escaping
// ===========================================================================

std::string htmlEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '&':
            out += "&amp;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

// ===========================================================================
// Topology data structures (only public types -- safe to pass to helpers)
// ===========================================================================

struct TopologyEdge {
    std::size_t fromPass = 0;
    std::size_t toPass = 0;
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    TopologyDependencyKind dependency = TopologyDependencyKind::Raw;
    /// True when the producer wrote this resource as a render attachment.
    bool renderTargetDependency = false;
};

/// A resource hazard that requires an explicit graphics/compute queue handoff
/// when async compute is enabled. The executor realizes this as a timeline
/// semaphore signal/wait between queue segments.
struct QueueSyncEdge {
    std::size_t fromPass = 0;
    std::size_t toPass = 0;
    std::pmr::string resourceName;
    QueueRole fromQueue = QueueRole::Graphics;
    QueueRole toQueue = QueueRole::Graphics;
};

struct RenderTargetFlow {
    std::size_t writerPass = 0;
    std::pmr::string targetName;
    AttachmentRole role = AttachmentRole::ColorAttachment;
    rhi::LoadOp loadOp = rhi::LoadOp::Load;
    rhi::StoreOp storeOp = rhi::StoreOp::Store;
    std::pmr::vector<std::size_t> consumers;
};

struct SubpassTopology {
    std::pmr::string groupName;
    std::size_t parentPass = 0;

    struct Entry {
        std::pmr::string name;
        std::pmr::vector<std::pmr::string> writes;
        std::pmr::vector<std::pmr::string> inputs;
    };

    std::pmr::vector<Entry> entries;
};

struct PassResourceUse {
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    ResourceAccess access = ResourceAccess::Read;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
};

/// Layout / metadata for a single pass node, extracted from the FrameGraph.
/// This is a copy of the relevant private fields so that SVG rendering helpers
/// do not need friend access to FrameGraph.
struct PassNodeInfo {
    std::pmr::string label;
    PassKind kind = PassKind::Graphics;
    QueueRole queue = QueueRole::Graphics;
    bool asyncComputeEligible = false;
    bool culled = false;
    /// Index in executionOrder(), or SIZE_MAX if culled / not executable.
    std::size_t executionOrder = SIZE_MAX;
};

struct TopologyData {
    std::pmr::vector<TopologyEdge> edges;
    std::pmr::vector<std::pmr::vector<PassResourceUse>> passUses;
    std::pmr::vector<PassNodeInfo> passInfos;
    std::pmr::vector<QueueSyncEdge> queueSyncEdges;
    std::pmr::vector<RenderTargetFlow> renderTargetFlows;
    std::pmr::vector<SubpassTopology> subpassGroups;
};

// ===========================================================================
// SVG graph layout and rendering
// ===========================================================================

struct PassNodeLayout {
    int x = 0;
    int y = 0;
    int height = 44;
};

int passNodeHeight(const std::pmr::vector<PassResourceUse>& uses)
{
    std::size_t inputs = 0;
    std::size_t outputs = 0;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    for (const auto& use : uses) {
        auto& names = use.access == ResourceAccess::Read ? inputNames : outputNames;
        if (std::find(names.begin(), names.end(), use.resourceName.c_str()) != names.end())
            continue;
        names.emplace_back(use.resourceName.c_str());
        if (use.access == ResourceAccess::Read)
            ++inputs;
        else
            ++outputs;
    }
    // Blueprint-style two-column resource rows beneath the pass header.
    return 58 + static_cast<int>(std::max(inputs, outputs)) * 16;
}

QueueRole visualQueue(const PassNodeInfo& pass)
{
    return pass.asyncComputeEligible ? QueueRole::Compute : pass.queue;
}

int wireDeckBottom(const TopologyData& topology)
{
    // Resource hazards and cross-queue handoffs each receive an orthogonal
    // routing track above the nodes, keeping wires out of node bodies.
    return 70 + static_cast<int>(topology.edges.size() + topology.queueSyncEdges.size()) * 14;
}

/// Compute pixel positions for each pass node. Executable passes are arranged
/// left-to-right by execution order; queue roles occupy swimlanes.
std::pmr::vector<PassNodeLayout> computeLayout(
    const TopologyData& topology, int& outWidth, int& outHeight)
{
    const int nodeWidth = 460;
    const int nodeGapX = 40;
    const int marginX = 40;

    std::pmr::vector<PassNodeLayout> layouts(topology.passInfos.size());

    // Blueprint-style graph: execution advances left-to-right. Queue role
    // selects a swimlane, so graphics/compute handoffs are visually obvious.
    const int graphicsLaneY = wireDeckBottom(topology) + 42;
    const int computeLaneY = graphicsLaneY + 268;
    const int transferLaneY = computeLaneY + 268;
    const int presentLaneY = graphicsLaneY;
    std::size_t executableCount = 0;
    for (std::size_t i = 0; i < topology.passInfos.size(); ++i) {
        if (topology.passInfos[i].culled)
            continue;
        layouts[i].x = marginX
            + static_cast<int>(topology.passInfos[i].executionOrder) * (nodeWidth + nodeGapX);
        switch (visualQueue(topology.passInfos[i])) {
        case QueueRole::Compute:
            layouts[i].y = computeLaneY;
            break;
        case QueueRole::Transfer:
            layouts[i].y = transferLaneY;
            break;
        case QueueRole::Present:
            layouts[i].y = presentLaneY;
            break;
        case QueueRole::Graphics:
            layouts[i].y = graphicsLaneY;
            break;
        }
        layouts[i].height = passNodeHeight(topology.passUses[i]);
        ++executableCount;
    }

    // Culled passes: up to 3 per row below the executable section.
    const int culledStartY = transferLaneY + 268;
    const int culledPerRow = 3;
    int culledRowHeight = 0;
    for (std::size_t i = 0; i < topology.passInfos.size(); ++i) {
        if (!topology.passInfos[i].culled)
            continue;
        layouts[i].height = passNodeHeight(topology.passUses[i]);
        culledRowHeight = std::max(culledRowHeight, layouts[i].height);
    }
    const int culledRowGapY = culledRowHeight + 32;
    std::size_t culledCol = 0;
    for (std::size_t i = 0; i < topology.passInfos.size(); ++i) {
        if (!topology.passInfos[i].culled)
            continue;
        const int row = static_cast<int>(culledCol / culledPerRow);
        const int col = static_cast<int>(culledCol % culledPerRow);
        layouts[i].x = marginX + col * (nodeWidth + nodeGapX);
        layouts[i].y = culledStartY + row * culledRowGapY;
        ++culledCol;
    }

    const int totalCulledRows = static_cast<int>((culledCol + culledPerRow - 1) / culledPerRow);
    outHeight = culledStartY + totalCulledRows * culledRowGapY + 40;
    outWidth = std::max(900, marginX + static_cast<int>(executableCount) * (nodeWidth + nodeGapX));

    return layouts;
}

/// Build the SVG dependency graph section. Passes are arranged in horizontal
/// layers by execution order; edges are drawn as colored bezier curves with a
/// mid-point resource label.
std::string renderSvgGraph(
    const std::pmr::vector<PassNodeInfo>& passInfos, const TopologyData& topology)
{
    int svgWidth = 0;
    int svgHeight = 0;
    const auto layouts = computeLayout(topology, svgWidth, svgHeight);

    const int nodeWidth = 460;
    const int marginX = 40;

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << svgWidth << "\" height=\""
        << svgHeight << "\" style=\"background:#f8fafc;\">";
    svg << "<defs>";
    // Arrowhead marker for each dependency color.
    for (const auto kind :
        {TopologyDependencyKind::Raw, TopologyDependencyKind::War, TopologyDependencyKind::Waw}) {
        const auto color = dependencyKindColor(kind);
        const auto name = dependencyKindName(kind);
        svg << "<marker id=\"arrow-" << name << "\" markerWidth=\"10\" markerHeight=\"10\" "
            << "refX=\"9\" refY=\"3\" orient=\"auto\" markerUnits=\"strokeWidth\">"
            << "<path d=\"M0,0 L0,6 L9,3 z\" fill=\"" << color << "\"/></marker>";
    }
    svg << "<marker id=\"arrow-queue-sync\" markerWidth=\"10\" markerHeight=\"10\" "
        << "refX=\"9\" refY=\"3\" orient=\"auto\" markerUnits=\"strokeWidth\">"
        << "<path d=\"M0,0 L0,6 L9,3 z\" fill=\"#7c3aed\"/></marker>";
    svg << "</defs>";

    // Title.
    svg << "<text x=\"" << marginX << "\" y=\"20\" font-size=\"15\" font-weight=\"bold\""
        << " fill=\"#1e293b\">Render Graph Topology</text>";
    svg << "<text x=\"" << marginX + 240 << "\" y=\"20\" font-size=\"12\" fill=\"#64748b\">"
        << "RAW=blue (read-after-write)  WAR=orange (write-after-read)  WAW=red (write-after-write)</text>";
    const int graphicsLaneY = wireDeckBottom(topology) + 42;
    const int computeLaneY = graphicsLaneY + 268;
    const int transferLaneY = computeLaneY + 268;
    svg << "<text x=\"8\" y=\"" << graphicsLaneY + 24
        << "\" font-size=\"11\" fill=\"#475569\">Graphics lane</text>";
    svg << "<text x=\"8\" y=\"" << computeLaneY + 24
        << "\" font-size=\"11\" fill=\"#475569\">Compute lane</text>";
    svg << "<text x=\"8\" y=\"" << transferLaneY + 24
        << "\" font-size=\"11\" fill=\"#475569\">Transfer lane</text>";

    const auto portY =
        [&](std::size_t passIndex, ResourceAccess access, const std::pmr::string& resourceName) {
            std::size_t row = 0;
            std::vector<std::string> seen;
            for (const auto& use : topology.passUses[passIndex]) {
                if (use.access != access)
                    continue;
                if (std::find(seen.begin(), seen.end(), use.resourceName.c_str()) != seen.end())
                    continue;
                if (use.resourceName == resourceName) {
                    return layouts[passIndex].y + 64 + static_cast<int>(row) * 16;
                }
                seen.emplace_back(use.resourceName.c_str());
                ++row;
            }
            return layouts[passIndex].y + layouts[passIndex].height / 2;
        };

    // Draw edges first (behind nodes).
    for (std::size_t edgeIndex = 0; edgeIndex < topology.edges.size(); ++edgeIndex) {
        const auto& edge = topology.edges[edgeIndex];
        const auto& fromLayout = layouts[edge.fromPass];
        const auto& toLayout = layouts[edge.toPass];
        const auto color = dependencyKindColor(edge.dependency);
        const auto depName = dependencyKindName(edge.dependency);

        // Blueprint association: output port (right) → input port (left).
        const int x1 = fromLayout.x + nodeWidth;
        const int y1 = portY(edge.fromPass, ResourceAccess::Write, edge.resourceName);
        const int x2 = toLayout.x;
        const int y2 = portY(edge.toPass, ResourceAccess::Read, edge.resourceName);

        // Each resource edge gets a dedicated Manhattan-routing track above
        // all nodes. This prevents long-lived dependencies from crossing or
        // disappearing behind intermediate pass boxes.
        const int trackY = 70 + static_cast<int>(edgeIndex) * 14;
        const int exitX = x1 + 10;
        const int entryX = x2 - 10;
        svg << "<path d=\"M" << x1 << "," << y1 << " L" << exitX << "," << y1 << " L" << exitX
            << "," << trackY << " L" << entryX << "," << trackY << " L" << entryX << "," << y2
            << " L" << x2 << "," << y2 << "\" fill=\"none\" stroke=\"" << color
            << "\" stroke-width=\"" << (edge.renderTargetDependency ? 3 : 2)
            << "\" marker-end=\"url(#arrow-" << depName << ")\""
            << " stroke-dasharray=\"7,4\""
            << " opacity=\"0.7\">";
        // Tooltip with resource name and dependency kind.
        svg << "<title>" << htmlEscape(edge.resourceName.c_str()) << " (" << depName << ")</title>";
        svg << "</path>";
    }

    // Queue handoffs are explicit semaphore signal/wait boundaries. Draw
    // above the resource flow so synchronization remains visible even when
    // several hazards share the same producer/consumer pair.
    for (std::size_t syncIndex = 0; syncIndex < topology.queueSyncEdges.size(); ++syncIndex) {
        const auto& sync = topology.queueSyncEdges[syncIndex];
        const auto& fromLayout = layouts[sync.fromPass];
        const auto& toLayout = layouts[sync.toPass];
        const int x1 = fromLayout.x + nodeWidth;
        const int y1 = portY(sync.fromPass, ResourceAccess::Write, sync.resourceName);
        const int x2 = toLayout.x;
        const int y2 = portY(sync.toPass, ResourceAccess::Read, sync.resourceName);
        const int trackY = 70 + static_cast<int>(topology.edges.size() + syncIndex) * 14;
        const int exitX = x1 + 16;
        const int entryX = x2 - 16;
        svg << "<path d=\"M" << x1 << "," << y1 << " L" << exitX << "," << y1 << " L" << exitX
            << "," << trackY << " L" << entryX << "," << trackY << " L" << entryX << "," << y2
            << " L" << x2 << "," << y2 << "\" fill=\"none\" stroke=\"#7c3aed\" stroke-width=\"2.5\""
            << " stroke-dasharray=\"5,4\" marker-end=\"url(#arrow-queue-sync)\">"
            << "<title>queue sync: " << htmlEscape(sync.resourceName.c_str()) << " | "
            << queueRoleName(sync.fromQueue) << " signal → " << queueRoleName(sync.toQueue)
            << " wait</title></path>";
    }

    // Draw pass nodes.
    for (std::size_t i = 0; i < passInfos.size(); ++i) {
        const auto& info = passInfos[i];
        const auto& layout = layouts[i];
        const int x = layout.x;
        const int y = layout.y;
        const bool culled = info.culled;

        // Node fill color by pass kind.
        const char* fillColor = "#dbeafe"; // Graphics = light blue
        if (culled)
            fillColor = "#e2e8f0"; // grey
        else if (info.kind == PassKind::Compute)
            fillColor = "#dcfce7"; // green
        else if (info.kind == PassKind::Copy)
            fillColor = "#fef9c3"; // yellow
        else if (info.kind == PassKind::Present)
            fillColor = "#fce7f3"; // pink

        svg << "<g>";
        svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << nodeWidth << "\" height=\""
            << layout.height << "\" rx=\"6\" fill=\"" << fillColor << "\" stroke=\""
            << (culled ? "#94a3b8" : "#475569") << "\" stroke-width=\"1.5\""
            << (culled ? " stroke-dasharray=\"4,3\"" : "") << ">";
        // Tooltip with pass details.
        svg << "<title>" << htmlEscape(info.label.c_str()) << " | " << passKindName(info.kind)
            << " | " << queueRoleName(info.queue);
        if (culled)
            svg << " | CULLED";
        else if (info.executionOrder != SIZE_MAX)
            svg << " | exec#" << info.executionOrder;
        svg << "</title>";
        svg << "</rect>";

        // Pass name.
        svg << "<text x=\"" << x + 8 << "\" y=\"" << y + 18
            << "\" font-size=\"13\" font-weight=\"bold\" fill=\""
            << (culled ? "#94a3b8" : "#1e293b") << "\">" << htmlEscape(info.label.c_str())
            << "</text>";
        // Pass kind + queue + order.
        std::ostringstream meta;
        meta << passKindName(info.kind) << " / " << queueRoleName(info.queue);
        if (!culled && info.executionOrder != SIZE_MAX) {
            meta << " / #" << info.executionOrder;
        } else if (culled) {
            meta << " / CULLED";
        }
        svg << "<text x=\"" << x + 8 << "\" y=\"" << y + 34
            << "\" font-size=\"10\" fill=\"#64748b\">" << htmlEscape(meta.str()) << "</text>";
        if (info.asyncComputeEligible) {
            svg << "<text x=\"" << x + 404 << "\" y=\"" << y + 18
                << "\" font-size=\"9\" font-weight=\"bold\" fill=\"#6d28d9\">ASYNC</text>";
        }
        svg << "<line x1=\"" << x + nodeWidth / 2 << "\" y1=\"" << y + 44 << "\" x2=\""
            << x + nodeWidth / 2 << "\" y2=\"" << y + layout.height << "\" stroke=\"#cbd5e1\"/>";
        svg << "<text x=\"" << x + 8 << "\" y=\"" << y + 53
            << "\" font-size=\"9\" font-weight=\"bold\" fill=\"#0369a1\">INPUTS</text>";
        svg << "<text x=\"" << x + nodeWidth / 2 + 8 << "\" y=\"" << y + 53
            << "\" font-size=\"9\" font-weight=\"bold\" fill=\"#047857\">OUTPUTS</text>";
        auto renderUses = [&](ResourceAccess access, int columnX, const char* color) {
            std::size_t row = 0;
            std::vector<std::string> seen;
            for (const auto& use : topology.passUses[i]) {
                if (use.access != access)
                    continue;
                if (std::find(seen.begin(), seen.end(), use.resourceName.c_str()) != seen.end())
                    continue;
                seen.emplace_back(use.resourceName.c_str());
                svg << "<circle cx=\"" << columnX + 3 << "\" cy=\"" << y + 64 + row * 16 - 3
                    << "\" r=\"3\" fill=\"" << color << "\"/>";
                svg << "<text x=\"" << columnX + 10 << "\" y=\"" << y + 64 + row * 16
                    << "\" font-size=\"9\" fill=\"" << color << "\">"
                    << htmlEscape(use.resourceName.c_str()) << "</text>";
                ++row;
            }
        };
        renderUses(ResourceAccess::Read, x + 8, "#0369a1");
        renderUses(ResourceAccess::Write, x + nodeWidth / 2 + 8, "#047857");
        svg << "</g>";
    }

    svg << "</svg>";
    return svg.str();
}

// ===========================================================================
// HTML table rendering
// ===========================================================================

std::string renderPassesTable(const CompiledFrameGraph& compiled)
{
    const auto& order = compiled.executionOrder();
    const auto& passes = compiled.passes();
    const auto passCount = passes.size();

    // Map original pass index -> execution order position.
    std::pmr::vector<std::size_t> execPosition(passCount, SIZE_MAX);
    for (std::size_t i = 0; i < order.size(); ++i) {
        execPosition[order[i]] = i;
    }

    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>#</th><th>Pass Name</th><th>Kind</th><th>Queue</th>"
      << "<th>Async Compute</th><th>Exec Order</th><th>Status</th>"
      << "</tr></thead><tbody>";
    for (std::size_t i = 0; i < passCount; ++i) {
        const auto& p = passes[i];
        t << "<tr" << (p.culled ? " class=\"culled\"" : "") << ">";
        t << "<td>" << i << "</td>";
        t << "<td>" << htmlEscape(p.name.c_str()) << "</td>";
        t << "<td>" << passKindName(p.kind) << "</td>";
        t << "<td>" << queueRoleName(p.queue) << "</td>";
        t << "<td>" << (p.asyncComputeEligible ? "yes" : "no") << "</td>";
        if (p.culled) {
            t << "<td>-</td><td><span class=\"badge badge-culled\">CULLED</span></td>";
        } else {
            t << "<td>" << execPosition[i] << "</td>";
            t << "<td><span class=\"badge badge-ok\">active</span></td>";
        }
        t << "</tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderResourceLifetimesTable(const CompiledFrameGraph& compiled)
{
    const auto& lifetimes = compiled.resourceLifetimes();
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Resource</th><th>Kind</th><th>Handle</th>"
      << "<th>Producer</th><th>Consumers</th>"
      << "<th>First Use</th><th>Last Use</th>"
      << "</tr></thead><tbody>";
    for (const auto& lt : lifetimes) {
        t << "<tr>";
        t << "<td>" << htmlEscape(lt.resourceName.c_str()) << "</td>";
        t << "<td>" << resourceKindName(lt.kind) << "</td>";
        t << "<td>" << lt.handleId << "</td>";
        t << "<td>" << htmlEscape(lt.producerPassName.c_str()) << "</td>";
        t << "<td>";
        for (std::size_t i = 0; i < lt.consumerPassNames.size(); ++i) {
            if (i > 0)
                t << ", ";
            t << htmlEscape(lt.consumerPassNames[i].c_str());
        }
        if (lt.consumerPassNames.empty())
            t << "<em>none</em>";
        t << "</td>";
        t << "<td>" << lt.firstUseOrder << "</td>";
        t << "<td>" << lt.lastUseOrder << "</td>";
        t << "</tr>";
    }
    if (lifetimes.empty()) {
        t << "<tr><td colspan=\"7\"><em>No resources.</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderResourceLifetimeTimeline(const CompiledFrameGraph& compiled)
{
    const auto& lifetimes = compiled.resourceLifetimes();
    if (lifetimes.empty()) {
        return "<p><em>No resource lifetimes.</em></p>";
    }
    std::pmr::map<std::uint32_t, std::uint32_t> aliasGroups;
    for (const auto& alias : compiled.aliasPlan()) {
        aliasGroups[alias.handle.id] = alias.aliasGroup;
    }
    const auto passCount = std::max<std::size_t>(compiled.executablePassCount(), 1);
    const int labelWidth = 260;
    const int cellWidth = 62;
    const int rowHeight = 28;
    const int width = labelWidth + static_cast<int>(passCount) * cellWidth + 40;
    const int height = 40 + static_cast<int>(lifetimes.size()) * rowHeight + 24;
    const char* colors[] = {"#bfdbfe", "#bbf7d0", "#fde68a", "#ddd6fe", "#fecaca", "#a7f3d0"};

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height
        << "\" style=\"background:#fff;\">";
    svg << "<text x=\"8\" y=\"18\" font-size=\"11\" fill=\"#475569\">Resource lifetime / alias reuse by execution order</text>";
    for (std::size_t pass = 0; pass < passCount; ++pass) {
        const int x = labelWidth + static_cast<int>(pass) * cellWidth;
        svg << "<line x1=\"" << x << "\" y1=\"28\" x2=\"" << x << "\" y2=\"" << height - 8
            << "\" stroke=\"#e2e8f0\"/>";
        svg << "<text x=\"" << x + 3 << "\" y=\"38\" font-size=\"9\" fill=\"#64748b\">#" << pass
            << "</text>";
    }
    for (std::size_t i = 0; i < lifetimes.size(); ++i) {
        const auto& lifetime = lifetimes[i];
        const int y = 46 + static_cast<int>(i) * rowHeight;
        const auto groupIt = aliasGroups.find(lifetime.handleId);
        const std::uint32_t group = groupIt == aliasGroups.end() ? 0u : groupIt->second;
        const int x = labelWidth + static_cast<int>(lifetime.firstUseOrder) * cellWidth;
        const int barWidth = std::max(12,
            static_cast<int>((lifetime.lastUseOrder - lifetime.firstUseOrder + 1) * cellWidth - 6));
        const char* color = colors[group % (sizeof(colors) / sizeof(colors[0]))];
        svg << "<text x=\"8\" y=\"" << y + 14 << "\" font-size=\"10\" fill=\"#1e293b\">"
            << htmlEscape(lifetime.resourceName.c_str());
        if (group)
            svg << " [alias " << group << "]";
        svg << "</text>";
        svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << barWidth
            << "\" height=\"18\" rx=\"4\" fill=\"" << color << "\" stroke=\"#475569\">"
            << "<title>" << htmlEscape(lifetime.resourceName.c_str())
            << " | producer: " << htmlEscape(lifetime.producerPassName.c_str()) << " | uses #"
            << lifetime.firstUseOrder << "–#" << lifetime.lastUseOrder << " | alias group " << group
            << "</title></rect>";
    }
    svg << "</svg>";
    return svg.str();
}

std::string renderBarrierTable(const CompiledFrameGraph& compiled)
{
    const auto& barriers = compiled.barrierPlan();
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Pass</th><th>Resource</th><th>Kind</th>"
      << "<th>Before</th><th>After</th>"
      << "<th>Src Stage</th><th>Dst Stage</th>"
      << "<th>Src Access</th><th>Dst Access</th>"
      << "</tr></thead><tbody>";
    for (const auto& b : barriers) {
        t << "<tr>";
        t << "<td>" << htmlEscape(b.passName.c_str()) << "</td>";
        t << "<td>" << htmlEscape(b.resourceName.c_str()) << "</td>";
        t << "<td>" << resourceKindName(b.kind) << "</td>";
        t << "<td>" << resourceStateName(b.before) << "</td>";
        t << "<td>" << resourceStateName(b.after) << "</td>";
        t << "<td>" << htmlEscape(pipelineStageLabel(b.srcStage)) << "</td>";
        t << "<td>" << htmlEscape(pipelineStageLabel(b.dstStage)) << "</td>";
        t << "<td>" << htmlEscape(resourceAccessLabel(b.srcAccess)) << "</td>";
        t << "<td>" << htmlEscape(resourceAccessLabel(b.dstAccess)) << "</td>";
        t << "</tr>";
    }
    if (barriers.empty()) {
        t << "<tr><td colspan=\"9\"><em>No barriers.</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderAliasTable(const CompiledFrameGraph& compiled)
{
    const auto& aliases = compiled.aliasPlan();
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Resource</th><th>Handle</th><th>Alias Group</th>"
      << "</tr></thead><tbody>";
    for (const auto& a : aliases) {
        t << "<tr>";
        t << "<td>" << htmlEscape(a.resourceName.c_str()) << "</td>";
        t << "<td>" << a.handle.id << "</td>";
        t << "<td>" << a.aliasGroup << "</td>";
        t << "</tr>";
    }
    if (aliases.empty()) {
        t << "<tr><td colspan=\"3\"><em>No aliasing (transient aliasing disabled or no transient resources).</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderExternalResourcesTable(const CompiledFrameGraph& compiled)
{
    const auto& ext = compiled.externalResources();
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Resource</th><th>Kind</th><th>Handle</th>"
      << "<th>Initial State</th><th>Required Final State</th>"
      << "</tr></thead><tbody>";
    for (const auto& r : ext) {
        t << "<tr>";
        t << "<td>" << htmlEscape(r.resourceName.c_str()) << "</td>";
        t << "<td>" << resourceKindName(r.kind) << "</td>";
        t << "<td>" << r.handleId << "</td>";
        t << "<td>" << resourceStateName(r.initialState) << "</td>";
        t << "<td>" << resourceStateName(r.requiredFinalState) << "</td>";
        t << "</tr>";
    }
    if (ext.empty()) {
        t << "<tr><td colspan=\"5\"><em>No external (imported) resources.</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderQueuePlanTable(const CompiledFrameGraph& compiled)
{
    const auto& qp = compiled.queuePlan();
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Pass</th><th>Kind</th><th>Queue</th><th>Async Compute</th>"
      << "</tr></thead><tbody>";
    for (const auto& q : qp) {
        t << "<tr>";
        t << "<td>" << htmlEscape(q.passName.c_str()) << "</td>";
        t << "<td>" << passKindName(q.kind) << "</td>";
        t << "<td>" << queueRoleName(q.queue) << "</td>";
        t << "<td>" << (q.asyncComputeEligible ? "yes" : "no") << "</td>";
        t << "</tr>";
    }
    if (qp.empty()) {
        t << "<tr><td colspan=\"4\"><em>No queue plan.</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderLoadStoreTable(const CompiledFrameGraph& compiled)
{
    const auto& ls = compiled.loadStorePlan();
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Pass</th><th>Resource</th><th>State</th>"
      << "<th>LoadOp</th><th>StoreOp</th>"
      << "<th>Transient</th><th>Imported</th>"
      << "</tr></thead><tbody>";
    for (const auto& a : ls) {
        t << "<tr>";
        t << "<td>" << htmlEscape(a.passName.c_str()) << "</td>";
        t << "<td>" << htmlEscape(a.resourceName.c_str()) << "</td>";
        t << "<td>" << resourceStateName(a.state) << "</td>";
        t << "<td>" << loadOpName(a.loadOp) << "</td>";
        t << "<td>" << storeOpName(a.storeOp) << "</td>";
        t << "<td>" << (a.transient ? "yes" : "no") << "</td>";
        t << "<td>" << (a.imported ? "yes" : "no") << "</td>";
        t << "</tr>";
    }
    if (ls.empty()) {
        t << "<tr><td colspan=\"7\"><em>No attachment load/store operations.</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderRenderTargetFlowTable(const TopologyData& topology)
{
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Writer Pass</th><th>Render Target</th><th>Role</th>"
      << "<th>Load / Store</th><th>Dependent Passes</th>"
      << "</tr></thead><tbody>";
    for (const auto& flow : topology.renderTargetFlows) {
        t << "<tr><td>" << htmlEscape(topology.passInfos[flow.writerPass].label.c_str()) << "</td>";
        t << "<td>" << htmlEscape(flow.targetName.c_str()) << "</td>";
        t << "<td>" << (flow.role == AttachmentRole::DepthAttachment ? "Depth" : "Color")
          << "</td>";
        t << "<td>" << loadOpName(flow.loadOp) << " / " << storeOpName(flow.storeOp) << "</td><td>";
        for (std::size_t i = 0; i < flow.consumers.size(); ++i) {
            if (i)
                t << ", ";
            t << htmlEscape(topology.passInfos[flow.consumers[i]].label.c_str());
        }
        if (flow.consumers.empty())
            t << "<em>no later graph consumer</em>";
        t << "</td></tr>";
    }
    if (topology.renderTargetFlows.empty()) {
        t << "<tr><td colspan=\"5\"><em>No render-target attachment writes.</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderQueueSyncTable(const TopologyData& topology)
{
    std::ostringstream t;
    t << "<table class=\"data-table\"><thead><tr>"
      << "<th>Producer</th><th>Signal Queue</th><th>Resource Hazard</th>"
      << "<th>Wait Queue</th><th>Consumer</th>"
      << "</tr></thead><tbody>";
    for (const auto& sync : topology.queueSyncEdges) {
        t << "<tr><td>" << htmlEscape(topology.passInfos[sync.fromPass].label.c_str()) << "</td>";
        t << "<td>" << queueRoleName(sync.fromQueue) << "</td>";
        t << "<td>" << htmlEscape(sync.resourceName.c_str()) << "</td>";
        t << "<td>" << queueRoleName(sync.toQueue) << "</td>";
        t << "<td>" << htmlEscape(topology.passInfos[sync.toPass].label.c_str()) << "</td></tr>";
    }
    if (topology.queueSyncEdges.empty()) {
        t << "<tr><td colspan=\"5\"><em>No cross-queue async synchronization in this frame.</em></td></tr>";
    }
    t << "</tbody></table>";
    return t.str();
}

std::string renderSubpassTopology(const TopologyData& topology)
{
    if (topology.subpassGroups.empty()) {
        return "<p><em>No native subpass groups were composed in this frame.</em></p>";
    }
    std::ostringstream html;
    for (const auto& group : topology.subpassGroups) {
        html << "<div class=\"subpass-group\"><strong>" << htmlEscape(group.groupName.c_str())
             << "</strong> <span class=\"subpass-parent\">inside "
             << htmlEscape(topology.passInfos[group.parentPass].label.c_str())
             << "</span><div class=\"subpass-row\">";
        for (std::size_t i = 0; i < group.entries.size(); ++i) {
            const auto& entry = group.entries[i];
            if (i)
                html << "<span class=\"subpass-arrow\">→</span>";
            html << "<div class=\"subpass-node\"><b>" << i << ". " << htmlEscape(entry.name.c_str())
                 << "</b><br><small>in: ";
            if (entry.inputs.empty())
                html << "—";
            for (std::size_t j = 0; j < entry.inputs.size(); ++j) {
                if (j)
                    html << ", ";
                html << htmlEscape(entry.inputs[j].c_str());
            }
            html << "<br>out: ";
            if (entry.writes.empty())
                html << "—";
            for (std::size_t j = 0; j < entry.writes.size(); ++j) {
                if (j)
                    html << ", ";
                html << htmlEscape(entry.writes[j].c_str());
            }
            html << "</small></div>";
        }
        html << "</div></div>";
    }
    return html.str();
}

// ===========================================================================
// Full HTML document assembly
// ===========================================================================

std::string renderHtmlDocument(const std::pmr::vector<PassNodeInfo>& passInfos,
    const CompiledFrameGraph& compiled,
    const TopologyData& topology)
{
    std::ostringstream html;

    html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    html << "<meta charset=\"utf-8\">\n";
    html << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    html << "<title>Render Graph Topology</title>\n<style>\n";
    html << ":root { --bg:#f8fafc; --card:#ffffff; --border:#e2e8f0;"
         << " --text:#1e293b; --muted:#64748b; --accent:#2563eb; }\n";
    html << "* { box-sizing:border-box; }\n";
    html << "body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,"
         << "Helvetica,Arial,sans-serif; margin:0; padding:24px; background:var(--bg);"
         << " color:var(--text); line-height:1.6; }\n";
    html << "h1 { font-size:1.6rem; margin:0 0 4px; }\n";
    html << "h2 { font-size:1.15rem; margin:28px 0 10px; padding-bottom:6px;"
         << " border-bottom:2px solid var(--border); cursor:pointer; }\n";
    html << "h2:hover { color:var(--accent); }\n";
    html << "h2 .toggle { font-size:0.8rem; color:var(--muted); font-weight:normal; }\n";
    html << ".summary { display:flex; gap:16px; flex-wrap:wrap; margin:12px 0 8px; }\n";
    html << ".stat { background:var(--card); border:1px solid var(--border);"
         << " border-radius:8px; padding:10px 16px; min-width:100px; }\n";
    html << ".stat .num { font-size:1.5rem; font-weight:700; color:var(--accent); }\n";
    html << ".stat .label { font-size:0.75rem; color:var(--muted); text-transform:uppercase; }\n";
    html << ".svg-container { background:var(--card); border:1px solid var(--border);"
         << " border-radius:8px; padding:16px; overflow-x:auto; margin:12px 0; }\n";
    html << "svg { max-width:none; }\n";
    html << ".table-container { overflow-x:auto; }\n";
    html << ".data-table { width:100%; border-collapse:collapse; font-size:0.85rem;"
         << " background:var(--card); border:1px solid var(--border); border-radius:8px;"
         << " overflow:hidden; }\n";
    html << ".data-table th { background:#f1f5f9; padding:8px 12px; text-align:left;"
         << " font-weight:600; border-bottom:2px solid var(--border); white-space:nowrap; }\n";
    html << ".data-table td { padding:6px 12px; border-bottom:1px solid var(--border);"
         << " white-space:nowrap; }\n";
    html << ".data-table tr:last-child td { border-bottom:none; }\n";
    html << ".data-table tr:hover { background:#f8fafc; }\n";
    html << ".data-table tr.culled { color:var(--muted); text-decoration:line-through; }\n";
    html << ".badge { display:inline-block; padding:2px 8px; border-radius:12px;"
         << " font-size:0.7rem; font-weight:600; }\n";
    html << ".badge-ok { background:#dcfce7; color:#166534; }\n";
    html << ".badge-culled { background:#f1f5f9; color:#64748b; }\n";
    html << ".collapsible-content { transition:max-height 0.2s ease; }\n";
    html << ".legend { display:flex; gap:16px; margin:8px 0; font-size:0.8rem; flex-wrap:wrap; }\n";
    html << ".legend-item { display:flex; align-items:center; gap:6px; }\n";
    html << ".legend-dot { width:12px; height:12px; border-radius:50%; display:inline-block; }\n";
    html
        << ".subpass-group { background:var(--card); border:1px solid var(--border); border-radius:8px; padding:12px; margin:10px 0; }\n";
    html << ".subpass-parent { color:var(--muted); font-size:0.8rem; }\n";
    html
        << ".subpass-row { display:flex; align-items:stretch; gap:8px; overflow-x:auto; padding-top:10px; }\n";
    html
        << ".subpass-node { min-width:180px; border:2px solid #0f766e; background:#ecfdf5; border-radius:6px; padding:8px; font-size:0.8rem; }\n";
    html
        << ".subpass-arrow { align-self:center; color:#0f766e; font-size:1.4rem; font-weight:bold; }\n";
    html << "</style>\n</head>\n<body>\n";

    // Header.
    html << "<h1>Render Graph Topology</h1>\n";
    html
        << "<p style=\"color:var(--muted);font-size:0.85rem;\">"
        << "Self-contained diagnostic export. "
        << "Passes are laid out by execution order; each node lists compact resource inputs and outputs. "
        << "Edges show resource dependencies.</p>\n";

    // Summary stats.
    const auto totalPasses = compiled.passes().size();
    const auto execPasses = compiled.executablePassCount();
    const auto culledCount = totalPasses - execPasses;
    const auto resourceCount = compiled.resourceLifetimes().size();
    const auto barrierCount = compiled.barrierPlan().size();
    const auto edgeCount = topology.edges.size();
    html << "<div class=\"summary\">\n";
    html << "<div class=\"stat\"><div class=\"num\">" << totalPasses << "</div>"
         << "<div class=\"label\">Total Passes</div></div>\n";
    html << "<div class=\"stat\"><div class=\"num\">" << execPasses << "</div>"
         << "<div class=\"label\">Executable</div></div>\n";
    html << "<div class=\"stat\"><div class=\"num\">" << culledCount << "</div>"
         << "<div class=\"label\">Culled</div></div>\n";
    html << "<div class=\"stat\"><div class=\"num\">" << resourceCount << "</div>"
         << "<div class=\"label\">Resources</div></div>\n";
    html << "<div class=\"stat\"><div class=\"num\">" << edgeCount << "</div>"
         << "<div class=\"label\">Dependency Edges</div></div>\n";
    html << "<div class=\"stat\"><div class=\"num\">" << barrierCount << "</div>"
         << "<div class=\"label\">Barriers</div></div>\n";
    html << "</div>\n";

    // Legend.
    html << "<div class=\"legend\">\n";
    html << "<div class=\"legend-item\"><span class=\"legend-dot\" style=\"background:"
         << dependencyKindColor(TopologyDependencyKind::Raw)
         << "\"></span>RAW (read-after-write)</div>\n";
    html << "<div class=\"legend-item\"><span class=\"legend-dot\" style=\"background:"
         << dependencyKindColor(TopologyDependencyKind::War)
         << "\"></span>WAR (write-after-read)</div>\n";
    html << "<div class=\"legend-item\"><span class=\"legend-dot\" style=\"background:"
         << dependencyKindColor(TopologyDependencyKind::Waw)
         << "\"></span>WAW (write-after-write)</div>\n";
    html
        << "<div class=\"legend-item\"><span style=\"color:#7c3aed;font-weight:bold\">- - →</span> async queue signal/wait</div>\n";
    html
        << "<div class=\"legend-item\"><span style=\"color:#2563eb;font-weight:bold\">━ ━ →</span> render-target dependency</div>\n";
    html << "</div>\n";

    // SVG graph.
    html << "<h2>Dependency Graph</h2>\n";
    html << "<div class=\"svg-container\">\n";
    html << renderSvgGraph(passInfos, topology);
    html << "\n</div>\n";

    html << "<h2>Native Subpass Topology <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"collapsible-content\">\n"
         << renderSubpassTopology(topology) << "\n</div>\n";

    html
        << "<h2>Async Queue Synchronization <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderQueueSyncTable(topology) << "\n</div>\n";

    html
        << "<h2>Render Target Dependencies <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderRenderTargetFlowTable(topology) << "\n</div>\n";

    // Passes table.
    html << "<h2>Passes <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderPassesTable(compiled);
    html << "\n</div>\n";

    // Resource lifetimes table.
    html << "<h2>Resource Lifetimes <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderResourceLifetimesTable(compiled);
    html << "\n</div>\n";

    html
        << "<h2>Resource Lifetime &amp; Reuse Timeline <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"svg-container collapsible-content\">\n";
    html << renderResourceLifetimeTimeline(compiled);
    html << "\n</div>\n";

    // Barriers table.
    html << "<h2>Barrier Plan <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderBarrierTable(compiled);
    html << "\n</div>\n";

    // Load/store table.
    html << "<h2>Attachment Load/Store <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderLoadStoreTable(compiled);
    html << "\n</div>\n";

    // Alias table.
    html << "<h2>Alias Plan <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderAliasTable(compiled);
    html << "\n</div>\n";

    // External resources table.
    html << "<h2>External Resources <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderExternalResourcesTable(compiled);
    html << "\n</div>\n";

    // Queue plan table.
    html << "<h2>Queue Plan <span class=\"toggle\">[click to collapse]</span></h2>\n";
    html << "<div class=\"table-container collapsible-content\">\n";
    html << renderQueuePlanTable(compiled);
    html << "\n</div>\n";

    // Collapsible JS.
    html << "<script>\n";
    html << "document.querySelectorAll('h2').forEach(function(h){\n";
    html << "  h.addEventListener('click',function(){\n";
    html << "    var n=h.nextElementSibling;\n";
    html << "    if(n&&n.classList.contains('collapsible-content')){\n";
    html
        << "      if(n.style.display==='none'){n.style.display='';h.querySelector('.toggle').textContent='[click to collapse]';}\n";
    html
        << "      else{n.style.display='none';h.querySelector('.toggle').textContent='[click to expand]';}\n";
    html << "    }\n";
    html << "  });\n";
    html << "});\n";
    html << "</script>\n";

    html << "</body>\n</html>\n";
    return html.str();
}

} // namespace

// ===========================================================================
// Public API implementation
//
// The topology extraction logic is defined as a lambda inside dumpTopologyHtml
// (the friend function). C++ lambdas defined within a friend function inherit
// the friend's access to FrameGraph's private members, so the lambda body can
// read passes_, textures_, buffers_, passTrace_, textureRecord(), and
// bufferRecord() directly. All rendering helpers above consume only the
// extracted TopologyData (public types) and the public CompiledFrameGraph API,
// so they do not need friend access.
// ===========================================================================

TopologyDumpResult dumpTopologyHtml(const FrameGraph& graph,
    const CompiledFrameGraph& compiled,
    const std::filesystem::path& outputPath)
{
    TopologyDumpResult result{};

    // --- Topology extraction (friend access via enclosing function) ---
    //
    // Mirrors the compiler's RAW/WAR/WAW dependency derivation
    // (render_graph.cpp:1016-1089). Unlike resourceLifetimes, this also
    // captures attachment writes so the graph reflects the full resource flow.
    auto extractTopology = [&]() -> TopologyData {
        TopologyData data{};

        const auto& compiledPasses = compiled.passes();
        const auto passCount = compiledPasses.size();
        data.passUses.resize(passCount);
        data.passInfos.resize(passCount);
        std::pmr::vector<std::size_t> execPosition(passCount, SIZE_MAX);
        for (std::size_t orderIndex = 0; orderIndex < compiled.executionOrder().size();
            ++orderIndex) {
            const auto passIndex = compiled.executionOrder()[orderIndex];
            if (passIndex < passCount) {
                execPosition[passIndex] = orderIndex;
            }
        }
        for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            const auto& pass = compiledPasses[passIndex];
            data.passInfos[passIndex] = PassNodeInfo{pass.name,
                pass.kind,
                pass.queue,
                pass.asyncComputeEligible,
                pass.culled,
                execPosition[passIndex]};
        }
        const auto& compiledUses = compiled.passResourceUses();
        for (std::size_t passIndex = 0; passIndex < compiledUses.size() && passIndex < passCount;
            ++passIndex) {
            for (const auto& use : compiledUses[passIndex]) {
                data.passUses[passIndex].push_back(
                    PassResourceUse{use.resourceName, use.kind, use.access, use.state});
            }
        }
        for (const auto& dependency : compiled.dependencies()) {
            data.edges.push_back(TopologyEdge{dependency.fromPass,
                dependency.toPass,
                dependency.resourceName,
                dependency.resourceKind,
                dependency.dependency,
                dependency.renderTargetDependency});
        }
        for (const auto& compiledFlow : compiled.renderTargetFlows()) {
            data.renderTargetFlows.push_back(RenderTargetFlow{compiledFlow.writerPass,
                compiledFlow.targetName,
                compiledFlow.role,
                compiledFlow.loadOp,
                compiledFlow.storeOp,
                compiledFlow.consumers});
        }
        for (const auto& compiledGroup : compiled.subpassTopologies()) {
            SubpassTopology group;
            group.groupName = compiledGroup.groupName;
            group.parentPass = compiledGroup.parentPass;
            for (const auto& compiledEntry : compiledGroup.entries) {
                group.entries.push_back(SubpassTopology::Entry{
                    compiledEntry.name, compiledEntry.writes, compiledEntry.inputs});
            }
            data.subpassGroups.push_back(std::move(group));
        }
        const auto effectiveQueue = [&](const PassNodeInfo& pass) {
            return pass.asyncComputeEligible ? QueueRole::Compute : QueueRole::Graphics;
        };
        for (const auto& edge : data.edges) {
            if (edge.fromPass >= passCount || edge.toPass >= passCount) {
                continue;
            }
            const auto fromQueue = effectiveQueue(data.passInfos[edge.fromPass]);
            const auto toQueue = effectiveQueue(data.passInfos[edge.toPass]);
            if (fromQueue != toQueue) {
                data.queueSyncEdges.push_back(QueueSyncEdge{
                    edge.fromPass, edge.toPass, edge.resourceName, fromQueue, toQueue});
            }
        }
        return data;

#if 0 // Legacy private-FrameGraph extraction retained temporarily for diff history.

        const auto passCount = graph.passes_.size();
        data.passUses.resize(passCount);
        data.passInfos.resize(passCount);

        // Build pass info from pass records + execution order.
        // Use compiled.passes()[i].culled (set by the compiler) rather than
        // passTrace_ (which is only populated during execute()).
        const auto& order = compiled.executionOrder();
        const auto& compiledPasses = compiled.passes();
        std::pmr::vector<std::size_t> execPosition(passCount, SIZE_MAX);
        for (std::size_t i = 0; i < order.size(); ++i) {
            execPosition[order[i]] = i;
        }
        for (std::size_t i = 0; i < passCount; ++i) {
            const auto& pr = graph.passes_[i];
            data.passInfos[i].label = pr.name;
            data.passInfos[i].kind = pr.kind;
            data.passInfos[i].queue = pr.queue;
            data.passInfos[i].asyncComputeEligible = pr.asyncComputeEligible;
            data.passInfos[i].culled = (i < compiledPasses.size())
                ? compiledPasses[i].culled : false;
            data.passInfos[i].executionOrder = execPosition[i];
        }

        // Dependency tracking state per resource handle, mirroring the compiler.
        const auto noPass = passCount;
        struct ResourceDependencyState {
            std::size_t lastWrite = 0;
            std::pmr::vector<std::size_t> lastReads;
            std::pmr::vector<std::size_t> pendingReadsBeforeFirstWrite;
            bool lastWriteWasAttachment = false;
        };

        auto addEdge = [&](std::size_t before, std::size_t after,
                           std::pmr::string resourceName, ResourceKind kind,
                           TopologyDependencyKind dep,
                           bool renderTargetDependency = false) {
            if (before == after
                || data.passInfos[before].culled
                || data.passInfos[after].culled) {
                return;
            }
            // Deduplicate identical edges (same from/to/resource).
            for (const auto& e : data.edges) {
                if (e.fromPass == before && e.toPass == after
                    && e.resourceName == resourceName) {
                    return;
                }
            }
            data.edges.push_back(TopologyEdge{before, after, std::move(resourceName), kind, dep,
                renderTargetDependency});
        };

        auto registerRead = [&](ResourceDependencyState& state, std::size_t passIndex,
                                std::pmr::string resourceName, ResourceKind kind) {
            if (state.lastWrite != noPass) {
                addEdge(state.lastWrite, passIndex, resourceName, kind, TopologyDependencyKind::Raw);
            } else {
                state.pendingReadsBeforeFirstWrite.push_back(passIndex);
            }
            state.lastReads.push_back(passIndex);
        };
        auto registerWrite = [&](ResourceDependencyState& state, std::size_t passIndex,
                                 std::pmr::string resourceName, ResourceKind kind,
                                 bool attachmentWrite = false) {
            if (state.lastWrite == noPass && !state.pendingReadsBeforeFirstWrite.empty()) {
                for (const auto pendingRead : state.pendingReadsBeforeFirstWrite) {
                    addEdge(passIndex, pendingRead, resourceName, kind, TopologyDependencyKind::Raw,
                        attachmentWrite);
                }
            } else {
                if (state.lastWrite != noPass) {
                    addEdge(state.lastWrite, passIndex, resourceName, kind, TopologyDependencyKind::Waw,
                        state.lastWriteWasAttachment || attachmentWrite);
                }
                for (const auto read : state.lastReads) {
                    addEdge(read, passIndex, resourceName, kind, TopologyDependencyKind::War);
                }
            }
            state.pendingReadsBeforeFirstWrite.clear();
            state.lastReads.clear();
            state.lastWrite = passIndex;
            state.lastWriteWasAttachment = attachmentWrite;
        };

        std::pmr::map<std::uint32_t, ResourceDependencyState> textureStates;
        std::pmr::map<std::uint32_t, ResourceDependencyState> bufferStates;
        for (const auto& tex : graph.textures_) {
            textureStates.emplace(tex.handle.id, ResourceDependencyState{noPass, {}, {}});
        }
        for (const auto& buf : graph.buffers_) {
            bufferStates.emplace(buf.handle.id, ResourceDependencyState{noPass, {}, {}});
        }

        for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            if (data.passInfos[passIndex].culled) {
                continue;
            }
            const auto& passRecord = graph.passes_[passIndex];
            auto& uses = data.passUses[passIndex];

            for (const auto& read : passRecord.reads) {
                const auto name = graph.textureRecord(read.handle).resolvedDesc().label;
                registerRead(textureStates[read.handle.id], passIndex, name, ResourceKind::Texture);
                uses.push_back(PassResourceUse{name, ResourceKind::Texture, ResourceAccess::Read, read.state});
            }
            for (const auto& read : passRecord.bufferReads) {
                const auto& rec = graph.bufferRecord(read.handle);
                std::pmr::string name = rec.buffer ? rec.buffer->desc().label : std::pmr::string{};
                registerRead(bufferStates[read.handle.id], passIndex, name, ResourceKind::Buffer);
                uses.push_back(PassResourceUse{name, ResourceKind::Buffer, ResourceAccess::Read, read.state});
            }
            for (const auto& write : passRecord.writes) {
                const auto name = graph.textureRecord(write.handle).resolvedDesc().label;
                registerWrite(textureStates[write.handle.id], passIndex, name, ResourceKind::Texture);
                uses.push_back(PassResourceUse{name, ResourceKind::Texture, ResourceAccess::Write, write.state});
            }
            // Attachment writes are the primary output of graphics passes. They
            // establish RAW dependencies just like explicit writes, but are NOT
            // tracked by resourceLifetimes -- so we include them here.
            for (const auto& att : passRecord.attachmentWrites) {
                const auto name = graph.textureRecord(att.handle).resolvedDesc().label;
                registerWrite(textureStates[att.handle.id], passIndex, name, ResourceKind::Texture, true);
                uses.push_back(PassResourceUse{name, ResourceKind::Texture, ResourceAccess::Write, att.state});
            }
            for (const auto& write : passRecord.bufferWrites) {
                const auto& rec = graph.bufferRecord(write.handle);
                std::pmr::string name = rec.buffer ? rec.buffer->desc().label : std::pmr::string{};
                registerWrite(bufferStates[write.handle.id], passIndex, name, ResourceKind::Buffer);
                uses.push_back(PassResourceUse{name, ResourceKind::Buffer, ResourceAccess::Write, write.state});
            }
        }

        // Attachment writer → dependent-pass view. This keeps render targets
        // distinct from generic sampled/storage resources in the HTML report.
        for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            const auto& passRecord = graph.passes_[passIndex];
            for (const auto& att : passRecord.attachmentWrites) {
                RenderTargetFlow flow{};
                flow.writerPass = passIndex;
                flow.targetName = graph.textureRecord(att.handle).resolvedDesc().label;
                flow.role = att.state == rhi::ResourceState::DepthAttachment
                    ? AttachmentRole::DepthAttachment : AttachmentRole::ColorAttachment;
                flow.loadOp = att.loadOp;
                flow.storeOp = att.storeOp;
                for (const auto& edge : data.edges) {
                    if (edge.fromPass == passIndex && edge.resourceName == flow.targetName) {
                        flow.consumers.push_back(edge.toPass);
                    }
                }
                data.renderTargetFlows.push_back(std::move(flow));
            }
        }

        // Native SubpassGraph lowering retains its subpass metadata on the
        // parent PassRecord. Copy it into public topology data for visualization.
        for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            const auto& passRecord = graph.passes_[passIndex];
            if (!passRecord.subpassGroup) continue;
            SubpassTopology group{};
            group.groupName = passRecord.subpassGroup->groupName;
            group.parentPass = passIndex;
            for (const auto& subpass : passRecord.subpassGroup->subpasses) {
                SubpassTopology::Entry entry{};
                entry.name = subpass.name;
                for (const auto index : subpass.writtenAttachmentIndices) {
                    if (index < passRecord.attachmentWrites.size()) {
                        entry.writes.push_back(graph.textureRecord(
                            passRecord.attachmentWrites[index].handle).resolvedDesc().label);
                    }
                }
                for (const auto index : subpass.inputAttachmentIndices) {
                    if (index < passRecord.attachmentWrites.size()) {
                        entry.inputs.push_back(graph.textureRecord(
                            passRecord.attachmentWrites[index].handle).resolvedDesc().label);
                    }
                }
                group.entries.push_back(std::move(entry));
            }
            data.subpassGroups.push_back(std::move(group));
        }

        // A cross-queue dependency is a timeline semaphore signal/wait in the
        // active executor. Async-eligible passes execute on the compute queue;
        // all others remain on graphics for this diagnostic plan.
        const auto effectiveQueue = [&](const PassNodeInfo& pass) {
            return pass.asyncComputeEligible ? QueueRole::Compute : QueueRole::Graphics;
        };
        for (const auto& edge : data.edges) {
            const auto fromQueue = effectiveQueue(data.passInfos[edge.fromPass]);
            const auto toQueue = effectiveQueue(data.passInfos[edge.toPass]);
            if (fromQueue == toQueue) continue;
            bool duplicate = false;
            for (const auto& existing : data.queueSyncEdges) {
                if (existing.fromPass == edge.fromPass && existing.toPass == edge.toPass
                    && existing.resourceName == edge.resourceName) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                data.queueSyncEdges.push_back(
                    QueueSyncEdge{edge.fromPass, edge.toPass, edge.resourceName, fromQueue, toQueue});
            }
        }

        return data;
#endif
    };

    // --- File writing ---
    //
    // Create parent directories first so a missing dir is reported before
    // we attempt to write.
    std::error_code ec;
    const auto parentPath = outputPath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath, ec);
        if (ec) {
            result.diagnostics = core::toPmrString(
                "failed to create directory '" + parentPath.string() + "': " + ec.message());
            return result;
        }
    }

    // Extract topology (friend access) and render HTML (public data only).
    const auto topology = extractTopology();
    const auto htmlContent = renderHtmlDocument(topology.passInfos, compiled, topology);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.good()) {
        result.diagnostics =
            core::toPmrString("failed to open file for writing: " + outputPath.string());
        return result;
    }
    out << htmlContent;
    out.close();
    if (out.fail()) {
        result.diagnostics = core::toPmrString("failed while writing file: " + outputPath.string());
        return result;
    }

    result.success = true;
    result.htmlPath = core::toPmrString(std::filesystem::absolute(outputPath, ec).string());
    if (ec) {
        // File was written successfully; just fall back to the original path.
        result.htmlPath = core::toPmrString(outputPath.string());
    }
    return result;
}

TopologyDumpResult dumpTopologyHtml(FrameGraph& graph, const std::filesystem::path& outputPath)
{
    const auto compiled = graph.compile();
    return dumpTopologyHtml(graph, compiled, outputPath);
}

} // namespace arp::render_graph
