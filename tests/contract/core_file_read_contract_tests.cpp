#include "core/io/blocking_file_reader.h"
#include "core/io/file_read_contract.h"
#include "core/io/file_service.h"
#include "core/io/thread_pool_file_backend.h"

#include <cassert>

namespace {

using namespace arp::core::io;

void record(void* context, FileReadResult)
{
    ++*static_cast<unsigned*>(context);
}

FileReadRequest boundedRequest()
{
    FileReadRequest request;
    request.pathScope = FilePathScope::HostFilesystem;
    // Validation must succeed even when no file exists at this location.
    request.path = "arp-validation-does-not-open-a-file/absent";
    request.contractVersion = 1;
    request.maxBytes = 64;
    return request;
}

} // namespace

int main()
{
    auto request = boundedRequest();
    assert(FileReadContract::validate(request).status == FileIoStatus::Success);
    BlockingFileReader reader;
    assert(reader.validateRead(request).status == FileIoStatus::Success);
    assert(reader.read(request).status == FileIoStatus::MissingFile);

    auto invalid = request;
    invalid.contractVersion = 2;
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = request;
    invalid.maxBytes = UINT64_MAX;
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid.maxBytes = 0;
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = request;
    invalid.path.push_back('\0');
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = request;
    invalid.pathScope = static_cast<FilePathScope>(255);
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidPathScope);
    invalid = request;
    invalid.range.mode = static_cast<FileReadRangeMode>(255);
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRange);
    invalid = request;
    invalid.range.offset = 1;
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRange);
    invalid.range = FileReadRange::exactRange(UINT64_MAX, 2);
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRange);
    invalid.range = FileReadRange::exactRange(0, 65);
    assert(FileReadContract::validate(invalid).status == FileIoStatus::BudgetExceeded);
    invalid.range = FileReadRange::exactRange(0, 0);
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRange);
    invalid = request;
    invalid.priority = static_cast<FileRequestPriority>(255);
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRequest);
    invalid = request;
    invalid.partialPolicy = static_cast<FilePartialReadPolicy>(255);
    assert(FileReadContract::validate(invalid).status == FileIoStatus::InvalidRequest);

    arp::core::runtime::CompletionQueue queue({2});
    NullFileBackend backend;
    FileService service(queue, backend);
    unsigned callbacks = 0;
    // Version1 rejects malformed input before a backend can queue any work.
    assert(service.read(invalid, {record, &callbacks}) == FileIoStatus::InvalidRequest);
    assert(queue.pending() == 0 && callbacks == 0);
    assert(service.read(request, {record, &callbacks}) == FileIoStatus::Accepted);
    assert(callbacks == 0);
    assert(queue.dispatch(2).dispatched == 1 && callbacks == 1);

    arp::core::runtime::WorkService work({&queue, 1, 2});
    ThreadPoolFallbackFileBackend local(work);
    assert(local.validateRead(request).status == FileIoStatus::Success);
    work.shutdown(arp::core::runtime::WorkShutdownPolicy::Drain);
}
