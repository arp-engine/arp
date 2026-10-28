#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/frontend/command_render_pass_preparation.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/frontend/descriptor_graphics_binding_inspection.h"
#include "tests/fixtures/original_command_render_pass_inputs_fixture.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>

#ifdef NDEBUG
#error Render pass storage tests require assertions
#endif

namespace {
void verifySampledColorContract()
{
    using namespace arp::rhi;
    using C = semantics::OriginalOrdinarySampledColorContract;
    for (auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = C::lowering(backend, {1u, true});
        assert(decision.nativeAvailable == (backend == Backend::Vulkan));
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(bool(decision.diagnostic) == (backend != Backend::Vulkan));
        assert(!C::lowering(backend, {}).nativeAvailable);
        assert(!C::lowering(backend, {1u, false}).nativeAvailable);
    }
    OriginalResourceRegistrationBudget registry;
    registry.contractVersion = 10u;
    registry.prototypeOptIn = true;
    registry.maxPipelineLayouts = registry.maxGraphicsPipelines = 1u;
    registry.maxDescriptorArenas = registry.maxDescriptorSets = 1u;
    registry.maxRetainedOwners = 64u;
    registry.maxCpuMetadataBytes = 65536u;
    registry.samplers = {1u, true, 1u};
    assert(OriginalTypedNativeLossJoinContract::validate({3u, true}, registry));
    assert(OriginalSamplerNativeLossJoinContract::validateRegistration(registry, 3u));
    registry.samplerNativeLoss = {1u, true};
    assert(!OriginalTypedNativeLossJoinContract::validate({3u, true}, registry));
    assert(!OriginalTypedNativeLossJoinContract::validate({4u, true}, registry));
    assert(!OriginalSamplerNativeLossJoinContract::validateRegistration(registry, 3u));
    for (auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto d = OriginalSamplerNativeLossJoinContract::lowering(backend, 3u);
        assert(d.nativeSourceAvailable == (backend == Backend::Vulkan));
        assert(d.validatorOnly == (backend == Backend::Null));
        assert(bool(d.diagnostic) == (backend != Backend::Vulkan));
        assert(!d.recordingAvailable && !d.executionAvailable);
    }
    auto invalid = registry;
    invalid.samplerNativeLoss.prototypeOptIn = false;
    assert(OriginalTypedNativeLossJoinContract::validate({3u, true}, invalid));
    invalid = registry;
    invalid.contractVersion = 7u;
    assert(OriginalSamplerNativeLossJoinContract::validateRegistration(invalid, 3u));
    invalid = registry;
    invalid.maxDescriptorTables = 1u;
    assert(OriginalTypedNativeLossJoinContract::validate({3u, true}, invalid));
    TextureDesc texture;
    texture.extent = {2u, 2u, 1u};
    texture.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    TextureViewDesc view;
    view.usage = TextureUsage::Sampled;
    for (auto format :
        {TextureFormat::Rgba8Unorm, TextureFormat::Rgba8UnormSrgb, TextureFormat::Rgba16Float}) {
        texture.format = view.format = format;
        assert(!C::validateView(texture, view));
        assert(!OriginalGraphicsBindingInspectionContract::validateTexture(
            texture, view, false, {1u, true}));
        if (format == TextureFormat::Rgba8UnormSrgb)
            assert(
                OriginalGraphicsBindingInspectionContract::validateTexture(texture, view, false));
        for (unsigned field = 0u; field < 9u; ++field) {
            auto t = texture;
            auto v = view;
            if (field == 0u)
                t.mipLevelCount = 2u;
            if (field == 1u)
                t.arrayLayerCount = 2u;
            if (field == 2u)
                t.sampleCount = 4u;
            if (field == 3u)
                t.extent.width = 0u;
            if (field == 4u)
                v.baseMipLevel = 1u;
            if (field == 5u)
                v.format = TextureFormat::D32Float;
            if (field == 6u)
                v.usage = TextureUsage::RenderAttachment;
            if (field == 7u)
                v.dimension = TextureViewDimension::Texture2DArray;
            if (field == 8u)
                v.aspect = TextureAspect::Depth;
            assert(C::validateView(t, v));
        }
    }
    OriginalGeneralDescriptorWriteRequest group;
    group.contractVersion = 4u;
    group.prototypeOptIn = true;
    group.sampledColor = {1u, true};
    assert(std::string_view(OriginalGeneralDescriptorWriteContract::validate(group))
        == "RHI-ORDINARY-SAMPLED-COLOR-GROUP-PROFILE");
    group.contractVersion = 3u;
    assert(std::string_view(OriginalGeneralDescriptorWriteContract::validate(group))
        == "RHI-ORDINARY-SAMPLED-COLOR-UPLOAD-COVERAGE");
    std::array<std::shared_ptr<const OriginalTextureUploadPublication>, 1u> proof;
    group.sampledUploads = proof;
    group.sampledColor = {};
    assert(std::string_view(OriginalGeneralDescriptorWriteContract::validate(group))
        == "RHI-ORDINARY-SAMPLED-COLOR-UPLOAD-PROFILE");
    group.sampledColor = {1u, true};
    assert(std::string_view(OriginalGeneralDescriptorWriteContract::validate(group))
        == "RHI-ORDINARY-SAMPLED-COLOR-UPLOAD-CAPACITY");
    assert(OriginalGraphicsBindingInspectionContract::validate({2u, true, {1u, true}}));
    assert(!OriginalGraphicsBindingInspectionContract::validate({1u, true, {1u, true}}));
}

void verifyNoAttachmentProgram()
{
    using namespace arp;
    using namespace rhi;
    // This is a CPU contract fixture, never proof of native execution.
    constexpr std::uint64_t metadataBytes = 65536u;
    auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
    auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
    auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
    OriginalCommandAdmissionBudget admission;
    admission.contractVersion = 1u;
    admission.prototypeOptIn = true;
    admission.maxCommands = 1u;
    admission.maxGraphOwners = 128u;
    admission.maxCpuBytes.fill(metadataBytes);
    const char* diagnostic = nullptr;
    auto sources = OriginalCommandResourceSources::prepareOriginal(
        nullptr, 0u, admission, sourceQuota, diagnostic);
    assert(sources && !diagnostic);
    const CommandUsage finish{CommandUsageKind::Finish, 0u, 0u};
    auto usage = OriginalCommandUsage::prepareOriginal(
        sources, &finish, 1u, nullptr, 0u, graphQuota, diagnostic);
    assert(usage && !diagnostic);
    const CommandVariableArguments::Input field{
        *CommandScalarArguments::capture(CommandUsageKind::Finish, CommandScalarArguments::None{})};
    auto arguments = CommandVariableArguments::prepareOriginal(
        &field, 1u, {1u, true, 1u, 0u, 0u, 0u, metadataBytes}, graphQuota, diagnostic);
    assert(arguments && !diagnostic);
    auto scalars =
        OriginalCommandScalars::prepareOriginal(usage, arguments, graphQuota, diagnostic);
    assert(scalars && !diagnostic);
    auto inputs =
        OriginalCommandInputs::prepareOriginal(scalars, nullptr, 0u, graphQuota, diagnostic);
    assert(inputs && !diagnostic);
    OriginalCommandStreamBudget stream;
    stream.contractVersion = 1u;
    stream.prototypeOptIn = true;
    stream.maxCommands = 1u;
    stream.maxRetainedOwners = 128u;
    stream.maxCpuMetadataBytes = metadataBytes;
    CommandRecordingBudget recording{1u, true, 1u};
    recording.limits.commands[static_cast<std::size_t>(CommandUsageKind::Finish)] = 1u;
    auto program = OriginalCommandStreamProgram::prepareRecordingBoundOriginal(
        inputs, stream, recording, recordingQuota, diagnostic);
    assert(program && !diagnostic && program->counts().renderPasses == 0u);
    OriginalRenderPassPreparationRequest preparation{4u,
        true,
        {1u, true, 1u, 0u, 1u, {1u, true, 1u, 0u, 0u, 0u, 0u, metadataBytes}},
        1u,
        1u,
        OriginalRenderPassContentBudget{1u, true, 1u, 1u, 1u, 1u, 1u},
        OriginalRenderPassRecordingBudget{1u, true, 1u, 1u}};
    assert(!OriginalRenderPassPreparationContract::validate(preparation));
    const auto content = OriginalRenderPassContentContract::inspect(
        program, preparation.storage, *preparation.content);
    assert(!content.diagnostic && content.counts.observations == 0u);
    assert(!OriginalRenderPassRecordingContract::inspect(
        program, preparation.storage, *preparation.content, *preparation.recording)
            .diagnostic);
    // The native compute test uses Inputs/10 + State/5 + Stream/5. This older
    // empty-program fixture cannot borrow that mixed recording profile.
    const auto mixed = OriginalRenderPassRecordingContract::inspect(
        program, preparation.storage, {3u, true, 1u, 1u, 1u, 1u, 1u}, {3u, true, 1u, 1u});
    assert(mixed.diagnostic
        && std::string_view(mixed.diagnostic) == "RHI-ORDINARY-GRAPHICS-PROGRAM-PROFILE");
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = OriginalRenderPassPreparationContract::lowering(backend, preparation);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.recordingAvailable == (backend == Backend::Vulkan));
        assert((decision.diagnostic == nullptr) == (backend == Backend::Vulkan));
    }
    --preparation.storage.maxAttachmentReferences;
    assert(OriginalRenderPassPreparationContract::validate(preparation));
}

void verifyPreparationContract()
{
    using namespace arp::rhi;
    using Contract = OriginalRenderPassPreparationContract;
    OriginalRenderPassPreparationRequest request{
        1u, true, {1u, true, 2u, 2u, 4u, {1u, true, 2u, 0u, 0u, 0u, 0u, 65536u}}, 8u};
    assert(!Contract::validate(request));
    for (const auto version : {1u, 2u, 3u}) {
        request.contractVersion = version;
        request.maxPassWrappers = version >= 2u ? 2u : 0u;
        request.content = version == 3u
            ? std::optional<OriginalRenderPassContentBudget>{{1u, true, 4u, 9u, 16u, 20u, 32u}}
            : std::nullopt;
        assert(!Contract::validate(request));
        assert(!Contract::inspectWrapperCapacity(request, 2u));
        assert(bool(Contract::inspectWrapperCapacity(request, 3u)) == (version >= 2u));
        for (const auto backend : {Backend::Null,
                 Backend::Vulkan,
                 Backend::Metal,
                 Backend::WebGPU,
                 Backend::WebGL,
                 static_cast<Backend>(255u)}) {
            const auto decision = Contract::lowering(backend, request);
            assert(!decision.recordingAvailable);
            assert(decision.validatorOnly == (backend == Backend::Null));
            assert(decision.privatePreparationAvailable == (backend == Backend::Vulkan));
            assert((decision.diagnostic == nullptr) == (backend == Backend::Vulkan));
        }
    }
    request.contractVersion = 2u;
    request.content.reset();
    auto invalid = request;
    invalid.contractVersion = 0u;
    assert(Contract::validate(invalid));
    invalid = request;
    invalid.prototypeOptIn = false;
    assert(Contract::validate(invalid));
    invalid = request;
    invalid.maxNativeObjects = 0u;
    assert(Contract::validate(invalid));
    invalid = request;
    invalid.storage.prototypeOptIn = false;
    assert(Contract::validate(invalid));
    invalid = request;
    invalid.maxPassWrappers = 0u;
    assert(std::string_view(Contract::validate(invalid))
        == "RHI-RENDER-PASS-PREPARATION-WRAPPER-BUDGET");
    invalid = request;
    invalid.contractVersion = 1u;
    assert(std::string_view(Contract::validate(invalid))
        == "RHI-RENDER-PASS-PREPARATION-WRAPPER-BUDGET");
    invalid = request;
    invalid.contractVersion = 10u;
    assert(std::string_view(Contract::validate(invalid)) == "RHI-RENDER-PASS-PREPARATION-VERSION");
    invalid = request;
    invalid.contractVersion = 3u;
    assert(Contract::validate(invalid));
    invalid.content = OriginalRenderPassContentBudget{1u, true, 4u, 9u, 16u, 20u, 32u};
    assert(!Contract::validate(invalid));
    invalid.contractVersion = 2u;
    assert(Contract::validate(invalid));
    invalid.contractVersion = 1u;
    invalid.maxPassWrappers = 0u;
    assert(Contract::validate(invalid));
    invalid.contractVersion = 3u;
    invalid.maxPassWrappers = 2u;
    for (const auto field : {&OriginalRenderPassContentBudget::maxAttachmentObservations,
             &OriginalRenderPassContentBudget::maxParentCells,
             &OriginalRenderPassContentBudget::maxContentSteps,
             &OriginalRenderPassContentBudget::maxCommandVisits,
             &OriginalRenderPassContentBudget::maxProjectionVisits}) {
        auto bad = invalid;
        (*bad.content).*field = 0u;
        assert(Contract::validate(bad));
    }
    auto bad = invalid;
    bad.content->prototypeOptIn = false;
    assert(Contract::validate(bad));
    bad = invalid;
    bad.content->contractVersion = 2u;
    assert(Contract::validate(bad));
    invalid.contractVersion = 4u;
    invalid.recording = OriginalRenderPassRecordingBudget{1u, true, 28u, 52u};
    assert(!Contract::validate(invalid));
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = Contract::lowering(backend, invalid);
        assert(decision.recordingAvailable == (backend == Backend::Vulkan));
        assert(decision.validatorOnly == (backend == Backend::Null));
    }
    for (unsigned field = 0u; field < 5u; ++field) {
        bad = invalid;
        if (field == 0u)
            bad.recording->contractVersion = 0u;
        if (field == 1u)
            bad.recording->prototypeOptIn = false;
        if (field == 2u)
            bad.recording->maxCommandVisits = 0u;
        if (field == 3u)
            bad.recording->maxAttachmentVisits = 0u;
        if (field == 4u)
            bad.contractVersion = 3u;
        assert(Contract::validate(bad));
    }
    // Preparation/7 is independent of /6: only the new profile admits D32
    // vertex-ID opaque rasterization, with the same content/journal obligations.
    auto opaque = invalid;
    opaque.contractVersion = 7u;
    opaque.content->contractVersion = opaque.recording->contractVersion = 4u;
    assert(!Contract::validate(opaque));
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = Contract::lowering(backend, opaque);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.recordingAvailable == (backend == Backend::Vulkan));
        assert((decision.diagnostic == nullptr) == (backend == Backend::Vulkan));
    }
    auto wrongJoin = opaque;
    wrongJoin.contractVersion = 6u;
    assert(Contract::validate(wrongJoin));
    wrongJoin = opaque;
    wrongJoin.prototypeOptIn = false;
    assert(Contract::validate(wrongJoin));
    RenderPipelineDesc pipeline;
    pipeline.depthStencil.format = TextureFormat::D32Float;
    pipeline.depthStencil.depthTestEnabled = pipeline.depthStencil.depthWriteEnabled = true;
    pipeline.depthStencil.depthCompare = CompareOp::Less;
    RenderPassDesc pass;
    pass.hasDepthStencilAttachment = true;
    pass.depthStencilAttachment.format = TextureFormat::D32Float;
    using Projection = OriginalReadOnlyGraphicsPassProjection;
    assert(!Projection::inspectDepth(pipeline, pass, true));
    assert(Projection::inspectDepth(pipeline, pass, false));
    for (unsigned field = 0u; field < 6u; ++field) {
        auto changed = pipeline;
        auto changedPass = pass;
        if (field == 0u)
            changed.depthStencil.depthWriteEnabled = false;
        if (field == 1u)
            changed.depthStencil.depthTestEnabled = false;
        if (field == 2u)
            changed.depthStencil.depthCompare = CompareOp::Always;
        if (field == 3u)
            changed.depthStencil.stencilEnabled = true;
        if (field == 4u)
            changedPass.hasDepthStencilAttachment = false;
        if (field == 5u)
            changedPass.depthStencilAttachment.format = TextureFormat::Rgba8Unorm;
        assert(Projection::inspectDepth(changed, changedPass, true));
    }
    assert(OriginalTextureContentCreationContract::cellControlMatchesPlan(
        4u, OriginalOrdinaryAttachmentContentContract::mutationVersion));
    for (const auto control : {1u, 2u, 3u, 5u, 6u, 7u})
        assert(!OriginalTextureContentCreationContract::cellControlMatchesPlan(
            control, OriginalOrdinaryAttachmentContentContract::mutationVersion));
    {
        using namespace semantics;
        const ResourceAccessScope attachment{ResourceState::ColorAttachment,
            PipelineStage::ColorAttachmentOutput,
            ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite};
        RecordedResourceAccessDependencies dependency;
        assert(!dependency.discardTo(attachment));
        assert(dependency.discardTo(attachment));
        assert(!dependency.consume(attachment));
        assert(dependency.consume(attachment));
        assert(!dependency.depend(attachment, attachment));
        assert(!dependency.consume(attachment));
    }

    RenderPassDesc descriptor;
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.colorAttachments.emplace_back();
    for (auto load : {LoadOp::Load, LoadOp::Clear, LoadOp::DontCare})
        for (auto store : {StoreOp::Store, StoreOp::DontCare}) {
            descriptor.colorAttachments[0].loadOp = load;
            descriptor.colorAttachments[0].storeOp = store;
            assert(!Contract::inspectDescriptor(descriptor));
            using Action = OriginalTextureContentCellSequence::Action;
            const auto begin =
                semantics::AttachmentContentActionContract::action(true, load, store);
            const auto end = semantics::AttachmentContentActionContract::action(false, load, store);
            assert(begin
                && *begin
                    == (load == LoadOp::Load        ? Action::Load
                            : load == LoadOp::Clear ? Action::Clear
                                                    : Action::DontCare));
            assert(end && *end == (store == StoreOp::Store ? Action::Store : Action::Discard));
        }
    for (bool begin : {false, true}) {
        assert(!semantics::AttachmentContentActionContract::action(
            begin, static_cast<LoadOp>(255u), StoreOp::Store));
        assert(!semantics::AttachmentContentActionContract::action(
            begin, LoadOp::Clear, static_cast<StoreOp>(255u)));
    }
    descriptor.colorAttachments[0].loadOp = static_cast<LoadOp>(255u);
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.colorAttachments[0].loadOp = LoadOp::Clear;
    descriptor.colorAttachments[0].storeOp = static_cast<StoreOp>(255u);
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.colorAttachments[0].storeOp = StoreOp::Store;
    descriptor.colorAttachments[0].clearColor.r = std::numeric_limits<float>::quiet_NaN();
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.colorAttachments[0].clearColor.r = 0.0f;
    descriptor.colorAttachments[0].hasResolveTarget = true;
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.colorAttachments[0].hasResolveTarget = false;
    descriptor.layeredRendering = true;
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.layeredRendering = false;
    descriptor.subpasses.emplace_back();
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.subpasses.clear();
    descriptor.subpassDependencies.emplace_back();
    assert(Contract::inspectDescriptor(descriptor));
    descriptor.subpassDependencies.clear();
    descriptor.hasDepthStencilAttachment = true;
    auto& depth = descriptor.depthStencilAttachment;
    depth.format = TextureFormat::D32Float;
    depth.stencilLoadOp = LoadOp::DontCare;
    depth.stencilStoreOp = StoreOp::DontCare;
    assert(!Contract::inspectDescriptor(descriptor));
    depth.stencilLoadOp = LoadOp::Clear;
    assert(Contract::inspectDescriptor(descriptor));
    depth.stencilLoadOp = LoadOp::DontCare;
    depth.depthReadOnly = true;
    assert(Contract::inspectDescriptor(descriptor));

    TextureDesc parent;
    parent.extent = {8u, 8u, 1u};
    parent.mipLevelCount = parent.arrayLayerCount = 3u;
    parent.format = TextureFormat::Rgba16Float;
    parent.usage = TextureUsage::RenderAttachment;
    TextureViewDesc view;
    view.format = parent.format;
    view.usage = parent.usage;
    view.baseMipLevel = 1u;
    view.baseArrayLayer = 2u;
    assert(!Contract::inspectView(parent, view));
    for (unsigned field = 0u; field < 10u; ++field) {
        auto p = parent;
        auto v = view;
        if (field == 0u)
            p.sampleCount = 4u;
        if (field == 1u)
            p.extent.depth = 2u;
        if (field == 2u)
            p.cubeCompatible = true;
        if (field == 3u)
            v.mipLevelCount = 2u;
        if (field == 4u)
            v.arrayLayerCount = 2u;
        if (field == 5u)
            v.dimension = TextureViewDimension::Texture2DArray;
        if (field == 6u)
            v.baseMipLevel = 3u;
        if (field == 7u)
            v.format = TextureFormat::Rgba8Unorm;
        if (field == 8u)
            v.usage = TextureUsage::Sampled;
        if (field == 9u)
            p.extent.width = 0u;
        assert(Contract::inspectView(p, v));
    }
}
} // namespace

int main(int argc, char** argv)
{
    verifySampledColorContract();
    verifyNoAttachmentProgram();
    verifyPreparationContract();
    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::test::OriginalCommandRenderPassInputsFixture::verify(*device);
    if (argc == 2 && std::string_view(argv[1]) == "--null-hidden-frame") {
        arp::rhi::NullHiddenFrameSmokeRunner runner(*device);
        const auto report = runner.run({});
        if (!report.success)
            std::fprintf(stderr, "%s\n", report.diagnostics.c_str());
        assert(report.success && report.submittedFrameCount == 2u
            && report.totalSubmittedCommandCount && report.descriptorCompletenessValidated
            && !report.nativeHandleExposedToUpperLayer
            && report.backendFallbacksHiddenFromUpperLayer
            && !report.resourceStats.transientResourceCount
            && !report.resourceStats.pendingDestroyCount);
        std::puts(
            "Render-pass cold storage projection plus Null hidden-frame PASS; no native pass execution claim");
    }
}
