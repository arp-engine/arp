#pragma once

#include "resource/internal_asset_package_loader.h"
#include "tests/fixtures/package_source_fixture.h"

namespace arp::tests {

class PackageAsyncFixture final {
public:
    enum class Action {
        None,
        CancelBefore,
        CancelAfterRead,
        Remount,
        Shutdown,
        ServiceClosed,
        ReadLimit,
        ByteLimit,
        QueueFull,
        AllocationBefore,
        AllocationAfter,
        CancelOnCommit,
        RemountOnCommit
    };

    struct Probe {
        unsigned calls = 0;
        resource::AssetPackageLoadResult result;

        static void complete(void* context, resource::AssetPackageLoadResult result)
        {
            auto& probe = *static_cast<Probe*>(context);
            ++probe.calls;
            probe.result = std::move(result);
        }
    };

    static resource::AssetPackageLoadResult run(
        core::SharedPtr<core::io::ManagedFileBackend> backend,
        bool expected,
        Action action = Action::None)
    {
        using namespace core;
        using namespace core::io;
        using namespace resource;
        auto router =
            makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 4, 4 * 1024 * 1024, 32, 1024});
        const auto mount = router->mount("package", backend);
        assert(mount);
        runtime::AsyncRuntimeDesc desc;
        desc.enableWork = false;
        desc.enableThreadPoolFileBackend = false;
        desc.fileBackend = router;
        desc.completionQueueCapacity = desc.completionBudget = 8;
        runtime::AsyncRuntime runtime(desc);
        assert(runtime.valid());
        ResourceManager manager;
        constexpr const char* sentinel = "1c88da51-195d-42ee-a98c-576f1b1cd114";
        assert(manager
                .loadResource(makeUnique<ResourceBase>(
                    ResourceReference{sentinel, "Sentinel", "retained", "Contract Complete", {}}))
                .success);
        const auto generation = manager.generationOf(sentinel);
        AssetPackageLoadRequest request;
        request.directory = "snapshot";
        request.origin = {FilePathScope::MountedResource, mount};
        request.budget = {65536, 32, 64, 1024 * 1024, 4 * 1024 * 1024, 1024 * 1024};
        runtime::CancellationSource cancellation;
        request.cancellation = cancellation.token();
        if (action == Action::CancelOnCommit || action == Action::RemountOnCommit)
            request.codecs = commitCodec(
                *router, mount, backend, cancellation, action == Action::CancelOnCommit);
        if (action == Action::CancelBefore)
            cancellation.cancel();
        if (action == Action::ReadLimit)
            request.budget.maxFileReads = 1;
        if (action == Action::ByteLimit)
            request.budget.maxTotalBytes = 1;
        if (action == Action::QueueFull)
            for (unsigned i = 0; i < 8; ++i)
                assert(runtime.completionQueue().post({[](void*) {}, nullptr}).accepted());
        Probe probe;
        auto quota = makeShared<CpuMemoryQuota>(1);
        FileIoStatus submitted;
        if (action == Action::AllocationBefore) {
            CpuMemoryQuotaScope scope(quota);
            submitted = InternalAssetPackageLoader(
                manager, runtime.fileService(), runtime.completionQueue())
                            .submit(std::move(request), {Probe::complete, &probe});
        } else
            submitted = InternalAssetPackageLoader(
                manager, runtime.fileService(), runtime.completionQueue())
                            .submit(std::move(request), {Probe::complete, &probe});
        assert(probe.calls == 0);
        if (action == Action::QueueFull || action == Action::AllocationBefore) {
            assert(submitted
                == (action == Action::QueueFull ? FileIoStatus::QueueFull
                                                : FileIoStatus::BudgetExceeded));
            runtime.dispatch();
            assert(probe.calls == 0 && quota->liveBytes() == 0);
            runtime.shutdown();
            return {};
        }
        assert(submitted == FileIoStatus::Accepted);
        if (action == Action::AllocationAfter) {
            CpuMemoryQuotaScope scope(quota);
            runtime.dispatch(1);
        } else if (action == Action::Remount || action == Action::Shutdown
            || action == Action::CancelAfterRead || action == Action::ServiceClosed) {
            runtime.dispatch(1); // Kickoff submitted the first read; delivery is still pending.
            assert(probe.calls == 0);
            if (action == Action::Remount) {
                assert(router->unmount(mount));
                const auto replacement = router->mount("package", backend);
                assert(replacement && !(replacement == mount));
            } else if (action == Action::Shutdown)
                router->shutdown();
            else if (action == Action::ServiceClosed)
                runtime.fileService().shutdown();
            else
                cancellation.cancel();
        }
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return probe.calls != 0;
        });
        assert(probe.calls == 1 && probe.result.telemetry.completions == 1);
        if (probe.result.resource.success != expected)
            std::fprintf(stderr,
                "async package: %s / %s\n",
                probe.result.diagnosticId ? probe.result.diagnosticId : "no diagnostic",
                probe.result.resource.diagnostics.c_str());
        assert(probe.result.resource.success == expected);
        assert(probe.result.telemetry.committed == expected);
        assert(manager.find<ResourceBase>(sentinel).ready()
            && manager.generationOf(sentinel) == generation);
        if (expected) {
            assert(probe.result.telemetry.plans >= 3 && probe.result.telemetry.submittedReads > 3);
            const auto terrain =
                manager.find<TerrainAssetResource>(PackageSourceFixture::terrainId);
            assert(terrain.ready() && terrain.get()->sample(1, 1).sample.height == 4);
        } else {
            for (const auto* type : {"TerrainAssetResource",
                     "VegetationCatalogResource",
                     "MaterialResource",
                     "ShaderPackageResource",
                     "TextureResource",
                     "ExternalAssetSourceResource",
                     "CommitResource"}) {
                const auto snapshot = manager.snapshotReferences(type, 32);
                assert(snapshot.success && snapshot.entries.empty());
            }
        }
        if (action == Action::Remount || action == Action::RemountOnCommit)
            assert(probe.result.fileStatus == FileIoStatus::StaleLocation);
        if (action == Action::CancelBefore || action == Action::CancelAfterRead
            || action == Action::CancelOnCommit)
            assert(probe.result.fileStatus == FileIoStatus::Cancelled);
        if (action == Action::Shutdown || action == Action::ServiceClosed)
            assert(probe.result.fileStatus == FileIoStatus::ServiceClosed);
        if (action == Action::ReadLimit || action == Action::ByteLimit
            || action == Action::AllocationAfter)
            assert(probe.result.fileStatus == FileIoStatus::BudgetExceeded);
        assert(quota->liveBytes() == 0);
        runtime.shutdown();
        assert(
            runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
        assert(router->telemetry().inFlight == 0);
        if (const auto* native = dynamic_cast<const NativeDirectoryFileBackend*>(backend.get()))
            assert(native->telemetry().openNativeObjects == 0);
        if (const auto* http = dynamic_cast<const HttpFileBackend*>(backend.get()))
            assert(http->telemetry().inFlight == 0 && http->telemetry().activeNativeTransfers == 0);
        return std::move(probe.result);
    }

    static core::SharedPtr<core::io::HttpFileBackend> http(const char* url)
    {
        core::io::HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 2;
        desc.maxBytesInFlight = 4 * 1024 * 1024;
        desc.maxPathBytes = 1024;
        desc.maxHeaderBytes = 4096;
        desc.ioChunkBytes = 4096;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 2;
        return core::makeShared<core::io::HttpFileBackend>(desc);
    }

    static void contracts(const std::filesystem::path& root)
    {
        atomicGateContracts();
        run(commitMemory(), false, Action::CancelOnCommit);
        run(commitMemory(), false, Action::RemountOnCommit);
        for (auto mutation : {PackageSourceFixture::Mutation::None,
                 PackageSourceFixture::Mutation::Missing,
                 PackageSourceFixture::Mutation::Corrupt,
                 PackageSourceFixture::Mutation::BadUtf8,
                 PackageSourceFixture::Mutation::UnknownVersion,
                 PackageSourceFixture::Mutation::UnclosedCorrupt})
            run(PackageSourceFixture::memory(root, mutation),
                mutation == PackageSourceFixture::Mutation::None);
        for (auto action : {Action::CancelBefore,
                 Action::CancelAfterRead,
                 Action::Remount,
                 Action::Shutdown,
                 Action::ServiceClosed,
                 Action::ReadLimit,
                 Action::ByteLimit,
                 Action::QueueFull,
                 Action::AllocationBefore,
                 Action::AllocationAfter})
            run(PackageSourceFixture::memory(root), false, action);
        for (unsigned i = 0; i < 20; ++i)
            run(PackageSourceFixture::memory(root), true);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run(PackageSourceFixture::memory(root), true);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes);
        assert(before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
        std::puts(
            "Async package planner/memory contracts, 20 ordinary cycles core live bytes/count delta 0 PASS");
    }

    static void atomicGateContracts()
    {
        using namespace resource;

        struct Gate {
            bool allowed = true;
            unsigned calls = 0;
        };

        class Candidate final : public ResourceBase {
        public:
            Candidate(Gate* gate = nullptr, bool throwing = false)
                : ResourceBase({"96e377d2-5cd0-4d24-8545-aab92ed79f31",
                      "Candidate",
                      "atomic-candidate",
                      "Contract Complete",
                      {}}),
                  gate_(gate), throwing_(throwing)
            {}

            ResourceLoadResult load(
                ResourceManager& manager, const ResourceLoadContext& context) override
            {
                auto result = ResourceBase::load(manager, context);
                if (gate_)
                    gate_->allowed = false;
                if (throwing_)
                    throw std::bad_alloc();
                return result;
            }

        private:
            Gate* gate_;
            bool throwing_;
        };

        for (bool throwing : {false, true}) {
            ResourceManager manager;
            auto seed = core::makeUnique<Candidate>();
            const auto id = seed->reference().id;
            assert(manager.loadResource(std::move(seed)).success);
            assert(manager.unload(id).success);
            const auto* before = manager.recordSnapshot(id);
            const auto generation = before->generation;
            Gate gate;
            ResourceLoadContext context;
            context.commitGate = {[](void* value) noexcept {
                                      auto& gate = *static_cast<Gate*>(value);
                                      ++gate.calls;
                                      return gate.allowed;
                                  },
                &gate};
            core::pmr::Vector<core::UniquePtr<ResourceBase>> batch;
            batch.push_back(core::makeUnique<Candidate>(&gate, throwing));
            const auto result = manager.loadResourcesAtomically(std::move(batch), context);
            assert(!result.success && result.rolledBack && result.loadedCount == 0);
            assert(manager.recordSnapshot(id) == before && before->generation == generation);
            assert(before->state == ResourceState::Unloaded);
            assert(manager.resolveAlias("atomic-candidate").success);
            if (!throwing)
                assert(gate.calls >= 3); // Includes the gate after the final load.
        }
    }

    static core::SharedPtr<core::io::MemoryFileBackend> commitMemory()
    {
        using namespace resource;
        ResourceEnvelopeBuildDesc desc;
        desc.resourceId = "96e377d2-5cd0-4d24-8545-aab92ed79f31";
        desc.resourceType = "CommitResource";
        desc.status = "Contract Complete";
        const auto envelope =
            buildResourceEnvelopeFromYaml(desc, "type: CommitResource\nfields: {}\n");
        assert(envelope.success);
        using Y = core::YamlContract;
        auto manifest = Y::Node::mapping();
        manifest["format"] = "ArpInternalAssetPackage";
        manifest["version"] = 1;
        manifest["name"] = "commit-gate";
        manifest["status"] = "Contract Complete";
        manifest["entries"] = Y::Node::sequence();
        auto entry = Y::Node::mapping();
        entry["resourceId"] = desc.resourceId.c_str();
        entry["resourceType"] = "CommitResource";
        entry["status"] = "Contract Complete";
        entry["envelopePath"] = "entry.yaml";
        entry["contentHash"] = envelope.contentHash;
        entry["dependencies"] = Y::Node::sequence();
        manifest["entries"].get_value_ref<Y::Node::sequence_type&>().push_back(entry);
        auto memory = core::makeShared<core::io::MemoryFileBackend>(
            core::io::MemoryFileBackendDesc{3, 65536, 2, 131072, 1024});
        assert(memory->registerDirectory("snapshot") == core::io::FileIoStatus::Success);
        const auto text = Y::serialize(manifest);
        assert(memory->registerFile("snapshot/manifest.arpkg.yaml",
                   reinterpret_cast<const std::uint8_t*>(text.data()),
                   text.size())
            == core::io::FileIoStatus::Success);
        assert(memory->registerFile("snapshot/entry.yaml",
                   reinterpret_cast<const std::uint8_t*>(envelope.envelopeYaml.data()),
                   envelope.envelopeYaml.size())
            == core::io::FileIoStatus::Success);
        return memory;
    }

    static core::SharedPtr<resource::AssetPackageCodecRegistry> commitCodec(
        core::io::MountedFileBackend& router,
        core::PoolHandle mount,
        core::SharedPtr<core::io::ManagedFileBackend> backend,
        core::runtime::CancellationSource& cancellation,
        bool cancel)
    {
        struct Trigger {
            core::io::MountedFileBackend* router;
            core::PoolHandle mount;
            core::SharedPtr<core::io::ManagedFileBackend> backend;
            core::runtime::CancellationSource* cancellation;
            bool cancel;

            void fire()
            {
                if (cancel)
                    cancellation->cancel();
                else {
                    assert(router->unmount(mount));
                    auto replacement = router->mount("package", backend);
                    assert(replacement && !(replacement == mount));
                }
            }
        };

        class Candidate final : public resource::ResourceBase {
        public:
            explicit Candidate(core::SharedPtr<Trigger> trigger)
                : ResourceBase({"96e377d2-5cd0-4d24-8545-aab92ed79f31",
                      "CommitResource",
                      {},
                      "Contract Complete",
                      {}}),
                  trigger_(std::move(trigger))
            {}

            resource::ResourceLoadResult load(resource::ResourceManager& manager,
                const resource::ResourceLoadContext& context) override
            {
                auto result = ResourceBase::load(manager, context);
                trigger_->fire();
                return result;
            }

        private:
            core::SharedPtr<Trigger> trigger_;
        };

        class Codec final : public resource::AssetPackageCodec {
        public:
            explicit Codec(core::SharedPtr<Trigger> trigger) : trigger_(std::move(trigger)) {}

            resource::NameView resourceType() const noexcept override { return "CommitResource"; }

            resource::AssetPackagePayloadIndex inspect(const resource::InternalAssetPackageEntry&,
                const std::filesystem::path&) const override
            {
                assert(false);
                return {};
            }

            core::UniquePtr<resource::ResourceBase> prepare(
                const resource::InternalAssetPackageEntry&,
                const std::filesystem::path&,
                core::pmr::String&) const override
            {
                assert(false);
                return {};
            }

            resource::AssetPackagePayloadIndex inspectSources(
                const resource::InternalAssetPackageEntry&,
                const std::filesystem::path&,
                const resource::AssetPackageSourceContext&) const override
            {
                resource::AssetPackagePayloadIndex result;
                result.success = true;
                return result;
            }

            bool planSourceReads(const resource::InternalAssetPackageEntry&,
                const std::filesystem::path&,
                const resource::AssetPackageSourceContext&,
                core::pmr::Vector<resource::ResourceFileReadIntent>&,
                core::pmr::String&) const override
            {
                return true;
            }

            core::UniquePtr<resource::ResourceBase> prepareSources(
                const resource::InternalAssetPackageEntry&,
                const std::filesystem::path&,
                const resource::AssetPackageSourceContext&,
                core::pmr::String&) const override
            {
                return core::makeUnique<Candidate>(trigger_);
            }

        private:
            core::SharedPtr<Trigger> trigger_;
        };

        auto trigger =
            core::makeShared<Trigger>(Trigger{&router, mount, backend, &cancellation, cancel});
        auto registry = core::makeShared<resource::AssetPackageCodecRegistry>(1);
        core::pmr::String diagnostic;
        assert(registry->add(core::makeShared<Codec>(trigger), diagnostic));
        assert(registry->freeze(diagnostic));
        return registry;
    }
};

} // namespace arp::tests
