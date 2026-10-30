#include "core/container/generation_pool.h"
#include "core/io/file_io.h"
#include "tests/fixtures/ui_bundle_fixture.h"
#include "ui/ui_font_source.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {

using namespace arp;
using Fixture = tests::UiBundleFixture;

enum class Kind { Atlas, Image, Fonts, Theme, Document };

const char* id(Kind kind)
{
    switch (kind) {
    case Kind::Atlas:
        return Fixture::atlasId;
    case Kind::Image:
        return Fixture::imageId;
    case Kind::Fonts:
        return Fixture::fontId;
    case Kind::Theme:
        return Fixture::themeId;
    case Kind::Document:
        return Fixture::documentId;
    }
    assert(false);
    return nullptr;
}

core::UniquePtr<resource::ResourceBase> candidate(Kind kind, const Fixture& fixture)
{
    switch (kind) {
    case Kind::Atlas:
        return core::makeUnique<ui::UiAtlasMetadataResource>(fixture.atlas());
    case Kind::Image:
        return core::makeUnique<ui::UiAtlasImageResource>(fixture.image());
    case Kind::Fonts:
        return core::makeUnique<ui::UiFontSetResource>(fixture.fonts());
    case Kind::Theme:
        return core::makeUnique<ui::UiThemeResource>(fixture.theme());
    case Kind::Document:
        return core::makeUnique<ui::UiDocumentResource>(fixture.document());
    }
    assert(false);
    return {};
}

void replace(core::pmr::String& value, std::string_view before, std::string_view after)
{
    const auto at = value.find(before);
    assert(at != core::pmr::String::npos);
    value.replace(at, before.size(), after.data(), after.size());
}

// A valid TGA allows an exact pixel edit without a second image encoder. The
// font change adds legal trailing SFNT padding and is also validated by FreeType.
resource::ResourceSourceSet sources(const core::pmr::Vector<std::uint8_t>& font,
    Kind kind,
    resource::ResourceFileOrigin origin,
    bool changed = false,
    bool invalid = false)
{
    core::pmr::String atlas = tests::uiFontMetadataFixture();
    replace(atlas, "images/atlas.png", "images/atlas.tga");
    core::pmr::String theme = tests::uiBundleThemeFixture();
    core::pmr::String document = tests::uiBundleDocumentFixture();
    auto image = tests::uiAtlasTgaBytes();
    auto fontBytes = font;
    if (changed) {
        switch (kind) {
        case Kind::Atlas:
            replace(atlas, "rect: { x: 0, y: 0, width: 32", "rect: { x: 0, y: 0, width: 16");
            break;
        case Kind::Image:
            image[18 + 2] = 173;
            break;
        case Kind::Fonts:
            fontBytes.insert(fontBytes.end(), 4, 0);
            break;
        case Kind::Theme:
            replace(theme, "0.25, 0.5, 0.75", "0.25, 0.125, 0.75");
            break;
        case Kind::Document:
            replace(document, "text: AV", "text: Hello");
            break;
        }
    }
    if (invalid) {
        switch (kind) {
        case Kind::Atlas:
            atlas = "!";
            break;
        case Kind::Image:
            image.clear();
            break;
        case Kind::Fonts:
            fontBytes.clear();
            break;
        case Kind::Theme:
            theme = "!";
            break;
        case Kind::Document:
            document = "!";
            break;
        }
    }
    resource::ResourceSourceSet result;
    const auto add = [&](const char* path, const void* data, std::size_t size) {
        resource::ResourceSourceBuffer buffer;
        buffer.path = path;
        buffer.pathScope = origin.pathScope;
        buffer.mount = origin.mount;
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        if (size)
            buffer.bytes.assign(bytes, bytes + size);
        assert(result.add(std::move(buffer)));
    };
    add("meta/atlas.arpui", atlas.data(), atlas.size());
    add("theme.arpui", theme.data(), theme.size());
    add("hud.arpui", document.data(), document.size());
    add("images/atlas.tga", image.data(), image.size());
    add("fonts/test.ttf", fontBytes.data(), fontBytes.size());
    return result;
}

resource::ResourceLoadContext context(const resource::ResourceSourceSet& sources)
{
    resource::ResourceLoadContext result;
    result.sources = &sources;
    result.requirePreloadedSources = true;
    result.maxDecodedBytes = Fixture::sourceBudget;
    return result;
}

void changedValue(const resource::ResourceManager& manager, Kind kind, std::size_t fontBytes)
{
    switch (kind) {
    case Kind::Atlas:
        assert(
            manager.find<ui::UiAtlasMetadataResource>(id(kind)).get()->data->slices[0].width == 16);
        break;
    case Kind::Image:
        assert(manager.find<ui::UiAtlasImageResource>(id(kind)).get()->data->rgba8[0] == 173);
        break;
    case Kind::Fonts: {
        const auto& face = manager.find<ui::UiFontSetResource>(id(kind)).get()->data->fonts[0];
        assert(face.source->bytes().size() == fontBytes + 4);
        assert(ui::UiFontFaceLoader().load(face).ok());
        break;
    }
    case Kind::Theme:
        assert(manager.find<ui::UiThemeResource>(id(kind)).get()->data->styleColors.at("body").g
            == 0.125f);
        break;
    case Kind::Document: {
        const auto& data = *manager.find<ui::UiDocumentResource>(id(kind)).get()->data;
        assert(data.document.nodes()[1].text == "Hello");
        assert(data.drawPlan.records()[1].text == "Hello");
        assert(data.bindings[1].text == "Hello");
        break;
    }
    }
}

void reload(const core::pmr::Vector<std::uint8_t>& bytes, Kind kind)
{
    core::GenerationPool<unsigned> provenance(1);
    const resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, provenance.emplace(1)};
    const Fixture fixture(origin);
    auto original = sources(bytes, kind, origin);
    resource::ResourceManager manager;
    fixture.loadChildren(manager, original);
    assert(renderer::ui::loadUiStartupBundleResource(manager, fixture.bundle()).success);
    const auto old = manager.find<resource::ResourceBase>(id(kind));
    const auto oldBundle = manager.find<renderer::ui::UiStartupBundleResource>(Fixture::bundleId);
    const auto* record = manager.recordSnapshot(id(kind));
    const auto oldHash = record->contentHash;
    const auto aliases = manager.resolveAlias(old.get()->reference().alias).resourceId;

    const auto same = manager.reloadResource(candidate(kind, fixture), context(original));
    assert(same.success && same.unchangedContent && !same.telemetry.committed);
    assert(old.ready() && old.get() == manager.find<resource::ResourceBase>(id(kind)).get());
    assert(manager.recordSnapshot(id(kind)) == record && oldBundle.ready());

    auto corrupt = sources(bytes, kind, origin, false, true);
    const auto failed = manager.reloadResource(candidate(kind, fixture), context(corrupt));
    assert(!failed.success && failed.telemetry.rolledBack && !failed.diagnostics.empty());
    assert(old.ready() && oldBundle.ready() && record->contentHash == oldHash);
    assert(manager.generationOf(id(kind)) == old.generation());

    auto edited = sources(bytes, kind, origin, true);
    const auto changed = manager.reloadResource(candidate(kind, fixture), context(edited));
    if (!changed.success)
        std::fprintf(stderr, "UI reload %s: %s\n", id(kind), changed.diagnostics.c_str());
    assert(changed.success && !changed.unchangedContent && changed.telemetry.committed);
    assert(record == manager.recordSnapshot(id(kind)) && record->contentHash != oldHash);
    assert(manager.generationOf(id(kind)) == old.generation() + 1);
    assert(old.stale() && !old.ready() && !old.get());
    assert(!oldBundle.ready() && oldBundle.state() == resource::ResourceState::Stale);
    assert(!changed.telemetry.invalidatedDependents.empty());
    changedValue(manager, kind, bytes.size());
    const auto current = manager.find<resource::ResourceBase>(id(kind));
    assert(manager.resolveAlias(current.get()->reference().alias).resourceId == aliases);
    const auto count = manager.retainedResourceCount();
    assert(count == 1);
    manager.releaseRetiredResources();
    assert(manager.retainedResourceCount() == 0 && current.ready());
}

std::uint64_t fingerprint(const resource::ResourceBase& value)
{
    core::StableHash64 hash;
    value.appendContentFingerprint(hash);
    return hash.value();
}

void identityContracts(const core::pmr::Vector<std::uint8_t>& bytes)
{
    core::GenerationPool<unsigned> provenance(1);
    const resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, provenance.emplace(1)};
    const Fixture fixture(origin);
    auto original = sources(bytes, Kind::Theme, origin);
    resource::ResourceManager manager;
    fixture.loadChildren(manager, original);
    const auto theme = manager.find<ui::UiThemeResource>(Fixture::themeId);
    auto copy = ui::UiThemeResource(fixture.theme());
    copy.data = core::makeShared<ui::UiThemeResourceData>(*theme.get()->data);
    for (const auto& dependency : theme.get()->dependencies())
        copy.addDependency(dependency);
    const auto themeHash = fingerprint(copy);
    copy.data->styleColors.rehash(73);
    copy.data->styleFonts.rehash(53);
    assert(fingerprint(copy) == themeHash);
    copy.data->styleColors.emplace("z", ui::UiControlColorValue{1, 0, 0, 1});
    copy.data->styleColors.emplace("a", ui::UiControlColorValue{0, 1, 0, 1});
    copy.data->styleFonts.emplace("z", "font.test");
    copy.data->styleFonts.emplace("a", "font.other");
    const auto dictionaryHash = fingerprint(copy);
    const auto body = copy.data->styleColors.at("body");
    copy.data->styleColors.clear();
    copy.data->styleFonts.clear();
    copy.data->styleColors.emplace("a", ui::UiControlColorValue{0, 1, 0, 1});
    copy.data->styleColors.emplace("body", body);
    copy.data->styleColors.emplace("z", ui::UiControlColorValue{1, 0, 0, 1});
    copy.data->styleFonts.emplace("a", "font.other");
    copy.data->styleFonts.emplace("body", "font.test");
    copy.data->styleFonts.emplace("z", "font.test");
    assert(fingerprint(copy) == dictionaryHash);
    // An unused token is still authored content, not merely a style projection.
    copy.data->theme.addToken({"unused", ui::UiThemeTokenValue::fromText("value")});
    assert(fingerprint(copy) != themeHash);

    ui::UiDocumentDesc document;
    document.canvas.canvasId = "a";
    document.canvas.contractVersion = "bc";
    const auto first = ui::UiDocument(document).stableHash();
    document.canvas.canvasId = "ab";
    document.canvas.contractVersion = "c";
    assert(ui::UiDocument(document).stableHash() != first);
    document.nodes.emplace_back();
    auto baseline = ui::UiDocument(document).stableHash();
    document.nodes[0].fontToken = "font.other";
    assert(ui::UiDocument(document).stableHash() != baseline);
    baseline = ui::UiDocument(document).stableHash();
    document.canvas.hasExplicitDpi = true;
    assert(ui::UiDocument(document).stableHash() != baseline);
    baseline = ui::UiDocument(document).stableHash();
    document.canvas.hasExplicitSize = true;
    assert(ui::UiDocument(document).stableHash() != baseline);

    const auto fonts = manager.find<ui::UiFontSetResource>(Fixture::fontId);
    auto fontCopy = ui::UiFontSetResource(fixture.fonts());
    fontCopy.data = core::makeShared<ui::UiFontSetResourceData>(*fonts.get()->data);
    for (const auto& dependency : fonts.get()->dependencies())
        fontCopy.addDependency(dependency);
    const auto fontHash = fingerprint(fontCopy);
    auto& face = fontCopy.data->fonts[0];
    auto sourceDesc = face.source->desc();
    sourceDesc.version = {};
    sourceDesc.maxBytes += 4;
    auto sameBytes = core::pmr::Vector<std::uint8_t>(bytes);
    core::pmr::String diagnostic;
    face.source = ui::UiFontSource::create(sourceDesc, std::move(sameBytes), diagnostic);
    assert(face.source && fingerprint(fontCopy) == fontHash);
    const auto atlas = manager.find<ui::UiAtlasMetadataResource>(Fixture::atlasId);
    auto atlasCopy = ui::UiAtlasMetadataResource(fixture.atlas());
    atlasCopy.data = core::makeShared<ui::UiAtlasMetadataResourceData>(*atlas.get()->data);
    const auto atlasHash = fingerprint(atlasCopy);
    atlasCopy.data->sourcePath = std::filesystem::u8path(u8"images/图集.tga");
    const auto unicodePathHash = fingerprint(atlasCopy);
    assert(unicodePathHash != atlasHash && unicodePathHash == fingerprint(atlasCopy));
    atlasCopy.addDependency({Fixture::fontId, "explicit.test.edge"});
    assert(fingerprint(atlasCopy) != unicodePathHash);

    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 16; ++i) {
        assert(fingerprint(fontCopy) == fontHash);
        assert(fingerprint(copy) != themeHash);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
}

void cycle(const core::pmr::Vector<std::uint8_t>& bytes)
{
    for (auto kind : {Kind::Atlas, Kind::Image, Kind::Fonts, Kind::Theme, Kind::Document})
        reload(bytes, kind);
    identityContracts(bytes);
}

} // namespace

int main(int argc, char** argv)
{
    const bool quick = argc == 2 && std::string_view(argv[1]) == "--quick";
    assert(argc == 1 || quick);
    auto font = core::readBinaryFile(ARP_FONT_IO_FIXTURE_PATH, Fixture::sourceBudget);
    assert(font.success && !font.bytes.empty());
    if (quick) {
        cycle(font.bytes);
        std::puts("UiContentFingerprint/1: five real unchanged/failed/changed reloads passed");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle(font.bytes);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(font.bytes);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf("UI content reload: 20 warmup +20 measured; core live bytes %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes));
}
