// Render graph topology HTML dump contract tests.
//
// Validates the self-contained HTML export of the compiled render graph:
//   1. Minimal graph (Clear -> Present) produces valid HTML with SVG.
//   2. RAW dependency edges are derived and rendered.
//   3. Culled passes are displayed with greyed-out styling.
//   4. Attachment writes produce dependency edges (not just explicit writes).
//   5. Explicit convenience overload compiles and dumps in one call.
//   6. Invalid output path returns {success=false} with diagnostics.
//   7. Pass/resource names with HTML special characters are escaped.
//   8. Generated HTML is offline self-contained (no http(s) references).
//
// All tests use the Null RHI backend -- no live GPU required, CI-safe.

#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/render_graph/render_graph_topology_dump.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/resource_state_sequence.h"

#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace {

namespace fs = std::filesystem;

/// Monotonic counter for unique temp file names within this process.
std::string uniqueId()
{
    static unsigned long counter = 0;
    ++counter;
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(now) + "_" + std::to_string(counter);
}

arp::rhi::TextureDesc textureDesc(std::pmr::string label,
    arp::rhi::Extent3D extent,
    arp::rhi::TextureFormat format,
    arp::rhi::TextureUsage usage,
    arp::rhi::ResourceState initialState = arp::rhi::ResourceState::Undefined)
{
    return arp::rhi::TextureDesc{
        std::move(label),
        extent,
        format,
        usage,
        arp::rhi::MemoryUsage::GpuOnly,
        initialState,
        1,
    };
}

arp::rhi::BufferDesc bufferDesc(std::pmr::string label,
    std::uint64_t sizeBytes,
    arp::rhi::ResourceUsage usage,
    arp::rhi::ResourceState initialState = arp::rhi::ResourceState::Undefined)
{
    return arp::rhi::BufferDesc{
        std::move(label),
        sizeBytes,
        usage,
        arp::rhi::MemoryUsage::GpuOnly,
        initialState,
    };
}

/// Read an entire file into a std::string. Returns empty string on failure.
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Check whether a string contains a substring (case-sensitive).
bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// Check whether a string contains a substring (case-insensitive).
bool containsIgnoreCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    if (haystack.size() < needle.size())
        return false;
    auto toLower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (toLower(haystack[i + j]) != toLower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

/// Create a unique temp file path for this test run.
fs::path makeTempHtmlPath(const char* suffix)
{
    auto base = fs::temp_directory_path();
    auto name = std::string("arp_rg_topology_") + uniqueId() + "_" + suffix + ".html";
    return base / name;
}

// ===========================================================================
// Test 1: Minimal graph dump
// ===========================================================================

void test_dumps_minimal_graph()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTexture(textureDesc("HdrColor",
        {1920, 1080, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("Clear")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.addPass("Present")
        .read(color, arp::rhi::ResourceState::Present)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    const auto path = makeTempHtmlPath("minimal");
    const auto result = arp::render_graph::dumpTopologyHtml(graph, compiled, path);

    assert(result.success);
    assert(fs::exists(path));
    assert(fs::file_size(path) > 0);

    const auto html = readFile(path);
    assert(contains(html, "<!DOCTYPE html>"));
    assert(contains(html, "<svg"));
    assert(contains(html, "Clear"));
    assert(contains(html, "Present"));
    assert(contains(html, "HdrColor"));

    fs::remove(path);
    std::printf("[ok] test_dumps_minimal_graph\n");
}

// ===========================================================================
// Test 2: RAW dependency edge rendering
// ===========================================================================

void test_dumps_dependency_edges()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    const auto color = graph.createTexture(textureDesc("DependencyColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    // SceneColor writes, ToneMap reads, Present reads.
    // This creates RAW edges: SceneColor -> ToneMap, SceneColor -> Present.
    graph.addPass("SceneColor")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.addPass("ToneMap")
        .read(color, arp::rhi::ResourceState::ShaderRead)
        .kind(arp::render_graph::PassKind::Compute)
        .queue(arp::render_graph::QueueRole::Compute)
        .asyncComputeEligible(true)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.addPass("Present")
        .read(color, arp::rhi::ResourceState::Present)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    const auto path = makeTempHtmlPath("edges");
    const auto result = arp::render_graph::dumpTopologyHtml(graph, compiled, path);

    assert(result.success);

    const auto html = readFile(path);
    // SVG path elements are the dependency edges.
    assert(contains(html, "<path"));
    // The resource name should appear on edge labels.
    assert(contains(html, "DependencyColor"));
    // RAW dependency kind should be mentioned (in legend or tooltip).
    assert(containsIgnoreCase(html, "RAW"));
    // All three passes must be present.
    assert(contains(html, "SceneColor"));
    assert(contains(html, "ToneMap"));
    assert(contains(html, "Present"));
    // Rich topology views: the exporter distinguishes attachment flow,
    // native subpass topology, and cross-queue semaphore handoffs.
    assert(contains(html, "Native Subpass Topology"));
    assert(contains(html, "Async Queue Synchronization"));
    assert(contains(html, "Render Target Dependencies"));
    assert(contains(html, "queue sync"));

    fs::remove(path);
    std::printf("[ok] test_dumps_dependency_edges\n");
}

// ===========================================================================
// Test 3: Culled pass display
// ===========================================================================

void test_dumps_culled_pass()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    graph.setPassCullingEnabled(true);

    const auto color = graph.createTexture(textureDesc("LiveColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    const auto dead = graph.createTexture(textureDesc("DeadColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    // Live pass chain: Clear -> Present (reads color).
    graph.addPass("Clear")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.addPass("Present")
        .read(color, arp::rhi::ResourceState::Present)
        .execute([](arp::render_graph::RenderContext&) {});

    // Dead pass: writes to a texture nobody reads -> culled.
    graph.addPass("DeadPass")
        .writeAttachment(dead,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    // DeadPass should be culled.
    bool foundCulled = false;
    for (const auto& p : compiled.passes()) {
        if (p.name == std::pmr::string("DeadPass")) {
            assert(p.culled);
            foundCulled = true;
        }
    }
    assert(foundCulled);

    const auto path = makeTempHtmlPath("culled");
    const auto result = arp::render_graph::dumpTopologyHtml(graph, compiled, path);
    assert(result.success);

    const auto html = readFile(path);
    // The culled pass name must appear in the HTML.
    assert(contains(html, "DeadPass"));
    // The culled badge class must be present.
    assert(contains(html, "badge-culled"));
    // The culled row class must be present.
    assert(contains(html, "class=\"culled\""));

    fs::remove(path);
    std::printf("[ok] test_dumps_culled_pass\n");
}

// ===========================================================================
// Test 4: Attachment write edges
// ===========================================================================

void test_dumps_attachment_writes()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    const auto color = graph.createTexture(textureDesc("AttachmentColor",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    // Graphics pass writes via writeAttachment (not explicit write()).
    graph.addPass("GeometryPass")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .executeRenderPass([](arp::render_graph::RenderPassContext&) {});

    // Downstream pass reads the attachment output.
    graph.addPass("PostProcess")
        .read(color, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    const auto path = makeTempHtmlPath("attachment");
    const auto result = arp::render_graph::dumpTopologyHtml(graph, compiled, path);
    assert(result.success);

    const auto html = readFile(path);
    // There must be an edge (SVG path) connecting GeometryPass to PostProcess
    // through the AttachmentColor resource.
    assert(contains(html, "<path"));
    assert(contains(html, "AttachmentColor"));
    assert(contains(html, "GeometryPass"));
    assert(contains(html, "PostProcess"));

    // The resource should also appear in the lifetimes table with a producer.
    assert(contains(html, "GeometryPass"));

    fs::remove(path);
    std::printf("[ok] test_dumps_attachment_writes\n");
}

// ===========================================================================
// Test 5: Explicit convenience-overload dump
// ===========================================================================

void test_convenience_overload_dumps()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto color = graph.createTexture(textureDesc("ExplicitApiColor",
        {32, 32, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("ExplicitApiClear")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("ExplicitApiPresent")
        .read(color, arp::rhi::ResourceState::Present)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto path = makeTempHtmlPath("explicit_api");
    const auto result = arp::render_graph::dumpTopologyHtml(graph, path);
    assert(result.success);
    const auto html = readFile(path);
    assert(contains(html, "ExplicitApiClear"));
    assert(contains(html, "ExplicitApiPresent"));
    fs::remove(path);
    std::printf("[ok] test_convenience_overload_dumps\n");
}

// ===========================================================================
// Test 6: Invalid path returns failure
// ===========================================================================

void test_invalid_path_returns_failure()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    const auto color = graph.createTexture(textureDesc("InvalidPathColor",
        {16, 16, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("Single")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();

    // On Windows, a path with a NUL char in a directory component or an
    // illegal character makes the path invalid. Use a path with an embedded
    // NUL-like invalid component via a colon in a directory name on Windows,
    // or a deeply non-existent unwritable path. We use a path where the parent
    // directory is itself a file (not a directory), which makes
    // create_directories fail.
    //
    // Simpler robust approach: create a file at the target parent path, so
    // create_directories(parent) fails because parent exists as a file.
    const auto blockingFile = fs::temp_directory_path() / "arp_rg_blocker_file";
    {
        std::ofstream f(blockingFile);
        f << "blocker";
    }
    assert(fs::exists(blockingFile));
    assert(fs::is_regular_file(blockingFile));

    // Try to dump to blockingFile/sub.html -- parent is a file, not a dir.
    const auto badPath = blockingFile / "sub.html";
    const auto result = arp::render_graph::dumpTopologyHtml(graph, compiled, badPath);

    assert(!result.success);
    assert(!result.diagnostics.empty());

    fs::remove(blockingFile);
    std::printf("[ok] test_invalid_path_returns_failure\n");
}

// ===========================================================================
// Test 7: HTML escaping
// ===========================================================================

void test_html_escaping()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    // Resource name with HTML special characters.
    const auto color = graph.createTexture(textureDesc("<Color&Res>",
        {16, 16, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("<Script>Pass")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.addPass("\"Quote\"Pass")
        .read(color, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    const auto path = makeTempHtmlPath("escaping");
    const auto result = arp::render_graph::dumpTopologyHtml(graph, compiled, path);
    assert(result.success);

    const auto html = readFile(path);
    // Escaped forms must be present.
    assert(contains(html, "&lt;Script&gt;Pass"));
    assert(contains(html, "&lt;Color&amp;Res&gt;"));
    assert(contains(html, "&quot;Quote&quot;Pass"));
    // Raw unescaped forms must NOT be present (no XSS / broken HTML).
    assert(!contains(html, "<Script>Pass"));
    assert(!contains(html, "<Color&Res>"));

    fs::remove(path);
    std::printf("[ok] test_html_escaping\n");
}

// ===========================================================================
// Test 8: Offline self-containment
// ===========================================================================

void test_offline_self_contained()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    const auto color = graph.createTexture(textureDesc("OfflineColor",
        {16, 16, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("OfflineClear")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.addPass("OfflinePresent")
        .read(color, arp::rhi::ResourceState::Present)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    const auto path = makeTempHtmlPath("offline");
    const auto result = arp::render_graph::dumpTopologyHtml(graph, compiled, path);
    assert(result.success);

    const auto html = readFile(path);

    // Must NOT contain external resource references. The SVG xmlns namespace
    // declaration (http://www.w3.org/2000/svg) is allowed -- it is a W3C
    // namespace identifier, not a network URL, and does not cause any fetch.
    // We check for actual external resource loading patterns instead.
    assert(!contains(html, "<link "));                // no external CSS via <link>
    assert(!contains(html, "<img "));                 // no external images
    assert(!containsIgnoreCase(html, "src=\"http"));  // no remote script/img src
    assert(!containsIgnoreCase(html, "href=\"http")); // no remote link href
    assert(!containsIgnoreCase(html, "@import"));     // no CSS @import
    assert(!containsIgnoreCase(html, "url(http"));    // no CSS url() to remote
    assert(!containsIgnoreCase(html, "url('http"));
    assert(!containsIgnoreCase(html, "url(\"http"));
    // The only allowed http(s) occurrence is the SVG xmlns namespace.
    // Verify it is the xmlns declaration, not something else.
    {
        std::string h = html;
        std::string lower;
        lower.reserve(h.size());
        for (char c : h)
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string::size_type pos = 0;
        while ((pos = lower.find("http", pos)) != std::string::npos) {
            // Check this is the xmlns="http://www.w3.org/2000/svg" declaration.
            std::string::size_type nsStart = lower.rfind("xmlns", pos);
            std::string::size_type nsEnd = lower.find("\"", pos);
            // If "xmlns" appears shortly before "http" and the URL is the W3C SVG
            // namespace, it's allowed.
            bool isSvgNs = (nsStart != std::string::npos && pos - nsStart < 10)
                && (lower.find("www.w3.org/2000/svg", pos) != std::string::npos);
            assert(isSvgNs);
            pos = lower.find("\"", pos);
            if (pos == std::string::npos)
                break;
            ++pos;
        }
    }

    // Must contain inline style and script (self-contained).
    assert(contains(html, "<style>"));
    assert(contains(html, "<script>"));

    fs::remove(path);
    std::printf("[ok] test_offline_self_contained\n");
}

/// FrameGraph reset must invalidate frame-local subpass lowering state while
/// preserving the authored SubpassGraph declarations.
void test_subpass_recompiles_after_reset()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTransientTexture({
        "ResetColor",
        {32, 32, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto depth = graph.createTransientTexture({
        "ResetDepth",
        {32, 32, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    auto& group = graph.addSubpassGraph("ResetSubpass");
    const auto colorAttachment = group.attachment(color,
        arp::rhi::ResourceState::ColorAttachment,
        arp::render_graph::AttachmentRole::ColorAttachment);
    const auto depthAttachment = group.attachment(depth,
        arp::rhi::ResourceState::DepthAttachment,
        arp::render_graph::AttachmentRole::DepthAttachment);

    arp::rhi::ShaderPackageDesc inputShaderPackage;
    inputShaderPackage.label = "ResetSubpassInputShader";
    inputShaderPackage.slangModulePath = "reset_subpass_input.slang";
    inputShaderPackage.entrypoints.push_back(
        {"reset_subpass_input.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    inputShaderPackage.entrypoints.push_back(
        {"reset_subpass_input.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    inputShaderPackage.artifacts.spirvPath = "reset_subpass_input.spv";
    inputShaderPackage.artifacts.mslPath = "reset_subpass_input.metal";
    inputShaderPackage.artifacts.reflectionPath = "reset_subpass_input.reflection.yaml";
    inputShaderPackage.reflection.descriptorBindings.push_back({0,
        0,
        arp::rhi::DescriptorType::InputAttachment,
        arp::rhi::ShaderStage::Fragment,
        1,
        depthAttachment.index,
        true});

    arp::rhi::DescriptorLayoutDesc inputLayout;
    inputLayout.name = "ResetSubpassInputSet";
    inputLayout.bindings.push_back(
        {0, arp::rhi::DescriptorType::InputAttachment, arp::rhi::ShaderStage::Fragment});
    auto descriptorArena = device->createDescriptorArena({"ResetSubpassInputArena", 1, 1});
    assert(descriptorArena);
    auto inputSet = descriptorArena->allocateDescriptorSet({"ResetSubpassInputSet", inputLayout});
    assert(inputSet);
    arp::rhi::DescriptorSet* inputSets[] = {inputSet.get()};
    std::uint32_t executionCount = 0;
    group.subpass("WriteDepth")
        .writeAttachment(depthAttachment, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::Store)
        .executeRenderPass([&](arp::render_graph::RenderPassContext&) {
            ++executionCount;
        });
    group.subpass("ReadDepth")
        .readInputAttachment(depthAttachment, 0u, 0u)
        .writeAttachment(colorAttachment, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::Store)
        .executeRenderPass([&](arp::render_graph::RenderPassContext& context) {
            const auto written =
                context.writeSubpassInputDescriptors(inputShaderPackage, inputSets, 1u);
            assert(written.success);
            ++executionCount;
        });

    graph.compile();
    graph.execute();
    assert(executionCount == 2u);

    graph.reset();
    graph.createTransientTexture({
        "ResetColor",
        {32, 32, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    graph.createTransientTexture({
        "ResetDepth",
        {32, 32, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    graph.compile();
    graph.execute();
    assert(executionCount == 4u);
    std::printf("[ok] test_subpass_recompiles_after_reset\n");
}

} // namespace

static void test_transition_consumes_previous_scope()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraph graph(*device);
    const auto color = graph.createTexture(textureDesc("OrderedSource",
        {8, 8, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled));
    graph.addPass("Clear")
        .writeAttachment(
            color, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
        .execute([](render_graph::RenderContext&) {});
    graph.addPass("Sample")
        .read(color, rhi::ResourceState::ShaderRead)
        .externalSideEffects()
        .execute([](render_graph::RenderContext&) {});
    graph.addPass("ClearAgain")
        .writeAttachment(
            color, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
        .externalSideEffects()
        .execute([](render_graph::RenderContext&) {});
    const auto compiled = graph.compile();
    unsigned transitions = 0;
    for (const auto& barrier : compiled.barrierPlan()) {
        if (barrier.kind != render_graph::ResourceKind::Texture)
            continue;
        const rhi::ResourceBarrierDesc desc{graph.texture(color).id(),
            rhi::defaultTextureSubresourceRange(graph.texture(color).desc()),
            barrier.before,
            barrier.after,
            barrier.srcStage,
            barrier.dstStage,
            barrier.srcAccess,
            barrier.dstAccess};
        assert(
            !rhi::semantics::RecordedResourceStateSequence::validateBarrier(desc, barrier.before));
        ++transitions;
    }
    assert(transitions == 3);
    graph.execute(compiled); // Null hidden-frame replay uses the same scopes.
}

int main()
{
    test_transition_consumes_previous_scope();
    test_dumps_minimal_graph();
    test_dumps_dependency_edges();
    test_dumps_culled_pass();
    test_dumps_attachment_writes();
    test_convenience_overload_dumps();
    test_invalid_path_returns_failure();
    test_html_escaping();
    test_offline_self_contained();
    test_subpass_recompiles_after_reset();
    return 0;
}
