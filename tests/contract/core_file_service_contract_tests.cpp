#include "core/io/file_backend.h"
#include "core/io/file_request.h"
#include "core/io/file_result.h"
#include "core/io/file_service.h"
#include "core/runtime/completion_queue.h"

#include <cassert>

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

    arp::core::runtime::CompletionQueue completions({4});
    NullFileBackend backend;
    FileService service(completions, backend);

    const auto capability = service.capability();
    assert(capability.kind == FileBackendKind::NullFileBackend);
    assert(capability.status == FileBackendStatus::ContractComplete);
    assert(!capability.nativeAsync);
    assert(capability.completesThroughCompletionQueue);

    FileReadRequest request;
    request.pathScope = FilePathScope::HostFilesystem;
    request.path = "/tmp/fixture.bin";
    request.range = FileReadRange::wholeFile();
    request.partialPolicy = FilePartialReadPolicy::DenyPartial;

    Probe probe;
    assert(
        service.read(request, FileReadCompletion{&recordResult, &probe}) == FileIoStatus::Accepted);
    assert(!probe.called);
    assert(completions.dispatch(1).dispatched == 1);
    assert(probe.called);
    assert(probe.result.status == FileIoStatus::BackendUnavailable);
    assert(probe.result.backendKind == FileBackendKind::NullFileBackend);
    assert(probe.result.bytes.empty());
    assert(!probe.result.diagnostics.empty());

    FileReadRequest invalidScope = request;
    invalidScope.pathScope = FilePathScope::Unspecified;
    Probe invalidProbe;
    assert(service.read(invalidScope, FileReadCompletion{&recordResult, &invalidProbe})
        == FileIoStatus::Accepted);
    assert(completions.dispatch(1).dispatched == 1);
    assert(invalidProbe.result.status == FileIoStatus::InvalidPathScope);

    FileReadRequest zeroExact = request;
    zeroExact.range = FileReadRange::exactRange(4, 0);
    Probe zeroProbe;
    assert(service.read(zeroExact, FileReadCompletion{&recordResult, &zeroProbe})
        == FileIoStatus::Accepted);
    assert(completions.dispatch(1).dispatched == 1);
    assert(zeroProbe.result.status == FileIoStatus::InvalidRange);

    service.shutdown();
    assert(service.read(request, FileReadCompletion{&recordResult, &probe})
        == FileIoStatus::ServiceClosed);

    return 0;
}
