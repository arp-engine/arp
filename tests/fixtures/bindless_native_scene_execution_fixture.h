#pragma once
#include "bindless_scene_geometry_data_fixture.h"
#include "bindless_scene_geometry_readback_fixture.h"
#include "original_bindless_graphics_stream_fixture.h"
#include "rhi/frontend/bindless_graphics_execution.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace arp::test {
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
    rhi::BindlessPbrGeometryDraw16 draw;
    bool cancelBeforeSubmit = false;

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
            data.geometry->actors = {};
            data.geometry->leases = {};
            data.geometry->resultReadback.reset();
            data.state->shaderActors.clear();
            return;
        }
        rhi::OriginalBindlessSceneContentInputs scene{1u, true, data.family, 4096u, 4096u, 4096u};
        require(rhi::OriginalBindlessSceneContentContract::validateRootInput(root, scene));
        OriginalBindlessGraphicsStreamFixture::execute(root, inventory, *device, *this);
    }

    // Stream traits and input bytes refer to this same retained Data instance.
    static constexpr bool graphicsPush = true, sceneStream = true, completedTextures = true;
    static constexpr auto geometryBufferCount = Data::geometryBufferCount;
    static constexpr auto resultBytes = Data::resultBytes;

    bool terrain() const { return data.terrain(); }

    const rhi::BindlessShaderArtifact* artifact = nullptr;
    std::shared_ptr<typename Data::Geometry> geometry;
    rhi::BindlessPbrGeometryDraw16 draw;

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
        const auto ownerCeiling = inventory->originalBudget().maxGraphOwners;
        budget.closure = {3u, true, resources, ownerCeiling, ownerCeiling, bytes};
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
        inputs.scene = {1u, true, data.family, resources * 4u, resources * 4u, operations};
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        core::pmr::String diagnostic;
        auto missing = inputs;
        missing.scene = {};
        assert(!extension->prepareOriginalGraphicsExecution(device, missing, budget, diagnostic)
            && diagnostic == "RHI-BINDLESS-SCENE-CONTENT-INPUTS-REQUIRED");
        assert(!closureQuota->liveBytes() && !physical->used() && tx->journalEmpty());
        auto issuer =
            extension->prepareOriginalGraphicsExecution(device, inputs, budget, diagnostic);
        require(issuer ? nullptr : diagnostic.c_str());
        assert(issuer && !issuer->recordingProven() && !issuer->submissionCompleted());
        require(Public::record(issuer, budget.intent));
        assert(!binding->originalResult()->publishedBytes());
        if (data.cancelBeforeSubmit) {
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
            if (results.count == 2u)
                require(BindlessSceneGeometryReadbackFixture::inspect(
                    results.bindings[1u]->originalResult()->publishedBytes(),
                    resultBytes,
                    terrain()));
        }
        issuer.reset();
        assert(!physical->used() && !closureQuota->liveBytes() && !passQuota->liveBytes()
            && !submissionQuota->liveBytes() && !viewQuota->liveBytes());
        std::printf("BINDLESS_NATIVE_SCENE_PUBLIC_LIFECYCLE entry=%s outcome=%s results=%zu\n",
            data.vertex->entry.c_str(),
            data.cancelBeforeSubmit ? "cancelled-unpublished" : "completed",
            results.count);
    }
};
} // namespace arp::test
