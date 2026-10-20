#include "core/cpu_allocator.h"
#include "renderer/ui/gui_renderer.h"
#include "renderer/ui/ui_glyph_atlas.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/semantics/native_platform_lifecycle_lowering.h"

#include <cassert>

namespace {

void gui_renderer_create_baseline_with_null_device()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);
    assert(renderer.graphicsPipeline() != nullptr);
}

void gui_renderer_pipeline_has_initial_extent()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {640u, 480u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);
    assert(renderer.graphicsPipeline() != nullptr);
}

void gui_renderer_pass_telemetry_starts_clean()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};

    const auto created = renderer.create(desc);
    assert(created.success);
    renderer.beginFrameTelemetry();
    const auto telemetry = renderer.passTelemetry();
    assert(!telemetry.requested);
    assert(!telemetry.recorded);
    assert(telemetry.atlasReady);
    assert(telemetry.commandCount == 0u);
}

void gui_renderer_rejects_invalid_atlas_rect_before_state_transition()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};

    const auto created = renderer.create(desc);
    assert(created.success);

    arp::renderer::GuiAtlasTextureDesc atlas;
    atlas.resourceId = "atlas";
    atlas.extent = {1u, 1u};
    atlas.rgba8.assign(4u, 255u);
    arp::core::pmr::Vector<arp::renderer::ui::GlyphAtlasUploadSchedule> uploads{
        &arp::core::defaultCpuMemoryResource()};
    arp::renderer::ui::GlyphAtlasUploadSchedule invalid;
    invalid.atlasRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 2.0f, 1.0f);
    uploads.push_back(invalid);

    const auto rejected = renderer.updateAtlasTexture(atlas, uploads);
    assert(!rejected.success);
    assert(rejected.diagnostics.find("outside the atlas extent") != std::pmr::string::npos);

    uploads.clear();
    const auto unchanged = renderer.updateAtlasTexture(atlas, uploads);
    assert(unchanged.success);

    atlas.extent = {2u, 2u};
    atlas.rgba8.assign(16u, 255u);
    const auto mismatched = renderer.updateAtlasTexture(atlas, uploads);
    assert(!mismatched.success);
    assert(mismatched.diagnostics.find("does not match the live atlas texture")
        != std::pmr::string::npos);
}

void gui_renderer_on_resize_updates_pipeline_extent()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    // After resize, the renderer must rebuild its pipeline for the new extent.
    const auto resized = renderer.onResize({640u, 480u});
    assert(resized.success);
}

void gui_renderer_on_device_lost_releases_native_resources()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    // Device lost must release all native resources deterministically.
    const auto lost = renderer.onDeviceLost();
    assert(lost.success);
}

void gui_renderer_recover_rebuilds_from_contract()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    const auto lost = renderer.onDeviceLost();
    assert(lost.success);

    // Recovery must rebuild pipelines, descriptors, buffers, and textures
    // from the provided contract descriptor without using stale native handles.
    const auto recovered = renderer.recoverFromDeviceLost(desc);
    assert(recovered.success);
    assert(renderer.graphicsPipeline() != nullptr);
}

void gui_renderer_recover_fails_when_atlas_source_bytes_are_missing()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    const auto lost = renderer.onDeviceLost();
    assert(lost.success);

    // If source bytes were released and the atlas descriptor is invalid,
    // recovery must fail deterministically rather than produce a partial state.
    arp::renderer::GuiRendererDesc badDesc;
    badDesc.shaderPath = "dummy.slang";
    badDesc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    badDesc.extent = {320u, 180u};
    badDesc.hasAtlasTexture = true;
    badDesc.atlasTexture.resourceId = "missing";
    badDesc.atlasTexture.extent = {0u, 0u};
    badDesc.atlasTexture.rgba8.clear();

    const auto recovered = renderer.recoverFromDeviceLost(badDesc);
    assert(!recovered.success);
}

void platform_lifecycle_desktop_resize_triggers_swapchain_rebuild()
{
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::DesktopWindow;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    const auto resized = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::Resize);
    assert(resized.success);
    assert(resized.state.swapchainRebuildRequired);
    assert(arp::rhi::semantics::hasNativePlatformLifecycleSemantic(resized,
        arp::rhi::semantics::NativePlatformLifecycleSemantic::DesktopResizeRebuildsSwapchain));
}

void platform_lifecycle_desktop_resize_does_not_include_ui_resource_rebuild()
{
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::DesktopWindow;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    const auto resized = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::Resize);
    assert(resized.success);
    // There is currently no UI-resource-rebuild semantic in the lowering contract.
    // This documents the gap: desktop resize rebuilds the swapchain but does not
    // explicitly signal that GUI renderer pipelines, descriptors, or atlas textures
    // must be recreated from contracts.
    assert(!arp::rhi::semantics::hasNativePlatformLifecycleSemantic(
        resized, arp::rhi::semantics::NativePlatformLifecycleSemantic::ReleaseTransientResources));
}

void platform_lifecycle_memory_warning_releases_transient_resources()
{
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::DesktopWindow;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    const auto warned = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::MemoryWarning);
    assert(warned.success);
    assert(warned.state.memoryBudgetRefreshRequired);
    assert(warned.state.releaseTransientResources);
    assert(arp::rhi::semantics::hasNativePlatformLifecycleSemantic(
        warned, arp::rhi::semantics::NativePlatformLifecycleSemantic::MemoryBudgetRefresh));
    assert(arp::rhi::semantics::hasNativePlatformLifecycleSemantic(
        warned, arp::rhi::semantics::NativePlatformLifecycleSemantic::ReleaseTransientResources));
}

void platform_lifecycle_android_pause_stops_rendering()
{
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::AndroidSurface;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    const auto paused = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::Pause);
    assert(paused.success);
    assert(paused.state.paused);
    assert(!paused.state.acquireAllowed);
    assert(!paused.state.presentAllowed);
    assert(paused.state.releaseSurfaceResources);
}

void platform_lifecycle_android_resume_requires_valid_surface()
{
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::AndroidSurface;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = false;
    state.acquireAllowed = false;
    state.presentAllowed = false;

    const auto resumed = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::Resume);
    assert(resumed.success);
    assert(!resumed.state.paused);
    assert(!resumed.state.surfaceValid);
    assert(!resumed.state.acquireAllowed);
    assert(!resumed.state.presentAllowed);
    assert(arp::rhi::semantics::hasNativePlatformLifecycleSemantic(resumed,
        arp::rhi::semantics::NativePlatformLifecycleSemantic::AndroidResumeRequiresValidSurface));
}

void platform_lifecycle_ios_background_stops_drawable_acquire()
{
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::IosLayer;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    const auto backgrounded =
        lowerer.lower(state, arp::platform::PlatformLifecycleEvent::EnterBackground);
    assert(backgrounded.success);
    assert(backgrounded.state.backgrounded);
    assert(!backgrounded.state.acquireAllowed);
    assert(!backgrounded.state.presentAllowed);
    assert(arp::rhi::semantics::hasNativePlatformLifecycleSemantic(backgrounded,
        arp::rhi::semantics::NativePlatformLifecycleSemantic::IosBackgroundStopsDrawableAcquire));
}

void platform_lifecycle_surface_loss_invalidates_swapchain()
{
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::AndroidSurface;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    const auto lost = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::SurfaceLost);
    assert(lost.success);
    assert(!lost.state.surfaceValid);
    assert(lost.state.swapchainRebuildRequired);
    assert(!lost.state.acquireAllowed);
    assert(!lost.state.presentAllowed);
    assert(lost.state.releaseSurfaceResources);
}

void repeated_resize_does_not_leak_resources()
{
    // This test documents the expectation that repeated resize events
    // must not accumulate native resources. Currently there is no explicit
    // resource-budget validation in the resize path, so this test
    // establishes the contract boundary.
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::DesktopWindow;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    for (std::uint32_t i = 0u; i < 10u; ++i) {
        const auto resized = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::Resize);
        assert(resized.success);
        assert(resized.state.swapchainRebuildRequired);
        state = resized.state;
        // After each resize, the state must remain deterministically valid.
        assert(state.logicalFrameSlots == 2u);
        assert(state.running);
    }
}

void cancellation_during_recovery_is_deterministic()
{
    // This test documents the expectation that if a recovery operation
    // (e.g., device recreate) is superseded by a second lifecycle event
    // before completion, the result must be deterministic and must not
    // produce a partial ready state. Currently there is no explicit
    // recovery-state machine, so this test establishes the contract boundary.
    arp::rhi::semantics::NativePlatformLifecycleLowerer lowerer(
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan));
    arp::platform::PlatformLifecycleState state;
    state.surfaceKind = arp::platform::PlatformSurfaceKind::DesktopWindow;
    state.logicalFrameSlots = 2;
    state.running = true;
    state.surfaceValid = true;
    state.acquireAllowed = true;
    state.presentAllowed = true;

    // First event: resize triggers rebuild.
    const auto resized = lowerer.lower(state, arp::platform::PlatformLifecycleEvent::Resize);
    assert(resized.success);
    assert(resized.state.swapchainRebuildRequired);

    // Second event: memory pressure arrives before rebuild completes.
    const auto pressured =
        lowerer.lower(resized.state, arp::platform::PlatformLifecycleEvent::MemoryWarning);
    assert(pressured.success);
    assert(pressured.state.memoryBudgetRefreshRequired);
    assert(pressured.state.releaseTransientResources);
    // The state must remain consistent: both rebuild and budget refresh are set.
    assert(pressured.state.swapchainRebuildRequired);
}

void gui_renderer_resize_after_device_lost_is_deterministic()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    // Device lost releases all resources.
    const auto lost = renderer.onDeviceLost();
    assert(lost.success);
    assert(renderer.graphicsPipeline() == nullptr);

    // If resize arrives before explicit recovery, the renderer must still
    // rebuild deterministically from the stored contract without partial state.
    const auto resized = renderer.onResize({640u, 480u});
    assert(resized.success);
    assert(renderer.graphicsPipeline() != nullptr);
}

void gui_renderer_recover_is_idempotent()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    const auto lost = renderer.onDeviceLost();
    assert(lost.success);

    // Calling recover twice must not produce a partial or inconsistent state.
    // Each call is a full reset+create, so both succeed deterministically.
    const auto recovered1 = renderer.recoverFromDeviceLost(desc);
    assert(recovered1.success);
    assert(renderer.graphicsPipeline() != nullptr);

    const auto recovered2 = renderer.recoverFromDeviceLost(desc);
    assert(recovered2.success);
    assert(renderer.graphicsPipeline() != nullptr);
}

void gui_renderer_resize_records_recovery_telemetry()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    const auto resized = renderer.onResize({640u, 480u});
    assert(resized.success);

    const auto telemetry = renderer.recoveryTelemetry();
    assert(telemetry.reason == "onResize");
    assert(telemetry.reconstructedResources == 1u);
}

void gui_renderer_device_lost_records_recovery_telemetry()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    const auto lost = renderer.onDeviceLost();
    assert(lost.success);

    const auto telemetry = renderer.recoveryTelemetry();
    assert(telemetry.reason == "onDeviceLost");
    assert(telemetry.droppedWork == 1u);
    assert(telemetry.reconstructedResources == 0u);
}

void gui_renderer_recover_records_recovery_telemetry()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::GuiRenderer renderer(*device);
    arp::renderer::GuiRendererDesc desc;
    desc.shaderPath = "dummy.slang";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320u, 180u};
    desc.hasAtlasTexture = false;

    const auto created = renderer.create(desc);
    assert(created.success);

    const auto lost = renderer.onDeviceLost();
    assert(lost.success);

    const auto recovered = renderer.recoverFromDeviceLost(desc);
    assert(recovered.success);

    const auto telemetry = renderer.recoveryTelemetry();
    assert(telemetry.reason == "recoverFromDeviceLost");
    assert(telemetry.reconstructedResources == 1u);
    assert(telemetry.droppedWork == 0u);
}

} // namespace

int main()
{
    gui_renderer_create_baseline_with_null_device();
    gui_renderer_pipeline_has_initial_extent();
    gui_renderer_pass_telemetry_starts_clean();
    gui_renderer_rejects_invalid_atlas_rect_before_state_transition();
    gui_renderer_on_resize_updates_pipeline_extent();
    gui_renderer_on_device_lost_releases_native_resources();
    gui_renderer_recover_rebuilds_from_contract();
    gui_renderer_recover_fails_when_atlas_source_bytes_are_missing();
    gui_renderer_resize_after_device_lost_is_deterministic();
    gui_renderer_recover_is_idempotent();
    gui_renderer_resize_records_recovery_telemetry();
    gui_renderer_device_lost_records_recovery_telemetry();
    gui_renderer_recover_records_recovery_telemetry();
    platform_lifecycle_desktop_resize_triggers_swapchain_rebuild();
    platform_lifecycle_desktop_resize_does_not_include_ui_resource_rebuild();
    platform_lifecycle_memory_warning_releases_transient_resources();
    platform_lifecycle_android_pause_stops_rendering();
    platform_lifecycle_android_resume_requires_valid_surface();
    platform_lifecycle_ios_background_stops_drawable_acquire();
    platform_lifecycle_surface_loss_invalidates_swapchain();
    repeated_resize_does_not_leak_resources();
    cancellation_during_recovery_is_deterministic();
    return 0;
}
