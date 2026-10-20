#include "resource/resource_module.h"

#include "core/runtime/async_runtime.h"

#include <algorithm>
#include <utility>

namespace arp::reflection::generated {
bool register_arp_resources_reflection(
    arp::scriptvm::ModuleRegistration*, const arp::reflection::object*);
}

namespace arp::resource {

// The IO completion owns this state independently of ResourceOperation. This
// keeps a late callback safe after the engine object has been invalidated.
struct ResourceOperationCallbackState {
    std::shared_ptr<ResourceOperationData> data;
};

} // namespace arp::resource

namespace arp::scriptvm {

namespace {

void completeResourceOperation(void* userData, resource::ResourceLoadResult result)
{
    core::UniquePtr<resource::ResourceOperationCallbackState> state{
        static_cast<resource::ResourceOperationCallbackState*>(userData),
        core::CpuAllocatedDeleter<resource::ResourceOperationCallbackState>{
            userData,
            &core::destroyCpuAllocatedUnique<resource::ResourceOperationCallbackState>,
        },
    };
    if (state == nullptr || state->data == nullptr)
        return;
    std::lock_guard lock(state->data->mutex);
    if (state->data->state == resource::ResourceOperationState::Destroyed)
        return;
    state->data->result = std::move(result);
    state->data->state = state->data->result.success ? resource::ResourceOperationState::Ready
                                                     : resource::ResourceOperationState::Failed;
}

void failResourceOperation(
    const std::shared_ptr<resource::ResourceOperationData>& data, const char* diagnostics)
{
    if (data == nullptr)
        return;
    std::lock_guard lock(data->mutex);
    if (data->state == resource::ResourceOperationState::Destroyed)
        return;
    data->result.success = false;
    data->result.state = resource::ResourceState::Failed;
    data->result.diagnostics = core::pmr::String(diagnostics, &core::defaultCpuMemoryResource());
    data->state = resource::ResourceOperationState::Failed;
}

arp::reflection::object makeResourceAwaitable(const resource::ResourceOperation& operation)
{
    const auto data = operation.sharedData();
    ScriptAwaitable awaitable;
    awaitable.operation = operation.nativeReflectionObject();
    awaitable.ready = [data] {
        std::lock_guard lock(data->mutex);
        return data->state != resource::ResourceOperationState::Pending;
    };
    awaitable.poll = [data](Value& value, core::pmr::String& diagnostics) {
        std::lock_guard lock(data->mutex);
        if (data->state == resource::ResourceOperationState::Pending)
            return ScriptAwaitStatus::Pending;
        if (data->state == resource::ResourceOperationState::Destroyed) {
            diagnostics = "resource operation was destroyed";
            return ScriptAwaitStatus::Failed;
        }
        if (data->state == resource::ResourceOperationState::Failed) {
            diagnostics = data->result.diagnostics;
            data->awaitConsumed = true;
            return ScriptAwaitStatus::Failed;
        }
        if (!data->awaitConsumed) {
            value = Value{data->result};
            data->awaitConsumed = true;
        }
        return ScriptAwaitStatus::Ready;
    };
    return Value{std::move(awaitable)};
}

} // namespace

} // namespace arp::scriptvm

namespace arp::resource {

ResourceOperation::ResourceOperation(ResourceManager* manager, core::runtime::AsyncRuntime* runtime)
    : owner_(manager), runtime_(runtime), data_(core::makeShared<ResourceOperationData>()),
      reflectionObject_(arp::reflection::object::native_reference(this))
{}

ResourceOperation::~ResourceOperation()
{
    if (data_ != nullptr) {
        std::lock_guard lock(data_->mutex);
        data_->state = ResourceOperationState::Destroyed;
    }
    reflectionObject_.invalidate_native_reference();
}

ResourceOperationState ResourceOperation::state() const
{
    std::lock_guard lock(data_->mutex);
    return data_->state;
}

bool ResourceOperation::isComplete() const
{
    std::lock_guard lock(data_->mutex);
    return data_->state != ResourceOperationState::Pending;
}

bool ResourceOperation::isSuccess() const
{
    std::lock_guard lock(data_->mutex);
    return data_->state == ResourceOperationState::Ready && data_->result.success;
}

ResourceLoadResult ResourceOperation::result() const
{
    std::lock_guard lock(data_->mutex);
    return data_->result;
}

core::pmr::String ResourceOperation::error() const
{
    std::lock_guard lock(data_->mutex);
    return data_->result.diagnostics;
}

ResourceBase* ResourceOperation::resource() const
{
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    {
        std::lock_guard lock(data_->mutex);
        if (data_->state != ResourceOperationState::Ready)
            return nullptr;
        resourceId = data_->result.resourceId;
    }
    return owner_ != nullptr ? owner_->find_resource(resourceId) : nullptr;
}

bool ResourceOperation::destroy()
{
    return owner_ != nullptr && owner_->destroyOperation(this);
}

bool ResourceOperation::startEnvelope(const core::pmr::String& path)
{
    if (owner_ == nullptr || runtime_ == nullptr || !runtime_->valid()) {
        arp::scriptvm::failResourceOperation(data_, "resource async runtime is unavailable");
        return false;
    }
    auto callbackState = core::makeUnique<resource::ResourceOperationCallbackState>();
    callbackState->data = data_;
    auto* raw = callbackState.get();
    resource::ResourceAsyncLoadRequest request;
    request.envelopeFile.pathScope = core::io::FilePathScope::HostFilesystem;
    request.envelopeFile.path = path;
    const auto status = owner_->loadEnvelopeFileAsync(runtime_->fileService(),
        request,
        resource::ResourceAsyncLoadCompletion{&arp::scriptvm::completeResourceOperation, raw});
    if (status != core::io::FileIoStatus::Accepted) {
        arp::scriptvm::failResourceOperation(data_, "resource async load could not be scheduled");
        return false;
    }
    callbackState.release();
    return true;
}

arp::reflection::object ResourceOperation::makeAwaitableNative() const
{
    return arp::scriptvm::makeResourceAwaitable(*this);
}

ResourceOperation* ResourceManager::createOperation()
{
    auto value = core::makeUnique<ResourceOperation>(this, scriptAsyncRuntime_);
    auto* result = value.get();
    operations_.push_back(std::move(value));
    return result;
}

bool ResourceManager::destroyOperation(ResourceOperation* operation)
{
    const auto it =
        std::find_if(operations_.begin(), operations_.end(), [operation](const auto& candidate) {
            return candidate.get() == operation;
        });
    if (it == operations_.end())
        return false;
    operations_.erase(it);
    return true;
}

arp::reflection::object ResourceManager::load_async(const core::pmr::String& path)
{
    auto* operation = createOperation();
    if (operation == nullptr)
        return arp::reflection::object::undefined();
    operation->startEnvelope(path);
    return operation->makeAwaitableNative();
}

} // namespace arp::resource

namespace arp::scriptvm {

int registerResourceScriptPlugin(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr || host->services == nullptr) {
        return 0;
    }
    auto* manager = host->services->find<resource::ResourceManager>(
        kResourceManagerService, kResourceManagerServiceVersion);
    auto* asyncRuntime = host->services->find<core::runtime::AsyncRuntime>(
        kAsyncRuntimeService, kAsyncRuntimeServiceVersion);
    if (manager == nullptr || asyncRuntime == nullptr) {
        return 0;
    }
    manager->bindScriptAsyncRuntime(asyncRuntime);
    return arp::reflection::generated::register_arp_resources_reflection(
               host->registrar->reflectionModule(), &manager->nativeReflectionObject())
        ? 1
        : 0;
}

} // namespace arp::scriptvm
