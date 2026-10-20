#include "core/runtime/async_runtime.h"
#include "reflection/reflection.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <chrono>
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
        .property("name", &AsyncContractMaterial::name);

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
    assert(loadProbe.result.success);
    assert(manager.find<AsyncContractMaterial>("cdbbd56b-640f-4201-9915-5b6d974b2a8e").ready());
    assert(runtime.telemetry().fileService.submitted >= 2);

    Probe typedProbe;
    arp::resource::ResourceReference typedRef;
    typedRef.id = text("8822d04b-2435-43ca-8625-fdf2c35576d1");
    typedRef.type = text("AsyncContractResource");
    typedRef.alias = text("async.contract.resource");
    assert(typedRef.valid());
    assert(manager.loadAsync<AsyncContractResource>(runtime.workService(),
               arp::resource::ResourceAsyncLoadCompletion{&recordLoad, &typedProbe},
               arp::resource::ResourceLoadContext{},
               typedRef)
        == arp::core::runtime::WorkServiceStatus::Accepted);
    for (int i = 0; i < 100 && !typedProbe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(typedProbe.called);
    assert(typedProbe.result.success);
    assert(manager.stateOf("8822d04b-2435-43ca-8625-fdf2c35576d1")
        == arp::resource::ResourceState::Ready);
    assert(manager.find<AsyncContractResource>("8822d04b-2435-43ca-8625-fdf2c35576d1").ready());

    runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
    return 0;
}
