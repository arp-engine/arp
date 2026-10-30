#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "reflection/reflection.h"
#include "resource/resource_graph_executor.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

struct AsyncContractMaterial {
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
};

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

class AsyncContractResource final : public arp::resource::ResourceBase {
public:
    explicit AsyncContractResource(arp::resource::ResourceReference reference)
        : arp::resource::ResourceBase(std::move(reference))
    {
        addDependency(arp::resource::ResourceDependency{
            text("cdbbd56b-640f-4201-9915-5b6d974b2a8e"),
            text("material"),
        });
    }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }
};

struct Probe {
    arp::resource::ResourceLoadResult result;
    bool called = false;
};

arp::core::pmr::String indentPayload(const arp::core::pmr::String& payload)
{
    arp::core::pmr::String indented(&arp::core::defaultCpuMemoryResource());
    indented += "  ";
    for (const char ch : payload) {
        indented.push_back(ch);
        if (ch == '\n') {
            indented += "  ";
        }
    }
    return indented;
}

void recordLoad(void* userData, arp::resource::ResourceLoadResult result)
{
    auto* probe = static_cast<Probe*>(userData);
    probe->result = result;
    probe->called = true;
}

} // namespace

int main()
{
    arp::reflection::registration::class_<AsyncContractMaterial>("AsyncContractMaterial")
        .construct<>()
        .property("name",
            &AsyncContractMaterial::name,
            {},
            static_cast<std::uint8_t>(arp::reflection::PropertyHint::Serialize));

    arp::core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8;
    desc.workerCount = 1;
    desc.workQueueCapacity = 2;
    desc.completionBudget = 4;

    arp::core::runtime::AsyncRuntime runtime(desc);
    arp::resource::ResourceManager manager;

    Probe invalidProbe;
    arp::core::io::FileReadRequest invalidFile;
    assert(manager.loadEnvelopeFileAsync(runtime.fileService(),
               arp::resource::ResourceAsyncLoadRequest{invalidFile, {}},
               arp::resource::ResourceAsyncLoadCompletion{&recordLoad, &invalidProbe})
        == arp::core::io::FileIoStatus::Accepted);
    for (int i = 0; i < 100 && !invalidProbe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(invalidProbe.called);
    assert(!invalidProbe.result.success);
    assert(
        invalidProbe.result.diagnostics.find("InvalidPathScope") != arp::core::pmr::String::npos);

    const auto root = std::filesystem::temp_directory_path() / "arp_resource_async_contract";
    std::filesystem::create_directories(root);
    const auto envelopePath = root / "material.arpres.yaml";

    const auto payload = text("type: AsyncContractMaterial\n"
                              "fields:\n"
                              "  name: AsyncRock\n");
    const auto hash = arp::resource::computeResourcePayloadHash(payload);
    assert(hash.success);

    arp::core::pmr::String envelope(&arp::core::defaultCpuMemoryResource());
    envelope += "format: ArpRuntimeResource\n";
    envelope += "version: 1\n";
    envelope += "resourceId: cdbbd56b-640f-4201-9915-5b6d974b2a8e\n";
    envelope += "resourceType: AsyncContractMaterial\n";
    envelope += "status: Contract Complete\n";
    envelope += "contentHash: ";
    envelope += std::to_string(hash.hash).c_str();
    envelope += "\n";
    envelope += "dependencies: []\n";
    envelope += "payload:\n";
    envelope += indentPayload(payload);
    {
        std::ofstream file(envelopePath, std::ios::binary);
        file << envelope;
    }

    Probe loadProbe;
    arp::core::io::FileReadRequest file;
    file.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    file.path = envelopePath.generic_string().c_str();
    assert(manager.loadEnvelopeFileAsync(runtime.fileService(),
               arp::resource::ResourceAsyncLoadRequest{file, {}},
               arp::resource::ResourceAsyncLoadCompletion{&recordLoad, &loadProbe})
        == arp::core::io::FileIoStatus::Accepted);

    for (int i = 0; i < 100 && !loadProbe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(loadProbe.called);
    if (!loadProbe.result.success)
        std::fprintf(stderr, "async resource failure: %s\n", loadProbe.result.diagnostics.c_str());
    assert(loadProbe.result.success);
    // This legacy envelope owns a reflected payload, not a native ResourceBase.
    const auto* admitted = manager.recordSnapshot("cdbbd56b-640f-4201-9915-5b6d974b2a8e");
    assert(admitted && admitted->state == arp::resource::ResourceState::Ready);
    const auto* material = admitted->value.ptr<AsyncContractMaterial>();
    assert(material && material->name == "AsyncRock");
    assert(!manager.find<AsyncContractMaterial>("cdbbd56b-640f-4201-9915-5b6d974b2a8e").ready());
    assert(runtime.telemetry().fileService.submitted >= 2);

    auto bounded = file;
    bounded.contractVersion = 1;
    bounded.maxBytes = 1;
    Probe budgetProbe;
    assert(manager.loadEnvelopeFileAsync(
               runtime.fileService(), {bounded, {}}, {recordLoad, &budgetProbe})
        == arp::core::io::FileIoStatus::Accepted);
    for (int i = 0; i < 100 && !budgetProbe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(budgetProbe.called && !budgetProbe.result.success);
    assert(budgetProbe.result.diagnostics.find("BudgetExceeded") != arp::core::pmr::String::npos);
    assert(manager.stateOf("cdbbd56b-640f-4201-9915-5b6d974b2a8e")
        == arp::resource::ResourceState::Ready);

    {
        const auto generation = manager.generationOf("cdbbd56b-640f-4201-9915-5b6d974b2a8e");
        auto invalidUtf8 = envelope;
        invalidUtf8 += static_cast<char>(0xFF);
        auto memory = arp::core::makeShared<arp::core::io::MemoryFileBackend>(
            arp::core::io::MemoryFileBackendDesc{1, invalidUtf8.size(), 1, invalidUtf8.size(), 64});
        assert(memory->registerFile("bad-envelope.yaml",
                   reinterpret_cast<const std::uint8_t*>(invalidUtf8.data()),
                   invalidUtf8.size())
            == arp::core::io::FileIoStatus::Success);
        arp::core::runtime::CompletionQueue queue({1});
        arp::core::io::FileService service(queue, *memory);
        auto read = bounded;
        read.pathScope = arp::core::io::FilePathScope::MountedResource;
        read.path = "bad-envelope.yaml";
        read.maxBytes = invalidUtf8.size();
        Probe invalidText;
        assert(manager.loadEnvelopeFileAsync(service, {read, {}}, {recordLoad, &invalidText})
            == arp::core::io::FileIoStatus::Accepted);
        queue.dispatch(1);
        assert(invalidText.called && !invalidText.result.success);
        assert(invalidText.result.diagnostics.find("TEXT-INVALID-UTF8")
            != arp::core::pmr::String::npos);
        assert(manager.generationOf("cdbbd56b-640f-4201-9915-5b6d974b2a8e") == generation);
        assert(manager.recordSnapshot("cdbbd56b-640f-4201-9915-5b6d974b2a8e")->state
            == arp::resource::ResourceState::Ready);
    }

    {
        auto memory = arp::core::makeShared<arp::core::io::MemoryFileBackend>(
            arp::core::io::MemoryFileBackendDesc{1, envelope.size(), 1, envelope.size(), 64});
        assert(memory->registerFile("material.arpres.yaml",
                   reinterpret_cast<const std::uint8_t*>(envelope.data()),
                   envelope.size())
            == arp::core::io::FileIoStatus::Success);
        auto mounts = arp::core::makeShared<arp::core::io::MountedFileBackend>(
            arp::core::io::MountedFileBackendDesc{1, 1, envelope.size(), 16, 64});
        const auto mount = mounts->mount("workspace", memory);
        assert(mount);
        arp::core::runtime::AsyncRuntimeDesc mountedDesc;
        mountedDesc.completionQueueCapacity = 1;
        mountedDesc.completionBudget = 1;
        mountedDesc.enableWork = false;
        mountedDesc.enableThreadPoolFileBackend = false;
        mountedDesc.fileBackend = mounts;
        arp::core::runtime::AsyncRuntime mountedRuntime(mountedDesc);
        arp::resource::ResourceManager staleManager;
        auto mountedRead = bounded;
        mountedRead.pathScope = arp::core::io::FilePathScope::MountedResource;
        mountedRead.path = "material.arpres.yaml";
        mountedRead.maxBytes = envelope.size();
        mountedRead.mount = mount;
        Probe staleProbe;
        assert(staleManager.loadEnvelopeFileAsync(
                   mountedRuntime.fileService(), {mountedRead, {}}, {recordLoad, &staleProbe})
            == arp::core::io::FileIoStatus::Accepted);
        assert(mounts->unmount(mount));
        mountedRuntime.dispatch();
        assert(staleProbe.called && !staleProbe.result.success);
        assert(staleProbe.result.diagnostics.find("StaleLocation") != arp::core::pmr::String::npos);
        assert(staleManager.recordSnapshot("cdbbd56b-640f-4201-9915-5b6d974b2a8e") == nullptr);
        mountedRuntime.shutdown();
    }

    Probe typedProbe;
    arp::resource::ResourceReference typedRef;
    typedRef.id = text("8822d04b-2435-43ca-8625-fdf2c35576d1");
    typedRef.type = text("AsyncContractResource");
    typedRef.alias = text("async.contract.resource");
    typedRef.status = text("Contract Complete");
    assert(typedRef.valid());
    assert(manager.loadAsync<AsyncContractResource>(runtime,
               arp::resource::ResourceGraphExecutionBudget{1, 1, 1, 1, 2},
               arp::resource::ResourceGraphExecutionCompletion{
                   [](void* user, arp::resource::ResourceGraphExecutionResult result) {
                       recordLoad(user, std::move(result.resource));
                   },
                   &typedProbe},
               arp::resource::ResourceLoadContext{},
               typedRef)
        == arp::resource::ResourceGraphSubmitStatus::Accepted);
    for (int i = 0; i < 100 && !typedProbe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(typedProbe.called);
    if (!typedProbe.result.success)
        std::fprintf(
            stderr, "typed async resource failure: %s\n", typedProbe.result.diagnostics.c_str());
    assert(typedProbe.result.success);
    assert(manager.stateOf("8822d04b-2435-43ca-8625-fdf2c35576d1")
        == arp::resource::ResourceState::Ready);
    assert(manager.find<AsyncContractResource>("8822d04b-2435-43ca-8625-fdf2c35576d1").ready());

    runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
    return 0;
}
