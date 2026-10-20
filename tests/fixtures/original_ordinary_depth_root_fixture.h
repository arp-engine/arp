#pragma once
#include "bindless_package_request_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "original_bindless_graphics_stream_fixture.h"
#include "original_bindless_root_assembly_fixture.h"
#include "rhi/frontend/bindless_ordinary_opaque_binding.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"
#include "rhi/semantics/bindless_ordinary_depth_domain.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// A genuine Graphics Root/2 assembled on the producer's existing device and
// registration. Color is an already completed upload; depth is the exact
// published Source2/control4 view. This fixture cannot issue a transaction.
class OriginalOrdinaryDepthRootFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "ordinary depth Root2: %s\n", id);
        assert(!id);
    }

public:
    struct Prepared {
        std::shared_ptr<const rhi::OriginalCommandBindlessRootSource> root;
        std::shared_ptr<const rhi::OriginalCommandResourceSources> inventory;
    };

    static Prepared prepare(rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& proof,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& color,
        bool fault = false,
        std::uint64_t attachmentGenerations = 2u,
        bool canonicalCold = false,
        void (*configureExecutionAdmission)(rhi::OriginalCommandAdmissionBudget&) = nullptr)
    {
        using namespace rhi;
        using Sources = OriginalCommandResourceSources;
        using Set = OriginalCommandDescriptorSetSource;
        using Root = OriginalCommandBindlessRootSource;
        const auto& depth = proof->originalViewSource();
        const auto& quota = depth->originalMetadataQuota();
        const auto bytes = quota->limitBytes();
        const char* id = nullptr;
        const OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 2u, bytes};
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension && !proof->validate());
        const auto& artifact = package->artifact();
        assert(artifact.additionalDomains.size() == 1u
            && artifact.additionalDomains.front().set == 2u && artifact.tables.size() == 1u);
        auto request =
            BindlessPackageRequestFixture::make(artifact, extension->capabilities().deviceIdentity);
        if (canonicalCold)
            request.originalPreparation = {12u, true};
        if (configureExecutionAdmission) {
            assert(canonicalCold);
            request.originalPreparation = {};
            request.originalExecution = {14u, true};
        }
        BindlessSnapshotPool snapshots(device, request.budget);
        core::pmr::String diagnostic;
        auto snapshot = core::shareUnique(
            snapshots.prepareOriginalDescriptors(request, artifact, {2u, true, true}, diagnostic));
        if (!snapshot)
            std::fprintf(stderr, "ordinary depth snapshot: %s\n", diagnostic.c_str());
        assert(snapshot);
        core::pmr::Vector<Sources::Source> inputs{
            depth->originalParentLease(), depth, color->originalParentLease(), color};
        const auto& table = artifact.tables.front();
        assert(table.tableId == 1u && table.capacity == 1u);
        auto output = device.createBuffer({"ordinary depth/color Results40",
            40u * sizeof(std::uint32_t),
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderWrite});
        assert(output);
        assert(snapshot->writeBuffer(snapshot->handle(table.tableId, 0u), *output).success);
        auto outputLease =
            OriginalCommandBufferLease::prepareOriginal(output, leaseBudget, quota, id);
        require(id);
        assert(outputLease);
        inputs.push_back(outputLease);
        assert(snapshot->seal(device).success);
        require(
            BindlessSnapshot::prepareOriginalBufferSources(snapshot, device, leaseBudget, quota));
        for (const auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                 BindlessSnapshot::OriginalBufferRole::FaultLatch,
                 BindlessSnapshot::OriginalBufferRole::FaultReadback}) {
            assert(snapshot->originalBufferSource(role));
            inputs.push_back(snapshot->originalBufferSource(role));
        }
        TextureDesc targetDesc;
        targetDesc.label = "ordinary depth/color independent attachment";
        targetDesc.extent = {2u, 2u, 1u};
        targetDesc.format = TextureFormat::Rgba8Unorm;
        targetDesc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
        targetDesc.initialState = ResourceState::Undefined;
        const auto required =
            canonicalCold ? 0u : device.queryOriginalContentTextureAllocation(targetDesc, id, 4u);
        require(id);
        assert(canonicalCold || required);
        std::uint64_t targetBytes = 0u;
        require(
            OriginalTextureContentCreationContract::inspectCellLayout(targetDesc, 1u, targetBytes));
        auto targetContentQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::UniquePtr<GpuTexture> target;
        {
            core::CpuMemoryQuotaScope allocation(quota);
            target = device.createOriginalContentTexture(targetDesc,
                {canonicalCold ? 5u : 4u,
                    true,
                    1u,
                    1u,
                    targetBytes,
                    attachmentGenerations,
                    bytes,
                    1u,
                    required},
                targetContentQuota,
                id);
        }
        require(id);
        assert(target);
        TextureViewDesc targetViewDesc;
        targetViewDesc.format = targetDesc.format;
        targetViewDesc.usage = TextureUsage::RenderAttachment;
        auto targetView = device.createTextureView(*target, targetViewDesc);
        assert(targetView);
        auto targetParent =
            OriginalCommandTextureLease::prepareOriginal(target, leaseBudget, quota, id);
        require(id);
        auto targetChild =
            OriginalCommandTextureViewLease::prepareOriginal(targetView, leaseBudget, quota, id);
        require(id);
        auto targetSource = OriginalCommandTextureViewSource::prepareOriginal(targetChild,
            targetParent,
            {1u, true, OriginalCommandTextureViewSource::completeRetainedOwnerCount, bytes},
            quota,
            id);
        require(id);
        assert(targetSource);
        inputs.push_back(targetParent);
        inputs.push_back(targetSource);
        auto authorization = extension->authorize(package, *snapshot, diagnostic);
        assert(authorization);
        RenderPipelineDesc state;
        state.colorFormat = TextureFormat::Rgba8Unorm;
        state.dynamicViewportScissor = true;
        const auto fragment = fault ? "faultFragment" : "cleanFragment";
        auto pipeline = core::shareUnique(extension->createRenderPipeline(
            authorization, state, "vertexMain", fragment, diagnostic));
        if (!pipeline)
            std::fprintf(stderr, "ordinary depth pipeline: %s\n", diagnostic.c_str());
        assert(pipeline);
        const auto retained = snapshot->plan().snapshotBytes + package->sourceBytes();
        BindlessGraphicsBindingPool bindings(
            *extension, {1u, retained + 4096u, retained + 4096u, 8u, 4096u});
        auto binding = bindings.prepare(
            {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()}, diagnostic);
        assert(binding);

        const auto& layout = artifact.additionalDomains.front().layout;
        std::shared_ptr<DescriptorArena> arena;
        {
            core::CpuMemoryQuotaScope allocation(quota);
            arena = core::shareUnique(device.createOriginalDescriptorArena(
                {"ordinary depth/color", 1u, static_cast<std::uint32_t>(layout.bindings.size())}));
        }
        assert(arena);
        auto set = device.allocateOriginalDescriptorSet(arena, {"ordinary depth/color", layout});
        assert(set);
        core::pmr::Vector<OriginalCommandDescriptorSetElement> elements;
        core::pmr::Vector<std::shared_ptr<const OriginalCompletedDepthTextureSource>> depths;
        for (const auto& shape : layout.bindings) {
            OriginalCommandDescriptorSetElement element;
            element.write.binding = shape.binding;
            element.write.type = shape.type;
            element.stages = bindlessPipelineStagesForShader(shape.stages);
            if (shape.type == DescriptorType::Sampler) {
                SamplerDesc desc;
                desc.minFilter = desc.magFilter = SamplerFilter::Nearest;
                desc.mipFilter = SamplerMipFilter::Nearest;
                desc.compareOp = !shape.comparisonSampler ? CompareOp::Never
                    : shape.binding == 3u                 ? CompareOp::Less
                                                          : CompareOp::Greater;
                auto object = device.createSampler(desc);
                assert(object);
                auto lease =
                    OriginalCommandSamplerLease::prepareOriginal(object, leaseBudget, quota, id);
                require(id);
                assert(lease);
                element.sourceIndex = inputs.size();
                element.write.resource = lease->resource();
                element.write.resourceKind = DescriptorResourceKind::Sampler;
                element.write.resourceState = ResourceState::Undefined;
                element.registrationGeneration =
                    lease->originalObject()->originalResourceRegistration().generation();
                element.access = ResourceAccess::None;
                assert(set->write(element.write).success);
                inputs.push_back(std::move(lease));
            } else {
                assert(shape.type == DescriptorType::SampledTexture);
                const auto& view = shape.depthTexture ? depth : color;
                element.sourceIndex = shape.depthTexture ? 1u : 3u;
                element.write.resource = view->resource();
                element.write.resourceKind = DescriptorResourceKind::TextureView;
                element.write.resourceState = ResourceState::ShaderRead;
                element.registrationGeneration =
                    view->originalObject()->originalResourceRegistration().generation();
                element.access = ResourceAccess::ShaderRead;
                if (shape.depthTexture) {
                    assert(
                        set->writeOriginalCompletedTexture(device,
                               element.write,
                               proof,
                               {canonicalCold ? 3u : 4u,
                                   true,
                                   1u,
                                   1u,
                                   OriginalCompletedDepthTextureSource::completeRetainedOwnerCount})
                            .success);
                } else {
                    const auto completed =
                        view->originalParentLease()->originalObject()->observeOriginalContent();
                    assert(completed
                        && set
                            ->writeOriginalCompletedTexture(device,
                                element.write,
                                view,
                                *completed,
                                {canonicalCold ? 6u : 1u,
                                    true,
                                    1u,
                                    1u,
                                    OriginalCommandTextureViewSource::completeRetainedOwnerCount})
                            .success);
                }
            }
            elements.push_back(element);
            depths.push_back(shape.depthTexture ? proof : nullptr);
        }
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        // Authored shared stream: initial fault3 + image barriers2 +
        // attachment/pass/draw7 + fault copy/restore6 = 18 commands.
        admission.maxCommands = 18u;
        admission.maxReferences = 12u;
        admission.maxInputDescriptors = 9u;
        admission.maxPassColorAttachments = admission.maxPassAttachmentReferences = 1u;
        admission.maxStateObservations = 18u;
        admission.maxTextureRanges = 6u;
        admission.maxStateTransactions = 1u;
        admission.maxPendingSubmissions = 1u;
        // One original command plus one-view pass (view/renderpass/framebuffer).
        admission.maxNativePayloadObjects = 4u;
        // Independent conservative union bound: each physical source can
        // retain its complete texture-view graph plus capacity owner slots.
        admission.maxGraphOwners = inputs.size()
                * (OriginalCommandTextureViewSource::completeRetainedOwnerCount
                    + OriginalCommandBufferLease::capacityOwnerSlots)
            + Set::canonicalExclusiveRetainedOwnerCount
            + OriginalCompletedDepthTextureSource::completeRetainedOwnerCount
            + Root::retainedOwnerCount + Root::cpuOwnerSlots;
        admission.maxCpuBytes.fill(bytes);
        admission.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 0u;
        for (const auto& input : inputs)
            ++admission.maxSourceOwners[static_cast<std::size_t>(Sources::domain(input))];
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] =
            1u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::BindlessRoot)] =
            1u;
        if (configureExecutionAdmission)
            configureExecutionAdmission(admission);
        auto inventory = Sources::prepareColdOriginalTyped(
            inputs.data(), inputs.size(), admission, {6u, true, 2u}, quota, id);
        require(id);
        assert(inventory);
        OriginalCommandDescriptorSourceBudget sourceBudget{6u,
            true,
            static_cast<std::uint32_t>(elements.size()),
            static_cast<std::uint32_t>(elements.size()),
            Set::canonicalExclusiveRetainedOwnerCount,
            bytes};
        std::uint64_t texelBytes = 0u;
        require(semantics::TextureUploadDestinationValidation::footprint(
            depth->originalParentLease()->originalObject()->desc(), texelBytes));
        sourceBudget.completedDepthInputs = {canonicalCold ? 1u : 3u,
            true,
            proof->cells().size(),
            texelBytes,
            OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        auto frozen = Set::prepareOriginal(set,
            arena,
            inventory,
            elements.data(),
            elements.size(),
            sourceBudget,
            quota,
            device,
            id,
            depths.data(),
            depths.size());
        require(id);
        assert(frozen && !set);
        const Root::OrdinaryDomain ordinary{2u, inventory->sources().size(), frozen};
        require(Sources::appendOriginalTyped(inventory, frozen, device));
        auto root = OriginalBindlessRootAssemblyFixture::prepare(
            snapshot, binding, inventory, device, quota, id, &ordinary, 1u);
        require(id);
        assert(root && root->sourceContractVersion() == 2u);
        core::pmr::Vector<OriginalCommandDescriptorSetElement> indexed;
        for (const auto& element : root->elements())
            indexed.push_back(element.declaration);
        OriginalCommandBindlessRootSourceBudget rootBudget{2u,
            true,
            static_cast<std::uint32_t>(indexed.size()),
            Root::retainedOwnerCount + Root::cpuOwnerSlots,
            bytes,
            1u,
            static_cast<std::uint32_t>(elements.size())};
        const auto rebuild = [&](const auto& limits, const auto& domain) {
            return Root::prepareOriginal(snapshot,
                binding,
                inventory,
                indexed.data(),
                indexed.size(),
                &domain,
                1u,
                limits,
                quota,
                device,
                id);
        };
        {
            auto exact = rebuild(rootBudget, ordinary);
            require(id);
            assert(exact);
        }
        const auto refuse = [&](const auto& limits, const auto& domain, const char* expected) {
            const auto before = quota->liveBytes();
            assert(!rebuild(limits, domain) && id && core::StringView(id) == expected);
            assert(quota->liveBytes() == before && !Root::validateOriginal(root, device));
        };
        auto less = rootBudget;
        --less.maxOrdinaryElements;
        refuse(less, ordinary, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-ELEMENT-CAPACITY");
        less = rootBudget;
        --less.maxOrdinaryDomains;
        refuse(less, ordinary, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-BUDGET");
        auto wrong = ordinary;
        wrong.sourceIndex = 1u; // A depth view cannot impersonate its descriptor owner.
        refuse(rootBudget, wrong, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-INVENTORY");
        wrong = ordinary;
        wrong.set = artifact.set;
        refuse(rootBudget, wrong, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-SET");
        const auto physicalDepth = root->elements().size() + 1u + 2u;
        assert(root->originalCompletedDepthAt(physicalDepth).get() == proof.get()
            && core::SharedOwnerProjection::sameOwner(
                root->originalCompletedDepthAt(physicalDepth), proof));
        assert(!root->originalCompletedDepthAt(physicalDepth - 1u)
            && !root->originalCompletedDepthAt(physicalDepth + 1u));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                require(Root::validateOriginal(root, device));
                BindlessOrdinaryOpaqueBinding selectedDepth, less, greater;
                require(Root::originalOrdinaryOpaqueBinding(root, fragment, 2u, 2u, selectedDepth));
                require(Root::originalOrdinaryOpaqueBinding(root, fragment, 2u, 3u, less));
                require(Root::originalOrdinaryOpaqueBinding(root, fragment, 2u, 4u, greater));
                assert(selectedDepth.sourceIndex == 1u
                    && selectedDepth.resource == depth->resource()
                    && selectedDepth.access.resource == depth->originalParentLease()->resource()
                    && less.resource != greater.resource
                    && less.sampler.compareOp == CompareOp::Less
                    && greater.sampler.compareOp == CompareOp::Greater);
                require(semantics::OriginalBindlessOrdinaryDepthDomain::inspectSelectedDeclarations(
                    selectedDepth.declaration, less.declaration, selectedDepth.selectedStage));
                BindlessOrdinaryOpaqueBinding inactive;
                assert(Root::originalOrdinaryOpaqueBinding(root, "vertexMain", 2u, 2u, inactive)
                    && !inactive.contractVersion && !inactive.resource.isValid());
                assert(Root::originalOrdinaryOpaqueBinding(
                           root, fault ? "cleanFragment" : "faultFragment", 2u, 2u, inactive)
                    && !inactive.contractVersion);
                return nullptr;
            }));
        std::printf(
            "BINDLESS_ORDINARY_DEPTH_ROOT2_OK Source6 Input%u exact-physical-proof selected-fragment color comparison-samplers2 exact-less-root-budget unchanged-failure hot0 no-execution-grant\n",
            canonicalCold ? 1u : 3u);
        return {std::move(root), std::move(inventory)};
    }

    static rhi::OriginalBindlessGraphicsDepthContentPlanContract::Budget combinedBudget(
        const Prepared& prepared, bool canonical = false)
    {
        using namespace rhi;
        OriginalBindlessGraphicsDepthContentPlanContract::Budget content{
            18u, true, {9u, true, 1u, 1u, 1u, 1u, 2u, 1u, 1u}, 1u, 1u, 1u};
        content.graphics.bindlessCompletedReadContractVersion = canonical ? 3u : 0u;
        if (canonical) {
            content.contractVersion = 19u;
            for (const auto& domain : prepared.root->ordinaryDomains())
                for (const auto& element : domain.source->frozenElements())
                    if (element.completedDepth) {
                        content.maxDepthCells = element.completedDepth->cells().size();
                        content.maxDepthReadSteps = content.maxDepthCells;
                    }
        }
        // Configure the existing inspector's cold ceiling, then include the
        // selected depth's order/timeline, classification and physical proof walks.
        const auto commands = prepared.inventory->originalBudget().maxCommands;
        const auto entries = prepared.inventory->originalBudget().maxTextureRanges;
        OriginalGraphicsContentVisitBudgetFixture::configure(
            content.graphics, commands, entries, 1u, 1u);
        content.graphics.maxCommandVisits += (1u + content.maxDepthCells) * commands;
        content.graphics.maxProjectionVisits += entries * entries + 2u * entries
            + content.maxDepthCells * commands
                * (entries + prepared.root->ordinaryDomains().size()
                    + prepared.root->ordinaryDomains().front().source->frozenElements().size());
        return content;
    }

    static void verifyCombinedPlan(
        const Prepared& prepared, rhi::GpuDevice& device, bool canonical = false)
    {
        using namespace rhi;
        const auto content = combinedBudget(prepared, canonical);
        OriginalBindlessGraphicsStreamFixture::execute(
            prepared.root,
            prepared.inventory,
            device,
            [canonical, depthCells = content.maxDepthCells](const auto& root,
                const auto& inventory,
                const auto&,
                const auto& tx,
                auto& device) {
                const auto* actual = tx->originalGraphicsDepthContentCounts();
                assert(actual && actual->depthControls == 1u && actual->depthCells == depthCells
                    && actual->depthReadSteps == depthCells
                    && actual->graphics.completedControls == 1u
                    && actual->graphics.attachmentControls == 1u
                    && tx->plannedContentControlCount() == 3u
                    && tx->plannedContentCellCount() == depthCells + 1u
                    && tx->originalContentPlanContractVersion() == (canonical ? 19u : 18u));
                std::shared_ptr<const OriginalCommandTextureViewSource> depth, color, attachment;
                for (const auto& input : inventory->sources()) {
                    const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &input);
                    if (!view)
                        continue;
                    if ((*view)->originalObject()->desc().usage == TextureUsage::RenderAttachment)
                        attachment = *view;
                    else if ((*view)->originalObject()->desc().aspect == TextureAspect::Depth)
                        depth = *view;
                    else
                        color = *view;
                }
                assert(depth && color && attachment);
                const auto& d = *depth->originalParentLease()->originalObject();
                const auto& c = *color->originalParentLease()->originalObject();
                const auto& a = *attachment->originalParentLease()->originalObject();
                const auto dr = defaultTextureSubresourceRange(d.desc());
                const auto ar = defaultTextureSubresourceRange(a.desc());
                std::shared_ptr<const OriginalCompletedDepthTextureSource> proof;
                for (const auto& domain : root->ordinaryDomains())
                    for (const auto& element : domain.source->frozenElements())
                        if (element.completedDepth)
                            proof = element.completedDepth;
                assert(proof && proof->cells().size() == depthCells);
                const auto unchangedDepth = [&]() noexcept {
                    for (const auto& cell : proof->cells())
                        if (d.validateOriginalContent(cell))
                            return false;
                    return true;
                };
                const auto cs = *c.observeOriginalContent();
                const auto as = *a.observeOriginalContent(ar);
                const std::array quotas{tx->originalMetadataQuota(), root->originalMetadataQuota()};
                assert(tx->executionDiagnostic(device.backend()));
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return tx->applyOriginalPrototypePlan();
                }));
                assert(tx->status() == OriginalCommandStateTransaction::Status::Pending
                    && !tx->journalEmpty() && !a.observeOriginalContent(ar)
                    && !d.observeOriginalCommittedState(dr) && unchangedDepth()
                    && !c.validateOriginalContent(cs));
                assert(tx->commitOriginalPendingOwnership()
                    && tx->status() == OriginalCommandStateTransaction::Status::Pending);
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return tx->validate();
                }));
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return tx->abandonOriginalPendingOwnership();
                }));
                assert(tx->journalEmpty()
                    && tx->status() == OriginalCommandStateTransaction::Status::Abandoned
                    && unchangedDepth() && !c.validateOriginalContent(cs));
                const auto restored = a.observeOriginalContent(ar);
                assert(restored && restored->state() == as.state()
                    && restored->contentGeneration() == as.contentGeneration()
                    && a.observeOriginalCommittedState(ar)->state() == ResourceState::Undefined
                    && !OriginalCommandBindlessRootSource::validateOriginal(root, device));
                std::printf(
                    "BINDLESS_GRAPHICS_DEPTH_CONTENT%u_PLAN_OK same-journal readonly-depth-color cells%llu attachment-rollback exact-less-visits hot0 native-execution-closed\n",
                    canonical ? 19u : 18u,
                    static_cast<unsigned long long>(depthCells));
            },
            &content);
        assert(!prepared.inventory->originalStateTransactionCount());
    }
};
} // namespace arp::test
