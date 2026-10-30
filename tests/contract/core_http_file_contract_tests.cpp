#include "core/io/http_file_contract.h"

#include <cassert>
#include <cstdio>

using namespace arp::core::io;

namespace {

FileReadRequest request()
{
    FileReadRequest result;
    result.pathScope = FilePathScope::MountedResource;
    result.path = "a.bin";
    result.contractVersion = 1;
    result.maxBytes = 64;
    return result;
}

void push(HttpFileBodyReceiver& receiver, std::string_view bytes)
{
    receiver.append(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

} // namespace

int main()
{
    HttpFileBackendDesc desc;
    desc.baseUrl = "http://127.0.0.1:8094/fixture/";
    desc.maxInFlight = 4;
    desc.maxBytesInFlight = 65536;
    desc.maxPathBytes = 1024;
    desc.maxHeaderBytes = 4096;
    desc.ioChunkBytes = 32;
    desc.timeoutMs = 2000;
    desc.cancellationPollMs = 5;
    assert(!HttpFileContract::validate(desc).diagnostic);
    const auto originalUrl = desc.baseUrl;
    for (const auto* bad : {"http://h//",
             "http://u:p@h/",
             "file:///d:/",
             "http://h/../",
             "http://h/a%2fb/",
             "http://h:0/",
             "http://h:65536/",
             "http://h/#/"}) {
        desc.baseUrl = bad;
        assert(HttpFileContract::validate(desc).diagnostic);
    }
    desc.baseUrl = originalUrl;
    assert(HttpFileContract::url(desc, u8"路径/a #%.bin")
        == "http://127.0.0.1:8094/fixture/%E8%B7%AF%E5%BE%84/a%20%23%25.bin");
    auto read = request();
    assert(!HttpFileContract::validate(desc, read).diagnostic);
    HttpFileBodyReceiver whole(read);
    assert(whole.headers({200, "6", {}, {}}));
    push(whole, "abc");
    push(whole, "def");
    whole.finish();
    const auto complete = whole.take();
    assert(complete.status == FileIoStatus::Success && complete.bytes.size() == 6);
    assert(complete.version.kind == FileVersionKind::ContentSha256);

    read.range = FileReadRange::exactRange(2, 3);
    for (const auto* bad : {"bytes 2-3/6",
             "bytes 1-3/6",
             "bytes 2-4/*",
             "bytes 2-4/4",
             "Bytes 2-4/6",
             "bytes 2-4/18446744073709551616"}) {
        HttpFileBodyReceiver receiver(read);
        assert(!receiver.headers({206, "3", bad, {}}));
        assert(receiver.take().bytes.empty());
    }
    HttpFileBodyReceiver range(read);
    assert(range.headers({206, "3", "bytes 2-4/6", "identity"}));
    push(range, "cde");
    range.finish();
    assert(range.take().status == FileIoStatus::Success);
    HttpFileBodyReceiver ignored(read);
    assert(!ignored.headers({200, "6", {}, {}}));
    assert(ignored.take().status == FileIoStatus::IntegrityMismatch);

    read.expectedVersion = complete.version;
    read.maxVersionBytes = 6;
    assert(!HttpFileContract::usesWireRange(read));
    HttpFileBodyReceiver conditioned(read);
    assert(conditioned.headers({200, {}, {}, {}}));
    push(conditioned, "ab");
    push(conditioned, "cdef");
    conditioned.finish();
    auto result = conditioned.take();
    assert(result.status == FileIoStatus::Success && result.bytes.size() == 3);
    assert(result.bytes[0] == 'c' && result.bytes[2] == 'e');
    HttpFileBodyReceiver changed(read);
    assert(changed.headers({200, "6", {}, {}}));
    push(changed, "abcxef");
    changed.finish();
    result = changed.take();
    assert(result.status == FileIoStatus::VersionMismatch && result.bytes.empty());
    assert(result.version.kind == FileVersionKind::None);

    read = request();
    for (const unsigned code : {301, 302, 307, 308, 404, 403, 500}) {
        HttpFileBodyReceiver receiver(read);
        assert(!receiver.headers({code, {}, {}, {}}));
        assert(receiver.take().status != FileIoStatus::Success);
    }
    HttpFileBodyReceiver encoded(read);
    assert(!encoded.headers({200, {}, {}, "gzip"}));
    HttpFileBodyReceiver truncated(read);
    assert(truncated.headers({200, "6", {}, {}}));
    push(truncated, "abc");
    truncated.finish();
    assert(truncated.take().status == FileIoStatus::IntegrityMismatch);
    HttpFileBodyReceiver excess(read);
    assert(excess.headers({200, "2", {}, {}}));
    push(excess, "abc");
    assert(excess.take().status == FileIoStatus::IntegrityMismatch);
    read.maxBytes = 2;
    HttpFileBodyReceiver streamed(read);
    assert(streamed.headers({200, {}, {}, {}}));
    push(streamed, "abc");
    assert(streamed.take().status == FileIoStatus::BudgetExceeded);
    read = request();
    read.range = FileReadRange::exactRange(4, 4);
    HttpFileBodyReceiver denied(read);
    assert(!denied.headers({206, "2", "bytes 4-5/6", {}}));
    read.partialPolicy = FilePartialReadPolicy::AllowPartial;
    HttpFileBodyReceiver partial(read);
    assert(partial.headers({206, "2", "bytes 4-5/6", {}}));
    push(partial, "ef");
    partial.finish();
    assert(partial.take().bytes.size() == 2);
    std::puts("HTTP file byte contract PASS");
}
