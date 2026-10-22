#include "core/memory/cpu_allocator.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

arp::renderer::CustomWorkloadPayloadRef makePayload(arp::renderer::CustomWorkloadPayloadKind kind)
{
    static int payloadToken = 0;
    return {
        kind,
        &payloadToken,
        "tests/custom_render_frame_workload",
        1u,
    };
}

template <typename Payload>
arp::renderer::CustomWorkloadPayloadRef makeTypedPayload(
    arp::renderer::CustomWorkloadPayloadKind kind, const Payload& payload)
{
    return arp::renderer::customTypedWorkloadPayloadRef(
        kind, payload, "tests/custom_render_frame_workload", 1u)
        .erase();
}

arp::renderer::CustomRenderFrameWorkloadDesc makeBaseDesc()
{
    using namespace arp::renderer;

    CustomRenderFrameWorkloadDesc desc;
    desc.extent = arp::rhi::Extent2D{640, 360};
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.frameSerial = 42u;
    desc.backbufferState = arp::rhi::ResourceState::Present;
    desc.gui = CustomWorkloadSection::enabled(makePayload(CustomWorkloadPayloadKind::GuiDrawData));
    return desc;
}

arp::renderer::CustomResourceContract makeExplicitTextureUpload()
{
    using namespace arp::renderer;

    CustomResourceContract resource;
    resource.name = "SceneAlbedo";
    resource.semantic = CustomResourceSemantic::ExplicitTextureUpload;
    resource.uploadMetadata = CustomUploadMetadataSource::ExplicitContract;
    resource.extent = arp::rhi::Extent2D{4, 4};
    resource.format = arp::rhi::TextureFormat::Rgba8Unorm;
    resource.bytesPerRow = 16u;
    resource.rowsPerImage = 4u;
    return resource;
}

arp::renderer::CustomResourceContract makePrototypeLegacyFallback()
{
    using namespace arp::renderer;

    CustomResourceContract resource;
    resource.name = "PrototypeLegacyPreviewPath";
    resource.semantic = CustomResourceSemantic::PrototypeLegacyFallback;
    resource.prototypeLegacyFallbackReason =
        "test explicitly marks this workload as a prototype legacy fallback";
    return resource;
}

void assertContains(const std::pmr::string& haystack, const char* needle)
{
    assert(haystack.find(needle) != std::pmr::string::npos);
}

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src" / "renderer")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
#endif
}

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

void assertValidationFails(
    arp::renderer::CustomRenderFrameWorkloadDesc desc, const char* diagnostic)
{
    const arp::renderer::CustomRenderFrameWorkload workload(desc);
    const auto result = workload.validate();
    assert(!result.success);
    assertContains(result.diagnostics, diagnostic);
}

void assertExplicitFrameContract()
{
    using namespace arp::renderer;

    auto desc = makeBaseDesc();
    desc.scene =
        CustomWorkloadSection::enabled(makePayload(CustomWorkloadPayloadKind::SceneDrawPlan));
    desc.particles =
        CustomWorkloadSection::enabled(makePayload(CustomWorkloadPayloadKind::ParticleDrawPlan));
    desc.postprocess =
        CustomWorkloadSection::enabled(makePayload(CustomWorkloadPayloadKind::PostprocessSettings));
    desc.globalIllumination = CustomWorkloadSection::enabled(
        makePayload(CustomWorkloadPayloadKind::GlobalIlluminationPlan));
    desc.resourceContracts[0] = makeExplicitTextureUpload();
    desc.resourceContractCount = 1u;

    const CustomRenderFrameWorkload workload(desc);
    const auto result = workload.validate();
    assert(result.success);
    assert(result.diagnostics.empty());
    assert(workload.extent().width == 640u);
    assert(workload.extent().height == 360u);
    assert(workload.colorFormat() == arp::rhi::TextureFormat::Bgra8Unorm);
    assert(workload.frameSerial() == 42u);
    assert(workload.backbufferState() == arp::rhi::ResourceState::Present);
    assert(workload.scene().enabled());
    assert(workload.particles().enabled());
    assert(workload.gui().enabled());
    assert(workload.postprocess().enabled());
    assert(workload.globalIllumination().enabled());
    assert(workload.resourceContractCount() == 1u);
    assert(!workload.allowsPrototypeLegacyFallback());
}

void assertPrototypeLegacyFallbackRequiresExplicitContract()
{
    using namespace arp::renderer;

    const CustomRenderFrameWorkload ordinary(makeBaseDesc());
    assert(!ordinary.allowsPrototypeLegacyFallback());

    auto desc = makeBaseDesc();
    desc.resourceContracts[0] = makePrototypeLegacyFallback();
    desc.resourceContractCount = 1u;
    const CustomRenderFrameWorkload prototypeLegacy(desc);
    const auto result = prototypeLegacy.validate();
    assert(result.success);
    assert(prototypeLegacy.allowsPrototypeLegacyFallback());
}

void assertParticlePayloadSourceIsExplicit()
{
    using namespace arp::renderer;

    SceneConstants scene;
    ParticleSystem system(ParticleSystemDesc{16u, 1u});
    std::pmr::vector<ParticleInstance> particles;
    particles.push_back(ParticleInstance{});

    CustomParticleGraphPayload systemPayload{
        &system,
        nullptr,
        &scene,
    };
    assert(systemPayload.isValid());
    assert(&systemPayload.particleInstances() == &system.particles());

    CustomParticleGraphPayload vectorPayload{
        nullptr,
        &particles,
        &scene,
    };
    assert(vectorPayload.isValid());
    assert(&vectorPayload.particleInstances() == &particles);

    const auto section = CustomWorkloadSection::enabled({
        CustomWorkloadPayloadKind::ParticleDrawPlan,
        &vectorPayload,
        kCustomParticleGraphPayloadContract,
        kCustomParticleGraphPayloadVersion,
    });
    assert(customParticleGraphPayloadFrom(section) == &vectorPayload);

    CustomParticleGraphPayload ambiguousPayload{
        &system,
        &particles,
        &scene,
    };
    assert(!ambiguousPayload.isValid());

    CustomParticleGraphPayload missingSource{
        nullptr,
        nullptr,
        &scene,
    };
    assert(!missingSource.isValid());

    CustomParticleGraphPayload uploadPayload{
        nullptr,
        &particles,
        &scene,
    };
    const std::array<std::uint8_t, 16u> uploadPixels{
        255u,
        128u,
        32u,
        255u,
        255u,
        96u,
        16u,
        255u,
        255u,
        64u,
        8u,
        255u,
        0u,
        0u,
        0u,
        0u,
    };
    uploadPayload.textureBinding.explicitUpload.rgba8 = uploadPixels.data();
    uploadPayload.textureBinding.explicitUpload.width = 2u;
    uploadPayload.textureBinding.explicitUpload.height = 2u;
    uploadPayload.textureBinding.explicitUpload.format = arp::rhi::TextureFormat::Rgba8Unorm;
    uploadPayload.textureBinding.explicitUpload.bytesPerRow = 8u;
    uploadPayload.textureBinding.explicitUpload.rowsPerImage = 2u;
    assert(uploadPayload.textureBinding.valid());
}

void assertTypedPayloadRefsPreserveContracts()
{
    using namespace arp::renderer;

    arp::gui::DebugDrawData DebugDrawData;
    CustomGuiGraphPayload guiPayload{
        &DebugDrawData,
    };
    const auto typedGui = customTypedWorkloadPayloadRef(CustomWorkloadPayloadKind::GuiDrawData,
        guiPayload,
        kCustomGuiGraphPayloadContract,
        kCustomGuiGraphPayloadVersion);
    assert(typedGui.isValid());
    const auto guiSection = CustomWorkloadSection::enabled(typedGui);
    assert(customGuiGraphPayloadFrom(guiSection) == &guiPayload);

    arp::renderer::ui::UiDrawPlanLowerResult loweredPlan =
        arp::renderer::ui::UiDrawPlanLowerResult::success({});
    CustomUiGraphPayload uiPayload{
        &loweredPlan,
    };
    const auto typedUi = customTypedWorkloadPayloadRef(CustomWorkloadPayloadKind::UiDrawPlan,
        uiPayload,
        kCustomUiGraphPayloadContract,
        kCustomUiGraphPayloadVersion);
    assert(typedUi.isValid());
    const auto uiSection = CustomWorkloadSection::enabled(typedUi);
    assert(customUiGraphPayloadFrom(uiSection) == &uiPayload);

    static int scenePayload = 0;
    const auto sceneRef = makeTypedPayload(CustomWorkloadPayloadKind::SceneDrawPlan, scenePayload);
    assert(sceneRef.kind == CustomWorkloadPayloadKind::SceneDrawPlan);
    assert(sceneRef.object == &scenePayload);
    assert(sceneRef.contractName == std::pmr::string("tests/custom_render_frame_workload"));
    assert(sceneRef.contractVersion == 1u);
}

void assertRuntimeCallersUseTypedPayloadRefs()
{
    const auto root = projectRoot();
    const char* runtimeFiles[] = {
        "samples/stylized_showcase/main.cpp",
        "samples/common/startup_splash.cpp",
        "samples/scene_runtime_demo/scene_runtime_demo.cpp",
        "games/material_editor/app/material_editor_runtime.cpp",
        "games/character_viewer/app/character_viewer_runtime.cpp",
    };

    for (const auto* relative : runtimeFiles) {
        const auto text = readText(root / relative);
        assertContains(text, "customTypedWorkloadPayloadRef");
        assert(text.find("static_cast<const void*>") == std::pmr::string::npos);
    }

    const char* prototypeLegacyRuntimeFiles[] = {
        "samples/stylized_showcase/main.cpp",
        "games/material_editor/app/material_editor_runtime.cpp",
        "games/character_viewer/app/character_viewer_runtime.cpp",
    };
    for (const auto* relative : prototypeLegacyRuntimeFiles) {
        const auto text = readText(root / relative);
        assertContains(text, "CustomResourceSemantic::PrototypeLegacyFallback");
        assertContains(text, "PrototypeLegacyFallback");
        assertContains(text, "resourceContractCount = 1u");
    }
}

void assertDeterministicValidationFailures()
{
    using namespace arp::renderer;

    auto missingExtent = makeBaseDesc();
    missingExtent.extent.reset();
    assertValidationFails(missingExtent, "explicit extent");

    auto unknownFormat = makeBaseDesc();
    unknownFormat.colorFormat = arp::rhi::TextureFormat::Unknown;
    assertValidationFails(unknownFormat, "known color format");

    auto missingSerial = makeBaseDesc();
    missingSerial.frameSerial.reset();
    assertValidationFails(missingSerial, "explicit frame serial");

    auto missingBackbufferState = makeBaseDesc();
    missingBackbufferState.backbufferState.reset();
    assertValidationFails(missingBackbufferState, "explicit backbuffer state");

    auto missingScenePayload = makeBaseDesc();
    missingScenePayload.scene = CustomWorkloadSection::enabled({});
    assertValidationFails(missingScenePayload, "scene payload");

    auto guessedUpload = makeBaseDesc();
    guessedUpload.resourceContracts[0] = makeExplicitTextureUpload();
    guessedUpload.resourceContracts[0].uploadMetadata = CustomUploadMetadataSource::GuessedFallback;
    guessedUpload.resourceContractCount = 1u;
    assertValidationFails(guessedUpload, "explicit upload metadata");

    auto unsupportedSemantic = makeBaseDesc();
    unsupportedSemantic.resourceContracts[0] = makeExplicitTextureUpload();
    unsupportedSemantic.resourceContracts[0].semantic = CustomResourceSemantic::Unsupported;
    unsupportedSemantic.resourceContractCount = 1u;
    assertValidationFails(unsupportedSemantic, "unsupported resource semantics");

    auto unlabeledPrototypeLegacy = makeBaseDesc();
    unlabeledPrototypeLegacy.resourceContracts[0] = makePrototypeLegacyFallback();
    unlabeledPrototypeLegacy.resourceContracts[0].prototypeLegacyFallbackReason = nullptr;
    unlabeledPrototypeLegacy.resourceContractCount = 1u;
    assertValidationFails(unlabeledPrototypeLegacy,
        "Prototype/Legacy fallback requires an explicit diagnostic label");
}

void runNullGraphSmoke(const arp::renderer::CustomRenderFrameWorkload& workload)
{
    const auto validation = workload.validate();
    assert(validation.success);

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    auto backbuffer = graph.createTexture(arp::rhi::TextureDesc{
        "CustomBackbuffer",
        {workload.extent().width, workload.extent().height, 1u},
        workload.colorFormat(),
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        workload.backbufferState(),
        1u,
    });

    if (workload.scene().enabled()) {
        graph.addPass("MainScene")
            .write(backbuffer, arp::rhi::ResourceState::ColorAttachment)
            .execute([](arp::render_graph::RenderContext&) {});
    }

    if (workload.gui().enabled()) {
        graph.addPass("Gui")
            .write(backbuffer, arp::rhi::ResourceState::ColorAttachment)
            .execute([](arp::render_graph::RenderContext&) {});
    }

    graph.addPass("Present")
        .write(backbuffer, arp::rhi::ResourceState::Present)
        .kind(arp::render_graph::PassKind::Present)
        .queue(arp::render_graph::QueueRole::Present)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.execute();
    assert(graph.texture(backbuffer).state() == arp::rhi::ResourceState::Present);
    assert(!graph.executionTrace().empty());
    assert(graph.executionTrace().back() == std::pmr::string("Present"));
}

void assertNullGraphSmokePath()
{
    using namespace arp::renderer;

    const CustomRenderFrameWorkload guiOnly(makeBaseDesc());
    runNullGraphSmoke(guiOnly);

    auto sceneAndGuiDesc = makeBaseDesc();
    sceneAndGuiDesc.scene =
        CustomWorkloadSection::enabled(makePayload(CustomWorkloadPayloadKind::SceneDrawPlan));
    const CustomRenderFrameWorkload sceneAndGui(sceneAndGuiDesc);
    runNullGraphSmoke(sceneAndGui);
}

} // namespace

int main()
{
    assertExplicitFrameContract();
    assertPrototypeLegacyFallbackRequiresExplicitContract();
    assertParticlePayloadSourceIsExplicit();
    assertTypedPayloadRefsPreserveContracts();
    assertRuntimeCallersUseTypedPayloadRefs();
    assertDeterministicValidationFailures();
    assertNullGraphSmokePath();
    return 0;
}
