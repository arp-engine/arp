#include "core/io/http_file_contract.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/resource_graph_executor.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace core::io;
constexpr const char* id = "a7529343-28cc-4fa9-850b-2b4a3eb4c340";
constexpr std::size_t sourceBudget = 1024;

// Contract fixture only: the actual HTTP receiver supplies its normal reserved
// capacity, without a socket or any claim of native backend execution.
class ReservedReceiverFixture final : public ManagedFileBackend {
public:
    FileBackendCapability capability() const noexcept override
    {
        return {FileBackendKind::NullFileBackend,
            FileBackendStatus::ContractComplete,
            false,
            true,
            true,
            false,
            false,
            true,
            true};
    }

    FileReadResult validateRead(const FileReadRequest& request) const override
    {
        FileReadResult result;
        const auto valid = FileReadContract::validate(request);
        result.status = closed_ ? FileIoStatus::ServiceClosed : valid.status;
        result.diagnosticId = valid.diagnostic;
        return result;
    }

    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion callback,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        struct Pending {
            FileReadResult result;
            FileReadCompletion callback;
        };

        try {
            auto reservation = queue.reserve();
            if (!reservation.valid())
                return FileIoStatus::QueueFull;
            HttpFileBodyReceiver receiver(request);
            assert(receiver.headers({200, "1", {}, {}, {}}));
            const std::uint8_t byte = 'x';
            assert(receiver.append(&byte, 1));
            receiver.finish();
            auto pending = core::makeUnique<Pending>();
            pending->result = receiver.take();
            assert(pending->result.bytes.capacity() >= request.maxBytes);
            pending->callback = callback;
            const auto posted = queue.postReserved(std::move(reservation),
                {[](void* raw) {
                     core::UniquePtr<Pending> owned{static_cast<Pending*>(raw),
                         core::CpuAllocatedDeleter<Pending>{
                             raw, &core::destroyCpuAllocatedUnique<Pending>}};
                     owned->callback.complete(owned->callback.userData, std::move(owned->result));
                 },
                    pending.get()});
            if (!posted.accepted())
                return FileIoStatus::QueueFull;
            pending.release();
            return FileIoStatus::Accepted;
        } catch (...) {
            return FileIoStatus::BudgetExceeded;
        }
    }

    void shutdown() noexcept override { closed_ = true; }

private:
    bool closed_ = false;
};

class CapacityResource final : public resource::ResourceBase {
public:
    explicit CapacityResource(core::PoolHandle mount)
        : ResourceBase({id, "CapacityContract", {}, "Contract Complete", {}}), mount_(mount)
    {}

    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>& intents,
        core::pmr::String&) const override
    {
        for (const auto* name : {"first", "second", "third", "fourth"}) {
            resource::ResourceFileReadIntent intent;
            intent.pathScope = FilePathScope::MountedResource;
            intent.mount = mount_;
            intent.path = name;
            intent.maxBytes = sourceBudget;
            intents.push_back(std::move(intent));
        }
        return true;
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        assert(context.sources && context.sources->size() == 4);
        std::size_t capacity = 0;
        for (std::size_t i = 0; i < context.sources->size(); ++i) {
            const auto& source = *context.sources->sourceAt(i);
            assert(source.bytes.size() == 1 && source.bytes[0] == 'x'
                && source.version.kind == FileVersionKind::ContentSha256);
            capacity += source.bytes.capacity();
        }
        if (capacity > sourceBudget)
            return {false,
                resource::ResourceState::Failed,
                id,
                "CapacityContract",
                "source payload fits, retained receiver capacity exceeds source budget"};
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    core::PoolHandle mount_;
};

void cycle()
{
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, sourceBudget, 32, 512});
    const auto mount = mounts->mount("capacity", core::makeShared<ReservedReceiverFixture>());
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = desc.completionBudget = 2;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    core::runtime::AsyncRuntime runtime(std::move(desc));
    resource::ResourceManager manager;
    resource::ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = id;
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<CapacityResource>(mount);
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 4, sourceBudget, sourceBudget, 2};

    struct Result {
        bool done = false;
        resource::ResourceGraphExecutionResult value;
    } result;

    resource::ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.completionQueue());
    assert(executor.submit(std::move(request),
               {[](void* raw, resource::ResourceGraphExecutionResult value) {
                    auto& result = *static_cast<Result*>(raw);
                    result.value = std::move(value);
                    result.done = true;
                },
                   &result})
        == resource::ResourceGraphSubmitStatus::Accepted);
    for (unsigned i = 0; !result.done && i < 32; ++i)
        assert(runtime.dispatch(2).success());
    assert(result.done);
    if (!result.value.resource.success)
        std::fprintf(stderr, "%s\n", result.value.resource.diagnostics.c_str());
    assert(result.value.resource.success && result.value.telemetry.bytesRead == 4);
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
}
} // namespace

int main()
{
    cycle();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::puts(
        "PASS receiver reserve is not retained by resource graph; ordinary20 owners return to baseline");
}
