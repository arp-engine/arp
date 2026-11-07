#include "rhi/backends/metal/metal_pipeline_archive.h"

#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_original_device_native_loss.h"
#include "rhi/backends/metal/metal_pipeline_archive_identity.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

namespace arp::rhi_metal {
namespace {
// Same lifetime/identity as the backend's shared native device. The mutex is
// used only for cold native pipeline construction and inspection, never draws.
class Archive final {
public:
    Archive()
        : device_(metalRhiNativeDeviceIdentity()), loss_(sharedMetalRhiOriginalResultLossSource())
    {}

    ~Archive()
    {
        @autoreleasepool {
            try {
                save();
            } catch (...) {
                report.save = "Exception";
            }
            [archive_ release];
        }
        std::fprintf(stderr,
            "[metal-pipeline-cache] status=%s input=%s save=%s uses=%llu loaded=%llu saved=%llu\n",
            report.status,
            report.input,
            report.save,
            static_cast<unsigned long long>(report.uses),
            static_cast<unsigned long long>(report.loadedBytes),
            static_cast<unsigned long long>(report.savedBytes));
    }

    void initialize() API_AVAILABLE(macos(11.0), ios(14.0))
    {
        if (attempted_)
            return;
        // Shared accelerator storage is not owned by an individual original
        // pipeline's transient creation quota. Admission remains file-bounded.
        core::CpuMemoryQuotaScope quota(nullptr);
        attempted_ = true;
        const auto device = reinterpret_cast<id<MTLDevice>>(device_.get());
        if (!device
            || ![device respondsToSelector:@selector(newBinaryArchiveWithDescriptor:error:)]) {
            report.status = "EquivalentUncached:ArchiveApiUnavailable";
            return;
        }
        const auto version = NSProcessInfo.processInfo.operatingSystemVersion;
        identity_ = {device.registryID,
            std::uint64_t(version.majorVersion),
            std::uint64_t(version.minorVersion),
            std::uint64_t(version.patchVersion)};
        if (const auto* path = std::getenv("ARP_METAL_PIPELINE_CACHE_PATH"); path && *path)
            path_ = path;
        else {
            const auto directories =
                NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
            if (directories.count)
                path_ = [[directories objectAtIndex:0]
                    stringByAppendingPathComponent:@"arp/pipeline-cache/metal.bin"]
                            .UTF8String;
        }
        MTLBinaryArchiveDescriptor* desc = [[[MTLBinaryArchiveDescriptor alloc] init] autorelease];
        core::BinaryFileReadResult input;
        if (!path_.empty())
            input =
                core::readBinaryFile(path_.c_str(), MetalPipelineArchiveIdentity::maximumFileBytes);
        std::error_code inputError;
        const bool inputExists = !path_.empty()
            && std::filesystem::exists(std::filesystem::path(path_.c_str()), inputError);
        report.input = input.success    ? "IncompatibleRejected"
            : inputExists || inputError ? "ReadFailedOrOversized"
                                        : "EmptyOrMissing";
        TemporaryFile staged;
        if (input.success
            && MetalPipelineArchiveIdentity::matches(
                {input.bytes.data(), input.bytes.size()}, identity_)) {
            core::pmr::Vector<std::uint8_t> bytes(&core::defaultCpuMemoryResource());
            bytes.assign(input.bytes.begin() + MetalPipelineArchiveIdentity::envelopeBytes,
                input.bytes.end());
            if (staged.create(path_) && replace(staged.path, std::move(bytes))) {
                desc.url =
                    [NSURL fileURLWithPath:[NSString stringWithUTF8String:staged.path.c_str()]];
                report.loadedBytes =
                    input.bytes.size() - MetalPipelineArchiveIdentity::envelopeBytes;
            } else
                report.input = "StageFailed";
        }
        NSError* error = nil;
        archive_ = [device newBinaryArchiveWithDescriptor:desc error:&error];
        if (!archive_ && desc.url) {
            // Rejection of optional archive data grants no PSO success.
            desc.url = nil;
            report.loadedBytes = 0;
            report.input = "NativeArchiveRejected";
            archive_ = [device newBinaryArchiveWithDescriptor:desc error:&error];
        } else if (archive_ && desc.url)
            report.input = "CompatibleLoaded";
        report.status =
            archive_ ? "NativeBinaryArchive" : "EquivalentUncached:ArchiveCreationFailed";
    }

    void attach(MTLRenderPipelineDescriptor* desc) API_AVAILABLE(macos(11.0), ios(14.0))
    {
        initialize();
        if (archive_) {
            NSError* error = nil;
            if ([archive_ addRenderPipelineFunctionsWithDescriptor:desc error:&error]) {
                desc.binaryArchives = @[ archive_ ];
                report.status = "NativeBinaryArchive";
                ++report.uses;
            } else
                report.status = "EquivalentUncached:ArchivePopulationFailed";
        }
    }

    void attach(MTLComputePipelineDescriptor* desc) API_AVAILABLE(macos(11.0), ios(14.0))
    {
        initialize();
        if (archive_) {
            NSError* error = nil;
            if ([archive_ addComputePipelineFunctionsWithDescriptor:desc error:&error]) {
                desc.binaryArchives = @[ archive_ ];
                report.status = "NativeBinaryArchive";
                ++report.uses;
            } else
                report.status = "EquivalentUncached:ArchivePopulationFailed";
        }
    }

    std::mutex mutex;
    MetalRhiPipelineArchiveReport report;

private:
    struct TemporaryFile {
        core::pmr::String path{&core::defaultCpuMemoryResource()};

        bool create(const core::pmr::String& destination)
        {
            std::error_code error;
            auto parent = std::filesystem::path(destination.c_str()).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent, error);
            if (error)
                return false;
            path = destination;
            path += ".";
            path += NSUUID.UUID.UUIDString.UTF8String;
            path += ".tmp";
            return true;
        }

        ~TemporaryFile()
        {
            if (!path.empty()) {
                std::error_code error;
                std::filesystem::remove(std::filesystem::path(path.c_str()), error);
            }
        }
    };

    static bool replace(const core::pmr::String& path, core::pmr::Vector<std::uint8_t> bytes)
    {
        core::io::AtomicFileService files(1, MetalPipelineArchiveIdentity::maximumFileBytes);
        const auto request = files.replace(path, std::move(bytes));
        if (!request)
            return false;
        core::io::AtomicFileResult result;
        while (!files.take(request, result)) {
            files.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return result.success;
    }

    void save()
    {
        if (!archive_)
            return;
        core::CpuMemoryQuotaScope quota(nullptr);
        if (loss_ && loss_->lost()) {
            report.save = "DeviceLost";
            return;
        }
        report.save = "PathDisabled";
        if (path_.empty())
            return;
        if (@available(macOS 11.0, iOS 14.0, *)) {
            TemporaryFile staged;
            NSError* error = nil;
            report.save = "SerializeFailed";
            if (!staged.create(path_)
                || ![archive_
                    serializeToURL:[NSURL
                                       fileURLWithPath:[NSString stringWithUTF8String:staged.path
                                                                                          .c_str()]]
                             error:&error])
                return;
            auto bytes = core::readBinaryFile(staged.path.c_str(),
                MetalPipelineArchiveIdentity::maximumFileBytes
                    - MetalPipelineArchiveIdentity::envelopeBytes);
            report.save = "DataInvalidOrOversized";
            if (!bytes.success || bytes.bytes.empty())
                return;
            const auto size = bytes.bytes.size();
            bytes.bytes.insert(bytes.bytes.begin(), MetalPipelineArchiveIdentity::envelopeBytes, 0);
            MetalPipelineArchiveIdentity::seal(bytes.bytes.data(), bytes.bytes.size(), identity_);
            report.save = "AtomicReplaceFailed";
            if (replace(path_, std::move(bytes.bytes))) {
                report.savedBytes = size;
                report.save = "Saved";
            }
        }
    }

    core::SharedPtr<void> device_;
    core::SharedPtr<MetalOriginalDeviceNativeResultLossSource> loss_;
    bool attempted_ = false;
    id<MTLBinaryArchive> archive_ = nil;
    MetalPipelineArchiveIdentity::Device identity_{};
    core::pmr::String path_{&core::defaultCpuMemoryResource()};
};

Archive& archive()
{
    static Archive value;
    return value;
}
} // namespace

MetalRhiPipelineArchiveReport describeMetalRhiPipelineArchive()
{
    auto& state = archive();
    std::lock_guard lock(state.mutex);
    return state.report;
}

id<MTLRenderPipelineState> newMetalRhiCachedRenderPipeline(MTLRenderPipelineDescriptor* desc,
    MTLPipelineOption options,
    MTLRenderPipelineReflection** reflection,
    NSError** error)
{
    auto& state = archive();
    std::lock_guard lock(state.mutex);
    if (@available(macOS 11.0, iOS 14.0, *)) {
        try {
            state.attach(desc);
        } catch (...) {
            desc.binaryArchives = nil;
            state.report.status = "EquivalentUncached:ArchiveException";
        }
    } else
        state.report.status = "EquivalentUncached:ArchiveApiUnavailable";
    // Keep the original reflection request and failure; no native retry.
    return [reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle())
        newRenderPipelineStateWithDescriptor:desc
                                     options:options
                                  reflection:reflection
                                       error:error];
}

id<MTLComputePipelineState> newMetalRhiCachedComputePipeline(id<MTLFunction> function,
    MTLPipelineOption options,
    MTLComputePipelineReflection** reflection,
    NSError** error)
{
    auto& state = archive();
    std::lock_guard lock(state.mutex);
    const auto device = reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
    if (@available(macOS 11.0, iOS 14.0, *)) {
        auto desc = [[[MTLComputePipelineDescriptor alloc] init] autorelease];
        desc.computeFunction = function;
        desc.threadGroupSizeIsMultipleOfThreadExecutionWidth = NO;
        desc.supportIndirectCommandBuffers = NO;
        try {
            state.attach(desc);
        } catch (...) {
            desc.binaryArchives = nil;
            state.report.status = "EquivalentUncached:ArchiveException";
        }
        auto result = [device newComputePipelineStateWithDescriptor:desc
                                                            options:options
                                                         reflection:reflection
                                                              error:error];
        return result;
    }
    state.report.status = "EquivalentUncached:ArchiveApiUnavailable";
    return [device newComputePipelineStateWithFunction:function
                                               options:options
                                            reflection:reflection
                                                 error:error];
}
} // namespace arp::rhi_metal
