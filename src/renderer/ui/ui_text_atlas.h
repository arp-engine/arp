#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/render_frame.h"
#include "renderer/ui/ui_draw_plan.h"
#include "ui/ui_font.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace arp::renderer::ui {

struct UiTextAtlasBuildDesc {
    renderer::GuiAtlasTextureDesc baseAtlas;
    const arp::core::pmr::Vector<UiDrawCommand>* commands = nullptr;
    std::uint32_t maxWidth = 1024u;
    std::uint32_t maxHeight = 1024u;
};

class UiTextAtlasBuildResult final {
public:
    struct UiTextAtlasEntry {
        arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
        arp::core::pmr::String textureResourceId{&arp::core::defaultCpuMemoryResource()};
        arp::ui::LayoutRect sourceRect;
    };

    static UiTextAtlasBuildResult success(renderer::GuiAtlasTextureDesc atlas,
        arp::core::pmr::Vector<UiDrawCommand> commands,
        arp::core::pmr::Vector<UiTextAtlasEntry> entries,
        std::size_t generatedTextRunCount)
    {
        UiTextAtlasBuildResult result;
        result.ok_ = true;
        result.atlas_ = std::move(atlas);
        result.commands_ = std::move(commands);
        result.entries_ = std::move(entries);
        result.generatedTextRunCount_ = generatedTextRunCount;
        return result;
    }

    static UiTextAtlasBuildResult failure(arp::core::pmr::String diagnostics)
    {
        UiTextAtlasBuildResult result;
        result.ok_ = false;
        result.diagnostics_ = std::move(diagnostics);
        return result;
    }

    bool ok() const { return ok_; }

    const arp::core::pmr::String& diagnostics() const { return diagnostics_; }

    const renderer::GuiAtlasTextureDesc& atlas() const { return atlas_; }

    const arp::core::pmr::Vector<UiDrawCommand>& commands() const { return commands_; }

    std::size_t generatedTextRunCount() const { return generatedTextRunCount_; }

    std::size_t generatedDrawCommandCount() const { return commands_.size(); }

    bool exposesNativeBackendHandles() const { return false; }

    std::size_t perFrameSourceParsingCount() const { return 0u; }

    UiTextAtlasBuildResult resolve(const arp::core::pmr::Vector<UiDrawCommand>& commands) const
    {
        if (!ok_) {
            return failure(diagnostics_);
        }
        arp::core::pmr::Vector<UiDrawCommand> output{&arp::core::defaultCpuMemoryResource()};
        output.reserve(commands.size());
        for (const auto& command : commands) {
            if (command.kind != UiDrawCommandKind::Text) {
                output.push_back(command);
                continue;
            }
            const auto found =
                std::find_if(entries_.begin(), entries_.end(), [&](const UiTextAtlasEntry& entry) {
                    return entry.text == command.text;
                });
            if (found == entries_.end()) {
                arp::core::pmr::String message("UI text atlas cache is missing text run: ",
                    &arp::core::defaultCpuMemoryResource());
                message += command.text;
                return failure(std::move(message));
            }
            UiDrawCommand text = command;
            text.textureResourceId = found->textureResourceId;
            text.textureSourceRect = found->sourceRect;
            text.hasTextureSourceRect = true;
            output.push_back(std::move(text));
        }
        return success(atlas_, std::move(output), entries_, entries_.size());
    }

private:
    bool ok_ = false;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
    renderer::GuiAtlasTextureDesc atlas_;
    arp::core::pmr::Vector<UiDrawCommand> commands_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiTextAtlasEntry> entries_{&arp::core::defaultCpuMemoryResource()};
    std::size_t generatedTextRunCount_ = 0u;
};

class UiTextAtlasBuilder final {
public:
    UiTextAtlasBuildResult build(UiTextAtlasBuildDesc desc) const
    {
        if (desc.commands == nullptr) {
            return UiTextAtlasBuildResult::failure(
                text("UI text atlas build requires explicit draw commands."));
        }
        if (!desc.baseAtlas.valid()) {
            return UiTextAtlasBuildResult::failure(
                text("UI text atlas build requires a valid base atlas texture."));
        }
        if (desc.maxWidth < desc.baseAtlas.extent.width
            || desc.maxHeight < desc.baseAtlas.extent.height) {
            return UiTextAtlasBuildResult::failure(
                text("UI text atlas budget is smaller than the base atlas."));
        }

        auto font = arp::ui::loadDefaultUiFontFace();
        if (!font.ok()) {
            return UiTextAtlasBuildResult::failure(font.diagnostic().message);
        }

        arp::core::pmr::Vector<TextRun> runs{&arp::core::defaultCpuMemoryResource()};
        runs.reserve(desc.commands->size());
        std::uint32_t packedWidth = desc.baseAtlas.extent.width;
        std::uint32_t packedHeight = desc.baseAtlas.extent.height;
        std::uint32_t cursorX = 0u;
        std::uint32_t cursorY = desc.baseAtlas.extent.height;
        std::uint32_t rowHeight = 0u;

        for (const auto& command : *desc.commands) {
            if (command.kind != UiDrawCommandKind::Text || command.text.empty()) {
                continue;
            }
            arp::ui::UiFontRasterizeDesc rasterDesc;
            rasterDesc.face = font.face().desc;
            const auto sizePx = static_cast<std::uint32_t>(
                std::max(6.0f, std::min(24.0f, command.rect.height - 4.0f)));
            rasterDesc.face.sizePx = sizePx;
            rasterDesc.face.lineHeightPx =
                static_cast<std::uint32_t>(std::max<float>(sizePx + 4u, command.rect.height));
            rasterDesc.text = command.text;
            rasterDesc.originX = 0.0f;
            rasterDesc.originY = 0.0f;
            rasterDesc.maxX = std::max(1.0f, command.rect.width);
            rasterDesc.maxY = std::max(1.0f, command.rect.height);

            const auto rasterized = arp::ui::UiFontTextRasterizer().rasterize(rasterDesc);
            if (!rasterized.ok()) {
                return UiTextAtlasBuildResult::failure(rasterized.diagnostic().message);
            }
            if (rasterized.text().pixels.empty()) {
                continue;
            }

            const auto runWidth = static_cast<std::uint32_t>(std::max(
                1.0f, std::ceil(std::min(command.rect.width, rasterized.text().advanceX + 2.0f))));
            const auto runHeight =
                static_cast<std::uint32_t>(std::max(1.0f, std::ceil(command.rect.height)));
            if (runWidth > desc.maxWidth || runHeight > desc.maxHeight) {
                return UiTextAtlasBuildResult::failure(
                    text("UI text run exceeds the text atlas budget."));
            }
            if (cursorX + runWidth > desc.maxWidth) {
                cursorX = 0u;
                cursorY += rowHeight;
                rowHeight = 0u;
            }
            if (cursorY + runHeight > desc.maxHeight) {
                return UiTextAtlasBuildResult::failure(text("UI text atlas budget exhausted."));
            }

            TextRun run;
            run.sourceCommandIndex = runs.size();
            run.commandText = command.text;
            run.x = cursorX;
            run.y = cursorY;
            run.width = runWidth;
            run.height = runHeight;
            run.pixels = rasterized.text().pixels;
            runs.push_back(std::move(run));

            cursorX += runWidth;
            rowHeight = std::max(rowHeight, runHeight);
            packedWidth = std::max(packedWidth, cursorX);
            packedHeight = std::max(packedHeight, cursorY + rowHeight);
        }

        renderer::GuiAtlasTextureDesc atlas;
        atlas.resourceId = desc.baseAtlas.resourceId + "+ui_text_atlas";
        atlas.extent = {std::max(1u, packedWidth), std::max(1u, packedHeight)};
        atlas.srgb = desc.baseAtlas.srgb;
        atlas.rgba8.resize(static_cast<std::size_t>(atlas.extent.width)
                * static_cast<std::size_t>(atlas.extent.height) * 4u,
            0u);
        copyBaseAtlas(desc.baseAtlas, atlas);
        for (const auto& run : runs) {
            writeRun(run, atlas);
        }

        arp::core::pmr::Vector<UiTextAtlasBuildResult::UiTextAtlasEntry> entries{
            &arp::core::defaultCpuMemoryResource()};
        entries.reserve(runs.size());
        for (const auto& run : runs) {
            UiTextAtlasBuildResult::UiTextAtlasEntry entry;
            entry.text = run.commandText;
            entry.textureResourceId = arp::core::pmr::String(
                atlas.resourceId.c_str(), &arp::core::defaultCpuMemoryResource());
            entry.sourceRect = arp::ui::LayoutRect::fromOriginSize(
                static_cast<float>(run.x) / static_cast<float>(atlas.extent.width),
                static_cast<float>(run.y) / static_cast<float>(atlas.extent.height),
                static_cast<float>(run.width) / static_cast<float>(atlas.extent.width),
                static_cast<float>(run.height) / static_cast<float>(atlas.extent.height));
            entries.push_back(std::move(entry));
        }

        arp::core::pmr::Vector<UiDrawCommand> output{&arp::core::defaultCpuMemoryResource()};
        output.reserve(desc.commands->size());
        for (const auto& command : *desc.commands) {
            if (command.kind != UiDrawCommandKind::Text) {
                output.push_back(command);
                continue;
            }
            const auto found = std::find_if(entries.begin(),
                entries.end(),
                [&](const UiTextAtlasBuildResult::UiTextAtlasEntry& entry) {
                    return entry.text == command.text;
                });
            if (found == entries.end()) {
                continue;
            }
            UiDrawCommand text = command;
            text.textureResourceId = found->textureResourceId;
            text.textureSourceRect = found->sourceRect;
            text.hasTextureSourceRect = true;
            output.push_back(std::move(text));
        }

        return UiTextAtlasBuildResult::success(
            std::move(atlas), std::move(output), std::move(entries), runs.size());
    }

private:
    struct TextRun {
        std::size_t sourceCommandIndex = 0u;
        arp::core::pmr::String commandText{&arp::core::defaultCpuMemoryResource()};
        std::uint32_t x = 0u;
        std::uint32_t y = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        arp::core::pmr::Vector<arp::ui::UiFontRasterPixel> pixels{
            &arp::core::defaultCpuMemoryResource()};
    };

    static arp::core::pmr::String text(const char* value)
    {
        return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
    }

    static void copyBaseAtlas(
        const renderer::GuiAtlasTextureDesc& source, renderer::GuiAtlasTextureDesc& target)
    {
        for (std::uint32_t y = 0u; y < source.extent.height; ++y) {
            const auto sourceOffset =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(source.extent.width) * 4u;
            const auto targetOffset =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(target.extent.width) * 4u;
            std::copy(source.rgba8.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                source.rgba8.begin()
                    + static_cast<std::ptrdiff_t>(
                        sourceOffset + static_cast<std::size_t>(source.extent.width) * 4u),
                target.rgba8.begin() + static_cast<std::ptrdiff_t>(targetOffset));
        }
    }

    static void writeRun(const TextRun& run, renderer::GuiAtlasTextureDesc& atlas)
    {
        for (const auto& pixel : run.pixels) {
            const auto x = run.x + static_cast<std::uint32_t>(std::max(0.0f, pixel.x));
            const auto y = run.y + static_cast<std::uint32_t>(std::max(0.0f, pixel.y));
            const auto width = static_cast<std::uint32_t>(std::max(1.0f, pixel.width));
            const auto alpha =
                static_cast<std::uint8_t>(std::clamp(pixel.alpha, 0.0f, 1.0f) * 255.0f);
            for (std::uint32_t column = 0u; column < width; ++column) {
                if (x + column >= atlas.extent.width || y >= atlas.extent.height) {
                    continue;
                }
                const auto offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(atlas.extent.width)
                        + static_cast<std::size_t>(x + column))
                    * 4u;
                atlas.rgba8[offset + 0u] = 255u;
                atlas.rgba8[offset + 1u] = 255u;
                atlas.rgba8[offset + 2u] = 255u;
                atlas.rgba8[offset + 3u] = alpha;
            }
        }
    }
};

} // namespace arp::renderer::ui
