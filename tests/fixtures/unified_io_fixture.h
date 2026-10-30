#pragma once

#include "core/io/filesystem_path.h"
#include "core/io/native_directory_file_backend.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace unified_io_fixture {

class Workspace final {
public:
    Workspace()
    {
        temporary_ = std::filesystem::canonical(std::filesystem::temp_directory_path());
        base_ = temporary_
            / ("arp-unified-io-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        root = base_ / "root";
        std::filesystem::create_directories(root / "nested");
        std::filesystem::create_directories(base_ / "outside");
        write("a.bin", "abcdef");
        write("b.bin", "b");
        write(u8"nested/路径.bin", "unicode");
        std::ofstream outside(base_ / "outside/secret.bin", std::ios::binary);
        outside << "outside";
    }

    ~Workspace()
    {
        assert(base_.parent_path() == temporary_);
        assert(base_.filename().u8string().find("arp-unified-io-") == 0);
#if defined(_WIN32)
        RemoveDirectoryW((root / "link").c_str());
#endif
        std::error_code error;
        std::filesystem::remove_all(base_, error);
        assert(!error);
    }

    void write(std::string_view name, std::string_view bytes)
    {
        const auto filePath = root / std::filesystem::u8path(name.begin(), name.end());
        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        assert(file.good());
    }

    arp::core::io::NativeDirectoryFileBackendDesc descriptor() const
    {
        arp::core::io::NativeDirectoryFileBackendDesc desc;
        desc.rootPath = arp::core::FilePath::utf8(root);
        desc.maxInFlight = 4;
        desc.maxBytesInFlight = 256 * 1024;
        desc.maxPathBytes = 1024;
        desc.ioChunkBytes = 2;
        desc.directoryBufferBytes = 4096;
        desc.maxScanEntries = 32;
        desc.maxScanNameBytes = 4096;
        return desc;
    }
#if defined(_WIN32)
    bool createJunction()
    {
        const auto link = root / "link";
        std::filesystem::create_directory(link);
        const auto target = std::filesystem::canonical(base_ / "outside").native();
        const std::wstring substitute = L"\\??\\" + target;
        const auto substituteBytes = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
        const auto printBytes = static_cast<WORD>(target.size() * sizeof(wchar_t));

        struct Header {
            DWORD tag;
            WORD dataLength;
            WORD reserved;
            WORD substituteOffset;
            WORD substituteLength;
            WORD printOffset;
            WORD printLength;
        };

        const auto pathBytes = substituteBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
        std::vector<std::uint64_t> aligned(
            (sizeof(Header) + pathBytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
        auto* header = reinterpret_cast<Header*>(aligned.data());
        header->tag = IO_REPARSE_TAG_MOUNT_POINT;
        header->dataLength = static_cast<WORD>(sizeof(Header) - 8 + pathBytes);
        header->substituteLength = substituteBytes;
        header->printOffset = static_cast<WORD>(substituteBytes + sizeof(wchar_t));
        header->printLength = printBytes;
        auto* paths = reinterpret_cast<std::uint8_t*>(aligned.data()) + sizeof(Header);
        std::memcpy(paths, substitute.c_str(), substituteBytes + sizeof(wchar_t));
        std::memcpy(paths + header->printOffset, target.c_str(), printBytes + sizeof(wchar_t));
        HANDLE file = CreateFileW(link.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        DWORD returned = 0;
        const bool created = DeviceIoControl(file,
                                 FSCTL_SET_REPARSE_POINT,
                                 aligned.data(),
                                 static_cast<DWORD>(sizeof(Header) + pathBytes),
                                 nullptr,
                                 0,
                                 &returned,
                                 nullptr)
            != 0;
        if (!created)
            std::fprintf(stderr, "junction fixture error: %lu\n", GetLastError());
        CloseHandle(file);
        return created;
    }
#endif
    std::filesystem::path root;

private:
    std::filesystem::path temporary_;
    std::filesystem::path base_;
};

template <class Result> struct Probe {
    unsigned calls = 0;
    Result result;
    std::thread::id thread;

    static void complete(void* context, Result result)
    {
        auto& self = *static_cast<Probe*>(context);
        ++self.calls;
        self.thread = std::this_thread::get_id();
        self.result = std::move(result);
    }
};

template <class Predicate>
void wait(
    arp::core::runtime::CompletionQueue& queue, Predicate predicate, std::size_t dispatchBudget = 8)
{
    assert(dispatchBudget);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        queue.dispatch(dispatchBudget);
        std::this_thread::yield();
    }
    assert(predicate());
}

inline arp::core::io::FileQueryRequest query(
    arp::core::io::FileQueryOperation op, const char* path = "")
{
    arp::core::io::FileQueryRequest request;
    request.operation = op;
    request.pathScope = arp::core::io::FilePathScope::MountedResource;
    request.path = path;
    request.maxResultBytes = 8192;
    if (op == arp::core::io::FileQueryOperation::Enumerate) {
        request.pageSize = 1;
        request.maxScanEntries = 32;
        request.maxScanNameBytes = 4096;
    }
    return request;
}

} // namespace unified_io_fixture
