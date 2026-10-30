#include "core/runtime/async_runtime.h"
#include "reflection/reflection.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

struct AsyncSmokeResource {
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
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
    constexpr const char* resourceId = "8ccdb6c4-91ec-4260-b456-57f3f93c32fd";
    arp::reflection::registration::class_<AsyncSmokeResource>("AsyncSmokeResource")
        .construct<>()
        .property("name",
            &AsyncSmokeResource::name,
            {},
            static_cast<std::uint8_t>(arp::reflection::PropertyHint::Serialize));

    arp::core::pmr::String payload("type: AsyncSmokeResource\n"
                                   "fields:\n"
                                   "  name: SmokeAsync\n",
        &arp::core::defaultCpuMemoryResource());
    const auto hash = arp::resource::computeResourcePayloadHash(payload);
    assert(hash.success);

    arp::core::pmr::String envelope(&arp::core::defaultCpuMemoryResource());
    envelope += "format: ArpRuntimeResource\n";
    envelope += "version: 1\n";
    envelope += "resourceId: ";
    envelope += resourceId;
    envelope += "\nresourceType: AsyncSmokeResource\n";
    envelope += "status: Prototype Complete\n";
    envelope += "contentHash: ";
    envelope += std::to_string(hash.hash).c_str();
    envelope += "\n";
    envelope += "dependencies: []\n";
    envelope += "payload:\n";
    envelope += indentPayload(payload);

    const auto root = std::filesystem::temp_directory_path() / "arp_resource_async_smoke";
    std::filesystem::create_directories(root);
    const auto envelopePath = root / "smoke.arpres.yaml";
    {
        std::ofstream file(envelopePath, std::ios::binary);
        file << envelope;
    }

    arp::core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8;
    desc.workerCount = 1;
    desc.workQueueCapacity = 2;
    desc.completionBudget = 2;
    arp::core::runtime::AsyncRuntime runtime(desc);

    arp::resource::ResourceManager manager;
    arp::core::io::FileReadRequest request;
    request.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    request.path = envelopePath.generic_string().c_str();

    Probe probe;
    assert(manager.loadEnvelopeFileAsync(runtime.fileService(),
               arp::resource::ResourceAsyncLoadRequest{request, {}},
               arp::resource::ResourceAsyncLoadCompletion{&recordLoad, &probe})
        == arp::core::io::FileIoStatus::Accepted);

    for (int i = 0; i < 100 && !probe.called; ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(probe.called);
    if (!probe.result.success)
        std::fprintf(stderr, "async resource failure: %s\n", probe.result.diagnostics.c_str());
    assert(probe.result.success);
    // Legacy envelope admission retains reflection ownership explicitly.
    const auto* admitted = manager.recordSnapshot(resourceId);
    assert(admitted && admitted->state == arp::resource::ResourceState::Ready);
    const auto* resource = admitted->value.ptr<AsyncSmokeResource>();
    assert(resource && resource->name == "SmokeAsync");
    assert(!manager.find<AsyncSmokeResource>(resourceId).ready());
    assert(runtime.telemetry().fileService.submitted == 1);

    runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
    return 0;
}
