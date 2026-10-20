#include "core/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "tests/fixtures/ui_document_fixture.h"
#include "ui/ui_document_resource.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::filesystem::path fixturePath()
{
    const auto path = std::filesystem::path("artifacts/ui_document_resource/fixture.arpui");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    assert(output.good());
    output << arp::tests::uiDocumentFixture();
    return path;
}

void ui_document_resource_loads_yaml_document()
{
    arp::resource::ResourceManager manager;
    const auto path = fixturePath();
    arp::ui::UiDocumentResourceLoadDesc desc;
    desc.resourceId = text("c28d4d95-953b-4c4c-9631-0c54d44db13a");
    desc.alias = text("ui/pika/document/hud");
    desc.status = text("Prototype Complete");
    desc.documentPath = path;
    desc.documentContract = text("ui.document/v1");

    const auto loaded = arp::ui::loadUiDocumentResource(manager, desc);
    assert(loaded.success);
    assert(loaded.resource.resourceType == text("UiDocumentResource"));

    const auto handle =
        manager.findByAlias<arp::ui::UiDocumentResource>(text("ui/pika/document/hud"));
    assert(handle.ready());
    assert(handle.get() != nullptr);
    assert(handle.get()->data != nullptr);
    assert(handle.get()->data->sourcePath == desc.documentPath);
    assert(handle.get()->data->drawPlan.recordCount() == 3u);
    assert(handle.get()->data->drawPlan.textRecordCount() == 1u);
    assert(handle.get()->data->drawPlan.imageRecordCount() == 1u);
    assert(!handle.get()->data->drawPlan.exposesNativeBackendHandles());
    assert(!handle.get()->data->bindings.empty());
}

void ui_document_resource_rejects_missing_document()
{
    arp::resource::ResourceManager manager;
    arp::ui::UiDocumentResourceLoadDesc desc;
    desc.resourceId = text("c28d4d95-953b-4c4c-9631-0c54d44db13a");
    desc.alias = text("ui/pika/document/missing");
    desc.status = text("Prototype Complete");
    desc.documentPath = std::filesystem::path("artifacts/ui_document_resource/missing.arpui");
    desc.documentContract = text("ui.document/v1");

    const auto loaded = arp::ui::loadUiDocumentResource(manager, desc);
    assert(!loaded.success);
    assert(!loaded.diagnostics.empty());
}

} // namespace

int main()
{
    ui_document_resource_loads_yaml_document();
    ui_document_resource_rejects_missing_document();
    return 0;
}
