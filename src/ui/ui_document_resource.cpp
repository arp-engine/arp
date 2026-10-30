#include "ui/ui_document_resource.h"

#include "reflection/reflection.h"
#include "resource/resource_source_io.h"
#include "scene/scene.h"
#include "ui/detail/ui_content_fingerprint.h"

#include <charconv>
#include <filesystem>
#include <system_error>
#include <utility>

namespace arp::ui {

namespace {

arp::core::pmr::String documentResourceText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

bool validDocumentResourceStatus(const arp::core::pmr::String& status)
{
    return arp::resource::validateResourceStatus({status.data(), status.size()},
        {
            arp::resource::ResourceStatus::ContractComplete,
            arp::resource::ResourceStatus::PrototypeComplete,
        },
        "UI document resource")
        .success;
}

void appendUnsigned(arp::core::pmr::String& out, std::uint64_t value)
{
    char buffer[32]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec == std::errc{}) {
        out.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
    }
}

void appendIndentedBlock(
    arp::core::pmr::String& out, const arp::core::pmr::String& textBlock, const char* indent)
{
    out += indent;
    for (const char ch : textBlock) {
        out.push_back(ch);
        if (ch == '\n') {
            out += indent;
        }
    }
    if (out.empty() || out.back() != '\n') {
        out += "\n";
    }
}

arp::core::pmr::String pathToText(const std::filesystem::path& path)
{
    return arp::core::FilePath::utf8(path);
}

arp::core::pmr::String payloadYaml(
    const arp::core::pmr::String& resourceType, const UiDocumentResourceData& data)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += resourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ";
    yaml += kUiDocumentResourceContract;
    yaml += "\n  documentContract: ";
    yaml += data.documentContract;
    yaml += "\n  sourcePath: ";
    yaml += pathToText(data.sourcePath);
    yaml += "\n  nodeCount: ";
    appendUnsigned(yaml, data.document.nodes().size());
    yaml += "\n  textureCount: ";
    appendUnsigned(yaml, data.document.textures().size());
    yaml += "\n  drawRecordCount: ";
    appendUnsigned(yaml, data.drawPlan.recordCount());
    yaml += "\n  bindingCount: ";
    appendUnsigned(yaml, data.bindings.size());
    yaml += "\n";
    return yaml;
}

arp::core::pmr::String envelopeYaml(const UiDocumentResourceLoadDesc& desc,
    const arp::core::pmr::String& resourceType,
    const arp::core::pmr::String& payload,
    std::uint32_t contentHash)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "format: ArpRuntimeResource\n";
    yaml += "version: 1\n";
    yaml += "resourceId: ";
    yaml += desc.resourceId;
    yaml += "\nresourceType: ";
    yaml += resourceType;
    yaml += "\n";
    if (!desc.alias.empty()) {
        yaml += "alias: ";
        yaml += desc.alias;
        yaml += "\n";
    }
    yaml += "displayName: UI Document Resource\n";
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUnsigned(yaml, contentHash);
    yaml += "\ndependencies: []\n";
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payload, "  ");
    return yaml;
}

void registerUiDocumentResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<UiDocumentResource>{
        kUiDocumentResourceType,
    };
    arp::reflection::class_of<UiDocumentResource>()->set_name(kUiDocumentResourceType);
    registered = true;
}

} // namespace

void UiDocumentResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    detail::UiContentFingerprint hash(hasher);
    hash.resource(*this, kUiDocumentResourceContract);
    hash.number(data != nullptr);
    if (!data)
        return;
    hash.path(data->sourcePath);
    hash.text(data->documentContract);
    data->document.appendContentFingerprint(hasher);
    hash.number(data->drawPlan.recordCount());
    for (const auto& record : data->drawPlan.records())
        hash.draw(record);
    hash.number(data->bindings.size());
    for (const auto& binding : data->bindings) {
        hash.text(binding.sourceId);
        hash.number(binding.widgetId.value());
        hash.rect(binding.rect);
        hash.text(binding.eventRole);
        hash.text(binding.text);
        hash.number(binding.order);
        hash.number(binding.visible);
        hash.number(binding.enabled);
    }
}

arp::resource::ResourceLoadResult UiDocumentResource::load(
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics = documentResourceText("UI document resource requires UUID resourceId.");
        return result;
    }
    if (!validDocumentResourceStatus(desc.status)) {
        result.diagnostics = documentResourceText("invalid UI document resource status");
        return result;
    }
    if (desc.documentContract != contracts::kArpuiDocumentContract) {
        result.diagnostics =
            documentResourceText("UI document resource requires documentContract ui.document/v1.");
        return result;
    }
    arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent> intents;
    if (!collectFileReadIntents(resourceManager, intents, result.diagnostics))
        return result;
    auto source = arp::resource::readResourceText(
        intents.front().path, context, desc.maxDocumentBytes, &desc.origin);
    if (!source.success) {
        result.diagnostics = std::move(source.diagnostics);
        return result;
    }
    // An empty acquired document must never select the loader's local-file entry.
    if (source.text.empty()) {
        result.diagnostics = "UI-DOCUMENT-SOURCE-EMPTY";
        return result;
    }

    UiArpuiRuntimeLoadDesc loadDesc;
    loadDesc.path = desc.documentPath;
    loadDesc.sourceYaml = std::move(source.text);
    loadDesc.sourceYamlProvided = true;
    loadDesc.maxDocumentBytes = desc.maxDocumentBytes;
    auto loaded = UiArpuiRuntimeLoader().load(loadDesc);
    if (!loaded.ok()) {
        result.diagnostics = loaded.diagnostic().message;
        return result;
    }
    const auto layout = loaded.runtime().canvas().resolveLayout();
    if (!layout.ok()) {
        result.diagnostics = layout.diagnostic().message;
        return result;
    }

    DocumentComponentDesc componentDesc;
    componentDesc.documentSource = pathToText(desc.documentPath);
    componentDesc.resourceId = documentResourceText("ui.document.resource");
    componentDesc.layer = 0u;
    componentDesc.inputRoute = documentResourceText("ui.input");
    componentDesc.renderRoute = documentResourceText("ui.render");
    arp::scene::Scene runtimeScene(documentResourceText("UiDocumentResourceScene"));
    auto& uiObject = runtimeScene.createObject(documentResourceText("ui-document-resource"));
    const auto reparented = runtimeScene.reparent(uiObject.id(), runtimeScene.rootId());
    if (!reparented.ok()) {
        result.diagnostics = reparented.diagnostic().message;
        return result;
    }
    auto& component = uiObject.addComponent<DocumentComponent>(componentDesc);
    const auto mounted = DocumentComponentMounter().mount(component, loaded);
    if (!mounted.ok()) {
        result.diagnostics = mounted.diagnostic().message;
        return result;
    }

    data = arp::core::makeShared<UiDocumentResourceData>();
    data->sourcePath = desc.documentPath;
    data->documentContract = desc.documentContract;
    data->document = loaded.document();
    data->drawPlan = component.mountedDrawPlan();
    for (const auto& node : data->document.nodes()) {
        if (node.sourceId.empty()) {
            continue;
        }
        const auto widgetId = loaded.widgetIdForSourceNodeId(node.sourceId);
        const auto* resolved = layout.findWidget(widgetId);
        if (resolved == nullptr) {
            continue;
        }
        data->bindings.push_back(UiDocumentRuntimeBinding{
            node.sourceId,
            widgetId,
            resolved->rect,
            node.eventRole,
            node.text,
            resolved->order,
            resolved->visibleInHierarchy,
            resolved->enabledInHierarchy,
        });
    }

    return resourceManager.admitResourceMetadata(*this, context.policy);
}

bool UiDocumentResource::collectFileReadIntents(arp::resource::ResourceManager&,
    arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
    arp::core::pmr::String& diagnostics) const
{
    if (!arp::resource::appendResourceReadIntent(pathToText(loadDesc.documentPath),
            loadDesc.maxDocumentBytes,
            intents,
            diagnostics,
            loadDesc.origin))
        return false;
    intents.back().role = documentResourceText("ui.document.yaml");
    return true;
}

UiDocumentResourceLoadResult loadUiDocumentResource(
    arp::resource::ResourceManager& resourceManager, const UiDocumentResourceLoadDesc& desc)
{
    UiDocumentResourceLoadResult result;
    result.resource =
        resourceManager.load<UiDocumentResource>(arp::resource::ResourceLoadContext{}, desc);
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::ui
