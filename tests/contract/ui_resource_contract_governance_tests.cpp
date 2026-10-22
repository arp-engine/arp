#include "core/memory/cpu_allocator.h"
#include "resource/resource_contract.h"
#include "ui/ui_resource_contracts.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "TODO.md")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
#endif
}

arp::core::pmr::String readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    std::ostringstream stream;
    stream << input.rdbuf();
    return arp::core::toPmrString(stream.str());
}

void requireToken(const arp::core::pmr::String& source, const char* token)
{
    assert(source.find(token) != arp::core::pmr::String::npos);
}

void rejectToken(const arp::core::pmr::String& source, const char* token)
{
    assert(source.find(token) == arp::core::pmr::String::npos);
}

void canonical_contracts_are_stable()
{
    using namespace arp::ui::contracts;
    assert(kTheme.type == "UiThemeResource");
    assert(kTheme.contractVersion == "ui.theme/v1");
    assert(kAtlasMetadata.type == "UiAtlasMetadataResource");
    assert(kAtlasMetadata.contractVersion == "ui.atlas/v2");
    assert(kAtlasImage.contractVersion == "ui.atlas.image/v1");
    assert(kAtlasTexture.contractVersion == "ui.atlas.texture/v1");
    assert(kFontSet.contractVersion == "ui.font-set/v1");
    assert(kDocument.contractVersion == "ui.document.resource/v1");
    assert(kArpuiDocumentContract == "ui.document/v1");
    assert(kStartupBundle.contractVersion == "ui.startup-bundle/v1");
    assert(kPikaVisibleStartup.contractVersion == "pika.visible.ui.startup/v1");
    assert(kPikaVisibleGraph.contractVersion == "pika.visible.ui.resource-graph/v1");
}

void status_parser_and_promotion_are_deterministic()
{
    using arp::resource::ResourceStatus;
    const auto contract = arp::resource::parseResourceStatus("Contract Complete");
    assert(contract.success);
    assert(contract.status == ResourceStatus::ContractComplete);
    assert(arp::resource::resourceStatusName(ResourceStatus::PrototypeComplete)
        == "Prototype Complete");

    const auto unknown = arp::resource::parseResourceStatus("almost ready");
    assert(!unknown.success);
    assert(unknown.diagnostics == "unknown resource status: almost ready");

    const auto promoted = arp::resource::validateResourceStatusPromotion(
        ResourceStatus::PrototypeComplete, ResourceStatus::BackendValidated);
    assert(promoted.success);
    const auto skipped = arp::resource::validateResourceStatusPromotion(
        ResourceStatus::ContractComplete, ResourceStatus::ProductionReady);
    assert(!skipped.success);
    assert(skipped.diagnostics
        == "invalid resource status promotion: Contract Complete -> Production Ready");
}

void contract_dependency_and_redirect_validation_is_deterministic()
{
    using namespace arp::ui::contracts;
    const auto mismatch = arp::resource::validateResourceContract(
        "UiAtlasImageResource", "ui.atlas-image/v1", kAtlasImage);
    assert(!mismatch.success);
    assert(mismatch.diagnostics
        == "resource contract version mismatch for UiAtlasImageResource: expected ui.atlas.image/v1, got ui.atlas-image/v1");

    const auto dependency =
        arp::resource::validateResourceDependencyUsage(kUsageAtlasImage, kUsageAtlasMetadata);
    assert(!dependency.success);
    assert(dependency.diagnostics
        == "resource dependency usage mismatch: expected ui.atlas.metadata, got ui.atlas.image");

    arp::resource::ResourceContractRedirect redirect;
    redirect.source = {"UiAtlasImageResource", "ui.atlas-image/v1"};
    redirect.target = kAtlasImage;
    redirect.status = arp::resource::ResourceStatus::LegacyFallback;
    const auto accepted = arp::resource::validateResourceContractRedirect(redirect);
    assert(accepted.success);

    redirect.status = arp::resource::ResourceStatus::PrototypeComplete;
    const auto rejected = arp::resource::validateResourceContractRedirect(redirect);
    assert(!rejected.success);
    assert(rejected.diagnostics == "resource contract redirect requires Legacy fallback status");
}

void docs_and_sources_use_canonical_vocabulary()
{
    const auto root = projectRoot();
    const auto readiness = readText(root / "docs/pika-ui-production-readiness.md");
    requireToken(readiness, "`UiAtlasImageResource` | `ui.atlas.image/v1`");
    requireToken(readiness, "`UiAtlasTextureResource` | `ui.atlas.texture/v1`");
    requireToken(readiness, "`UiDocumentResource` | `ui.document.resource/v1`");
    requireToken(readiness, "`.arpui` document contract `ui.document/v1`");
    requireToken(
        readiness, "`PikaVisibleUiResourceGraphResource` | `pika.visible.ui.resource-graph/v1`");

    const std::filesystem::path guardedSources[] = {
        root / "src/ui/ui_theme_resource.cpp",
        root / "src/ui/ui_font_set_resource.cpp",
        root / "src/ui/ui_document_resource.cpp",
        root / "src/ui/ui_atlas_image_resource.cpp",
        root / "src/renderer/ui/ui_atlas_texture_resource.cpp",
        root / "src/renderer/ui/ui_startup_bundle_resource.cpp",
        root / "games/pika/app/pika_visible_ui_startup_resource.cpp",
        root / "games/pika/app/visible_character_runtime.cpp",
    };
    for (const auto& path : guardedSources) {
        const auto source = readText(path);
        rejectToken(source, "status == \"Contract Complete\"");
        rejectToken(source, "status == \"Prototype Complete\"");
        rejectToken(source, "status == \"Backend Validated\"");
        rejectToken(source, "text(\"Contract Complete\")");
        rejectToken(source, "text(\"Prototype Complete\")");
        rejectToken(source, "text(\"Backend Validated\")");
    }

    const auto visibleRuntime = readText(root / "games/pika/app/visible_character_runtime.cpp");
    rejectToken(visibleRuntime, "text(\"ui.document/v1\")");
    rejectToken(visibleRuntime, "\"ui.atlas-image/v1\"");
    rejectToken(visibleRuntime, "\"ui.atlas-texture/v1\"");
}

} // namespace

int main()
{
    canonical_contracts_are_stable();
    status_parser_and_promotion_are_deterministic();
    contract_dependency_and_redirect_validation_is_deterministic();
    docs_and_sources_use_canonical_vocabulary();
    return 0;
}
