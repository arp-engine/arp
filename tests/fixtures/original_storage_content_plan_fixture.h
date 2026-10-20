#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_storage_pipeline_fixture.h"
#include "original_storage_snapshot_fixture.h"
#include "rhi/frontend/command_state.h"

namespace arp::test {
class OriginalStorageContentPlanFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Storage content plan: %s\n", id);
        assert(!id);
    }

public:
    static void verifyContract()
    {
        using namespace rhi;
        using C = OriginalStorageTextureContentPlanContract;
        using Creation = OriginalTextureContentCreationContract;
        static_assert(C::controlVersion(13u) == 6u && C::controlVersion(14u) == 7u);
        static_assert(!C::controlVersion(12u) && !C::controlVersion(17u));
        TextureDesc texture;
        texture.extent = {4u, 4u, 1u};
        texture.mipLevelCount = 3u;
        texture.format = TextureFormat::Rgba16Float;
        TextureViewDesc view;
        view.format = texture.format;
        view.usage = TextureUsage::Storage;
        require(C::inspectViewBinding(texture,
            view,
            ResourceState::ShaderWrite,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderWrite,
            true));
        assert(C::inspectViewBinding(texture,
            view,
            ResourceState::ShaderWrite,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
            true));
        assert(C::inspectViewBinding(texture,
            view,
            ResourceState::ShaderWrite,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderWrite,
            true));
        view.mipLevelCount = 2u;
        assert(C::inspectViewBinding(texture,
            view,
            ResourceState::ShaderWrite,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderWrite,
            true));
        view.mipLevelCount = 1u;
        view.usage = TextureUsage::Sampled;
        require(C::inspectViewBinding(texture,
            view,
            ResourceState::ShaderRead,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead,
            false));
        view.baseMipLevel = 3u;
        assert(C::inspectViewBinding(texture,
            view,
            ResourceState::ShaderRead,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead,
            false));
        for (auto version : {13u, 14u}) {
            OriginalTextureContentPlanBudget b{version, true, 1u, 0u, 3u, 2u, 16u, 1024u};
            require(C::validateIntent(b));
            for (unsigned field = 0u; field != 8u; ++field) {
                auto bad = b;
                if (field == 0u)
                    bad.contractVersion = 17u;
                if (field == 1u)
                    bad.prototypeOptIn = false;
                if (field == 2u)
                    bad.maxContentControls = 0u;
                if (field == 3u)
                    bad.maxContentCells = 0u;
                if (field == 4u)
                    bad.maxContentSteps = 0u;
                if (field == 5u)
                    bad.maxCommandVisits = 0u;
                if (field == 6u)
                    bad.maxProjectionVisits = 0u;
                if (field == 7u)
                    bad.maxUploadCommands = 1u;
                assert(C::validateIntent(bad));
            }
            auto bad = b;
            bad.maxReadbackCommands = 1u;
            assert(C::validateIntent(bad));
            for (std::uint32_t control = 1u; control != 8u; ++control)
                assert(Creation::cellControlMatchesPlan(control, version)
                    == (control == C::controlVersion(version)));
            for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto d = C::lowering(backend, version);
                assert(d.contractVersion == version && !d.executionAvailable && d.diagnostic
                    && d.validatorOnly == (backend == Backend::Null)
                    && d.validationAvailable
                        == (backend == Backend::Null
                            || (version == 13u
                                    ? backend == Backend::Vulkan || backend == Backend::Metal
                                    : backend == Backend::WebGPU)));
            }
            assert(!C::lowering(static_cast<Backend>(99u), version).validationAvailable);
        }
        for (const auto version : {15u, 16u}) {
            const bool native = version == 15u;
            assert(C::isMixedVersion(version)
                && C::storagePlanVersion(version) == (native ? 13u : 14u)
                && C::depthPlanVersion(version) == (native ? 12u : 11u)
                && C::controlVersion(version) == (native ? 6u : 7u));
            OriginalTextureContentPlanBudget b{version, true, 2u, 0u, 4u, 8u, 64u, 4096u};
            require(C::validateIntent(b));
            for (unsigned field = 0u; field != 9u; ++field) {
                auto bad = b;
                if (field == 0u)
                    bad.contractVersion = 17u;
                if (field == 1u)
                    bad.prototypeOptIn = false;
                if (field == 2u)
                    bad.maxContentControls = 1u;
                if (field == 3u)
                    bad.maxContentCells = 1u;
                if (field == 4u)
                    bad.maxContentSteps = 0u;
                if (field == 5u)
                    bad.maxCommandVisits = 0u;
                if (field == 6u)
                    bad.maxProjectionVisits = 0u;
                if (field == 7u)
                    bad.maxUploadCommands = 1u;
                if (field == 8u)
                    bad.maxReadbackCommands = 1u;
                assert(C::validateIntent(bad));
            }
            // Read-only completed D32 content is never authorized for mutation.
            assert(!Creation::cellControlMatchesPlan(native ? 4u : 2u, version));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto d = C::lowering(backend, version);
                assert(d.contractVersion == version && d.diagnostic && !d.executionAvailable
                    && d.validatorOnly == (backend == Backend::Null)
                    && d.validationAvailable
                        == (backend == Backend::Null
                            || (native ? backend == Backend::Vulkan || backend == Backend::Metal
                                       : backend == Backend::WebGPU)));
            }
            assert(!C::lowering(static_cast<Backend>(99u), version).validationAvailable);
        }
        std::puts(
            "BINDLESS_RHI_MIXED_DEPTH_STORAGE_CONTENT1516_CONTRACT_OK bounded read-only-D32 storage67 same-journal execution-closed");
        std::puts(
            "BINDLESS_RHI_STORAGE_CONTENT_PLAN1314_CONTRACT_OK typed-cell-profile compute-write-only single-mip bounded native-closed");
    }

    // Real exclusive resources, authentic inventory/graph and the original
    // mutation journal. CPU proposal/rollback only, never a GPU receipt.
    static std::shared_ptr<const rhi::OriginalCommandTextureLease> verifyDevice(
        rhi::GpuDevice& device,
        core::UniquePtr<rhi::GpuTexture>& texture,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
        OriginalTextureCellDescriptorFixture::NativeExportCheck exportCheck = nullptr,
        OriginalStoragePipelineFixture::RootCheck rootCheck = nullptr)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        using S = ResourceState;
        using Root = OriginalCommandStateTransaction;
        using Content = common::OriginalTextureContentControl;
        const char* id = nullptr;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
        TextureViewDesc vd;
        vd.format = texture->desc().format;
        vd.usage = TextureUsage::RenderAttachment;
        vd.baseMipLevel = 1u;
        core::UniquePtr<GpuTextureView> view;
        {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            view = device.createTextureView(*texture, vd);
        }
        assert(view);
        auto parent =
            OriginalCommandTextureLease::prepareOriginal(texture, {1u, true, 2u, bytes}, q[0], id);
        require(id);
        assert(parent);
        auto publication = OriginalTextureCellDescriptorFixture::prepareDevice(
            device, parent, resourceQuota, exportCheck);
        OriginalStoragePipelineFixture::verifyDevice(
            device, *publication, resourceQuota, rootCheck);
        auto snapshot =
            OriginalStorageSnapshotFixture::prepareDevice(device, *publication, resourceQuota);
        auto lease =
            OriginalCommandTextureViewLease::prepareOriginal(view, {1u, true, 2u, bytes}, q[0], id);
        require(id);
        auto attachment = OriginalCommandTextureViewSource::prepareOriginal(
            lease, parent, {1u, true, 5u, bytes}, q[0], id);
        require(id);
        const auto* object = parent->originalObject();
        const TextureSubresourceRange range{1u, 1u, 0u, 1u, TextureAspect::Color};
        RenderPassDesc pd;
        RenderPassColorAttachmentDesc color;
        color.target = attachment->resource();
        color.format = vd.format;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        pd.colorAttachments.push_back(color);
        auto pass = RenderPassArguments::prepareOriginal(
            pd, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, q[1], id);
        require(id);
        assert(pass);
        ResourceBarrierDesc barrier{parent->resource(), range, S::Undefined, S::ColorAttachment};
        barrier.sourceStages = PipelineStage::TopOfPipe;
        barrier.sourceAccess = ResourceAccess::None;
        barrier.destinationStages = PipelineStage::ColorAttachmentOutput;
        barrier.destinationAccess =
            ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite;
        const std::array<OriginalCommandResourceSources::Source, 2u> retained{parent, attachment};
        const std::array<CommandUsage, 4u> commands{{{K::TextureBarrier, 0u, 1u},
            {K::BeginRenderPass, 1u, 1u},
            {K::EndRenderPass, 2u, 0u},
            {K::Finish, 2u, 0u}}};
        const std::array<OriginalCommandUsage::Reference, 2u> refs{{{0u}, {1u}}};
        const std::array<OriginalCommandInputs::OriginalDescriptor, 2u> descriptors{
            {{0u, *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)}, {1u, pass}}};
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxStateTransactions = 1u;
        admission.maxStateObservations = 2u;
        admission.maxTextureRanges = 2u;
        admission.maxPassAttachmentReferences = admission.maxPassColorAttachments = 1u;
        admission.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Recording)] = bytes;
        admission.maxCommands = commands.size();
        admission.maxReferences = refs.size();
        admission.maxInputDescriptors = descriptors.size();
        admission.maxGraphOwners = 64u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] = 1u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)] =
            1u;
        admission.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Source)] = bytes;
        admission.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Graph)] = bytes;
        auto inventory = OriginalCommandResourceSources::prepareOriginal(
            retained.data(), retained.size(), admission, q[0], id);
        require(id);
        assert(inventory);
        const std::array<CommandVariableArguments::Input, 4u> fields{
            {{*CommandScalarArguments::capture(K::TextureBarrier, CommandScalarArguments::None{})},
                {*CommandScalarArguments::capture(
                    K::BeginRenderPass, CommandScalarArguments::None{})},
                {*CommandScalarArguments::capture(
                    K::EndRenderPass, CommandScalarArguments::None{})},
                {*CommandScalarArguments::capture(K::Finish, CommandScalarArguments::None{})}}};
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            fields.data(),
            descriptors.data(),
            descriptors.size(),
            {},
            q[1],
            id);
        require(id);
        assert(inputs);
        std::uint32_t owners = 0u, registrations = 0u;
        require(OriginalCommandResourceSources::registeredOwnerUnion(
            inputs->originalScalars()->originalUsage()->originalSources(),
            inputs->retainedOwnerCount() + 1u,
            UINT32_MAX,
            UINT32_MAX,
            owners,
            registrations));
        OriginalCommandStateAdmissionBudget state{
            1u, true, 1u, registrations, 2u, 2u, owners + 2u, bytes};
        auto graph =
            OriginalCommandResourceStates::prepareOriginal(inputs, device, state, q[2], id);
        require(id);
        assert(graph);
        const auto make = [&] {
            return Root::prepareOriginalOwnership(graph, device, state, q[3], id);
        };
        OriginalCommandStateSequenceBudget sequence{1u, true, 4u, 0u, 3u, 3u};
        OriginalTextureContentPlanBudget budget{
            device.backend() == Backend::WebGPU ? 14u : 13u, true, 1u, 0u, 3u, 2u, 16u, 4096u};
        auto root = make();
        require(id);
        assert(root);
        auto inspected = root->preflightOriginalContentCells(budget);
        require(inspected.diagnostic);
        assert(inspected.controls == 1u && inspected.cells == 3u && inspected.steps == 2u
            && inspected.commandVisits == 16u);
        budget.maxProjectionVisits = inspected.projectionVisits;
        const auto refused = [&](auto bad) {
            const auto r = root->preflightOriginalContentCells(bad);
            assert(r.diagnostic && !r.controls && !r.cells && !r.steps && !r.commandVisits
                && !r.projectionVisits && root->journalEmpty()
                && !root->hasOriginalPrototypePlan());
        };
        auto bad = budget;
        --bad.maxContentCells;
        refused(bad);
        bad = budget;
        --bad.maxContentSteps;
        refused(bad);
        bad = budget;
        --bad.maxCommandVisits;
        refused(bad);
        bad = budget;
        --bad.maxProjectionVisits;
        refused(bad);
        bad = budget;
        bad.contractVersion = budget.contractVersion == 13u ? 14u : 13u;
        refused(bad);
        bad = budget;
        bad.contractVersion = 2u;
        refused(bad);
        bad = budget;
        bad.maxReadbackCommands = 1u;
        refused(bad);
        bad = budget;
        bad.contractVersion = budget.contractVersion == 13u ? 15u : 16u;
        bad.maxContentControls = 2u;
        const auto baseline = q[3]->liveBytes();
        const auto mixed = root->preflightOriginalContentCells(bad);
        assert(mixed.diagnostic
            && core::StringView(mixed.diagnostic) == "RHI-MIXED-DEPTH-STORAGE-CONTENT-COVERAGE");
        refused(bad);
        assert(q[3]->liveBytes() == baseline);
        root.reset();
        const auto planned = [&] {
            auto candidate = make();
            if (candidate && (id = candidate->materializeOriginalPrototypePlan(sequence, budget)))
                candidate.reset();
            return candidate;
        };
        CpuQuotaFactoryFixture pressure(q[3]);
        const auto peak = pressure.measure(planned);
        pressure.verify(peak, planned);
        root = planned();
        require(id);
        assert(root && root->plannedContentCellCount() == 3u
            && root->plannedContentControlCount() == 1u);
        auto original = object->observeOriginalContent(range);
        assert(original);
        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
            if (const auto* diagnostic = root->applyOriginalPrototypePlan())
                return diagnostic;
            assert(root->status() == Root::Status::Pending && !root->journalEmpty()
                && !object->observeOriginalContent(range));
            assert(object->validateOriginalContent(*original));
            require(object->validateOriginalContentIdentity(*original));
            publication->validate();
            assert(snapshot->validate().success);
            assert(root->commitOriginalPendingOwnership());
            if (const auto* diagnostic = root->validate())
                return diagnostic;
            return root->abandonOriginalPendingOwnership();
        }));
        auto restored = object->observeOriginalContent(range);
        assert(restored && restored->state() == Content::State::Uninitialized
            && !restored->contentGeneration() && restored->revision() > original->revision()
            && original->diagnostic());
        require(object->validateOriginalContentIdentity(*original));
        publication->validate();
        assert(snapshot->validate().success);
        for (std::uint32_t mip = 0u; mip != 3u; ++mip) {
            const auto cell =
                object->observeOriginalContent({mip, 1u, 0u, 1u, TextureAspect::Color});
            assert(cell && cell->state() == Content::State::Uninitialized
                && !cell->contentGeneration());
        }
        // Rebuild observations after rollback; dropping a pending root must
        // roll the same journal back without publishing completion.
        root.reset();
        graph.reset();
        graph = OriginalCommandResourceStates::prepareOriginal(inputs, device, state, q[2], id);
        require(id);
        assert(graph);
        root = planned();
        require(id);
        assert(root);
        require(root->applyOriginalPrototypePlan());
        root.reset();
        assert(restored->diagnostic()
            && object->observeOriginalContent(range)->revision() > restored->revision());
        publication->validate();
        assert(snapshot->validate().success);
        std::printf(
            "BINDLESS_RHI_STORAGE_SNAPSHOT7_DEVICE_OK backend=%u mip1 native-arrays-or-emulated padding immutable original-buffers pending-rollback pool-retirement no-execution\n",
            static_cast<unsigned>(device.backend()));
        std::printf(
            "BINDLESS_RHI_TEXTURE_CELL_DESCRIPTOR12_DEVICE_OK backend=%u mip1 sampled-write-only identity-pending-stale retention refusal rollback no-completion\n",
            static_cast<unsigned>(device.backend()));
        std::printf(
            "BINDLESS_RHI_STORAGE_CONTENT_PLAN1314_DEVICE_OK backend=%u cells3 steps2 same-journal proposal rollback-drop no-cpu-completion exact-less hot0 native-closed\n",
            static_cast<unsigned>(device.backend()));
        return parent;
    }
};
} // namespace arp::test
