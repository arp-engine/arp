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
    bool called = false;
};

void recordResult(void* userData, arp::core::io::FileReadResult result)
{
    auto* probe = static_cast<Probe*>(userData);
    probe->result = result;
    probe->called = true;
}

} // namespace

int main()
{
    using namespace arp::core::io;

    const auto path =
        std::filesystem::temp_directory_path() / "arp_file_service_smoke" / "fixture.txt";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path, std::ios::binary);
        file << "smoke";
    }

    arp::core::runtime::CompletionQueue completions({4});
    arp::core::runtime::WorkService work({&completions, 1, 2});
    ThreadPoolFallbackFileBackend backend(work);
    FileService service(completions, backend);

    FileReadRequest request;
    request.pathScope = FilePathScope::HostFilesystem;
    request.path = path.generic_string().c_str();

    Probe probe;
    assert(
        service.read(request, FileReadCompletion{&recordResult, &probe}) == FileIoStatus::Accepted);
    for (int i = 0; i < 100 && !probe.called; ++i) {
        completions.dispatch(4);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(probe.called);
    assert(probe.result.status == FileIoStatus::Success);
    assert(probe.result.bytes.size() == 5);
    assert(probe.result.bytes[0] == 's');

    work.shutdown(arp::core::runtime::WorkShutdownPolicy::Drain);
    return 0;
}
