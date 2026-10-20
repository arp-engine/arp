#include "core/io/file_service.h"
#include "core/io/thread_pool_file_backend.h"
#include "core/runtime/completion_queue.h"
#include "core/runtime/work_service.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

struct Probe {
    arp::core::io::FileReadResult result;
    std::thread::id completionThread{};
    bool called = false;
};

std::filesystem::path testRoot()
{
    return std::filesystem::temp_directory_path() / "arp_file_service_thread_pool_tests";
}

void writeFixture(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "thread-pool";
}

arp::core::io::FileReadRequest requestFor(const std::filesystem::path& path)
{
    arp::core::io::FileReadRequest request;
    request.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    request.path = path.generic_string().c_str();
    request.range = arp::core::io::FileReadRange::wholeFile();
    return request;
}

void recordResult(void* userData, arp::core::io::FileReadResult result)
{
    auto* probe = static_cast<Probe*>(userData);
    probe->result = result;
    probe->completionThread = std::this_thread::get_id();
    probe->called = true;
}

} // namespace

int main()
{
    using namespace arp::core::io;

    const auto path = testRoot() / "fixture.bin";
    writeFixture(path);

    arp::core::runtime::CompletionQueue completions({8});
    arp::core::runtime::WorkService work({
        &completions,
        1,
        4,
    });
    ThreadPoolFallbackFileBackend backend(work);
    FileService service(completions, backend);

    const auto capability = service.capability();
    assert(capability.kind == FileBackendKind::ThreadPoolFallbackFileBackend);
    assert(capability.status == FileBackendStatus::PrototypeComplete);
    assert(!capability.nativeAsync);

    Probe probe;
    assert(service.read(requestFor(path), FileReadCompletion{&recordResult, &probe})
        == FileIoStatus::Accepted);

    for (int i = 0; i < 100 && !probe.called; ++i) {
        completions.dispatch(8);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::yield();
    }
    assert(probe.called);
    assert(probe.completionThread == std::this_thread::get_id());
    assert(probe.result.status == FileIoStatus::Success);
    assert(probe.result.backendKind == FileBackendKind::ThreadPoolFallbackFileBackend);
    assert(probe.result.bytes.size() == 11);

    work.shutdown(arp::core::runtime::WorkShutdownPolicy::Drain);
    return 0;
}
