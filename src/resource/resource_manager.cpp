#include "resource/resource_manager.h"

#include "core/hash/stable_hash.h"
#include "reflection/serialization.h"
#include "resource/resource_contract.h"
#include "resource/resource_graph_executor.h"
#include "resource/resource_graph_plan.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <fkYAML/Node.hpp>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>

namespace arp::resource {

ResourceBase::~ResourceBase()
{
    reflectionObject_.invalidate_native_reference();
}

ResourceManager::ResourceManager()
    : reflectionObject_(arp::reflection::object::native_reference(this))
{}

ResourceManager::~ResourceManager()
{
    operations_.clear();
    reflectionObject_.invalidate_native_reference();
}

namespace {

using YamlNode = fkyaml::node;

constexpr NameView kResourceFormat = "ArpRuntimeResource";
constexpr std::uint32_t kResourceVersion = 1;
constexpr std::uint32_t kFnvOffset = 2166136261u;
constexpr std::uint32_t kFnvPrime = 16777619u;

struct ResourceRecordReloadSnapshot {
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String displayName{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
    ResourceState state = ResourceState::Declared;
    bool metadataAdmitted = false;
    ResourceGeneration generation = 0u;
    std::uint32_t contentHash = 0u;
};

bool isHex(const char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

const YamlNode* findMappingValue(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

core::pmr::String makeString(NameView text)
{
    return core::pmr::String(text.data(), text.size(), &core::defaultCpuMemoryResource());
}

core::pmr::String makeDiagnostic(const char* text)
{
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

core::pmr::String makeDiagnostic(const char* prefix, NameView value)
{
    core::pmr::String result(prefix, &core::defaultCpuMemoryResource());
    result.append(value.data(), value.size());
    return result;
}

core::pmr::String nodeString(const YamlNode& node)
{
    const auto& value = node.get_value_ref<const YamlNode::string_type&>();
    return makeString(NameView(value.data(), value.size()));
}

std::uint32_t fnv1a(NameView text)
{
    std::uint32_t hash = kFnvOffset;
    for (const char ch : text) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= kFnvPrime;
    }
    return hash;
}

ResourceHashResult hashPayloadNode(const YamlNode& payload)
{
    ResourceHashResult result;
    try {
        const auto canonical = YamlNode::serialize(payload);
        result.success = true;
        result.canonicalPayloadYaml = makeString(NameView(canonical.data(), canonical.size()));
        result.hash = fnv1a(result.canonicalPayloadYaml);
    } catch (const std::exception& err) {
        result.diagnostics = makeDiagnostic("payload YAML serialization failed: ");
        result.diagnostics += err.what();
    }
    return result;
}

bool requireString(
    const YamlNode& root, const char* key, core::pmr::String& out, core::pmr::String& diagnostics)
{
    const YamlNode* value = findMappingValue(root, key);
    if (value == nullptr || !value->is_string()) {
        diagnostics = makeDiagnostic("missing required ", key);
        return false;
    }
    out = nodeString(*value);
    if (out.empty()) {
        diagnostics = makeDiagnostic("empty required ", key);
        return false;
    }
    return true;
}

bool requireVersion(const YamlNode& root, std::uint32_t& out, core::pmr::String& diagnostics)
{
    const YamlNode* value = findMappingValue(root, "version");
    if (value == nullptr || !value->is_integer()) {
        diagnostics = makeDiagnostic("missing required version");
        return false;
    }
    const auto parsed = value->get_value<YamlNode::integer_type>();
    if (parsed < 0
        || parsed
            > static_cast<YamlNode::integer_type>(std::numeric_limits<std::uint32_t>::max())) {
        diagnostics = makeDiagnostic("version out of range");
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool requireContentHash(const YamlNode& root, std::uint32_t& out, core::pmr::String& diagnostics)
{
    const YamlNode* value = findMappingValue(root, "contentHash");
    if (value == nullptr || !value->is_integer()) {
        diagnostics = makeDiagnostic("missing required contentHash");
        return false;
    }
    const auto parsed = value->get_value<YamlNode::integer_type>();
    if (parsed < 0
        || parsed
            > static_cast<YamlNode::integer_type>(std::numeric_limits<std::uint32_t>::max())) {
        diagnostics = makeDiagnostic("contentHash out of range");
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool readDependencies(const YamlNode& root,
    core::pmr::Vector<ResourceDependency>& out,
    core::pmr::String& diagnostics)
{
    const YamlNode* dependencies = findMappingValue(root, "dependencies");
    if (dependencies == nullptr || dependencies->is_null()) {
        return true;
    }
    if (!dependencies->is_sequence()) {
        diagnostics = makeDiagnostic("dependencies must be a sequence");
        return false;
    }
    const auto& sequence = dependencies->get_value_ref<const YamlNode::sequence_type&>();
    for (const auto& item : sequence) {
        if (!item.is_mapping()) {
            diagnostics = makeDiagnostic("dependency entry must be a mapping");
            return false;
        }
        const YamlNode* id = findMappingValue(item, "id");
        const YamlNode* usage = findMappingValue(item, "usage");
        if (id == nullptr || !id->is_string()) {
            diagnostics = makeDiagnostic("dependency entry missing id");
            return false;
        }
        if (usage == nullptr || !usage->is_string()) {
            diagnostics = makeDiagnostic("dependency entry missing usage");
            return false;
        }
        ResourceDependency dependency;
        dependency.id = nodeString(*id);
        dependency.usage = nodeString(*usage);
        if (dependency.id.empty() || dependency.usage.empty()) {
            diagnostics = makeDiagnostic("dependency id and usage must be non-empty");
            return false;
        }
        if (!isResourceUuid(dependency.id)) {
            diagnostics = makeDiagnostic("dependency id must be UUID: ", dependency.id);
            return false;
        }
        out.push_back(std::move(dependency));
    }
    return true;
}

bool parseEnvelope(NameView envelopeYaml, ResourceEnvelope& out, core::pmr::String& diagnostics)
{
    YamlNode root;
    try {
        root =
            YamlNode::deserialize(envelopeYaml.data(), envelopeYaml.data() + envelopeYaml.size());
    } catch (const std::exception& err) {
        diagnostics = makeDiagnostic("resource envelope YAML parse failed: ");
        diagnostics += err.what();
        return false;
    }
    if (!root.is_mapping()) {
        diagnostics = makeDiagnostic("resource envelope root must be a mapping");
        return false;
    }
    if (!requireString(root, "format", out.format, diagnostics)) {
        return false;
    }
    if (out.format != kResourceFormat) {
        diagnostics = makeDiagnostic("unsupported resource format: ", out.format);
        return false;
    }
    if (!requireVersion(root, out.version, diagnostics)) {
        return false;
    }
    if (out.version != kResourceVersion) {
        diagnostics = makeDiagnostic("unsupported resource version");
        return false;
    }
    if (!requireString(root, "resourceId", out.resourceId, diagnostics)) {
        return false;
    }
    if (!isResourceUuid(out.resourceId)) {
        diagnostics = makeDiagnostic("resource id must be UUID: ", out.resourceId);
        return false;
    }
    if (!requireString(root, "resourceType", out.resourceType, diagnostics)) {
        return false;
    }
    const YamlNode* alias = findMappingValue(root, "alias");
    if (alias != nullptr && alias->is_string()) {
        out.alias = nodeString(*alias);
    }
    const YamlNode* displayName = findMappingValue(root, "displayName");
    if (displayName != nullptr && displayName->is_string()) {
        out.displayName = nodeString(*displayName);
    }
    const YamlNode* status = findMappingValue(root, "status");
    if (status != nullptr && status->is_string()) {
        out.status = nodeString(*status);
    }
    if (!requireContentHash(root, out.contentHash, diagnostics)) {
        return false;
    }
    if (!readDependencies(root, out.dependencies, diagnostics)) {
        return false;
    }
    const YamlNode* payload = findMappingValue(root, "payload");
    if (payload == nullptr || !payload->is_mapping()) {
        diagnostics = makeDiagnostic("missing required payload");
        return false;
    }
    const YamlNode* payloadType = findMappingValue(*payload, "type");
    if (payloadType == nullptr || !payloadType->is_string()) {
        diagnostics = makeDiagnostic("payload missing required type");
        return false;
    }
    const auto reflectedType = nodeString(*payloadType);
    if (reflectedType != out.resourceType) {
        diagnostics = makeDiagnostic("payload type does not match resourceType");
        return false;
    }
    auto payloadHash = hashPayloadNode(*payload);
    if (!payloadHash.success) {
        diagnostics = payloadHash.diagnostics;
        return false;
    }
    out.payloadYaml = payloadHash.canonicalPayloadYaml;
    if (payloadHash.hash != out.contentHash) {
        diagnostics = makeDiagnostic("contentHash mismatch");
        return false;
    }
    return true;
}

ResourceLoadResult resultFor(bool success,
    ResourceState state,
    NameView resourceId,
    NameView resourceType,
    const core::pmr::String& diagnostics)
{
    ResourceLoadResult result;
    result.success = success;
    result.state = state;
    result.resourceId = makeString(resourceId);
    result.resourceType = makeString(resourceType);
    result.diagnostics = diagnostics;
    return result;
}

const char* fileStatusName(core::io::FileIoStatus status)
{
    switch (status) {
    case core::io::FileIoStatus::Accepted:
        return "Accepted";
    case core::io::FileIoStatus::Success:
        return "Success";
    case core::io::FileIoStatus::MissingFile:
        return "MissingFile";
    case core::io::FileIoStatus::PermissionDenied:
        return "PermissionDenied";
    case core::io::FileIoStatus::InvalidPathScope:
        return "InvalidPathScope";
    case core::io::FileIoStatus::InvalidRange:
        return "InvalidRange";
    case core::io::FileIoStatus::PartialReadDenied:
        return "PartialReadDenied";
    case core::io::FileIoStatus::BackendUnavailable:
        return "BackendUnavailable";
    case core::io::FileIoStatus::Cancelled:
        return "Cancelled";
    case core::io::FileIoStatus::QueueFull:
        return "QueueFull";
    case core::io::FileIoStatus::ServiceClosed:
        return "ServiceClosed";
    case core::io::FileIoStatus::InvalidRequest:
        return "InvalidRequest";
    case core::io::FileIoStatus::BudgetExceeded:
        return "BudgetExceeded";
    case core::io::FileIoStatus::Unsupported:
        return "Unsupported";
    case core::io::FileIoStatus::VersionMismatch:
        return "VersionMismatch";
    case core::io::FileIoStatus::IntegrityMismatch:
        return "IntegrityMismatch";
    case core::io::FileIoStatus::Timeout:
        return "Timeout";
    case core::io::FileIoStatus::StaleLocation:
        return "StaleLocation";
    case core::io::FileIoStatus::OutcomeUnknown:
        return "OutcomeUnknown";
    }
    return "Unknown";
}

struct AsyncEnvelopeLoadState {
    ResourceManager* manager = nullptr;
    ResourceLoadPolicy policy{};
    ResourceAsyncLoadCompletion completion;
};

void completeAsyncEnvelopeLoad(void* userData, core::io::FileTextReadResult fileResult)
{
    core::UniquePtr<AsyncEnvelopeLoadState> state{
        static_cast<AsyncEnvelopeLoadState*>(userData),
        core::CpuAllocatedDeleter<AsyncEnvelopeLoadState>{
            userData,
            &core::destroyCpuAllocatedUnique<AsyncEnvelopeLoadState>,
        },
    };

    ResourceLoadResult result;
    if (fileResult.status() != core::io::FileIoStatus::Success) {
        result.success = false;
        result.state = ResourceState::Failed;
        result.diagnostics = makeDiagnostic("resource envelope file read failed: ");
        result.diagnostics += fileStatusName(fileResult.status());
        if (!fileResult.source.diagnostics.empty()) {
            result.diagnostics += ": ";
            result.diagnostics += fileResult.source.diagnostics;
        } else if (fileResult.diagnosticId()) {
            result.diagnostics += ": ";
            result.diagnostics += fileResult.diagnosticId();
        }
    } else {
        result = state->manager->loadFromEnvelope(fileResult.decoded.text, state->policy);
    }
    state->completion.complete(state->completion.userData, result);
}

// Explicit contract0 compatibility. Existing host callers retain their IO
// admission behavior; the already fetched byte count bounds text conversion.
// All contract1 callers use the bounded FileService text entry below.
void completeLegacyEnvelopeRead(void* context, core::io::FileReadResult source)
{
    core::io::FileTextReadResult result;
    result.source = std::move(source);
    if (result.source.status == core::io::FileIoStatus::Success) {
        core::TextDecodeDesc desc;
        desc.maxOutputBytes = result.source.bytes.size();
        desc.bom = core::TextBomPolicy::Strip;
        result.decoded = core::TextDecoder::decode(
            {reinterpret_cast<const char*>(result.source.bytes.data()), result.source.bytes.size()},
            desc);
    }
    completeAsyncEnvelopeLoad(context, std::move(result));
}

ResourceRecordReloadSnapshot captureReloadSnapshot(const ResourceRecord& record)
{
    ResourceRecordReloadSnapshot snapshot;
    snapshot.alias = record.alias;
    snapshot.displayName = record.displayName;
    snapshot.status = record.status;
    snapshot.diagnostics = record.diagnostics;
    snapshot.dependencies = record.dependencies;
    snapshot.state = record.state;
    snapshot.metadataAdmitted = record.metadataAdmitted;
    snapshot.generation = record.generation;
    snapshot.contentHash = record.contentHash;
    return snapshot;
}

// Both records are registry-owned and use the same core memory resource.
// Preserve the public record address without allocating during publication.
void swapRecordContents(ResourceRecord& left, ResourceRecord& right) noexcept
{
    for (const auto member : {&ResourceRecord::id,
             &ResourceRecord::type,
             &ResourceRecord::alias,
             &ResourceRecord::displayName,
             &ResourceRecord::status,
             &ResourceRecord::diagnostics}) {
        assert((left.*member).get_allocator() == (right.*member).get_allocator());
        (left.*member).swap(right.*member);
    }
    assert(left.dependencies.get_allocator() == right.dependencies.get_allocator());
    assert(left.ownedObjects.get_allocator() == right.ownedObjects.get_allocator());
    left.dependencies.swap(right.dependencies);
    left.ownedObjects.swap(right.ownedObjects);
    left.ownedResource.swap(right.ownedResource);
    static_assert(std::is_nothrow_swappable_v<reflection::object>);
    std::swap(left.value, right.value);
    std::swap(left.state, right.state);
    std::swap(left.metadataAdmitted, right.metadataAdmitted);
    std::swap(left.generation, right.generation);
    std::swap(left.contentHash, right.contentHash);
    std::swap(left.reloadInProgress, right.reloadInProgress);
}

void restoreReloadSnapshot(ResourceRecord& record, const ResourceRecordReloadSnapshot& snapshot)
{
    record.alias = snapshot.alias;
    record.displayName = snapshot.displayName;
    record.status = snapshot.status;
    record.diagnostics = snapshot.diagnostics;
    record.dependencies = snapshot.dependencies;
    record.state = snapshot.state;
    record.metadataAdmitted = snapshot.metadataAdmitted;
    record.generation = snapshot.generation;
    record.contentHash = snapshot.contentHash;
}

} // namespace

ResourceHashResult computeResourcePayloadHash(NameView payloadYaml)
{
    YamlNode payload;
    ResourceHashResult result;
    try {
        payload =
            YamlNode::deserialize(payloadYaml.data(), payloadYaml.data() + payloadYaml.size());
    } catch (const std::exception& err) {
        result.diagnostics = makeDiagnostic("payload YAML parse failed: ");
        result.diagnostics += err.what();
        return result;
    }
    if (!payload.is_mapping()) {
        result.diagnostics = makeDiagnostic("payload YAML root must be a mapping");
        return result;
    }
    return hashPayloadNode(payload);
}

bool isResourceUuid(NameView value)
{
    if (value.size() != 36u) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool hyphenPosition = index == 8u || index == 13u || index == 18u || index == 23u;
        if (hyphenPosition) {
            if (value[index] != '-') {
                return false;
            }
        } else if (!isHex(value[index])) {
            return false;
        }
    }
    return true;
}

bool ResourceSourceSet::add(ResourceSourceBuffer source)
{
    if (source.path.empty() || find(source.path) != nullptr
        || source.bytes.size() > SIZE_MAX - totalBytes_) {
        return false;
    }
    const auto bytes = source.bytes.size();
    sources_.push_back(std::move(source));
    totalBytes_ += bytes;
    return true;
}

const ResourceSourceBuffer* ResourceSourceSet::find(NameView path) const
{
    for (const auto& source : sources_) {
        if (source.path == path) {
            return &source;
        }
    }
    return nullptr;
}

void ResourceSourceSet::clear()
{
    sources_.clear();
    totalBytes_ = 0u;
}

std::size_t ResourceSourceSet::totalBytes() const noexcept
{
    return totalBytes_;
}

ResourceLoadResult ResourceBase::load(ResourceManager& manager, const ResourceLoadContext& context)
{
    return manager.admitResourceMetadata(*this, context.policy);
}

bool ResourceBase::collectFileReadIntents(
    ResourceManager&, core::pmr::Vector<ResourceFileReadIntent>&, core::pmr::String&) const
{
    return true;
}

bool ResourceBase::planFileReadIntents(ResourceManager& manager,
    const ResourceLoadContext& context,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostics) const
{
    if (context.sources && context.sources->size() != 0)
        return true;
    return collectFileReadIntents(manager, intents, diagnostics);
}

ResourceGraphStage ResourceBase::processingStage() const noexcept
{
    return ResourceGraphStage::TypedProcessing;
}

std::size_t ResourceBase::processedByteCount() const noexcept
{
    return 0u;
}

ResourceLoadResult ResourceManager::declareResource(NameView resourceId, NameView resourceType)
{
    if (resourceId.empty()) {
        return resultFor(false,
            ResourceState::Failed,
            resourceId,
            resourceType,
            makeDiagnostic("empty required resourceId"));
    }
    if (!isResourceUuid(resourceId)) {
        return resultFor(false,
            ResourceState::Failed,
            resourceId,
            resourceType,
            makeDiagnostic("resource id must be UUID: ", resourceId));
    }
    if (resourceType.empty()) {
        return resultFor(false,
            ResourceState::Failed,
            resourceId,
            resourceType,
            makeDiagnostic("empty required resourceType"));
    }

    auto key = makeString(resourceId);
    auto found = records_.find(key);
    if (found != records_.end() && found->second->state != ResourceState::Unloaded) {
        return resultFor(false,
            found->second->state,
            resourceId,
            resourceType,
            makeDiagnostic("duplicate resource id: ", resourceId));
    }

    auto record = core::makeUnique<ResourceRecord>();
    record->id = makeString(resourceId);
    record->type = makeString(resourceType);
    record->state = ResourceState::Declared;
    record->metadataAdmitted = false;
    ResourceRecord* recordPtr = record.get();
    records_[record->id] = std::move(record);
    return resultFor(
        true, recordPtr->state, recordPtr->id, recordPtr->type, recordPtr->diagnostics);
}

ResourceLoadResult ResourceManager::admitResourceMetadata(
    const ResourceBase& resource, ResourceLoadPolicy policy)
{
    const ResourceReference& reference = resource.reference();
    if (reference.id.empty()) {
        return resultFor(false,
            ResourceState::Failed,
            reference.id,
            reference.type,
            makeDiagnostic("empty required resourceId"));
    }
    if (!isResourceUuid(reference.id)) {
        return resultFor(false,
            ResourceState::Failed,
            reference.id,
            reference.type,
            makeDiagnostic("resource id must be UUID: ", reference.id));
    }
    if (reference.type.empty()) {
        return resultFor(false,
            ResourceState::Failed,
            reference.id,
            reference.type,
            makeDiagnostic("empty required resourceType"));
    }
    const auto status = parseResourceStatus({reference.status.data(), reference.status.size()});
    if (!status.success) {
        return resultFor(
            false, ResourceState::Failed, reference.id, reference.type, status.diagnostics);
    }

    for (const auto& dependency : resource.dependencies()) {
        if (dependency.id.empty() || dependency.usage.empty()) {
            return resultFor(false,
                ResourceState::Failed,
                reference.id,
                reference.type,
                makeDiagnostic("dependency id and usage must be non-empty"));
        }
        if (!isResourceUuid(dependency.id)) {
            return resultFor(false,
                ResourceState::Failed,
                reference.id,
                reference.type,
                makeDiagnostic("dependency id must be UUID: ", dependency.id));
        }
    }

    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!reference.alias.empty()) {
        auto aliasFound = aliases_.find(reference.alias);
        if (aliasFound != aliases_.end() && aliasFound->second != reference.id) {
            diagnostics = makeDiagnostic("duplicate alias: ", reference.alias);
            return resultFor(
                false, ResourceState::Failed, reference.id, reference.type, diagnostics);
        }
    }

    auto existing = records_.find(reference.id);
    if (existing != records_.end()) {
        ResourceRecord& record = *existing->second;
        if (record.metadataAdmitted || record.state == ResourceState::Ready
            || record.state == ResourceState::Loading) {
            diagnostics = makeDiagnostic("duplicate resource id: ", reference.id);
            record.diagnostics = diagnostics;
            record.state = ResourceState::Failed;
            return resultFor(false, record.state, record.id, record.type, diagnostics);
        }
        if (!record.type.empty() && record.type != reference.type) {
            diagnostics = makeDiagnostic("declared resource type does not match ResourceBase");
            record.diagnostics = diagnostics;
            record.state = ResourceState::Failed;
            return resultFor(false, record.state, record.id, record.type, diagnostics);
        }
    }

    if (policy.dependencyPolicy == ResourceDependencyPolicy::RequireDeclared) {
        for (const auto& dependency : resource.dependencies()) {
            const ResourceRecord* dependencyRecord = findRecord(dependency.id);
            if (dependencyRecord == nullptr || dependencyRecord->state == ResourceState::Unloaded) {
                diagnostics = makeDiagnostic("unresolved dependency: ", dependency.id);
                auto record = core::makeUnique<ResourceRecord>();
                record->id = reference.id;
                record->type = reference.type;
                record->alias = reference.alias;
                record->displayName = reference.displayName;
                record->status = reference.status;
                record->dependencies = resource.dependencies();
                record->state = ResourceState::Failed;
                record->metadataAdmitted = true;
                record->diagnostics = diagnostics;
                records_[record->id] = std::move(record);
                return resultFor(
                    false, ResourceState::Failed, reference.id, reference.type, diagnostics);
            }
        }
    }

    ResourceRecord* recordPtr = nullptr;
    if (existing != records_.end()) {
        recordPtr = existing->second.get();
    } else {
        auto record = core::makeUnique<ResourceRecord>();
        recordPtr = record.get();
        records_[reference.id] = std::move(record);
    }

    recordPtr->id = reference.id;
    recordPtr->type = reference.type;
    recordPtr->alias = reference.alias;
    recordPtr->displayName = reference.displayName;
    recordPtr->status = reference.status;
    recordPtr->dependencies = resource.dependencies();
    recordPtr->state = ResourceState::Declared;
    recordPtr->metadataAdmitted = true;
    recordPtr->diagnostics.clear();
    if (!recordPtr->alias.empty()) {
        aliases_[recordPtr->alias] = recordPtr->id;
    }
    return resultFor(
        true, recordPtr->state, recordPtr->id, recordPtr->type, recordPtr->diagnostics);
}

ResourceLoadResult ResourceManager::loadResource(
    core::UniquePtr<ResourceBase>&& resource, ResourceLoadContext context)
{
    core::UniquePtr<ResourceBase> ownedResource = std::move(resource);
    applyBudgetConfig(context);
    if (!ownedResource || !ownedResource->reference().valid()) {
        return applyDiagnosticsBudget(resultFor(false,
            ResourceState::Failed,
            {},
            {},
            makeDiagnostic("invalid ResourceBase load request")));
    }

    const core::pmr::String resourceId = ownedResource->reference().id;
    const core::pmr::String resourceType = ownedResource->reference().type;
    ResourceLoadResult loaded = ownedResource->load(*this, context);
    if (!loaded.success) {
        return applyDiagnosticsBudget(std::move(loaded));
    }

    ResourceRecord* record = findRecord(resourceId);
    if (record == nullptr) {
        core::pmr::String diagnostics =
            makeDiagnostic("ResourceBase load did not admit resource metadata: ");
        diagnostics += resourceId;
        diagnostics += " type ";
        diagnostics += resourceType;
        return applyDiagnosticsBudget(
            resultFor(false, ResourceState::Failed, resourceId, resourceType, diagnostics));
    }
    if (record->type != resourceType) {
        record->state = ResourceState::Failed;
        record->diagnostics = makeDiagnostic("ResourceBase loaded resource type mismatch");
        return applyDiagnosticsBudget(
            resultFor(false, record->state, record->id, record->type, record->diagnostics));
    }

    record->ownedResource = std::move(ownedResource);
    record->value = reflection::object::undefined();
    record->ownedObjects.clear();
    record->state = ResourceState::Ready;
    // Re-admission after unload must never resurrect handles to a destroyed
    // payload. The registry record survives unload, so its epoch is monotonic.
    ++record->generation;
    record->contentHash = computeContentHash(record->ownedResource.get());
    record->diagnostics.clear();
    return applyDiagnosticsBudget(
        resultFor(true, record->state, record->id, record->type, record->diagnostics));
}

ResourceBatchLoadResult ResourceManager::loadResourcesAtomically(
    core::pmr::Vector<core::UniquePtr<ResourceBase>> resources, ResourceLoadContext context)
{
    struct Snapshot {
        core::pmr::String id{&core::defaultCpuMemoryResource()};
        bool hadRecord = false;
        std::optional<ResourceRecordMap::node_type> record;
    };

    ResourceBatchLoadResult result;
    core::pmr::Vector<core::pmr::String> ids{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Snapshot> snapshots{&core::defaultCpuMemoryResource()};
    ids.reserve(resources.size());
    snapshots.reserve(resources.size());

    for (const auto& resource : resources) {
        if (!resource || !resource->reference().valid()
            || !isResourceUuid(resource->reference().id)) {
            result.diagnostics =
                makeDiagnostic("atomic resource batch contains an invalid resource");
            return result;
        }
        const auto& id = resource->reference().id;
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
            result.failedResourceId = id;
            result.diagnostics =
                makeDiagnostic("atomic resource batch contains duplicate id: ", id);
            return result;
        }
        ids.push_back(id);

        const ResourceRecord* existing = findRecord(id);
        if (existing != nullptr
            && ((existing->state != ResourceState::Unloaded
                    && existing->state != ResourceState::Declared)
                || existing->metadataAdmitted || existing->ownedResource
                || !existing->ownedObjects.empty())) {
            result.failedResourceId = id;
            result.diagnostics =
                makeDiagnostic("atomic resource batch target is already admitted: ", id);
            return result;
        }
        if (existing && existing->type != resource->reference().type) {
            result.failedResourceId = id;
            result.diagnostics = "RESOURCE-TRANSACTION-DECLARED-TYPE-MISMATCH";
            return result;
        }
        if (existing && existing->generation == std::numeric_limits<ResourceGeneration>::max()) {
            result.failedResourceId = id;
            result.diagnostics = "RESOURCE-TRANSACTION-GENERATION-EXHAUSTED";
            return result;
        }
        Snapshot snapshot;
        snapshot.id = id;
        snapshot.hadRecord = existing != nullptr;
        snapshots.push_back(std::move(snapshot));
    }

    const auto allowed = [&] {
        return !context.commitGate.allow || context.commitGate.allow(context.commitGate.userData);
    };
    if (!allowed()) {
        result.diagnostics = "RESOURCE-TRANSACTION-ADMISSION-REJECTED";
        return result;
    }
    if (resources.size() > SIZE_MAX - records_.size())
        throw std::bad_alloc();
    // Rollback reinserts retained map nodes without allocating, even if a
    // loader exhausted its core quota while constructing a candidate.
    records_.reserve(records_.size() + resources.size());
    ResourceAliasMap aliasesBefore(aliases_, &core::defaultCpuMemoryResource());
    const auto budgetBefore = budgetConfig_;
    const auto budgetEventsBefore = budgetExhaustionEvents_;
    for (auto& snapshot : snapshots) {
        if (!snapshot.hadRecord)
            continue;
        snapshot.record.emplace(records_.extract(snapshot.id));
    }

    auto rollback = [&]() {
        if (result.rolledBack)
            return;
        result.rolledBack = true;
        for (const auto& id : ids)
            records_.erase(id);
        for (auto& snapshot : snapshots) {
            if (snapshot.hadRecord)
                records_.insert(std::move(*snapshot.record));
        }
        aliases_.swap(aliasesBefore);
        budgetConfig_ = budgetBefore;
        budgetExhaustionEvents_ = budgetEventsBefore;
        result.loadedCount = 0u;
        result.rolledBack = true;
    };

    try {
        for (std::size_t index = 0u; index < resources.size(); ++index) {
            if (!allowed()) {
                rollback();
                result.diagnostics = "RESOURCE-TRANSACTION-ADMISSION-REJECTED";
                return result;
            }
            result.failedResourceId = ids[index];
            ResourceLoadResult loaded;
            try {
                loaded = loadResource(std::move(resources[index]), context);
            } catch (const std::exception& error) {
                loaded.diagnostics = makeDiagnostic("atomic resource batch load threw: ");
                loaded.diagnostics += error.what();
            } catch (...) {
                loaded.diagnostics =
                    makeDiagnostic("atomic resource batch load threw an unknown exception");
            }
            if (!loaded.success) {
                result.diagnostics = loaded.diagnostics.empty()
                    ? makeDiagnostic("atomic resource batch load failed: ", ids[index])
                    : std::move(loaded.diagnostics);
                rollback();
                return result;
            }
            ++result.loadedCount;
        }

        if (!allowed()) {
            rollback();
            result.diagnostics = "RESOURCE-TRANSACTION-ADMISSION-REJECTED";
            return result;
        }
    } catch (...) {
        rollback();
        throw;
    }

    // No failing operations remain after the final gate. Keep record pointers
    // stable and advance the previous epoch, rather than publishing a new
    // record with generation 1 and reviving handles from before unload.
    for (auto& snapshot : snapshots) {
        if (!snapshot.hadRecord)
            continue;
        auto candidate = records_.find(snapshot.id);
        auto& original = *snapshot.record->mapped();
        if (original.alias != candidate->second->alias && !original.alias.empty()) {
            const auto alias = aliases_.find(original.alias);
            if (alias != aliases_.end() && alias->second == snapshot.id)
                aliases_.erase(alias);
        }
        candidate->second->generation = original.generation + 1;
        swapRecordContents(original, *candidate->second);
        candidate->second.swap(snapshot.record->mapped());
    }
    result.success = true;
    result.failedResourceId.clear();
    result.diagnostics.clear();
    return result;
}

ResourceLoadResult ResourceManager::loadGraph(ResourceGraphRequest request)
{
    if (budgetConfig_.maxGraphNodes > 0 && request.nodes.size() > budgetConfig_.maxGraphNodes) {
        ++budgetExhaustionEvents_;
        return applyDiagnosticsBudget(resultFor(false,
            ResourceState::Failed,
            request.rootResourceId,
            {},
            makeDiagnostic("graph node budget exhausted: ", request.rootResourceId)));
    }

    auto plan = buildResourceGraphPlan(request);
    if (!plan.success) {
        return applyDiagnosticsBudget(resultFor(
            false, ResourceState::Failed, plan.resourceId, plan.resourceType, plan.diagnostics));
    }
    if (budgetConfig_.maxCompletionDispatchCount > 0
        && plan.order.size() > budgetConfig_.maxCompletionDispatchCount) {
        ++budgetExhaustionEvents_;
        return applyDiagnosticsBudget(resultFor(false,
            ResourceState::Failed,
            request.rootResourceId,
            {},
            makeDiagnostic("completion dispatch budget exhausted: ", request.rootResourceId)));
    }
    ResourceLoadResult last;
    applyBudgetConfig(request.context);
    for (const std::size_t index : plan.order) {
        auto& node = request.nodes[index];
        for (const auto& dependency : node.dependencies) {
            node.resource->addDependency(ResourceDependency{dependency.id, dependency.usage});
        }
        last = loadResource(std::move(node.resource), request.context);
        if (!last.success) {
            return last;
        }
    }
    ResourceRecord* rootRecord = findRecord(request.rootResourceId);
    if (rootRecord == nullptr || rootRecord->state != ResourceState::Ready) {
        return applyDiagnosticsBudget(resultFor(false,
            ResourceState::Failed,
            request.rootResourceId,
            {},
            makeDiagnostic("resource graph root did not become ready: ", request.rootResourceId)));
    }
    return applyDiagnosticsBudget(resultFor(
        true, rootRecord->state, rootRecord->id, rootRecord->type, rootRecord->diagnostics));
}

core::runtime::WorkServiceStatus ResourceManager::loadGraphAsync(
    core::runtime::WorkService&, ResourceGraphRequest, ResourceAsyncLoadCompletion) noexcept
{
    // ResourceAsyncOwnerAdmission/1: a worker alone cannot acquire bounded
    // sources or publish the registry on its owner. No legacy execution path.
    return core::runtime::WorkServiceStatus::InvalidWorkItem;
}

ResourceGraphSubmitStatus ResourceManager::loadGraphAsync(core::runtime::AsyncRuntime& runtime,
    ResourceGraphRequest graph,
    const ResourceGraphExecutionBudget& budget,
    const ResourceGraphExecutionCompletion& completion,
    core::runtime::CancellationToken cancellation) noexcept
{
    if (!isOwnerThread())
        return ResourceGraphSubmitStatus::InvalidRequest;
    if (budgetConfig_.maxGraphNodes && graph.nodes.size() > budgetConfig_.maxGraphNodes) {
        ++budgetExhaustionEvents_;
        return ResourceGraphSubmitStatus::NodeBudgetExceeded;
    }
    if (budgetConfig_.maxCompletionDispatchCount
        && graph.nodes.size() > budgetConfig_.maxCompletionDispatchCount) {
        ++budgetExhaustionEvents_;
        return ResourceGraphSubmitStatus::InvalidRequest;
    }
    try {
        applyBudgetConfig(graph.context);
        ResourceGraphExecutionRequest request;
        request.graph = std::move(graph);
        request.budget = budget;
        request.cancellation = std::move(cancellation);
        ResourceGraphExecutor executor(*this, runtime.fileService(), runtime.completionQueue());
        return executor.submit(std::move(request), completion);
    } catch (const std::bad_alloc&) {
        return ResourceGraphSubmitStatus::AllocationFailed;
    } catch (...) {
        return ResourceGraphSubmitStatus::InvalidRequest;
    }
}

ResourceLoadResult ResourceManager::loadFromEnvelope(
    NameView envelopeYaml, ResourceLoadPolicy policy)
{
    ResourceEnvelope envelope;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!parseEnvelope(envelopeYaml, envelope, diagnostics)) {
        return resultFor(
            false, ResourceState::Failed, envelope.resourceId, envelope.resourceType, diagnostics);
    }

    auto payload = reflection::deserialize_from_yaml(envelope.payloadYaml);
    if (!payload.success) {
        return resultFor(false,
            ResourceState::Failed,
            envelope.resourceId,
            envelope.resourceType,
            payload.diagnostics);
    }

    return admitParsedEnvelope(
        envelope, policy, std::move(payload.value), std::move(payload.ownedObjects));
}

core::io::FileIoStatus ResourceManager::loadEnvelopeFileAsync(core::io::FileService& fileService,
    const ResourceAsyncLoadRequest& request,
    ResourceAsyncLoadCompletion completion) noexcept
{
    if (!completion.valid()) {
        return core::io::FileIoStatus::InvalidRequest;
    }

    try {
        auto state = core::makeUnique<AsyncEnvelopeLoadState>();
        state->manager = this;
        state->policy = request.policy;
        state->completion = completion;
        core::io::FileIoStatus status;
        if (request.envelopeFile.contractVersion == 1) {
            core::io::FileTextReadRequest text;
            text.file = request.envelopeFile;
            text.text.maxOutputBytes = request.envelopeFile.maxBytes;
            text.text.bom = core::TextBomPolicy::Strip;
            status = fileService.readText(text, {completeAsyncEnvelopeLoad, state.get()});
        } else {
            status =
                fileService.read(request.envelopeFile, {completeLegacyEnvelopeRead, state.get()});
        }
        if (status == core::io::FileIoStatus::Accepted)
            state.release();
        return status;
    } catch (const std::bad_alloc&) {
        return core::io::FileIoStatus::BudgetExceeded;
    } catch (...) {
        return core::io::FileIoStatus::InvalidRequest;
    }
}

core::runtime::WorkServiceStatus ResourceManager::loadAsyncResource(core::runtime::WorkService&,
    core::UniquePtr<ResourceBase>,
    ResourceAsyncLoadCompletion,
    ResourceLoadContext) noexcept
{
    return core::runtime::WorkServiceStatus::InvalidWorkItem;
}

ResourceGraphSubmitStatus ResourceManager::loadAsyncResource(core::runtime::AsyncRuntime& runtime,
    core::UniquePtr<ResourceBase> resource,
    const ResourceGraphExecutionBudget& budget,
    const ResourceGraphExecutionCompletion& completion,
    ResourceLoadContext context,
    core::runtime::CancellationToken cancellation) noexcept
{
    if (!isOwnerThread() || !resource || !resource->reference().valid())
        return ResourceGraphSubmitStatus::InvalidRequest;
    try {
        ResourceGraphRequest request;
        request.rootResourceId = resource->reference().id;
        request.context = context;
        ResourceGraphNode node;
        node.resource = std::move(resource);
        request.nodes.push_back(std::move(node));
        return loadGraphAsync(
            runtime, std::move(request), budget, completion, std::move(cancellation));
    } catch (const std::bad_alloc&) {
        return ResourceGraphSubmitStatus::AllocationFailed;
    } catch (...) {
        return ResourceGraphSubmitStatus::InvalidRequest;
    }
}

ResourceLoadResult ResourceManager::loadObjectFromEnvelope(
    NameView envelopeYaml, reflection::object value, ResourceLoadPolicy policy)
{
    ResourceEnvelope envelope;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    if (!parseEnvelope(envelopeYaml, envelope, diagnostics)) {
        return resultFor(
            false, ResourceState::Failed, envelope.resourceId, envelope.resourceType, diagnostics);
    }

    const auto* cls = value.class_();
    if (cls == nullptr || cls->name() != envelope.resourceType) {
        diagnostics = makeDiagnostic("object type does not match resourceType");
        return resultFor(
            false, ResourceState::Failed, envelope.resourceId, envelope.resourceType, diagnostics);
    }

    auto ownedObjects = core::pmr::Vector<reflection::object>(
        core::pmr::Vector<reflection::object>::allocator_type(&core::defaultCpuMemoryResource()));
    return admitParsedEnvelope(envelope, policy, std::move(value), std::move(ownedObjects));
}

ResourceLoadResult ResourceManager::admitParsedEnvelope(const ResourceEnvelope& envelope,
    ResourceLoadPolicy policy,
    reflection::object value,
    core::pmr::Vector<reflection::object>&& ownedObjects)
{
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    const auto status = parseResourceStatus({envelope.status.data(), envelope.status.size()});
    if (!status.success) {
        return resultFor(false,
            ResourceState::Failed,
            envelope.resourceId,
            envelope.resourceType,
            status.diagnostics);
    }

    if (!envelope.alias.empty()) {
        auto aliasFound = aliases_.find(envelope.alias);
        if (aliasFound != aliases_.end() && aliasFound->second != envelope.resourceId) {
            diagnostics = makeDiagnostic("duplicate alias: ", envelope.alias);
            return resultFor(false,
                ResourceState::Failed,
                envelope.resourceId,
                envelope.resourceType,
                diagnostics);
        }
    }

    auto existing = records_.find(envelope.resourceId);
    if (existing != records_.end()) {
        ResourceRecord& record = *existing->second;
        if (record.state == ResourceState::Ready || record.state == ResourceState::Loading) {
            diagnostics = makeDiagnostic("duplicate resource id: ", envelope.resourceId);
            record.diagnostics = diagnostics;
            record.state = ResourceState::Failed;
            return resultFor(false, record.state, record.id, record.type, diagnostics);
        }
        if (!record.type.empty() && record.type != envelope.resourceType) {
            diagnostics = makeDiagnostic("declared resource type does not match envelope");
            record.diagnostics = diagnostics;
            record.state = ResourceState::Failed;
            return resultFor(false, record.state, record.id, record.type, diagnostics);
        }
    }

    if (policy.dependencyPolicy == ResourceDependencyPolicy::RequireDeclared) {
        for (const auto& dependency : envelope.dependencies) {
            const ResourceRecord* dependencyRecord = findRecord(dependency.id);
            if (dependencyRecord == nullptr || dependencyRecord->state == ResourceState::Unloaded) {
                diagnostics = makeDiagnostic("unresolved dependency: ", dependency.id);
                ResourceRecord& record = upsertRecord(envelope);
                record.state = ResourceState::Failed;
                record.diagnostics = diagnostics;
                return resultFor(false, record.state, record.id, record.type, diagnostics);
            }
        }
    }

    ResourceRecord& record = upsertRecord(envelope);
    record.state = ResourceState::Loading;
    record.metadataAdmitted = true;
    record.diagnostics.clear();
    record.value = reflection::object::undefined();
    record.ownedObjects.clear();
    record.ownedResource.reset();
    record.dependencies = envelope.dependencies;
    record.alias = envelope.alias;
    record.displayName = envelope.displayName;
    record.status = envelope.status;

    record.value = std::move(value);
    record.ownedObjects = std::move(ownedObjects);
    record.state = ResourceState::Ready;
    if (!record.alias.empty()) {
        aliases_[record.alias] = record.id;
    }
    return resultFor(true, record.state, record.id, record.type, record.diagnostics);
}

ResourceLoadResult ResourceManager::unload(NameView resourceId)
{
    ResourceRecord* record = findRecord(resourceId);
    if (record == nullptr) {
        return resultFor(false,
            ResourceState::Unloaded,
            resourceId,
            {},
            makeDiagnostic("resource not found: ", resourceId));
    }
    for (auto* observer : lifecycleObservers_) {
        if (!observer->beforeResourceUnload(resourceId)) {
            return resultFor(false,
                record->state,
                record->id,
                record->type,
                makeDiagnostic(
                    "resource unload deferred until lifecycle consumers stop: ", resourceId));
        }
    }
    record->value = reflection::object::undefined();
    record->ownedObjects.clear();
    record->ownedResource.reset();
    record->dependencies.clear();
    record->metadataAdmitted = false;
    record->state = ResourceState::Unloaded;
    record->diagnostics.clear();
    return resultFor(true, record->state, record->id, record->type, record->diagnostics);
}

void ResourceManager::addLifecycleObserver(ResourceLifecycleObserver& observer)
{
    for (const auto* existing : lifecycleObservers_)
        if (existing == &observer)
            return;
    lifecycleObservers_.push_back(&observer);
}

void ResourceManager::removeLifecycleObserver(ResourceLifecycleObserver& observer)
{
    lifecycleObservers_.erase(
        std::remove(lifecycleObservers_.begin(), lifecycleObservers_.end(), &observer),
        lifecycleObservers_.end());
}

ResourceState ResourceManager::stateOf(NameView resourceId) const
{
    const ResourceRecord* record = findRecord(resourceId);
    return record != nullptr ? record->state : ResourceState::Unloaded;
}

core::pmr::String ResourceManager::diagnosticsOf(NameView resourceId) const
{
    const ResourceRecord* record = findRecord(resourceId);
    return record != nullptr ? record->diagnostics
                             : makeDiagnostic("resource not found: ", resourceId);
}

ResourceAliasResult ResourceManager::resolveAlias(NameView alias) const
{
    ResourceAliasResult result;
    auto found = aliases_.find(makeString(alias));
    if (found == aliases_.end()) {
        result.diagnostics = makeDiagnostic("alias not found: ", alias);
        return result;
    }
    result.success = true;
    result.resourceId = found->second;
    return result;
}

ResourceBase* ResourceManager::find_resource(const core::pmr::String& resourceId)
{
    ResourceRecord* record = findRecord(resourceId);
    if (record == nullptr || record->state != ResourceState::Ready || !record->ownedResource) {
        return nullptr;
    }
    return record->ownedResource.get();
}

arp::reflection::object ResourceManager::find_object(const core::pmr::String& resourceId) const
{
    const ResourceRecord* record = findRecord(resourceId);
    if (record == nullptr || record->state != ResourceState::Ready) {
        return arp::reflection::object::undefined();
    }
    if (record->ownedResource) {
        return record->ownedResource->nativeReflectionObject();
    }
    return record->value;
}

const ResourceRecord* ResourceManager::recordSnapshot(NameView resourceId) const
{
    return findRecord(resourceId);
}

ResourceReferenceSnapshotResult ResourceManager::snapshotReferences(
    NameView resourceType, std::size_t maxEntries) const
{
    ResourceReferenceSnapshotResult result;
    if (resourceType.empty()) {
        result.diagnostics = "resource reference snapshot requires an exact resource type";
        return result;
    }

    for (const auto& item : records_) {
        const auto& record = *item.second;
        if (record.type != resourceType)
            continue;
        if (result.entries.size() == maxEntries) {
            result.entries.clear();
            result.diagnostics = "resource reference snapshot exceeds caller entry limit";
            return result;
        }

        ResourceReferenceSnapshotEntry entry;
        entry.reference.id = record.id;
        entry.reference.type = record.type;
        entry.reference.alias = record.alias;
        entry.reference.status = record.status;
        entry.reference.displayName = record.displayName;
        entry.state = record.state;
        entry.generation = record.generation;
        result.entries.push_back(std::move(entry));
    }

    std::sort(result.entries.begin(), result.entries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.reference.id < rhs.reference.id;
    });
    result.success = true;
    return result;
}

void ResourceManager::setBudgetConfig(ResourceBudgetConfig config)
{
    budgetConfig_ = config;
}

ResourceManager::ResourceManagerTelemetry ResourceManager::telemetry() const noexcept
{
    ResourceManagerTelemetry result;
    result.budget = budgetConfig_;
    result.budgetExhaustionEvents = budgetExhaustionEvents_;
    return result;
}

void ResourceManager::applyBudgetConfig(ResourceLoadContext& context) const noexcept
{
    if (context.maxDecodedBytes == 0u && budgetConfig_.maxDecodedBytes > 0u) {
        context.maxDecodedBytes = budgetConfig_.maxDecodedBytes;
    }
}

ResourceLoadResult ResourceManager::applyDiagnosticsBudget(ResourceLoadResult result)
{
    if (budgetConfig_.maxDiagnosticsLength == 0u
        || result.diagnostics.size() <= budgetConfig_.maxDiagnosticsLength) {
        return result;
    }
    ++budgetExhaustionEvents_;
    core::pmr::String truncated{&core::defaultCpuMemoryResource()};
    if (budgetConfig_.maxDiagnosticsLength > 3u) {
        truncated.assign(result.diagnostics.data(),
            result.diagnostics.data()
                + static_cast<std::ptrdiff_t>(budgetConfig_.maxDiagnosticsLength - 3u));
        truncated += "...";
    } else {
        truncated.assign(result.diagnostics.data(),
            result.diagnostics.data()
                + static_cast<std::ptrdiff_t>(budgetConfig_.maxDiagnosticsLength));
    }
    result.diagnostics = std::move(truncated);
    return result;
}

std::size_t ResourceManager::retainedRecoveryBytes() const noexcept
{
    std::size_t total = 0u;
    for (const auto& resource : retainedResources_) {
        if (resource) {
            total += resource->processedByteCount();
        }
    }
    return total;
}

bool ResourceManager::retainedRecoveryBudgetAllows(const ResourceBase* resource) const noexcept
{
    if (budgetConfig_.maxRetainedRecoveryBytes == 0u || resource == nullptr) {
        return true;
    }
    const std::size_t retainedBytes = retainedRecoveryBytes();
    const std::size_t resourceBytes = resource->processedByteCount();
    return resourceBytes <= budgetConfig_.maxRetainedRecoveryBytes
        && retainedBytes <= budgetConfig_.maxRetainedRecoveryBytes - resourceBytes;
}

ResourceRecord* ResourceManager::findRecord(NameView resourceId)
{
    auto found = records_.find(makeString(resourceId));
    return found == records_.end() ? nullptr : found->second.get();
}

const ResourceRecord* ResourceManager::findRecord(NameView resourceId) const
{
    auto found = records_.find(makeString(resourceId));
    return found == records_.end() ? nullptr : found->second.get();
}

ResourceRecord& ResourceManager::upsertRecord(const ResourceEnvelope& envelope)
{
    auto found = records_.find(envelope.resourceId);
    if (found != records_.end()) {
        ResourceRecord& record = *found->second;
        record.id = envelope.resourceId;
        record.type = envelope.resourceType;
        record.alias = envelope.alias;
        record.displayName = envelope.displayName;
        record.status = envelope.status;
        return record;
    }

    auto record = core::makeUnique<ResourceRecord>();
    record->id = envelope.resourceId;
    record->type = envelope.resourceType;
    record->alias = envelope.alias;
    record->displayName = envelope.displayName;
    record->status = envelope.status;
    ResourceRecord* recordPtr = record.get();
    records_[record->id] = std::move(record);
    return *recordPtr;
}

core::pmr::String ResourceManager::makeString(NameView text)
{
    return resource::makeString(text);
}

core::pmr::String ResourceManager::makeDiagnostic(const char* text)
{
    return resource::makeDiagnostic(text);
}

core::pmr::String ResourceManager::makeDiagnostic(const char* prefix, NameView value)
{
    return resource::makeDiagnostic(prefix, value);
}

ResourceGeneration ResourceManager::generationOf(NameView resourceId) const
{
    const ResourceRecord* record = findRecord(resourceId);
    return record != nullptr ? record->generation : 0u;
}

ResourceManager::ResourceReloadResult ResourceManager::reloadResource(
    core::UniquePtr<ResourceBase> resource, ResourceLoadContext context)
{
    applyBudgetConfig(context);
    ResourceReloadResult result;
    if (!resource || !resource->reference().valid()) {
        result.diagnostics = makeDiagnostic("invalid ResourceBase reload request");
        return result;
    }

    const core::pmr::String resourceId = resource->reference().id;
    const core::pmr::String resourceType = resource->reference().type;

    ResourceRecord* record = findRecord(resourceId);
    if (record == nullptr || record->state != ResourceState::Ready) {
        result.diagnostics = makeDiagnostic("resource not ready for reload: ", resourceId);
        return result;
    }

    if (budgetConfig_.maxReloadGenerations > 0
        && record->generation >= budgetConfig_.maxReloadGenerations) {
        ++budgetExhaustionEvents_;
        result.diagnostics = makeDiagnostic("reload generation budget exhausted: ", resourceId);
        return result;
    }

    if (record->reloadInProgress) {
        result.diagnostics = makeDiagnostic("reload in progress for resource: ", resourceId);
        return result;
    }

    if (record->type != resourceType) {
        result.diagnostics = makeDiagnostic("resource type mismatch during reload: ", resourceId);
        return result;
    }

    // Validate contract status change: reject demotion or invalid promotion
    const core::pmr::String newStatus = resource->reference().status;
    const auto oldStatusParsed = parseResourceStatus(record->status);
    const auto newStatusParsed = parseResourceStatus(newStatus);
    if (oldStatusParsed.success && newStatusParsed.success) {
        const auto promotion =
            validateResourceStatusPromotion(oldStatusParsed.status, newStatusParsed.status);
        if (!promotion.success) {
            result.diagnostics =
                makeDiagnostic("resource reload rejected: invalid contract status change for ");
            result.diagnostics += resourceId;
            result.diagnostics += ": ";
            result.diagnostics += promotion.diagnostics;
            return result;
        }
    }

    record->reloadInProgress = true;

    const std::uint32_t oldContentHash = record->contentHash;
    const ResourceGeneration oldGeneration = record->generation;

    // Capture old ready state for rollback
    const auto recordSnapshot = captureReloadSnapshot(*record);
    core::UniquePtr<ResourceBase> oldOwnedResource = std::move(record->ownedResource);

    // Temporarily reset state so admitResourceMetadata allows the update
    record->metadataAdmitted = false;
    record->state = ResourceState::Declared;

    // Mark dependents as stale before attempting reload
    core::pmr::Vector<core::pmr::String> dependentsToInvalidate{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> visited{&core::defaultCpuMemoryResource()};
    collectDependentsTopological(resourceId, dependentsToInvalidate, visited);

    // Mark dependents as stale
    for (const auto& depId : dependentsToInvalidate) {
        ResourceRecord* depRecord = findRecord(depId);
        if (depRecord != nullptr && depRecord->state == ResourceState::Ready) {
            depRecord->state = ResourceState::Stale;
        }
    }

    // Attempt to load the new resource
    ResourceLoadResult loaded = resource->load(*this, context);

    // Re-lookup the record in case admitResourceMetadata replaced it during load
    record = findRecord(resourceId);

    if (!loaded.success) {
        // Rollback: restore old state
        record->ownedResource = std::move(oldOwnedResource);
        restoreReloadSnapshot(*record, recordSnapshot);
        record->reloadInProgress = false;

        // Restore dependents to ready
        for (const auto& depId : dependentsToInvalidate) {
            ResourceRecord* depRecord = findRecord(depId);
            if (depRecord != nullptr && depRecord->state == ResourceState::Stale) {
                depRecord->state = ResourceState::Ready;
            }
        }

        result.success = false;
        result.state = ResourceState::Failed;
        result.diagnostics = loaded.diagnostics;
        result.telemetry.rolledBack = true;
        result.telemetry.oldContentHash = oldContentHash;
        return result;
    }

    // Check if content actually changed
    const std::uint32_t newContentHash = computeContentHash(resource.get());
    if (newContentHash == oldContentHash && oldGeneration > 0u) {
        // No-op reload: content unchanged
        record->ownedResource = std::move(oldOwnedResource);
        restoreReloadSnapshot(*record, recordSnapshot);
        record->reloadInProgress = false;

        // Restore dependents to ready
        for (const auto& depId : dependentsToInvalidate) {
            ResourceRecord* depRecord = findRecord(depId);
            if (depRecord != nullptr && depRecord->state == ResourceState::Stale) {
                depRecord->state = ResourceState::Ready;
            }
        }

        result.success = true;
        result.unchangedContent = true;
        result.state = ResourceState::Ready;
        result.telemetry.generation = record->generation;
        result.telemetry.oldContentHash = oldContentHash;
        result.telemetry.newContentHash = newContentHash;
        result.telemetry.committed = false;
        return result;
    }

    // Commit: replace with new resource, retire old for in-flight consumers
    if (oldOwnedResource) {
        if (!retainedRecoveryBudgetAllows(oldOwnedResource.get())) {
            restoreReloadSnapshot(*record, recordSnapshot);
            record->ownedResource = std::move(oldOwnedResource);
            record->reloadInProgress = false;
            for (const auto& depId : dependentsToInvalidate) {
                ResourceRecord* depRecord = findRecord(depId);
                if (depRecord != nullptr && depRecord->state == ResourceState::Stale) {
                    depRecord->state = ResourceState::Ready;
                }
            }
            ++budgetExhaustionEvents_;
            result.success = false;
            result.state = ResourceState::Failed;
            result.diagnostics =
                makeDiagnostic("retained recovery byte budget exhausted: ", resourceId);
            result.telemetry.rolledBack = true;
            result.telemetry.oldContentHash = oldContentHash;
            result.telemetry.newContentHash = newContentHash;
            result.telemetry.diagnostics = result.diagnostics;
            return result;
        }
        retainedResources_.push_back(std::move(oldOwnedResource));
        result.telemetry.retiredCount = 1u;
    }
    record->ownedResource = std::move(resource);
    record->generation = oldGeneration + 1u;
    record->contentHash = newContentHash;
    record->state = ResourceState::Ready;
    record->reloadInProgress = false;

    result.success = true;
    result.unchangedContent = false;
    result.state = ResourceState::Ready;
    result.telemetry.generation = record->generation;
    result.telemetry.oldContentHash = oldContentHash;
    result.telemetry.newContentHash = newContentHash;
    result.telemetry.committed = true;
    result.telemetry.diagnostics = record->diagnostics;
    result.telemetry.invalidatedDependents = std::move(dependentsToInvalidate);

    return result;
}

void ResourceManager::collectDependentsTopological(NameView resourceId,
    core::pmr::Vector<core::pmr::String>& outDependents,
    core::pmr::Vector<core::pmr::String>& visited)
{
    const core::pmr::String id = makeString(resourceId);

    // Check if already visited
    for (const auto& v : visited) {
        if (v == id) {
            return;
        }
    }
    visited.push_back(id);

    // Find all records that directly depend on this resource
    core::pmr::Vector<core::pmr::String> directDependents{&core::defaultCpuMemoryResource()};
    for (auto& pair : records_) {
        ResourceRecord& record = *pair.second;
        if (record.state == ResourceState::Unloaded || record.state == ResourceState::Failed) {
            continue;
        }

        for (const auto& dep : record.dependencies) {
            if (dep.id == id) {
                directDependents.push_back(record.id);
                break;
            }
        }
    }

    std::sort(directDependents.begin(),
        directDependents.end(),
        [](const core::pmr::String& lhs, const core::pmr::String& rhs) {
            return lhs < rhs;
        });

    // Add direct dependents first (forward topological order)
    for (const auto& depId : directDependents) {
        bool alreadyAdded = false;
        for (const auto& existing : outDependents) {
            if (existing == depId) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            outDependents.push_back(depId);
        }
    }

    // Then recursively collect their dependents
    for (const auto& depId : directDependents) {
        collectDependentsTopological(depId, outDependents, visited);
    }
}

std::uint32_t ResourceManager::computeContentHash(const ResourceBase* resource)
{
    if (resource == nullptr) {
        return 0u;
    }

    core::StableHash64 hasher;
    const ResourceReference& ref = resource->reference();
    hasher.appendString({ref.id.data(), ref.id.size()});
    hasher.appendString({ref.type.data(), ref.type.size()});
    hasher.appendString({ref.status.data(), ref.status.size()});
    resource->appendContentFingerprint(hasher);

    return static_cast<std::uint32_t>(hasher.value() & 0xFFFFFFFFu);
}

void ResourceManager::retireResource(NameView resourceId)
{
    ResourceRecord* record = findRecord(resourceId);
    if (record == nullptr || record->state != ResourceState::Ready) {
        return;
    }
    if (record->ownedResource) {
        retainedResources_.push_back(std::move(record->ownedResource));
    }
    record->value = reflection::object::undefined();
    record->ownedObjects.clear();
    record->state = ResourceState::Unloaded;
    record->metadataAdmitted = false;
    record->diagnostics = makeDiagnostic("resource retired");
}

void ResourceManager::releaseRetiredResources()
{
    for (auto* observer : lifecycleObservers_) {
        if (!observer->beforeRetiredResourcesRelease())
            return;
    }
    retainedResources_.clear();
}

std::size_t ResourceManager::retainedResourceCount() const noexcept
{
    return retainedResources_.size();
}

void ResourceBase::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    (void)hasher;
}

} // namespace arp::resource
