#pragma once
#include "bindless_scene_attachment_fixture.h"
#include "bindless_scene_geometry_data_fixture.h"
#include "bindless_scene_geometry_readback_fixture.h"
#include "original_bindless_graphics_stream_fixture.h"
#include "rhi/frontend/bindless_graphics_execution.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace arp::test {
using namespace renderer;

// Platform upload fixtures supply actual completed images. All scene bytes,
// geometry and command construction are shared with the Null correspondence
// tests; native actors are issued only through the public immutable factory.
template <class CompletedTextures>
struct BindlessNativeSceneDataFixture : BindlessSceneGeometryDataFixture, CompletedTextures {
    static constexpr bool enabled = false, completedTextures = true, sceneStream = true;
    static constexpr auto originalRootCpuBytes =
        BindlessSceneGeometryDataFixture::originalRootCpuBytes;
    using BindlessSceneGeometryDataFixture::configureBuffer;
    using BindlessSceneGeometryDataFixture::configureOrdinaryBuffer;
    using CompletedTextures::completedTexturePublicationVersion;
    renderer::BindlessPbrGeometryDraw16 draw;
    bool cancelBeforeSubmit = false;
    // Baseline per entry, then finite representative failure cases per pass.
    enum class Terminal {
        Baseline,
        ColdCancel,
        RetiredRecord,
        RetiredSubmit,
        CallerDrop,
        PendingDrop,
        RetiredPending,
        UnknownSubmit,
        UnknownFence,
        ShutdownDrain,
        MaterialRangeRejected
    };
    Terminal terminal = Terminal::Baseline;
    bool retiredDeviceExpected = false;

    bool originalDeviceLifetimeRetired() const noexcept
    {
        return retiredDeviceExpected || terminal == Terminal::RetiredRecord
            || terminal == Terminal::RetiredSubmit || terminal == Terminal::RetiredPending;
    }

    BindlessSceneAttachmentFixture::Observer observeAttachment = nullptr;
    using SubmittedObserver = void (*)(rhi::GpuDevice&,
        std::shared_ptr<const rhi::OriginalBindlessGraphicsExecution>&,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>&,
        const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&,
        rhi::OriginalBindlessGraphicsExecutionIntent,
        unsigned);
    SubmittedObserver observeSubmitted = nullptr;

    BindlessNativeSceneDataFixture()
    {
        this->initializeTexturePixels = &BindlessSceneAttachmentFixture::initializeTexturePixels;
    }

    void releaseSceneInputs() const
    {
        this->geometry->actors = {};
        this->geometry->leases = {};
        this->geometry->resultReadback.reset();
        // The vector grew inside the source quota. Release its capacity before
        // the original Root fixture verifies that quota has returned to zero.
        decltype(this->state->shaderActors){}.swap(this->state->shaderActors);
    }

    template <class Root>
    rhi::OriginalBindlessInitializedGraphicsInputs sceneInputs(
        const Root& root, std::uint64_t commands) const
    {
        // Authored fixture: two PBR materials or one terrain material, one
        // instance and one page. Each resource validation can scan the tables
        // twice; descriptor lookups scan the full original declaration set.
        const auto materials = this->terrain() ? 1u : 2u;
        const auto references = this->terrain() ? 27u : 5u;
        std::uint64_t declarations = root->elements().size();
        for (const auto& domain : root->ordinaryDomains())
            declarations += domain.source->frozenElements().size();
        const auto tables = root->authorization()->package().artifact().tables.size();
        const auto lookups = 4u + 3u + 2u * materials;
        const auto visits = lookups * (declarations + tables)
            + materials * (2u * references + 1u) * tables + commands * declarations;
        return {2u, true, visits, commands};
    }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        BindlessSceneGeometryDataFixture::configureAdmission(budget);
        std::uint32_t images = 0u;
        for (const auto& table : this->artifact->tables) {
            const auto* binding =
                rhi::findDescriptorSetBinding(this->artifact->physicalLayout, table.firstBinding);
            assert(binding);
            if (binding->type == rhi::DescriptorType::SampledTexture)
                images += table.capacity;
        }
        // Each completed image may add one original read dependency. Preserve
        // the Root builder's already-derived resource/range/owner ceilings.
        budget.maxCommands += images;
        budget.maxReferences += images;
        budget.maxInputDescriptors += images;
        // A sampled image has an explicit transition plus scoped Root reads.
        // Bound each original command's possible observation; graph capture
        // later counts and stores only the actual canonical observations.
        const auto observations = images * (1u + budget.maxCommands);
        budget.maxTextureRanges += observations;
        budget.maxStateObservations += observations;
        budget.maxCallbackTickets = budget.maxCallbackRegistrations = 0u;
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 0u;
        budget.maxUnpublishedResultBytes = sizeof(std::uint32_t) + this->resultBytes;
        ++budget.maxNativePayloadObjects; // Original native command beside its pass.
    }

    template <class... Args> void preparedSceneInputs(Args&&...) const
    {
        assert(false && "A native scene must pass the original graph admission");
    }
};

// This fixture inspects the existing private closure/pass only to measure
// their exact custody budgets. Recording, submission and completion all use
// BindlessCrossExtension::prepareOriginalGraphicsExecution and its public view.
template <class Closure, class Pass, class Content, class Data>
class BindlessNativeSceneExecutionFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Public native scene: %s\n", id);
        assert(!id);
    }

public:
    Data data;

    template <class Root, class Inventory>
    void operator()(const Root& root, const Inventory& inventory, rhi::GpuDevice* device) const
    {
        if (!device) {
            data.releaseSceneInputs();
            return;
        }
        arp::test::BindlessSceneConformanceInputs scene{1u, true, data.family, 4096u, 4096u, 4096u};
        require(arp::test::BindlessSceneConformanceContract::validateRootInput(root, scene));
        OriginalBindlessGraphicsStreamFixture::execute(root, inventory, *device, *this);
    }

    // Stream traits and input bytes refer to this same retained Data instance.
    static constexpr bool graphicsPush = true, sceneStream = true, completedTextures = true;
    static constexpr auto geometryBufferCount = Data::geometryBufferCount;
    static constexpr auto resultBytes = Data::resultBytes;

    bool terrain() const { return data.terrain(); }

    const rhi::BindlessShaderArtifact* artifact = nullptr;
    std::shared_ptr<typename Data::Geometry> geometry;
    renderer::BindlessPbrGeometryDraw16 draw;

    explicit BindlessNativeSceneExecutionFixture(Data value)
        : data(std::move(value)), artifact(data.artifact), geometry(data.geometry), draw(data.draw)
    {}

    template <class... Args> void preparedSceneInputs(Args&&...) const
    {
        assert(false && "Native scene graph admission unexpectedly failed");
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        rhi::GpuDevice& device,
        const rhi::OriginalBindlessRootReadbackSequence::Input& results) const
    {
        using namespace rhi;
        using Public = OriginalBindlessGraphicsExecution;
        const auto& program = binding->originalProgram();
        const BindlessSceneConformanceInputs content{1u, true, data.family, 4096u, 4096u, 4096u};
        const auto* contentId = BindlessSceneConformanceContract::inspectInitialized<Content>(root,
            inventory,
            program,
            data.state->shaderActors.data(),
            data.state->shaderActors.size(),
            content);
        if (data.terminal == Data::Terminal::MaterialRangeRejected) {
            assert(contentId && core::StringView(contentId) == "RHI-BINDLESS-PBR-GEOMETRY-SELECTION"
                && tx->journalEmpty());
            for (std::size_t i = 0u; i < results.count; ++i)
                assert(!results.bindings[i]->originalResult()->publishedBytes());
            std::printf(
                "BINDLESS_NATIVE_SCENE_RANGE_REJECTED entry=%s diagnostic=%s caller-conformance unpublished\n",
                data.vertex->entry.c_str(),
                contentId);
            return;
        }
        if (contentId)
            std::fprintf(
                stderr, "Caller scene conformance %s: %s\n", data.vertex->entry.c_str(), contentId);
        require(contentId);
        const auto bytes = tx->originalMetadataQuota()->limitBytes();
        const auto quota = [&] {
            return core::makeShared<core::CpuMemoryQuota>(bytes);
        };
        const char* id = nullptr;
        std::array<std::shared_ptr<const Content>, 2u> nativeGeometry;
        core::pmr::Vector<std::shared_ptr<const Content>> nativeShader;
        std::uint64_t geometryBytes = 0u, shaderBytes = 0u;
        for (std::size_t i = 0u; i < nativeGeometry.size(); ++i) {
            nativeGeometry[i] = std::dynamic_pointer_cast<const Content>(geometry->actors[i]);
            require(Content::validateOriginal(nativeGeometry[i]));
            geometryBytes += nativeGeometry[i]->initializedBytes();
        }
        for (const auto& actor : data.state->shaderActors) {
            auto native = std::dynamic_pointer_cast<const Content>(actor);
            require(Content::validateOriginal(native));
            shaderBytes += native->initializedBytes();
            nativeShader.push_back(std::move(native));
        }
        auto closureQuota = quota(), passQuota = quota(), submissionQuota = quota(),
             viewQuota = quota();
        OriginalBindlessGraphicsExecutionBudget budget;
        budget.intent = {1u, true};
        budget.requireWorkloadRetention = true;
        const auto resources = static_cast<std::uint32_t>(inventory->sources().size());
        // The graph union excludes backend-private custody. Bound the native
        // projection's resource slots, descriptor-group slots, stage arrays
        // and pipeline slots separately; Vulkan's projection is a subset.
        const auto groups = 1u + root->ordinaryDomains().size();
        const auto nativeOwners = resources * (10u + 2u) + groups * 8u + 9u;
        // Each entry can retain an initialized actor, its exclusive quota
        // domains and a completed-depth proof. Four roots cover either backend.
        const auto actorOwners = resources * (2u + Content::exclusiveCpuQuotaSlots);
        std::uint32_t ownerCeiling = 0u;
        assert(nativeOwners + actorOwners + 4u <= UINT32_MAX);
        require(inventory->cpuOwnerCount(
            static_cast<std::uint32_t>(nativeOwners + actorOwners + 4u), UINT32_MAX, ownerCeiling));
        budget.closure = {
            3u, true, resources, static_cast<std::uint32_t>(nativeOwners), ownerCeiling, bytes};
        budget.closure.maxInitializedGeometryActors = nativeGeometry.size();
        budget.closure.maxInitializedGeometryBytes = geometryBytes;
        budget.closure.initializedShader = {1u,
            true,
            static_cast<std::uint32_t>(nativeShader.size()),
            shaderBytes,
            resources * (1u + nativeShader.size())};
        auto closure = Closure::prepareOriginal(root,
            inventory,
            device,
            budget.closure,
            closureQuota,
            id,
            {nativeGeometry.data(),
                nativeGeometry.size(),
                nativeShader.data(),
                nativeShader.size()});
        require(id);
        assert(closure);
        const auto closureOwners = closure->retainedOwnerCount();
        budget.closure.maxNativeOwners = closure->nativeOwnerCount();
        budget.closure.maxRetainedOwners = closureOwners;
        // Both native profiles have one attachment; this is a finite test
        // ceiling. The actual native pass reports its exact count below.
        const auto nativeCeiling = inventory->originalBudget().maxNativePayloadObjects - 1u;
        auto physical = core::makeShared<core::AtomicCapacityBudget>(nativeCeiling);
        budget.pass = {1u, true, 1u, nativeCeiling, ownerCeiling, bytes};
        auto pass = Pass::prepareOriginal(
            closure, tx, program, device, budget.pass, physical, passQuota, id);
        require(id);
        assert(pass);
        budget.pass.maxRetainedOwners = pass->retainedOwnerCount();
        budget.pass.maxNativeObjects = pass->nativeObjectCount();
        const auto passOwners = pass->retainedOwnerCount();
        pass.reset();
        closure.reset();
        nativeShader.clear();
        nativeGeometry = {};
        assert(!closureQuota->liveBytes() && !passQuota->liveBytes() && !physical->used());
        physical = core::makeShared<core::AtomicCapacityBudget>(budget.pass.maxNativeObjects);

        const auto operations = program->operations().size();
        budget.fetch = {2u, true, operations, 1u, 1u, operations * 2u, 3u};
        budget.lifecycle = {
            9u, true, 1u, 0u, 0u, tx->retainedOwnerCount() + 2u, 0u, 0u, bytes, bytes, 0u};
        // Common upper custody bound includes Metal's three additional view
        // owners and every extra readback's original declared closure.
        budget.lifecycle.maxSubmissionOwners = closureOwners + tx->retainedOwnerCount()
            + OriginalCommandRecordingSeal::additionalOwnerCountFor(budget.lifecycle)
            + OriginalCommandLifecycleReservation::additionalOwnerCount
            + program->retainedOwnerCount()
            + OriginalCommandBufferReadbackBinding::retainedOwnerCount + passOwners + 8u
            + OriginalBindlessRootReadbackSequence::additionalOwnerBound(results);
        budget.capacity = {5u, true, 1u, 0u, 0u};
        budget.maxCpuViewBytes = bytes;
        budget.maxDiagnosticBytes = 512u;
        OriginalBindlessGraphicsExecutionInputs inputs{root,
            inventory,
            tx,
            binding,
            geometry->actors.data(),
            geometry->actors.size(),
            closureQuota,
            passQuota,
            submissionQuota,
            viewQuota,
            physical};
        inputs.initializedShader = data.state->shaderActors.data();
        inputs.initializedShaderCount = data.state->shaderActors.size();
        inputs.readbacks = results;
        inputs.initializedGraphics = data.sceneInputs(root, operations);
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        core::pmr::String diagnostic;
        auto missing = inputs;
        missing.initializedGraphics = {};
        assert(!extension->prepareOriginalGraphicsExecution(device, missing, budget, diagnostic)
            && diagnostic == "RHI-BINDLESS-INITIALIZED-GRAPHICS-INPUTS-REQUIRED");
        assert(!closureQuota->liveBytes() && !physical->used() && tx->journalEmpty());
        {
            core::CpuMemoryQuotaLease pressure(closureQuota, closureQuota->limitBytes());
            assert(pressure.charged());
            assert(!extension->prepareOriginalGraphicsExecution(device, inputs, budget, diagnostic)
                && !diagnostic.empty() && tx->journalEmpty() && !physical->used()
                && !binding->originalResult()->publishedBytes());
        }
        assert(!closureQuota->liveBytes() && !passQuota->liveBytes()
            && !submissionQuota->liveBytes() && !viewQuota->liveBytes());
        auto issuer =
            extension->prepareOriginalGraphicsExecution(device, inputs, budget, diagnostic);
        require(issuer ? nullptr : diagnostic.c_str());
        assert(issuer && !issuer->recordingProven() && !issuer->submissionCompleted());
        assert(Public::submit(issuer, budget.intent) && tx->journalEmpty());
        using Terminal = typename Data::Terminal;
        if (data.terminal == Terminal::ColdCancel) {
            require(issuer->cancel());
        } else if (data.terminal == Terminal::CallerDrop) {
            issuer.reset();
        } else if (data.terminal == Terminal::RetiredRecord) {
            device.retireOriginalDeviceLifetime();
            assert(Public::record(issuer, budget.intent));
        } else {
            require(Public::record(issuer, budget.intent));
            assert(Public::record(issuer, budget.intent));
            if (data.terminal == Terminal::RetiredSubmit) {
                device.retireOriginalDeviceLifetime();
                assert(Public::submit(issuer, budget.intent));
                issuer.reset();
            }
        }
        assert(!binding->originalResult()->publishedBytes());

        const bool inFlight = data.terminal >= Terminal::PendingDrop;
        const bool droppedSuccess =
            data.terminal == Terminal::PendingDrop || data.terminal == Terminal::ShutdownDrain;
        if (inFlight) {
            assert(data.observeSubmitted);
            data.observeSubmitted(
                device, issuer, tx, root, budget.intent, static_cast<unsigned>(data.terminal));
            assert(tx->status()
                == (droppedSuccess ? OriginalCommandStateTransaction::Status::Committed
                                   : OriginalCommandStateTransaction::Status::Invalidated));
            for (std::size_t i = 0u; i < results.count; ++i)
                assert(bool(results.bindings[i]->originalResult()->publishedBytes())
                    == droppedSuccess);
            if (droppedSuccess) {
                std::uint32_t fault = ~0u;
                std::memcpy(&fault, binding->originalResult()->publishedBytes(), sizeof(fault));
                assert(!fault);
                if (results.count == 2u)
                    require(BindlessSceneGeometryReadbackFixture::inspect(
                        results.bindings[1u]->originalResult()->publishedBytes(),
                        resultBytes,
                        terrain()));
            }
        } else if (data.terminal != Terminal::Baseline) {
            issuer.reset();
            assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned
                || tx->status() == OriginalCommandStateTransaction::Status::Invalidated);
            for (std::size_t i = 0u; i < results.count; ++i)
                assert(!results.bindings[i]->originalResult()->publishedBytes());
        } else if (data.cancelBeforeSubmit) {
            require(issuer->cancel());
            assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned);
            for (std::size_t i = 0u; i < results.count; ++i)
                assert(!results.bindings[i]->originalResult()->publishedBytes());
        } else {
            require(Public::submit(issuer, budget.intent));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!issuer->pollCompletion() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            require(issuer->completionDiagnostic());
            assert(issuer->submissionCompleted()
                && tx->status() == OriginalCommandStateTransaction::Status::Committed);
            std::uint32_t fault = ~0u;
            std::memcpy(&fault, binding->originalResult()->publishedBytes(), sizeof(fault));
            assert(!fault);
            for (std::size_t i = 0u; i < results.count; ++i)
                assert(results.bindings[i]->originalResult()->publishedBytes());
            std::printf("BINDLESS_SCENE_FAULT_WORD entry=%s scenario=%s word=%u\n",
                data.vertex->entry.c_str(),
                "success",
                fault);
            if (results.count == 2u)
                require(BindlessSceneGeometryReadbackFixture::inspectAndReport(
                    data.vertex->entry.c_str(),
                    results.bindings[1u]->originalResult()->publishedBytes(),
                    resultBytes,
                    terrain()));
            assert(data.observeAttachment);
            data.observeAttachment(BindlessSceneAttachmentFixture::completedTarget(*inventory),
                data.vertex->entry.c_str(),
                terrain(),
                results.count == 2u ? results.bindings[1u]->originalResult()->publishedBytes()
                                    : nullptr,
                false);
        }
        issuer.reset();
        if (inFlight) {
            // Fault retirement occurs inside the real completion handler,
            // before that handler drops its final closure/pass owners. Observe
            // actual custody drain rather than treating fault release as it.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (physical->used() || closureQuota->liveBytes() || passQuota->liveBytes()
                || submissionQuota->liveBytes() || viewQuota->liveBytes()) {
                assert(std::chrono::steady_clock::now() < deadline);
                std::this_thread::yield();
            }
        }
        assert(!physical->used() && !closureQuota->liveBytes() && !passQuota->liveBytes()
            && !submissionQuota->liveBytes() && !viewQuota->liveBytes());
        if (data.terminal == Terminal::Baseline)
            std::printf("BINDLESS_NATIVE_SCENE_PUBLIC_LIFECYCLE entry=%s outcome=%s results=%zu\n",
                data.vertex->entry.c_str(),
                data.cancelBeforeSubmit ? "cancelled-unpublished" : "completed",
                results.count);
        else
            std::printf(
                "BINDLESS_NATIVE_SCENE_FAILURE_LIFECYCLE entry=%s terminal=%u outcome=%s results=%zu\n",
                data.vertex->entry.c_str(),
                static_cast<unsigned>(data.terminal),
                droppedSuccess ? "completed" : "unpublished",
                results.count);
    }
};
} // namespace arp::test
