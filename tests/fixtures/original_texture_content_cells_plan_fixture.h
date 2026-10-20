#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_state.h"
#include "texture_readback_source_lease_fixture.h"

namespace arp::test {
class OriginalTextureContentCellsPlanFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "ContentPlan2 expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verify(rhi::GpuDevice& device,
        core::UniquePtr<rhi::GpuTexture>& texture,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota)
    {
        using K = rhi::CommandUsageKind;
        using S = rhi::ResourceState;
        using Root = rhi::OriginalCommandStateTransaction;
        using Graph = rhi::OriginalCommandResourceStates;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        using Content = rhi::common::OriginalTextureContentControl;
        const char* id = nullptr;
        const rhi::TextureSubresourceRange range{1u, 1u, 2u, 1u, rhi::TextureAspect::Depth};
        rhi::TextureViewDesc vd;
        vd.format = texture->desc().format;
        vd.usage = rhi::TextureUsage::RenderAttachment;
        vd.baseMipLevel = range.baseMipLevel;
        vd.baseArrayLayer = range.baseArrayLayer;
        vd.aspect = range.aspect;
        core::UniquePtr<rhi::GpuTextureView> view;
        {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            view = device.createTextureView(*texture, vd);
        }
        assert(view);
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto parent = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, bytes}, sourceQuota, id);
        auto lease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, bytes}, sourceQuota, id);
        auto attachment = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            lease, parent, {1u, true, 5u, bytes}, sourceQuota, id);
        assert(attachment && !id);
        rhi::RenderPassDesc clear;
        clear.hasDepthStencilAttachment = true;
        clear.depthStencilAttachment.target = attachment->resource();
        clear.depthStencilAttachment.format = vd.format;
        clear.depthStencilAttachment.depthLoadOp = rhi::LoadOp::Clear;
        clear.depthStencilAttachment.depthStoreOp = rhi::StoreOp::Store;
        clear.depthStencilAttachment.stencilLoadOp = rhi::LoadOp::DontCare;
        clear.depthStencilAttachment.stencilStoreOp = rhi::StoreOp::DontCare;
        clear.depthStencilAttachment.depthClearValue = 0.75f;
        auto load = rhi::RenderPassStateCopy::copy(clear);
        load.depthStencilAttachment.depthLoadOp = rhi::LoadOp::Load;
        const rhi::RenderPassArgumentsBudget passBudget{1u, true, 0u, 0u, 0u, 0u, 0u, bytes};
        const auto clearPass =
            rhi::RenderPassArguments::prepareOriginal(clear, passBudget, inputQuota, id);
        const auto loadPass =
            rhi::RenderPassArguments::prepareOriginal(load, passBudget, inputQuota, id);
        assert(clearPass && loadPass);
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = 6u;
        admission.maxReferences = 3u;
        admission.maxInputDescriptors = 3u;
        admission.maxGraphOwners = 64u;
        admission.maxStateObservations = 3u;
        admission.maxTextureRanges = 3u;
        admission.maxStateTransactions = 1u;
        admission.maxPassAttachmentReferences = 2u;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
            1u;
        admission
            .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] =
            1u;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Source)] = bytes;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Graph)] = bytes;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)] = bytes;
        const std::array<rhi::OriginalCommandResourceSources::Source, 2> retained{
            parent, attachment};
        auto sources = rhi::OriginalCommandResourceSources::prepareOriginal(
            retained.data(), retained.size(), admission, sourceQuota, id);
        if (!sources)
            std::fprintf(stderr, "ContentPlan2 Sources: %s\n", id ? id : "missing");
        assert(sources && !id);
        const std::array<rhi::CommandUsage, 6> commands{{{K::TextureBarrier, 0u, 1u},
            {K::BeginRenderPass, 1u, 1u},
            {K::EndRenderPass, 2u, 0u},
            {K::BeginRenderPass, 2u, 1u},
            {K::EndRenderPass, 3u, 0u},
            {K::Finish, 3u, 0u}}};
        const std::array<rhi::OriginalCommandUsage::Reference, 3> refs{{{0u}, {1u}, {1u}}};
        const std::array<rhi::CommandVariableArguments::Input, 6> fields{
            {{*Scalar::capture(K::TextureBarrier, Scalar::None{})},
                {*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        rhi::ResourceBarrierDesc barrier{
            parent->resource(), range, S::Undefined, S::DepthAttachment};
        barrier.sourceStages = rhi::PipelineStage::TopOfPipe;
        barrier.sourceAccess = rhi::ResourceAccess::None;
        barrier.destinationStages = rhi::PipelineStage::DepthStencil;
        barrier.destinationAccess =
            rhi::ResourceAccess::DepthStencilRead | rhi::ResourceAccess::DepthStencilWrite;
        std::array<Inputs::OriginalDescriptor, 3> records{
            {{0u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier)},
                {1u, clearPass},
                {3u, loadPass}}};
        const auto assemble = [&] {
            return OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                records.data(),
                records.size(),
                {},
                inputQuota,
                id);
        };
        auto inputs = assemble();
        if (!inputs)
            std::fprintf(stderr, "ContentPlan2 Inputs: %s\n", id ? id : "missing");
        assert(inputs && !id);
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            UINT32_MAX,
            UINT32_MAX,
            owners,
            registrations));
        rhi::OriginalCommandStateAdmissionBudget graphBudget{
            1u, true, 1u, registrations, 3u, 3u, owners, bytes};
        auto graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, id);
        if (!graph)
            std::fprintf(stderr, "ContentPlan2 Graph: %s\n", id ? id : "missing");
        assert(graph && !id);
        auto rootBudget = graphBudget;
        rootBudget.maxRetainedOwners += 2u;
        const auto make = [&] {
            return Root::prepareOriginalOwnership(graph, device, rootBudget, recordingQuota, id);
        };
        const rhi::OriginalCommandStateSequenceBudget sequence{1u, true, 6u, 0u, 9u, 9u};
        rhi::OriginalTextureContentPlanBudget budget{2u, true, 1u, 0u, 9u, 4u, 60u, 1000u};
        auto root = make();
        if (!root)
            std::fprintf(stderr, "ContentPlan2 Root: %s\n", id ? id : "missing");
        assert(root && !id);
        auto result = root->preflightOriginalContentCells(budget);
        if (result.diagnostic)
            std::fprintf(stderr, "ContentPlan2 preflight: %s\n", result.diagnostic);
        assert(!result.diagnostic && result.controls == 1u && result.cells == 9u
            && result.steps == 4u && result.commandVisits == 60u
            && !result.nativeExecutionProven());
        budget.maxProjectionVisits = result.projectionVisits;
        const auto reject = [&](auto changed, const char* expected) {
            const auto refused = root->preflightOriginalContentCells(changed);
            is(refused.diagnostic, expected);
            assert(!refused.cells && !refused.controls && !refused.steps && !refused.commandVisits
                && !refused.projectionVisits && root->journalEmpty()
                && !root->hasOriginalPrototypePlan());
        };
        auto bad = budget;
        --bad.maxContentCells;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-CELL-CAPACITY");
        bad = budget;
        --bad.maxContentSteps;
        reject(bad, "RHI-ORIGINAL-CONTENT-SEQUENCE-STEP-CAPACITY");
        bad = budget;
        --bad.maxCommandVisits;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-COMMAND-CAPACITY");
        bad = budget;
        --bad.maxProjectionVisits;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-PROJECTION-CAPACITY");
        bad = budget;
        bad.prototypeOptIn = false;
        reject(bad, "RHI-CANONICAL-CONTENT-PLAN-OPT-IN");
        bad = budget;
        bad.maxUploadCommands = 1u;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-CELL-BUDGET");
        bad = budget;
        bad.maxContentControls = 0u;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-CELL-BUDGET");
        bad = budget;
        bad.contractVersion = 3u;
        bad.maxReadbackCommands = 1u;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-READBACK-MISSING");
        is(root->materializeOriginalPrototypePlan(sequence),
            "RHI-CANONICAL-CONTENT-PLAN-CELL-PROFILE-UNIMPLEMENTED");
        assert(root->journalEmpty() && !root->hasOriginalPrototypePlan());
        root.reset();
        const auto planned = [&] {
            auto candidate = make();
            if (candidate && (id = candidate->materializeOriginalPrototypePlan(sequence, budget)))
                candidate.reset();
            return candidate;
        };
        CpuQuotaFactoryFixture pressure(recordingQuota);
        const auto peak = pressure.measure(planned);
        pressure.verify(peak, planned);
        root = planned();
        if (!root)
            std::fprintf(stderr, "ContentPlan2 plan: %s\n", id ? id : "missing");
        assert(root && !id && root->plannedContentControlCount() == 1u
            && root->plannedContentCellCount() == 9u && !root->plannedContentUploadCount());
        const auto* object = parent->originalObject();
        auto original = object->observeOriginalContent(range);
        assert(original);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, inputQuota, stateQuota, recordingQuota},
            [&]() noexcept -> const char* {
                if (const auto* diagnostic = root->applyOriginalPrototypePlan())
                    return diagnostic;
                assert(root->status() == Root::Status::Pending && !root->journalEmpty()
                    && !object->observeOriginalContent(range));
                is(root->commitOriginalPendingOwnership(),
                    "RHI-CANONICAL-CONTENT-NATIVE-COMPLETION-REQUIRED");
                if (const auto* diagnostic = root->validate())
                    return diagnostic;
                return root->abandonOriginalPendingOwnership();
            }));
        auto restored = object->observeOriginalContent(range);
        assert(restored && restored->state() == Content::State::Uninitialized
            && !restored->contentGeneration() && restored->revision() > original->revision()
            && original->diagnostic());
        // Every untouched mip/layer remains genuinely uninitialized after rollback.
        for (std::uint32_t mip = 0u; mip < 3u; ++mip)
            for (std::uint32_t layer = 0u; layer < 3u; ++layer) {
                auto cell =
                    object->observeOriginalContent({mip, 1u, layer, 1u, rhi::TextureAspect::Depth});
                assert(cell && cell->state() == Content::State::Uninitialized
                    && !cell->contentGeneration());
            }
        root.reset();
        graph.reset();
        graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, id);
        assert(graph);
        root = planned();
        assert(root && !root->applyOriginalPrototypePlan());
        root.reset();
        assert(restored->diagnostic()
            && object->observeOriginalContent(range)->revision() > restored->revision());
        graph.reset();
        // The frozen second pass may Load only after the first pass stores.
        records[1].arguments = loadPass;
        inputs = assemble();
        assert(inputs);
        graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, id);
        assert(graph);
        root = make();
        assert(root);
        is(root->preflightOriginalContentCells(budget).diagnostic,
            "RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED");
        assert(root->journalEmpty());
        root.reset();
        graph.reset();
        auto discard = rhi::RenderPassStateCopy::copy(clear);
        discard.depthStencilAttachment.depthStoreOp = rhi::StoreOp::DontCare;
        records[1].arguments =
            rhi::RenderPassArguments::prepareOriginal(discard, passBudget, inputQuota, id);
        inputs = assemble();
        assert(inputs);
        graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, id);
        assert(graph);
        root = make();
        assert(root);
        is(root->preflightOriginalContentCells(budget).diagnostic,
            "RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED");
        assert(root->journalEmpty());
        std::printf(
            "BINDLESS_RHI_CONTENT_PLAN2_OK peak%llu controls1 cells9 steps4 commandVisits60 projectionVisits%llu frozen-two-passes partial-clear-store-load same-root-journal rollback-drop uninitialized-others exact-less no-partial hot0 native-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(budget.maxProjectionVisits));
        root.reset();
        graph.reset();
        inputs.reset();
        sources.reset();
        verifyReadback(device, sourceQuota, resourceQuota);
    }

private:
    static void verifyReadback(rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota)
    {
        using K = rhi::CommandUsageKind;
        using S = rhi::ResourceState;
        using Scalar = rhi::CommandScalarArguments;
        using Sources = rhi::OriginalCommandResourceSources;
        using Inputs = rhi::OriginalCommandInputs;
        using Graph = rhi::OriginalCommandResourceStates;
        using Root = rhi::OriginalCommandStateTransaction;
        using Content = rhi::common::OriginalTextureContentControl;
        const char* id = nullptr;
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto contentQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const rhi::TextureSubresourceRange range{1u, 1u, 2u, 1u, rhi::TextureAspect::Color};
        // Readback's existing color-only layout contract stays intact. The
        // same genuine parent is adopted once and used by attachment + Source2.
        rhi::TextureDesc td;
        td.extent = {8u, 8u, 1u};
        td.format = rhi::TextureFormat::Rgba8Unorm;
        td.mipLevelCount = td.arrayLayerCount = 3u;
        td.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        core::UniquePtr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
        {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            texture = device.createOriginalContentTexture(
                td, {2u, true, 1u, 1u, 1008u, 4u, bytes, 9u}, contentQuota, id);
            assert(texture && !id);
            rhi::TextureViewDesc vd;
            vd.format = td.format;
            vd.usage = rhi::TextureUsage::RenderAttachment;
            vd.baseMipLevel = range.baseMipLevel;
            vd.baseArrayLayer = range.baseArrayLayer;
            view = device.createTextureView(*texture, vd);
            assert(view);
        }
        auto parent = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, bytes}, sourceQuota, id);
        auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, bytes}, sourceQuota, id);
        auto attachment = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            viewLease, parent, {1u, true, 5u, bytes}, sourceQuota, id);
        assert(attachment && !id);
        rhi::BufferDesc bufferDesc;
        bufferDesc.sizeBytes = 1024u;
        bufferDesc.usage = rhi::ResourceUsage::CopyDst;
        bufferDesc.memory = rhi::MemoryUsage::GpuToCpu;
        bufferDesc.initialState = S::CopyDst;
        core::UniquePtr<rhi::GpuBuffer> buffer;
        {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            buffer = device.createBuffer(bufferDesc);
        }
        assert(buffer);
        auto destination = rhi::OriginalCommandBufferLease::prepareOriginal(
            buffer, {1u, true, 2u, bytes}, sourceQuota, id);
        assert(destination && !id);
        const auto declareReadback = [&](rhi::TextureSubresourceRange copied) {
            rhi::ReadbackTextureDesc read;
            read.sourceResource = parent->resource();
            read.destinationReadbackBuffer = destination->resource();
            read.extent = {4u, 4u, 1u};
            read.bytesPerRow = 256u;
            read.rowsPerImage = 4u;
            read.sizeBytes = bufferDesc.sizeBytes;
            read.sourceSubresources = copied;
            read.label = "copy";
            auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
                read, {1u, true, 1u, 4u, bytes, bytes}, inputQuota, inputQuota, id);
            assert(input && !id);
            auto layout = rhi::TextureReadbackLayout::prepareOriginal(input,
                parent->originalObject()->desc(),
                {1u, true, 1u, read.sizeBytes, bytes},
                inputQuota,
                id);
            if (!layout)
                std::fprintf(stderr, "ContentPlan3 layout: %s\n", id ? id : "missing");
            assert(layout && !id);
            auto request = TextureReadbackSourceLeaseFixture::request(layout, inputQuota);
            auto leaseBudget = TextureReadbackSourceLeaseFixture::budget(request);
            leaseBudget.contractVersion = 2u;
            leaseBudget.maxRetainedOwners =
                rhi::TextureReadbackSourceLease::retainedOriginalOwnerCount;
            auto lease = rhi::TextureReadbackSourceLease::prepareRetainedOriginal(
                parent, request, leaseBudget, sourceQuota, id);
            assert(lease && !id);
            rhi::OriginalCommandReadbackSourceBudget sourceBudget{
                2u, true, 8u, 1u, 4u, read.sizeBytes, bytes, bytes};
            auto source = rhi::OriginalCommandReadbackTextureSource::prepareOriginal(
                lease, sourceBudget, sourceQuota, inputQuota, id);
            assert(source && !id);
            sourceBudget.maxRetainedOwners =
                rhi::OriginalCommandTextureReadbackSource::completeRetainedOwnerCount
                + rhi::TextureReadbackSourceLease::retainedOriginalOwnerCount
                - rhi::TextureReadbackSourceLease::retainedOwnerCount;
            auto result = rhi::OriginalCommandTextureReadbackSource::prepareOriginal(
                source, destination, sourceBudget, sourceQuota, id);
            if (!result)
                std::fprintf(stderr, "ContentPlan3 Source2: %s\n", id ? id : "missing");
            assert(result && !id);
            return result;
        };
        auto readback = declareReadback(range);
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = 9u;
        admission.maxReferences = 7u;
        admission.maxInputDescriptors = 5u;
        admission.maxGraphOwners = 64u;
        admission.maxStateObservations = 6u;
        admission.maxTextureRanges = 5u;
        admission.maxStateTransactions = 1u;
        admission.maxPassAttachmentReferences = 1u;
        admission.maxPassColorAttachments = 1u;
        admission.maxMipMappings = 2u;
        admission.maxLabelBytes = 16u;
        admission.maxUnpublishedResultBytes = 2048u;
        admission.maxCpuBytes.fill(bytes);
        for (const auto domain : {rhi::CommandReferenceDomain::Texture,
                 rhi::CommandReferenceDomain::TextureView,
                 rhi::CommandReferenceDomain::Buffer,
                 rhi::CommandReferenceDomain::Readback})
            admission.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
        const rhi::OriginalRetainedReadbackCorrespondence correspondence{1u, true, 1u, 7u};
        std::array<Sources::Source, 4> retained{parent, attachment, destination, readback};
        auto sources = Sources::prepareOriginal(
            retained.data(), retained.size(), admission, sourceQuota, id, correspondence);
        if (!sources)
            std::fprintf(stderr, "ContentPlan3 Sources: %s\n", id ? id : "missing");
        assert(sources && !id);
        const std::array<rhi::CommandUsage, 9> commands{{{K::TextureBarrier, 0u, 1u},
            {K::BeginRenderPass, 1u, 1u},
            {K::EndRenderPass, 2u, 0u},
            {K::TextureBarrier, 2u, 1u},
            {K::BeginTransferPass, 3u, 0u},
            {K::TextureReadback, 3u, 2u},
            {K::TextureReadback, 5u, 2u},
            {K::EndTransferPass, 7u, 0u},
            {K::Finish, 7u, 0u}}};
        const std::array<rhi::OriginalCommandUsage::Reference, 7> refs{
            {{0u}, {1u}, {0u}, {0u}, {3u}, {0u}, {3u}}};
        const std::array<rhi::CommandVariableArguments::Input, 9> fields{
            {{*Scalar::capture(K::TextureBarrier, Scalar::None{})},
                {*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::TextureBarrier, Scalar::None{})},
                {*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "copy"},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "copy"},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        rhi::RenderPassDesc pass;
        rhi::RenderPassColorAttachmentDesc color;
        color.target = attachment->resource();
        color.format = td.format;
        color.loadOp = rhi::LoadOp::Clear;
        color.storeOp = rhi::StoreOp::Store;
        color.clearColor = {0.25f, 0.5f, 0.75f, 1.0f};
        pass.colorAttachments.push_back(color);
        const auto freezePass = [&] {
            auto result = rhi::RenderPassArguments::prepareOriginal(
                pass, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, inputQuota, id);
            assert(result && !id);
            return result;
        };
        rhi::ResourceBarrierDesc begin{parent->resource(), range, S::Undefined, S::ColorAttachment};
        begin.sourceStages = rhi::PipelineStage::TopOfPipe;
        begin.sourceAccess = rhi::ResourceAccess::None;
        begin.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
        begin.destinationAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        rhi::ResourceBarrierDesc copy{parent->resource(), range, S::ColorAttachment, S::CopySrc};
        copy.sourceStages = rhi::PipelineStage::ColorAttachmentOutput;
        copy.sourceAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        copy.destinationStages = rhi::PipelineStage::Transfer;
        copy.destinationAccess = rhi::ResourceAccess::TransferRead;
        const auto copied = [&] {
            return *rhi::TextureReadbackOriginalInputArguments::captureOriginal(
                readback->originalInput());
        };
        std::array<Inputs::OriginalDescriptor, 5> records{
            {{0u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, begin)},
                {1u, freezePass()},
                {3u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, copy)},
                {5u, copied()},
                {6u, copied()}}};
        rhi::OriginalCommandInputsBudget inputBudget;
        inputBudget.retainedReadback = correspondence;
        const auto assemble = [&] {
            return OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                records.data(),
                records.size(),
                inputBudget,
                inputQuota,
                id);
        };
        auto inputs = assemble();
        if (!inputs)
            std::fprintf(stderr, "ContentPlan3 Inputs: %s\n", id ? id : "missing");
        assert(inputs && !id);
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!Sources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            UINT32_MAX,
            UINT32_MAX,
            owners,
            registrations));
        rhi::OriginalCommandStateAdmissionBudget graphBudget{
            1u, true, 1u, registrations, 6u, 5u, owners, bytes};
        std::shared_ptr<const Graph> graph;
        const auto capture = [&] {
            graph = Graph::prepareOriginal(inputs, device, graphBudget, stateQuota, id);
            if (!graph)
                std::fprintf(stderr, "ContentPlan3 Graph: %s\n", id ? id : "missing");
            assert(graph && !id);
        };
        capture();
        auto rootBudget = graphBudget;
        rootBudget.maxRetainedOwners += 2u;
        const auto make = [&] {
            return Root::prepareOriginalOwnership(graph, device, rootBudget, recordingQuota, id);
        };
        const rhi::OriginalCommandStateSequenceBudget sequence{1u, true, 9u, 0u, 10u, 10u};
        rhi::OriginalTextureContentPlanBudget budget{3u, true, 1u, 0u, 9u, 4u, 90u, 4096u, 2u};
        auto root = make();
        assert(root && !id);
        auto result = root->preflightOriginalContentCells(budget);
        if (result.diagnostic)
            std::fprintf(stderr, "ContentPlan3 preflight: %s\n", result.diagnostic);
        assert(!result.diagnostic && result.controls == 1u && result.cells == 9u
            && result.steps == 4u && result.commandVisits == 90u && result.readbackCommands == 2u);
        budget.maxProjectionVisits = result.projectionVisits;
        const auto reject = [&](auto changed, const char* expected) {
            const auto refused = root->preflightOriginalContentCells(changed);
            is(refused.diagnostic, expected);
            assert(!refused.cells && !refused.controls && !refused.steps && !refused.commandVisits
                && !refused.projectionVisits && !refused.readbackCommands && root->journalEmpty());
        };
        auto bad = budget;
        --bad.maxReadbackCommands;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-READBACK-CAPACITY");
        bad = budget;
        bad.maxReadbackCommands = 0u;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-READBACK-BUDGET");
        bad = budget;
        bad.contractVersion = 2u;
        bad.maxReadbackCommands = 0u;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-READBACK-PROFILE");
        bad = budget;
        bad.contractVersion = 4u;
        reject(bad, "RHI-CANONICAL-CONTENT-PLAN-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        reject(bad, "RHI-CANONICAL-CONTENT-PLAN-OPT-IN");
        bad = budget;
        --bad.maxContentCells;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-CELL-CAPACITY");
        bad = budget;
        --bad.maxContentSteps;
        reject(bad, "RHI-ORIGINAL-CONTENT-SEQUENCE-STEP-CAPACITY");
        bad = budget;
        --bad.maxCommandVisits;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-COMMAND-CAPACITY");
        bad = budget;
        --bad.maxProjectionVisits;
        reject(bad, "RHI-ORIGINAL-CONTENT-PLAN-PROJECTION-CAPACITY");
        root.reset();
        const auto planned = [&] {
            auto candidate = make();
            if (candidate && (id = candidate->materializeOriginalPrototypePlan(sequence, budget)))
                candidate.reset();
            return candidate;
        };
        CpuQuotaFactoryFixture pressure(recordingQuota);
        const auto peak = pressure.measure(planned);
        pressure.verify(peak, planned);
        root = planned();
        if (!root)
            std::fprintf(stderr, "ContentPlan3 plan: %s\n", id ? id : "missing");
        assert(root && !id && root->originalContentPlanContractVersion() == 3u
            && root->plannedContentCellCount() == 9u);
        is(rhi::OriginalCommandNativeLifecycleContract::inspectOriginalPreparation(
               root, {}, {}, {}),
            "RHI-ORIGINAL-NATIVE-LIFECYCLE-CONTENT-READBACK-PROFILE");
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, inputQuota, stateQuota, recordingQuota},
            [&]() noexcept -> const char* {
                if (const auto* diagnostic = root->preflightOriginalContentCells(budget).diagnostic)
                    return diagnostic;
                if (const auto* diagnostic = root->applyOriginalPrototypePlan())
                    return diagnostic;
                assert(root->status() == Root::Status::Pending && !root->journalEmpty());
                is(root->commitOriginalPendingOwnership(),
                    "RHI-CANONICAL-CONTENT-NATIVE-COMPLETION-REQUIRED");
                return root->abandonOriginalPendingOwnership();
            }));
        root.reset();
        graph.reset();
        for (std::uint32_t mip = 0u; mip < 3u; ++mip)
            for (std::uint32_t layer = 0u; layer < 3u; ++layer) {
                auto cell = parent->originalObject()->observeOriginalContent(
                    {mip, 1u, layer, 1u, rhi::TextureAspect::Color});
                assert(cell && cell->state() == Content::State::Uninitialized
                    && !cell->contentGeneration());
            }
        for (const auto load : {rhi::LoadOp::DontCare, rhi::LoadOp::Load}) {
            pass.colorAttachments[0u].loadOp = load;
            records[1u].arguments = freezePass();
            inputs = assemble();
            assert(inputs);
            capture();
            root = make();
            assert(root);
            is(root->preflightOriginalContentCells(budget).diagnostic,
                load == rhi::LoadOp::Load ? "RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED"
                                          : "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
            root.reset();
            graph.reset();
        }
        pass.colorAttachments[0u].loadOp = rhi::LoadOp::Clear;
        pass.colorAttachments[0u].storeOp = rhi::StoreOp::DontCare;
        records[1u].arguments = freezePass();
        inputs = assemble();
        assert(inputs);
        capture();
        root = make();
        assert(root);
        is(root->preflightOriginalContentCells(budget).diagnostic,
            "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
        root.reset();
        graph.reset();
        pass.colorAttachments[0u].storeOp = rhi::StoreOp::Store;
        records[1u].arguments = freezePass();
        const auto originalCopy = records[2u].arguments;
        copy.after = S::ShaderRead;
        copy.destinationStages = rhi::PipelineStage::FragmentShader;
        copy.destinationAccess = rhi::ResourceAccess::ShaderRead;
        records[2u].arguments = *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, copy);
        inputs = assemble();
        assert(inputs);
        capture();
        root = make();
        assert(root);
        is(root->preflightOriginalContentCells(budget).diagnostic,
            "RHI-ORIGINAL-CONTENT-PLAN-READBACK-STATE");
        root.reset();
        graph.reset();
        records[2u].arguments = originalCopy;
        auto wrongRange = range;
        wrongRange.baseArrayLayer = 1u;
        readback = declareReadback(wrongRange);
        retained[3u] = readback;
        sources = Sources::prepareOriginal(
            retained.data(), retained.size(), admission, sourceQuota, id, correspondence);
        assert(sources);
        records[3u].arguments = copied();
        records[4u].arguments = copied();
        // State is correct for the copied neighbor; its content has never been cleared.
        rhi::ResourceBarrierDesc neighbor{parent->resource(), wrongRange, S::Undefined, S::CopySrc};
        neighbor.sourceStages = rhi::PipelineStage::TopOfPipe;
        neighbor.sourceAccess = rhi::ResourceAccess::None;
        neighbor.destinationStages = rhi::PipelineStage::Transfer;
        neighbor.destinationAccess = rhi::ResourceAccess::TransferRead;
        records[2u].arguments =
            *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, neighbor);
        inputs = assemble();
        assert(inputs);
        capture();
        root = make();
        assert(root);
        is(root->preflightOriginalContentCells(budget).diagnostic,
            "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
        root.reset();
        graph.reset();
        inputs.reset();
        sources.reset();
        assert(!stateQuota->liveBytes() && !recordingQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_CONTENT_READBACK_PLAN3_OK original-parent Source2 clear-store-copy2 mip1-layer2 cells9 steps4 visits90 peak%llu projection%llu profile2-refused missing-discard-neighbor-refused rollback hot0 native-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(budget.maxProjectionVisits));
    }
};
} // namespace arp::test
