#include "core/file_io.h"
#include "core/yaml_contract.h"
#include "ui/ui_bitmap_font_resource.h"

#include <cassert>

int main()
{
    using namespace arp;
    constexpr const char* atlasId = "00000000-0000-4000-8000-000000000006";
    constexpr const char* fontId = "00000000-0000-4000-8000-000000000007";
    const core::pmr::String root = ARP_SOURCE_DIR "/tests/fixtures/scene_2d/";
    resource::ResourceManager manager;
    assert(manager
            .load<scene2d::SpriteAtlasResource>(
                {}, core::pmr::String(atlasId), root + "glyph.yaml", root + "glyph.png", 65536u)
            .success);
    const auto original =
        core::readBinaryFile(std::filesystem::path((root + "glyph.font.yaml").c_str()), 65536);
    assert(original.success);
    const auto parsed = core::YamlContract::Node::deserialize(core::pmr::String(
        reinterpret_cast<const char*>(original.bytes.data()), original.bytes.size()));
    auto run = [&](const core::YamlContract::Node& node,
                   std::size_t bytes,
                   std::uint32_t glyphs,
                   const char* diagnostic) {
        resource::ResourceManager isolated;
        assert(isolated
                .load<scene2d::SpriteAtlasResource>(
                    {}, core::pmr::String(atlasId), root + "glyph.yaml", root + "glyph.png", 65536u)
                .success);
        resource::ResourceSourceSet sources;
        const auto yaml = core::YamlContract::Node::serialize(node);
        resource::ResourceSourceBuffer buffer;
        buffer.path = "font-test.yaml";
        buffer.bytes.assign(yaml.begin(), yaml.end());
        assert(sources.add(std::move(buffer)));
        ui::UiBitmapFontResource font(
            core::pmr::String(fontId), "font-test.yaml", core::pmr::String(atlasId), bytes, glyphs);
        resource::ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        const auto result = font.load(isolated, context);
        if (diagnostic) {
            assert(!result.success && result.diagnostics == diagnostic && !font.valid()
                && !font.glyph(65));
        } else {
            assert(result.success && font.valid() && font.page() == 0 && font.sizePx() == 8);
            assert(font.fontToken() == "survivors-5x7" && !font.glyph(66));
            const auto* glyph = font.glyph(65);
            assert(glyph && glyph->advanceX == 6 && glyph->bearingX == 0 && glyph->bearingY == 0);
            assert(font.atlas().get()->slice(glyph->sliceName)->width == 6);
        }
    };
    const auto exactBytes = core::YamlContract::Node::serialize(parsed).size();
    run(parsed, exactBytes, 1, nullptr);
    run(parsed, exactBytes - 1, 1, "ARP-RESOURCE-SOURCE-BUDGET");
    auto changed = parsed;
    changed["cookProfile"] = "SdfCook/1";
    run(changed, 65536, 1, "UI-BITMAP-FONT-COOK-PROFILE");
    changed = parsed;
    changed["rgba8Sha256"] = core::pmr::String(64, '0').c_str();
    run(changed, 65536, 1, "UI-BITMAP-FONT-PIXEL-IDENTITY");
    changed = parsed;
    changed["glyphs"][0]["slice"] = "missing";
    run(changed, 65536, 1, "UI-BITMAP-FONT-SLICE-MISSING");
    changed = parsed;
    changed["glyphs"][0]["codepoint"] = 0xd800;
    run(changed, 65536, 1, "UI-BITMAP-FONT-CODEPOINT");
    changed = parsed;
    changed["glyphs"].get_value_ref<core::YamlContract::Node::sequence_type&>().push_back(
        changed["glyphs"][0]);
    run(changed, 65536, 1, "UI-BITMAP-FONT-GLYPH-BUDGET");
    run(changed, 65536, 2, "UI-BITMAP-FONT-CODEPOINT-DUPLICATE");
    changed = parsed;
    changed["glyphs"][0]["advanceX"] = 0;
    run(changed, 65536, 1, "UI-BITMAP-FONT-METRICS");
    assert(manager
            .load<ui::UiBitmapFontResource>({},
                core::pmr::String(fontId),
                root + "glyph.font.yaml",
                core::pmr::String(atlasId),
                65536u,
                1u)
            .success);
    const auto font = manager.find<ui::UiBitmapFontResource>(fontId);
    assert(font.ready() && font.get()->glyph(65));
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0; i < 100; ++i)
        assert(font.get()->glyph(65));
    assert(core::defaultCpuAllocator().stats().allocationCount == before);
    assert(manager.unload(atlasId).success);
    assert(!font.get()->valid() && !font.get()->glyph(65));
}
