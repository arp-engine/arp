#include "resource/internal_asset_package.h"
#include "ui/ui_document.h"

#include <cassert>
#include <charconv>
#include <filesystem>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::resource::ResourceDependency textureDependency()
{
    arp::resource::ResourceDependency dependency;
    dependency.id = text("22222222-2222-4222-8222-222222222222");
    dependency.usage = text("ui.sampled-texture");
    return dependency;
}

void appendUint(arp::core::pmr::String& out, std::uint32_t value)
{
    char buffer[16]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec == std::errc{}) {
        out.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
    }
}

arp::core::pmr::String texturePayloadYaml()
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: TextureResource\n";
    yaml += "fields:\n";
    yaml += "  name: hud/button-atlas\n";
    yaml += "  sourcePath: textures/hud_button_atlas.ktx2\n";
    yaml += "  usage: ui.sampled-texture\n";
    yaml += "  srgb: true\n";
    yaml += "  alpha: true\n";
    return yaml;
}

const char* arpuiFixture()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas:
  id: hud
  width: 320
  height: 180
  dpi: 96
textures:
  - resourceId: 22222222-2222-4222-8222-222222222222
    layer: layer_button
    atlas: hud_atlas
    rect: { x: 8, y: 16, width: 64, height: 24 }
    pivotX: 0.5
    pivotY: 0.5
    sliceLeft: 4
    sliceTop: 4
    sliceRight: 4
    sliceBottom: 4
    alpha: straight
    srgb: true
    mips: none
    compression: ui-lossless
    sampler: linear-clamp
root:
  id: root
  type: Panel
  layer: layer_root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  style: root
  state: normal
  event: none
  children:
  - id: start_button
    type: Button
    layer: layer_button
    rect: { x: 12, y: 16, width: 96, height: 24 }
    style: primary
    state: enabled
    event: submit
    text: Start
)yaml";
}

arp::resource::InternalAssetPackageEntry textureEntry()
{
    const auto payload = texturePayloadYaml();
    const auto hash = arp::resource::computeResourcePayloadHash(payload);
    assert(hash.success);

    arp::resource::InternalAssetPackageEntry entry;
    entry.resourceId = text("22222222-2222-4222-8222-222222222222");
    entry.resourceType = text("TextureResource");
    entry.alias = text("texture/hud_button_atlas");
    entry.status = text("Contract Complete");
    entry.envelopePath = text("resources/hud_button_atlas.arpres.yaml");
    entry.contentHash = hash.hash;
    entry.envelopeYaml = text(
        "format: ArpRuntimeResource\nversion: 1\nresourceId: 22222222-2222-4222-8222-222222222222\nresourceType: TextureResource\nalias: texture/hud_button_atlas\nstatus: Contract Complete\ncontentHash: ");
    appendUint(entry.envelopeYaml, hash.hash);
    entry.envelopeYaml += "\ndependencies: []\npayload:\n";
    entry.envelopeYaml += "  type: TextureResource\n";
    entry.envelopeYaml += "  fields:\n";
    entry.envelopeYaml += "    name: hud/button-atlas\n";
    entry.envelopeYaml += "    sourcePath: textures/hud_button_atlas.ktx2\n";
    entry.envelopeYaml += "    usage: ui.sampled-texture\n";
    entry.envelopeYaml += "    srgb: true\n";
    entry.envelopeYaml += "    alpha: true\n";
    return entry;
}

arp::ui::UiArpuiResourcePackageDesc packageDesc()
{
    arp::ui::UiArpuiResourcePackageDesc desc;
    desc.resourceId = text("11111111-1111-4111-8111-111111111111");
    desc.alias = text("ui/hud");
    desc.status = arp::ui::statusLabelName(arp::ui::UiFrameworkStatusLabel::ContractComplete);
    desc.documentYaml = text(arpuiFixture());
    desc.dependencies.push_back(textureDependency());
    return desc;
}

void arpui_resource_package_declares_dependencies_hash_status_and_loads_runtime()
{
    auto package = arp::resource::InternalAssetPackageDesc{};
    package.name = text("hud-ui-package");
    package.status = text("Contract Complete");
    package.entries.push_back(textureEntry());

    const auto built = arp::ui::UiArpuiResourcePackageBuilder().build(packageDesc());
    assert(built.success);
    assert(built.entry.resourceType == text("UiDocumentResource"));
    assert(built.entry.resourceId == text("11111111-1111-4111-8111-111111111111"));
    assert(built.entry.alias == text("ui/hud"));
    assert(built.entry.status == text("Contract Complete"));
    assert(built.entry.contentHash != 0u);
    assert(built.entry.dependencies.size() == 1u);
    assert(built.entry.dependencies[0].id == text("22222222-2222-4222-8222-222222222222"));
    package.entries.push_back(built.entry);

    const auto output = std::filesystem::path("artifacts/ui_arpui_resource_package/hud");
    const auto written = arp::resource::InternalAssetPackageWriter().write(package, output);
    assert(written.success);

    arp::ui::UiArpuiPackageRuntimeLoadDesc load;
    load.packageDirectory = output;
    load.resourceId = text("11111111-1111-4111-8111-111111111111");
    const auto loaded = arp::ui::UiArpuiPackageRuntimeLoader().load(load);
    assert(loaded.ok());
    assert(loaded.document().canvas().contractVersion == text("ui.document/v1"));
    assert(loaded.document().textures().size() == 1u);
    assert(loaded.runtime().controlCount() == 2u);
}

void package_loader_fails_deterministically_for_missing_resource_and_invalid_status()
{
    const auto built = arp::ui::UiArpuiResourcePackageBuilder().build(packageDesc());
    assert(built.success);

    auto package = arp::resource::InternalAssetPackageDesc{};
    package.name = text("hud-ui-package");
    package.status = text("Contract Complete");
    package.entries.push_back(textureEntry());
    package.entries.push_back(built.entry);
    const auto output = std::filesystem::path("artifacts/ui_arpui_resource_package/failure");
    assert(arp::resource::InternalAssetPackageWriter().write(package, output).success);

    arp::ui::UiArpuiPackageRuntimeLoadDesc missing;
    missing.packageDirectory = output;
    missing.resourceId = text("99999999-9999-4999-8999-999999999999");
    const auto missingResult = arp::ui::UiArpuiPackageRuntimeLoader().load(missing);
    assert(!missingResult.ok());
    assert(missingResult.diagnostic().message.find("missing .arpui resource")
        != arp::core::pmr::String::npos);

    auto invalidStatus = packageDesc();
    invalidStatus.status = text("Backend Validated");
    const auto rejected = arp::ui::UiArpuiResourcePackageBuilder().build(invalidStatus);
    assert(!rejected.success);
    assert(
        rejected.diagnostics.find("invalid .arpui package status") != arp::core::pmr::String::npos);
}

void package_reader_rejects_unresolved_texture_dependency_and_hash_mismatch()
{
    auto package = arp::resource::InternalAssetPackageDesc{};
    package.name = text("broken-hud-ui-package");
    package.status = text("Contract Complete");
    const auto built = arp::ui::UiArpuiResourcePackageBuilder().build(packageDesc());
    assert(built.success);
    package.entries.push_back(built.entry);
    const auto unresolved = arp::resource::InternalAssetPackageWriter().write(
        package, "artifacts/ui_arpui_resource_package/unresolved");
    assert(!unresolved.success);
    assert(unresolved.diagnostics.find("unresolved package dependency")
        != arp::core::pmr::String::npos);

    package.entries.clear();
    package.entries.push_back(textureEntry());
    auto tampered = built.entry;
    tampered.contentHash ^= 0x55u;
    package.entries.push_back(tampered);
    const auto hashMismatch = arp::resource::InternalAssetPackageWriter().write(
        package, "artifacts/ui_arpui_resource_package/hash-mismatch");
    assert(!hashMismatch.success);
    assert(hashMismatch.diagnostics.find("metadata does not match envelope")
        != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    arpui_resource_package_declares_dependencies_hash_status_and_loads_runtime();
    package_loader_fails_deterministically_for_missing_resource_and_invalid_status();
    package_reader_rejects_unresolved_texture_dependency_and_hash_mismatch();
    return 0;
}
