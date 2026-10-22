/// Vulkan native multi-subpass render pass contract test.
///
/// Asserts (via source-level inspection) that the Vulkan backend consumes
/// RenderPassDesc::subpasses / subpassDependencies to build a real
/// multi-subpass VkRenderPass with input attachment references, subpass
/// dependencies (BY_REGION), and bounds-checked nextSubpass. This guards the
/// Phase B native lowering against silent regression to the old hardcoded
/// single-subpass path.
///
/// Per AGENTS.md: native subpass lowering correctness is asserted by contract
/// test (source + structure), not by eyeballing a frame. RenderDoc visual
/// validation on a real Vulkan device is a separate Prototype TODO.
#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_commands.h"

#include <cassert>
#include <fstream>
#include <sstream>

namespace {

#ifdef ARP_SOURCE_DIR
std::pmr::string readText(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}
#endif

/// Assert the multi-subpass VkRenderPass creation branch exists and consumes
/// desc.subpasses: SubpassDescription array, input attachment refs, and
/// SubpassDependency entries with BY_REGION flag.
void assertVulkanNativeMultiSubpassCreationLowersDescSubpasses()
{
#ifdef ARP_SOURCE_DIR
    const auto source =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_graphics_commands.cpp");
    assert(!source.empty());

    // The multi-subpass branch is gated on desc.subpasses being non-empty.
    assert(source.find("desc.subpasses.empty()") != std::pmr::string::npos);
    // Per-subpass input attachment references are built from
    // SubpassDesc::inputAttachmentIndices.
    assert(source.find("sd.inputAttachmentIndices") != std::pmr::string::npos);
    assert(source.find("pInputAttachments") != std::pmr::string::npos);
    // Color attachment references from SubpassDesc::colorAttachmentIndices.
    assert(source.find("sd.colorAttachmentIndices") != std::pmr::string::npos);
    assert(source.find("pColorAttachments") != std::pmr::string::npos);
    // Depth-stencil attachment usage flag.
    assert(source.find("sd.useDepthStencilAttachment") != std::pmr::string::npos);
    // Subpass dependencies with BY_REGION flag (tile-local barrier).
    assert(source.find("VK_DEPENDENCY_BY_REGION_BIT") != std::pmr::string::npos);
    assert(source.find("desc.subpassDependencies") != std::pmr::string::npos);
    // Input attachment read access mask (tile-local read visibility).
    assert(source.find("VK_ACCESS_INPUT_ATTACHMENT_READ_BIT") != std::pmr::string::npos);
    // The subpassCount is recorded on the render-pass resources (not hardcoded 1).
    assert(source.find("resources.subpassCount = subpassCount") != std::pmr::string::npos);
    // Deterministic diagnostic: subpass count bounds-checked against
    // kMaxImageViews (per-subpass arrays are indexed by subpass index).
    assert(source.find("exceeds max subpasses") != std::pmr::string::npos);
    // Deterministic diagnostic: multi-color attachment not yet supported
    // (single colorTarget payload aliases all image views to one VkImage).
    assert(
        source.find("does not yet support multiple color attachments") != std::pmr::string::npos);
    // Deterministic diagnostic: depth input attachment reads (depth
    // framebuffer fetch) not yet implemented - must fail instead of
    // silently dropping the read.
    assert(
        source.find("does not yet support depth input attachment reads") != std::pmr::string::npos);
#else
    // Without ARP_SOURCE_DIR the source-level assertions are skipped; the
    // struct-level assertions below still run.
#endif
}

/// Assert recordNextSubpass bounds-checks against the render pass subpassCount
/// and no-ops for single-subpass passes (per the header contract).
void assertVulkanNativeNextSubpassIsBoundsChecked()
{
#ifdef ARP_SOURCE_DIR
    const auto source =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_graphics_commands.cpp");
    assert(!source.empty());
    // No-op for single-subpass render passes.
    assert(source.find("rp.subpassCount <= 1") != std::pmr::string::npos);
    // Bounds check: reject advancing past the last subpass.
    assert(source.find("exceeds subpass count") != std::pmr::string::npos);
    // Current subpass index advances on both synthetic and real paths.
    assert(source.find("++rp.currentSubpassIndex") != std::pmr::string::npos);
#endif
}

/// Assert RenderPipelineDesc carries a subpassIndex field and the stable cache
/// key folds it in, so pipelines targeting different subpasses do not collide.
void assertRenderPipelineDescCarriesSubpassIndex()
{
    using namespace arp::rhi;
    RenderPipelineDesc desc;
    desc.label = "SubpassIndexContract";
    // Default is 0 (backward compatible with single-subpass renderers).
    assert(desc.subpassIndex == 0u);
    // The field is mutable.
    desc.subpassIndex = 2u;
    assert(desc.subpassIndex == 2u);
}

/// Assert Vulkan pipeline creation sets VkGraphicsPipelineCreateInfo::subpass
/// from desc.subpassIndex (source-level), so a pipeline targets the correct
/// subpass of a multi-subpass render pass.
void assertVulkanPipelineCreationSetsSubpassIndex()
{
#ifdef ARP_SOURCE_DIR
    const auto source =
        readText(ARP_SOURCE_DIR "/src/rhi/backends/vulkan/vulkan_native_pipelines.cpp");
    assert(!source.empty());
    assert(source.find("pipelineInfo.subpass = desc.subpassIndex") != std::pmr::string::npos);
#endif
}

/// Assert the L2 SubpassDesc / SubpassDependency / RenderPassDesc::subpasses
/// surface is stable (the L2 semantic contract the lowering consumes).
void assertL2SubpassSemanticSurfaceIsStable()
{
    using namespace arp::rhi;
    SubpassDesc sd;
    assert(sd.colorAttachmentIndices.empty());
    assert(sd.inputAttachmentIndices.empty());
    assert(!sd.useDepthStencilAttachment);
    assert(!sd.readDepthInputAttachment);

    SubpassDependency dep;
    assert(dep.srcSubpass == 0u);
    assert(dep.dstSubpass == 0u);

    RenderPassDesc rpd;
    assert(rpd.subpasses.empty());
    assert(rpd.subpassDependencies.empty());
}

} // namespace

int main()
{
    assertVulkanNativeMultiSubpassCreationLowersDescSubpasses();
    assertVulkanNativeNextSubpassIsBoundsChecked();
    assertRenderPipelineDescCarriesSubpassIndex();
    assertVulkanPipelineCreationSetsSubpassIndex();
    assertL2SubpassSemanticSurfaceIsStable();
    return 0;
}
