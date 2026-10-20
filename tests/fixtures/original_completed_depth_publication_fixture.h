#pragma once
#include "bindless_package_request_fixture.h"
#include "completed_depth_texture_source_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_mixed_depth_storage_plan_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_extensions.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include "webgpu_async_wait_fixture.h"
#include "webgpu_completed_depth_texel_readback_fixture.h"
#include "webgpu_ordinary_depth_closure_fixture.h"
#include "webgpu_ordinary_depth_execution_fixture.h"
#include "webgpu_original_depth_root_preparation_fixture.h"
#include "webgpu_original_device_fixture.h"
#endif

namespace arp::test {
// Reuses OrderedCopyAttachment/1's resource-only clear vocabulary on Registry9.
// The optional public depth reader consumes those authentic completed cells
// through the existing Root executor and original journal/completion issuer.
class OriginalCompletedDepthPublicationFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "public depth initializer: %s\n", id);
        assert(!id);
    }

public:
    static void verify(const std::shared_ptr<const rhi::BindlessShaderPackage>& package = {},
        std::uint32_t terminalScenario = UINT32_MAX,
        bool mixedPlanning = false,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& ordinaryDepthPackage = {},
        bool ordinaryDepthExecution = false,
        std::uint32_t ordinaryScenario = 0u,
        bool ordinaryFault = false,
        bool ordinaryDepthRewrite = false)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using Sources = OriginalCommandResourceSources;
        using Root = OriginalCommandStateTransaction;
        using Admission = WebGpuCanonicalColdAdmission;
        using Issuer = WebGpuCanonicalIssuerPreparation;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> q;
        for (auto& quota : q)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto run = [&] {
            auto device = createWebGpuRhiDevice(WebGpuOriginalDeviceFixture::describe(
                "public depth initializer",
                20u + (package ? package->artifact().snapshotBytes + 32u : 0u)
                    + (mixedPlanning ? OriginalMixedDepthStoragePlanFixture::additionalGpuBytes()
                                     : 0u)
                    // Independent bounded upload staging plus authored RGBA8
                    // 2x2/mips2 chain20, attachment16 and Results40 (160 bytes).
                    + (ordinaryDepthPackage ? bytes + ordinaryDepthPackage->artifact().snapshotBytes
                                + 20u + 16u + 40u * sizeof(float)
                                            : 0u),
                kL2AtomicPushConstantLimitBytes,
                bytes));
            assert(device);
            require(device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, q[0]));
            require(device->prepareOriginalDeviceNativeLoss({{1u, true, 1u, 1u, bytes}, 1u}, q[1]));
            const char* id = nullptr;
            auto loss = device->observeOriginalDeviceNativeLoss(id);
            require(id);
            assert(loss);
            OriginalResourceRegistrationBudget registry{9u, true, 0u, 1u, 3u, 32u, bytes};
            registry.maxDescriptorArenas = registry.maxDescriptorSets = 1u;
            if (package) {
                registry.maxBuffers =
                    5u; // metadata, latch, readback, two independent Results resources
                registry.maxDescriptorArenas = registry.maxDescriptorSets = 2u;
                registry.samplers = {1u, true, 1u};
                registry.maxRetainedOwners = OriginalResourceRegistrationContract::fixedOwnerCount
                    + OriginalResourceRegistrationContract::perResourceOwnerCount
                        * (registry.maxBuffers + registry.maxTextures + registry.maxTextureViews
                            + registry.samplers.maxSamplers + registry.maxDescriptorArenas)
                    + OriginalResourceRegistrationContract::perDescriptorOwnerCount
                        * registry.maxDescriptorSets;
            }
            if (mixedPlanning) {
                assert(package);
                OriginalMixedDepthStoragePlanFixture::configureRegistration(registry);
            }
            if (ordinaryDepthPackage) {
                assert(!package && !mixedPlanning && terminalScenario == UINT32_MAX);
                registry.maxBuffers = 5u;   // Four snapshot/result buffers + upload staging.
                registry.maxTextures += 2u; // Ordinary color and independent attachment.
                registry.maxTextureViews += 2u;
                registry.maxDescriptorArenas = registry.maxDescriptorSets = 3u;
                registry.samplers = {1u, true, 3u};
                registry.maxRetainedOwners = OriginalResourceRegistrationContract::fixedOwnerCount
                    + OriginalResourceRegistrationContract::perResourceOwnerCount
                        * (registry.maxBuffers + registry.maxTextures + registry.maxTextureViews
                            + registry.samplers.maxSamplers + registry.maxDescriptorArenas)
                    + OriginalResourceRegistrationContract::perDescriptorOwnerCount
                        * registry.maxDescriptorSets;
            }
            require(device->prepareOriginalDescriptorNativeLossRegistration(
                {3u, true}, registry, q[2], *loss));
            TextureDesc desc;
            desc.label = "completed depth";
            desc.extent = {2u, 2u, 1u};
            desc.mipLevelCount = 2u;
            desc.format = TextureFormat::D32Float;
            desc.usage =
                TextureUsage::Sampled | TextureUsage::RenderAttachment | TextureUsage::CopySrc;
            core::UniquePtr<GpuTexture> texture;
            {
                core::CpuMemoryQuotaScope allocation(q[3]);
                texture = device->createOriginalContentTexture(
                    desc, {2u, true, 1u, 1u, 20u, 4u, bytes, 2u}, q[4], id);
            }
            require(id);
            assert(texture);
            auto parent = OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, bytes}, q[5], id);
            require(id);
            assert(parent);
            std::array<std::shared_ptr<const OriginalCommandTextureViewSource>, 3u> views;
            for (std::uint32_t i = 0u; i < views.size(); ++i) {
                TextureViewDesc vd;
                vd.format = desc.format;
                vd.aspect = TextureAspect::Depth;
                vd.usage = i == 2u ? TextureUsage::Sampled : TextureUsage::RenderAttachment;
                vd.baseMipLevel = i == 2u ? 0u : i;
                vd.mipLevelCount = i == 2u ? 2u : 1u;
                core::UniquePtr<GpuTextureView> view;
                {
                    core::CpuMemoryQuotaScope allocation(q[3]);
                    view = device->createTextureView(*parent->originalObject(), vd);
                }
                assert(view);
                auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                    view, {1u, true, 2u, bytes}, q[5], id);
                require(id);
                views[i] = OriginalCommandTextureViewSource::prepareOriginal(
                    lease, parent, {1u, true, 5u, bytes}, q[5], id);
                require(id);
                assert(views[i]);
            }
            const std::array<Sources::Source, 4u> retained{parent, views[0], views[1], views[2]};
            OriginalCommandAdmissionBudget authored;
            authored.contractVersion = 1u;
            authored.prototypeOptIn = true;
            authored.maxCommands = 6u;
            authored.maxReferences = 3u;
            authored.maxInputDescriptors = 3u;
            authored.maxGraphOwners = 128u;
            authored.maxStateObservations = 3u;
            authored.maxTextureRanges = 3u;
            authored.maxStateTransactions = 1u;
            authored.maxPassAttachmentReferences = 2u;
            authored.maxNativePayloadObjects = 4u;
            constexpr auto lifecycle = Admission::selectedRequirements().capacity;
            authored.maxPendingSubmissions = lifecycle.pendingSubmissions;
            authored.maxCallbackTickets = lifecycle.callbackTickets;
            authored.maxCallbackRegistrations = lifecycle.callbackRegistrations;
            authored.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] =
                1u;
            authored
                .maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)] =
                3u;
            authored.maxCpuBytes.fill(bytes);
            auto sources =
                Sources::prepareOriginal(retained.data(), retained.size(), authored, q[5], id);
            require(id);
            assert(sources);
            const auto clearOriginalMips =
                [&](std::uint32_t mode,
                    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& readProof =
                        {},
                    const std::shared_ptr<BindlessSnapshot>& readSnapshot = {},
                    const std::shared_ptr<const WebGpuBindlessRootNativeClosure>& readRoot = {}) {
                    const bool sampledReturn = mode == 4u;
                    const bool update = mode == 1u || sampledReturn;
                    const bool readOnly = mode == 2u || mode == 3u;
                    std::array<CommandUsage, 6u> commands{{{K::TextureBarrier, 0u, 1u},
                        {K::BeginRenderPass, 1u, 1u},
                        {K::EndRenderPass, 2u, 0u},
                        {K::BeginRenderPass, 2u, 1u},
                        {K::EndRenderPass, 3u, 0u},
                        {K::Finish, 3u, 0u}}};
                    // A later write touches mip0 only. Mip1 must keep its independently
                    // completed generation; a whole uniform expectation cannot replace it.
                    const std::size_t commandCount = readOnly ? 2u : update ? 4u : commands.size();
                    const std::size_t referenceCount = readOnly ? 1u : update ? 2u : 3u;
                    if (readOnly)
                        commands[1] = {K::Finish, 1u, 0u};
                    if (update)
                        commands[3] = {K::Finish, 2u, 0u};
                    const std::array<OriginalCommandUsage::Reference, 3u> refs{{{0u}, {1u}, {2u}}};
                    core::pmr::Vector<CommandVariableArguments::Input> fields;
                    for (std::size_t i = 0u; i < commandCount; ++i)
                        fields.push_back({*CommandScalarArguments::capture(
                            commands[i].kind, CommandScalarArguments::None{})});
                    ResourceBarrierDesc barrier{parent->resource(),
                        {0u, 2u, 0u, 1u, TextureAspect::Depth},
                        update || readOnly ? ResourceState::DepthAttachment
                                           : ResourceState::Undefined,
                        readOnly ? ResourceState::CopySrc : ResourceState::DepthAttachment};
                    barrier.sourceStages =
                        update || readOnly ? PipelineStage::DepthStencil : PipelineStage::TopOfPipe;
                    barrier.sourceAccess = update || readOnly
                        ? (ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite)
                        : ResourceAccess::None;
                    if (sampledReturn) {
                        const auto committed =
                            parent->originalObject()->observeOriginalCommittedState(
                                barrier.textureSubresources);
                        assert(committed && committed->state() == ResourceState::ShaderRead);
                        barrier.before = committed->state();
                        barrier.sourceStages = PipelineStage::FragmentShader;
                        barrier.sourceAccess = ResourceAccess::ShaderRead;
                    }
                    // OrderedCopyAttachment/1 permits exact transfer/attachment scopes.
                    // Exercise a genuine read-only transition within that existing domain;
                    // this fixture grants no shader-state lowering or shader consumption.
                    barrier.destinationStages =
                        readOnly ? PipelineStage::Transfer : PipelineStage::DepthStencil;
                    barrier.destinationAccess = readOnly
                        ? ResourceAccess::TransferRead
                        : ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite;
                    core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> records;
                    records.push_back(
                        {0u, *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)});
                    for (std::uint32_t i = 0u; i < (readOnly ? 0u : update ? 1u : 2u); ++i) {
                        RenderPassDesc pass;
                        pass.hasDepthStencilAttachment = true;
                        auto& depth = pass.depthStencilAttachment;
                        depth.target = views[i]->resource();
                        depth.format = desc.format;
                        depth.depthLoadOp = LoadOp::Clear;
                        depth.depthStoreOp = StoreOp::Store;
                        depth.stencilLoadOp = LoadOp::DontCare;
                        depth.stencilStoreOp = StoreOp::DontCare;
                        depth.depthClearValue = (i + 1u) * (update ? 0.125f : 0.25f);
                        auto frozen = RenderPassArguments::prepareOriginal(
                            pass, {1u, true, 0u, 0u, 0u, 0u, 0u, bytes}, q[6], id);
                        require(id);
                        assert(frozen);
                        records.push_back({1u + 2u * i, frozen});
                    }
                    auto inputs =
                        OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
                            commands.data(),
                            commandCount,
                            refs.data(),
                            referenceCount,
                            fields.data(),
                            records.data(),
                            records.size(),
                            {},
                            q[6],
                            id);
                    require(id);
                    assert(inputs);
                    std::uint32_t owners = 0u, registrations = 0u;
                    require(Sources::registeredOwnerUnion(sources,
                        inputs->retainedOwnerCount() + 1u,
                        128u,
                        128u,
                        owners,
                        registrations));
                    OriginalCommandStateAdmissionBudget state{
                        1u, true, 1u, registrations, 3u, 3u, owners, bytes};
                    auto graph = OriginalCommandResourceStates::prepareOriginal(
                        inputs, *device, state, q[7], id);
                    require(id);
                    assert(graph);
                    state.maxRetainedOwners += 2u;
                    auto root = Root::prepareOriginalOwnership(graph, *device, state, q[8], id);
                    require(id);
                    assert(root);
                    const OriginalCommandStateSequenceBudget sequence{1u, true, 6u, 0u, 16u, 16u};
                    OriginalTextureContentPlanBudget content{2u, true, 1u, 0u, 2u, 4u, 60u, 4096u};
                    require(root->materializeOriginalPrototypePlan(sequence, content));
                    const auto verifyReadProof = [&] {
                        assert(readProof);
                        require(OriginalCompletedDepthTextureSource::validateOriginal(readProof));
                        require(WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                            {readProof,
                                ResourceState::ShaderRead,
                                PipelineStage::ComputeShader,
                                ResourceAccess::ShaderRead},
                            views[2],
                            {1u,
                                true,
                                2u,
                                20u,
                                OriginalCompletedDepthTextureSource::completeRetainedOwnerCount}));
                        if (readSnapshot)
                            assert(readSnapshot->validate().success);
                        if (readRoot)
                            require(readRoot->validate());
                        require(root->validate());
                    };
                    if (mode == 3u) {
                        require(root->applyOriginalPrototypePlan());
                        assert(root->status() == Root::Status::Pending && !root->journalEmpty());
                        const auto allocations =
                            core::defaultCpuAllocator().stats().allocationCount;
                        verifyReadProof();
                        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                        assert(!parent->originalObject()->observeOriginalCommittedState(
                            barrier.textureSubresources));
                        require(root->abandonOriginalPendingOwnership());
                        assert(root->status() == Root::Status::Abandoned && root->journalEmpty());
                        require(OriginalCompletedDepthTextureSource::validateOriginal(readProof));
                        if (readSnapshot)
                            assert(readSnapshot->validate().success);
                        return;
                    }
                    auto closure = WebGpuCanonicalNativeClosure::prepareOriginalLossBound(
                        root, *device, {1u, true, 4u, 4u, 128u, 20u, bytes}, *loss, id);
                    require(id);
                    assert(closure);
                    OriginalCommandNativeColdBudget cold{
                        2u, true, 6u, 1u, 1u, 2u, 3u, 3u, 1u, 1u, 1u, 128u, bytes, 0u, 2u};
                    Admission::Requirements required;
                    require(Admission::deriveRequirements(root, cold, required));
                    auto submission = core::makeShared<core::CpuMemoryQuota>(bytes),
                         callback = core::makeShared<core::CpuMemoryQuota>(bytes);
                    auto seal = OriginalCommandRecordingSeal::prepareOriginal(root,
                        *device,
                        {1u, true, 1u, 8u, 8u, 128u, 128u, 128u, bytes, bytes, bytes},
                        submission,
                        callback,
                        id);
                    require(id);
                    assert(seal);
                    auto reservation = OriginalCommandLifecycleReservation::prepareOriginal(
                        seal, required.capacity, id);
                    require(id);
                    assert(reservation);
                    auto admission = Admission::prepareOriginal(closure, reservation, cold, id);
                    require(id);
                    assert(admission);
                    auto command = WebGpuCanonicalColdCommand::prepareOriginal(
                        admission, {1u, true, 1u, 128u, bytes}, id);
                    require(id);
                    assert(command);
                    auto custody = WebGpuCanonicalCallbackCustody::prepareOriginal(
                        command, {1u, true, 1u, 8u, 128u, bytes, bytes}, id);
                    require(id);
                    assert(custody);
                    OriginalCommandCopyPassLoweringBudget loweringBudget{
                        2u, true, 0u, 0u, 128u, bytes, 2u};
                    if (sampledReturn) {
                        assert(readProof && readRoot && !readRoot->validate());
                        const auto baseline = q[8]->liveBytes();
                        assert(!WebGpuCanonicalCopyPassLowering::prepareOriginal(
                                   custody, loweringBudget, id)
                            && id);
                        assert(q[8]->liveBytes() == baseline && !readProof->validate());
                        loweringBudget.depthWriteReturn = {
                            1u, true, PipelineStage::FragmentShader, 1u};
                        auto oneLess = loweringBudget;
                        --oneLess.depthWriteReturn.maxTransitions;
                        assert(
                            !WebGpuCanonicalCopyPassLowering::prepareOriginal(custody, oneLess, id)
                            && id && q[8]->liveBytes() == baseline && !readRoot->validate());
                    }
                    auto lowering = WebGpuCanonicalCopyPassLowering::prepareOriginal(
                        custody, loweringBudget, id);
                    require(id);
                    assert(lowering);
                    auto issuer = Issuer::prepareOriginal(
                        lowering, {1u, true, 1u, 1u, 128u, bytes, bytes}, id);
                    require(id);
                    assert(issuer);
                    require(Issuer::recordOriginal(issuer, {1u, true}));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return issuer->hasOriginalRecordedCommand();
                    }));
                    if (readOnly) {
                        assert(root->status() == Root::Status::Pending);
                        verifyReadProof();
                        assert(!parent->originalObject()->observeOriginalCommittedState(
                            barrier.textureSubresources));
                    }
                    require(Issuer::submitOriginal(issuer, {1u, true}));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return !custody->pendingOriginalRecords();
                    }));
                    assert(issuer->hasOriginalCompletedSubmission()
                        && root->status() == Root::Status::Committed && root->journalEmpty());
                    if (readOnly) {
                        verifyReadProof();
                        const auto state = parent->originalObject()->observeOriginalCommittedState(
                            barrier.textureSubresources);
                        assert(state && state->state() == ResourceState::CopySrc);
                    }
                };
            CompletedDepthTextureSourceFixture::verify(views[2], false);
            clearOriginalMips(false);
            CompletedDepthTextureSourceFixture::verify(views[2], true);
            auto completed = OriginalCompletedDepthTextureSource::prepareOriginal(
                views[2], {1u, true, 2u, bytes, 6u}, q[5], id);
            require(id);
            assert(completed);
            if (ordinaryDepthExecution) {
                assert(ordinaryDepthPackage);
                const auto consumed = WebGpuOrdinaryDepthExecutionFixture::verify(*device,
                    ordinaryDepthPackage,
                    completed,
                    ordinaryScenario,
                    ordinaryFault,
                    ordinaryDepthRewrite);
                if (ordinaryDepthRewrite) {
                    assert(consumed && !consumed->validate() && !completed->validate());
                    const auto before = completed->cells();
                    assert(before.size() == 2u);
                    clearOriginalMips(4u, completed, {}, consumed);
                    assert(completed->validate() && consumed->validate()
                        && consumed->originalRoot()->validate());
                    auto fresh = OriginalCompletedDepthTextureSource::prepareOriginal(
                        views[2], {1u, true, 2u, bytes, 6u}, q[5], id);
                    require(id);
                    // Generation IDs belong to the whole original control;
                    // mip1's initial publication has already consumed an ID.
                    // Rollback may also consume IDs, so only monotonicity is promised.
                    assert(fresh && fresh->cells().size() == 2u
                        && fresh->cells()[0].contentGeneration()
                            > std::max(before[0].contentGeneration(), before[1].contentGeneration())
                        && fresh->cells()[1].contentGeneration() == before[1].contentGeneration());
                    clearOriginalMips(2u, fresh);
                    WebGpuCompletedDepthTexelReadbackFixture::verify(
                        *parent->originalObject(), {0.125f, 0.125f, 0.125f, 0.125f, 0.5f});
                    require(fresh->validate());
                    std::printf(
                        "BINDLESS_WEBGPU_DEPTH_WRITE_RETURN1_OK fault%u consumer-completed Closure20-stale mip0-next mip1-unchanged legacy-one-less-refused genuine-producer-queue\n",
                        ordinaryFault ? 1u : 0u);
                }
                return;
            }
            auto ordinaryClosure = ordinaryDepthPackage
                ? WebGpuOrdinaryDepthClosureFixture::verify(
                      *device, ordinaryDepthPackage, completed)
                : nullptr;
            auto arena = core::shareUnique(
                device->createOriginalDescriptorArena({"depth publication", 1u, 1u}));
            assert(arena);
            DescriptorLayoutDesc layout;
            layout.name = "depth publication";
            layout.bindings.push_back(
                {0u, DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
            layout.bindings[0].depthTexture = true;
            layout.bindings[0].textureViewDimensionConstrained = true;
            layout.bindings[0].textureViewDimension = TextureViewDimension::Texture2D;
            auto set = device->allocateOriginalDescriptorSet(arena, {"depth publication", layout});
            assert(set);
            DescriptorBindingWrite write;
            write.type = DescriptorType::SampledTexture;
            write.resourceKind = DescriptorResourceKind::TextureView;
            write.resource = views[2]->resource();
            write.resourceState = ResourceState::ShaderRead;
            const auto revision = set->contentRevision();
            const auto ordinary = set->write(write);
            assert(!ordinary.success && ordinary.diagnostics == "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE"
                && set->contentRevision() == revision && !set->hasWrittenBinding(0u));
            const auto published = set->writeOriginalCompletedTexture(
                *device, write, completed, {3u, true, 1u, 1u, 6u});
            if (!published.success)
                std::fprintf(
                    stderr, "public depth descriptor: %s\n", published.diagnostics.c_str());
            assert(published.success && set->contentRevision() == revision + 1u
                && set->hasWrittenBinding(0u));
            const auto replacement = set->write(write);
            assert(!replacement.success && set->contentRevision() == revision + 1u);
            assert(set->validateBinding(0u).success);
            const auto* extension = static_cast<const BindlessCrossExtension*>(
                device->queryInterface(kExtensionBindlessCross));
            assert(extension);
            std::shared_ptr<const OriginalCommandSamplerLease> sampler;
            std::shared_ptr<const OriginalCommandBufferLease> result, secondResult;
            core::UniquePtr<BindlessSnapshotPool> pool;
            BindlessRequirements request;
            if (package) {
                request = BindlessPackageRequestFixture::make(
                    package->artifact(), extension->capabilities().deviceIdentity);
                request.originalPreparation = {4u, true};
                for (auto& table : request.tables)
                    table.resourceCount = 1u;
                pool = core::makeUnique<BindlessSnapshotPool>(*device, request.budget);
                SamplerDesc depthSampler;
                depthSampler.minFilter = depthSampler.magFilter = SamplerFilter::Nearest;
                depthSampler.mipFilter = SamplerMipFilter::Nearest;
                auto object = device->createSampler(depthSampler);
                assert(object);
                sampler = OriginalCommandSamplerLease::prepareOriginal(
                    object, {1u, true, 2u, bytes}, q[5], id);
                require(id);
                assert(sampler);
                auto buffer = device->createBuffer({"depth snapshot Results",
                    16u,
                    ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                    MemoryUsage::GpuOnly,
                    ResourceState::ShaderWrite});
                assert(buffer);
                result = OriginalCommandBufferLease::prepareOriginal(
                    buffer, {1u, true, 2u, bytes}, q[5], id);
                require(id);
                assert(result);
                auto second = device->createBuffer({"depth snapshot Results1",
                    16u,
                    ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                    MemoryUsage::GpuOnly,
                    ResourceState::ShaderWrite});
                assert(second);
                secondResult = OriginalCommandBufferLease::prepareOriginal(
                    second, {1u, true, 2u, bytes}, q[5], id);
                require(id);
                assert(secondResult);
            }
            const auto snapshotFor = [&](const auto& proof,
                                         std::uint32_t profile =
                                             4u) -> std::shared_ptr<BindlessSnapshot> {
                if (!package)
                    return {};
                auto selected = request;
                selected.originalPreparation = {profile, true};
                if (profile == 6u) {
                    selected.originalPreparation = {};
                    selected.originalExecution = {5u, true};
                }
                if (profile == 5u || profile == 6u)
                    for (auto& table : selected.tables)
                        if (table.tableId == 3u)
                            table.resourceCount = 2u;
                core::pmr::String diagnostic;
                auto snapshot = core::shareUnique(pool->prepareOriginalDescriptors(
                    selected, package->artifact(), {2u, true, true}, diagnostic));
                if (!snapshot)
                    std::fprintf(stderr, "depth Snapshot4: %s\n", diagnostic.c_str());
                assert(snapshot);
                assert(snapshot
                        ->writeOriginalCompletedTexture(
                            snapshot->handle(1u, 0u), *device, proof, {3u, true, 1u, 1u, 6u})
                        .success);
                assert(
                    snapshot->initializeInactiveSlots(snapshot->handle(1u, 0u), *device).success);
                assert(snapshot->writeSampler(snapshot->handle(2u, 0u), *sampler->originalObject())
                        .success);
                assert(snapshot->initializeInactiveSlots(snapshot->handle(2u, 0u)).success);
                assert(snapshot->writeBuffer(snapshot->handle(3u, 0u), *result->originalObject())
                        .success);
                if (profile == 5u || profile == 6u)
                    assert(snapshot
                            ->writeBuffer(snapshot->handle(3u, 1u), *secondResult->originalObject())
                            .success);
                else
                    assert(snapshot->initializeInactiveSlots(snapshot->handle(3u, 0u)).success);
                for (const auto table : {1u, 2u, 3u}) {
                    const auto count = (profile == 5u || profile == 6u) && table == 3u ? 2u : 1u;
                    assert(snapshot->activeCount(table) == count);
                    if (count == 1u)
                        assert(!snapshot->handle(table, 1u).generation);
                }
                auto sealed = snapshot->seal(*device);
                if (!sealed.success)
                    std::fprintf(stderr, "depth Snapshot4 seal: %s\n", sealed.diagnostics.c_str());
                assert(sealed.success);
                require(BindlessSnapshot::prepareOriginalBufferSources(
                    snapshot, *device, {1u, true, 2u, bytes}, q[5]));
                assert(snapshot->validate().success && pool->liveSnapshots() == 1u
                    && pool->chargedBytes() == package->artifact().snapshotBytes);
                const auto revision = snapshot->descriptorSet()->contentRevision();
                assert(!snapshot
                        ->writeOriginalCompletedTexture(
                            snapshot->handle(1u, 0u), *device, proof, {3u, true, 1u, 1u, 6u})
                        .success);
                assert(snapshot->descriptorSet()->contentRevision() == revision);
                assert(!pool->prepareOriginalDescriptors(
                    request, package->artifact(), {2u, true, true}, diagnostic));
                assert(pool->liveSnapshots() == 1u
                    && pool->chargedBytes() == package->artifact().snapshotBytes);
                if (profile == 4u)
                    assert(!extension->authorize(package, *snapshot, diagnostic)
                        && diagnostic == "RHI-WEBGPU-BINDLESS-AUTHORIZATION-PAYLOAD-INCOMPLETE");
                auto selectionRequest = selected;
                selectionRequest.shaderPackageIdentity = 0u;
                for (const auto policy : {BindlessPolicy::Auto, BindlessPolicy::Required}) {
                    const auto choice = semantics::selectBindlessVariant(policy,
                        extension->capabilities(),
                        selectionRequest,
                        &package->artifact(),
                        1u);
                    if (profile == 6u)
                        assert(choice.path == BindlessSelectionPath::Indexed);
                    else {
                        assert(choice.path
                            == (policy == BindlessPolicy::Required
                                    ? BindlessSelectionPath::Rejected
                                    : BindlessSelectionPath::Traditional));
                        assert(core::StringView(choice.diagnosticId)
                            == "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE");
                    }
                }
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(snapshot->validate().success);
                assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                return snapshot;
            };
            auto depthSnapshot = snapshotFor(completed);
            depthSnapshot.reset();
            depthSnapshot = snapshotFor(completed, 5u);
            std::shared_ptr<const WebGpuBindlessRootNativeClosure> depthRoot;
            if (depthSnapshot)
                depthRoot = WebGpuOriginalDepthRootPreparationFixture::prepare(depthSnapshot,
                    package,
                    views[2],
                    sampler,
                    result,
                    *device,
                    q[5],
                    nullptr,
                    secondResult);
            if (mixedPlanning) {
                assert(depthRoot);
                const auto lifetime = device->originalDeviceLifetimeTicket();
                OriginalMixedDepthStoragePlanFixture::verify<
                    rhi_webgpu::WebGpuBindlessRootNativeClosure>(
                    *device, depthRoot->originalRoot(), depthRoot->originalInventory());
                if (device->validateOriginalDeviceLifetime(lifetime))
                    assert(depthRoot->validate());
                else
                    require(depthRoot->validate());
            }
            auto depthNativeQuota = depthRoot ? depthRoot->originalMetadataQuota() : nullptr;
            if (!OriginalMixedDepthStoragePlanFixture::executionCheck) {
                std::weak_ptr<const OriginalCompletedDepthTextureSource> retiredProof = completed;
                const auto firstGeneration = completed->cells().front().contentGeneration();
                const auto untouchedGeneration = completed->cells().back().contentGeneration();
                clearOriginalMips(true);
                if (ordinaryClosure) {
                    assert(ordinaryClosure->validate());
                    const auto nativeQuota = ordinaryClosure->originalMetadataQuota();
                    ordinaryClosure.reset();
                    assert(!nativeQuota->liveBytes());
                    std::puts(
                        "BINDLESS_WEBGPU_ORDINARY_DEPTH_CLOSURE19_RETIRE_OK genuine-reclear stale-refused quota0 no-execution");
                }
                // The original journal advanced real content. A retained old binding
                // must fail before another command or descriptor publication can use it.
                assert(OriginalCompletedDepthTextureSource::validateOriginal(completed));
                const auto staleInput = OriginalCompletedDepthReadInputContract::inspect(
                    {completed,
                        ResourceState::ShaderRead,
                        PipelineStage::ComputeShader,
                        ResourceAccess::ShaderRead},
                    views[2],
                    {1u,
                        true,
                        2u,
                        20u,
                        OriginalCompletedDepthTextureSource::completeRetainedOwnerCount});
                assert(staleInput.diagnostic && !staleInput.cells && !staleInput.textureTexelBytes);
                assert(!set->validateBinding(0u).success);
                if (depthSnapshot)
                    assert(!depthSnapshot->validate().success);
                if (depthRoot)
                    assert(depthRoot->validate());
                const auto stale = set->writeOriginalCompletedTexture(
                    *device, write, completed, {3u, true, 1u, 1u, 6u});
                assert(!stale.success && set->contentRevision() == revision + 1u);
                auto fresh = OriginalCompletedDepthTextureSource::prepareOriginal(
                    views[2], {1u, true, 2u, bytes, 6u}, q[5], id);
                require(id);
                assert(fresh && fresh->cells().front().contentGeneration() > firstGeneration);
                assert(fresh->cells().back().contentGeneration() == untouchedGeneration
                    && fresh->cells().front().contentGeneration()
                        != fresh->cells().back().contentGeneration());
                const auto republished = set->writeOriginalCompletedTexture(
                    *device, write, fresh, {3u, true, 1u, 1u, 6u});
                assert(republished.success && set->contentRevision() == revision + 2u
                    && set->validateBinding(0u).success);
                depthRoot.reset();
                depthSnapshot.reset();
                if (depthNativeQuota)
                    assert(!depthNativeQuota->liveBytes());
                if (pool)
                    assert(!pool->liveSnapshots() && !pool->chargedBytes());
                completed.reset();
                assert(retiredProof.expired());
                depthSnapshot = snapshotFor(fresh);
                depthSnapshot.reset();
                depthSnapshot = snapshotFor(fresh, 5u);
                if (depthSnapshot)
                    depthRoot = WebGpuOriginalDepthRootPreparationFixture::prepare(depthSnapshot,
                        package,
                        views[2],
                        sampler,
                        result,
                        *device,
                        q[5],
                        nullptr,
                        secondResult);
                if (depthRoot)
                    depthNativeQuota = depthRoot->originalMetadataQuota();
                const auto contentRevision = fresh->cells().front().revision();
                const auto generation0 = fresh->cells().front().contentGeneration();
                const auto generation1 = fresh->cells().back().contentGeneration();
                clearOriginalMips(3u, fresh, depthSnapshot, depthRoot);
                if (depthRoot)
                    require(depthRoot->validate());
                clearOriginalMips(2u, fresh, depthSnapshot, depthRoot);
                if (depthRoot)
                    require(depthRoot->validate());
                assert(fresh->cells().front().revision() == contentRevision
                    && fresh->cells().back().revision() == contentRevision
                    && fresh->cells().front().contentGeneration() == generation0
                    && fresh->cells().back().contentGeneration() == generation1);
                std::puts(
                    "COMPLETED_DEPTH_READONLY_CELLS_OK same-Plan2-journal state-claim-exclusive content-unenlisted pending-proof-live rollback-preserved genuine-transition-completion mixed-generations-unchanged no-shader-execution");
                std::puts(
                    "COMPLETED_DEPTH_READ_INPUT1_OK canonical-view all-visible-cells mixed-generations pending-original-native bounded-cell-texel-owner hot0 input-only state-and-execution-closed");
                if (package) {
                    depthRoot.reset();
                    depthSnapshot.reset();
                    assert(!depthNativeQuota->liveBytes());
                    using Execution = WebGpuOriginalBindlessDepthExecutionFixture;
                    using Scenario = Execution::Scenario;
                    const std::array cases{std::pair{Scenario::ColdCancel, false},
                        std::pair{Scenario::RecordedCancel, false},
                        std::pair{Scenario::Success, false},
                        std::pair{Scenario::Success, true},
                        std::pair{Scenario::Reverse, false}};
                    const auto run = [&](const auto& test) {
                        Execution execute;
                        execute.scenario = test.first;
                        execute.fault = test.second;
                        const auto before = parent->originalObject()->state();
                        depthSnapshot = snapshotFor(fresh, 6u);
                        depthRoot =
                            WebGpuOriginalDepthRootPreparationFixture::prepare(depthSnapshot,
                                package,
                                views[2],
                                sampler,
                                result,
                                *device,
                                q[5],
                                &execute,
                                secondResult);
                        if (terminalScenario == UINT32_MAX)
                            require(depthRoot->validate());
                        assert(fresh->cells().front().contentGeneration() == generation0
                            && fresh->cells().back().contentGeneration() == generation1);
                        const bool completedRead =
                            test.first == Scenario::Success || test.first == Scenario::Reverse;
                        if (terminalScenario == UINT32_MAX)
                            assert(parent->originalObject()->state()
                                == (completedRead ? ResourceState::ShaderRead : before));
                        depthNativeQuota = depthRoot->originalMetadataQuota();
                        depthRoot.reset();
                        depthSnapshot.reset();
                        assert(!depthNativeQuota->liveBytes());
                    };
                    if (terminalScenario == UINT32_MAX)
                        for (const auto& test : cases)
                            run(test);
                    else {
                        assert(terminalScenario >= 3u && terminalScenario <= 10u
                            && terminalScenario != 5u);
                        run(std::pair{static_cast<Scenario>(terminalScenario), false});
                    }
                    if (terminalScenario == UINT32_MAX)
                        depthSnapshot = snapshotFor(fresh, 5u);
                }
                std::weak_ptr<const OriginalCompletedDepthTextureSource> retainedProof = fresh;
                fresh.reset();
                assert(!retainedProof.expired());
                set.reset();
                if (depthSnapshot) {
                    assert(!retainedProof.expired() && depthSnapshot->validate().success);
                    depthRoot.reset();
                    assert(depthNativeQuota && !depthNativeQuota->liveBytes());
                    depthSnapshot.reset();
                    assert(!pool->liveSnapshots() && !pool->chargedBytes());
                    std::puts(
                        "COMPLETED_DEPTH_SNAPSHOT4_OK authentic-package active1-physical2 same-depth-proof sealed stale-refused mixed-generation-replacement capacity-atomic internal-original-leases authorization-refused retired");
                    std::puts(
                        "COMPLETED_DEPTH_ROOT5_OK authentic-authorization pipeline physical-group canonical-root closure7 retained-input mixed-generations stale-refused bounded hot0 retired no-shader-state-or-execution");
                }
                assert(retainedProof.expired());
                std::puts(
                    "COMPLETED_DEPTH_PUBLIC_UPDATE3_OK same-original-cells genuine-second-completion mixed-mip-generations untouched-mip-preserved stale-binding-refused atomic-republication old-proof-retired");
                std::puts(
                    "COMPLETED_DEPTH_PUBLIC_DESCRIPTOR3_OK registry9 original-clear2-mips genuine-completion retained-proof no-shader-execution");
            } else {
                const auto* retired =
                    device->validateOriginalDeviceLifetime(device->originalDeviceLifetimeTicket());
                if (retired)
                    assert(depthRoot->validate());
                else
                    require(depthRoot->validate());
                depthRoot.reset();
                depthSnapshot.reset();
                assert(!depthNativeQuota->liveBytes());
                assert(!pool->liveSnapshots() && !pool->chargedBytes());
                if (retired)
                    std::puts(
                        "COMPLETED_DEPTH_MIXED_EXECUTION_RETIRED retired-device proof-refused pool0");
                else
                    std::puts(
                        "COMPLETED_DEPTH_MIXED_EXECUTION_RETIRED same-depth-proof contents-preserved pool0");
            }
        };
        run();
        for (const auto& quota : q)
            assert(!quota->liveBytes());
#endif
    }
};
} // namespace arp::test
