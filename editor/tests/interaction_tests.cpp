#include "editor/app/game_preview.h"
#include "editor/editor_shell.h"
#include "editor/ui/editor_atlas_layout.h"
#include "ui/ui_font.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

int main()
{
    using namespace arp;
    editor::EditorShell shell;
    auto loaded =
        shell.load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui");
    if (!loaded.ok()) {
        std::fprintf(stderr, "%s\n", loaded.diagnostic().message.c_str());
        return 1;
    }
    assert(shell.runtime().canvas().findWidget(shell.id("version"))->desc().text == "ARP Editor");
    const auto documentLayout = shell.runtime().canvas().resolveLayout();
    for (const auto& node : documentLayout.resolvedWidgets())
        assert(shell.runtime().canvas().findWidget(node.widgetId)->desc().text.find_first_of("\r\n")
            == core::pmr::String::npos);
    assert(shell.selection() == "ReflectionProbe2");
    assert(shell.workspace() == "Game");
    assert(!shell.id("workspace_2D").valid());
    assert(shell.id("workspace_3D").valid());
    assert(!shell.id("workspace_AssetLib").valid());
    assert(shell.id("workspace_Game").valid() && shell.id("workspace_Script").valid());
    assert(shell.runtime().findControl(shell.id("projection"))->value().boolean);
    assert(shell.runtime().canvas().findWidget(shell.id("viewport"))->enabled());
    assert(!shell.runtime().canvas().findWidget(shell.id("viewport"))->desc().acceptsPointerInput);
    auto layout = shell.runtime().canvas().resolveLayout();
    assert(
        shell.runtime().canvas().hitTest(layout, {105, 95}).widgetId() == shell.id("scene_search"));
    assert(!shell.runtime().findControl(shell.id("interior"))->value().boolean);
    const auto* branchIcon =
        shell.runtime().canvas().findWidget(shell.id("branch_ReflectionProbes"));
    const auto* folderArrow = shell.runtime().canvas().findWidget(shell.id("folder_arrow_door"));
    assert(branchIcon && branchIcon->desc().controlType == ui::UiControlType::Image);
    assert(branchIcon->desc().textureResourceId == "icon-chevron-down");
    assert(folderArrow && folderArrow->desc().controlType == ui::UiControlType::Image);
    assert(folderArrow->desc().textureResourceId == "icon-chevron-right");
    const auto* folderIcon = shell.runtime().canvas().findWidget(shell.id("folder_icon_door"));
    assert(folderIcon);
    const auto arrowCenter =
        folderArrow->desc().localRect.y + folderArrow->desc().localRect.height * 0.5f;
    const auto folderCenter =
        folderIcon->desc().localRect.y + folderIcon->desc().localRect.height * 0.5f;
    assert(std::abs(arrowCenter - folderCenter) <= 1.0f);
    auto drawn = shell.drawCommands();
    assert(drawn.ok());
    assert(drawn.commands().size() > 250);
    assert(std::any_of(drawn.commands().begin(), drawn.commands().end(), [&](const auto& c) {
        return c.widgetId == shell.id("viewport")
            && c.kind == renderer::ui::UiDrawCommandKind::Image;
    }));
    const auto initialTick =
        std::find_if(drawn.commands().begin(), drawn.commands().end(), [](const auto& c) {
            return c.textureResourceId == "icon-check";
        });
    assert(initialTick != drawn.commands().end());
    assert(std::abs(initialTick->textureSourceRect.x
               - editor::editor_atlas::kCheck.x / float(editor::editor_atlas::kWidth))
        < 0.000001f);
    const auto roundedSelectedCount =
        std::count_if(drawn.commands().begin(), drawn.commands().end(), [&](const auto& c) {
            return c.widgetId == shell.id("node_ReflectionProbe2")
                && c.textureResourceId == "texture/editor-reference"
                && c.kind == renderer::ui::UiDrawCommandKind::Image;
        });
    assert(roundedSelectedCount == 9);
    const auto selectedBox = documentLayout.findWidget(shell.id("node_ReflectionProbe2"));
    assert(selectedBox);
    const auto selectedRightPatch =
        std::find_if(drawn.commands().begin(), drawn.commands().end(), [&](const auto& c) {
            return c.widgetId == shell.id("node_ReflectionProbe2")
                && c.textureResourceId == "texture/editor-reference"
                && std::abs(
                       c.rect.x + c.rect.width - (selectedBox->rect.x + selectedBox->rect.width))
                < 0.0001f;
        });
    assert(selectedRightPatch != drawn.commands().end());
    assert(std::abs(selectedRightPatch->textureSourceRect.x
               - (editor::editor_atlas::kRoundedSelection.x
                     + editor::editor_atlas::kRoundedSelection.width
                     - editor::editor_atlas::kRoundedCorner)
                   / float(editor::editor_atlas::kWidth))
        < 0.000001f);
    auto routed = shell.scenario();
    assert(routed.ok());
    assert(shell.selection() == "ReflectionProbe1");
    assert(shell.workspace() == "Game");
    assert(shell.inspectorScrollOffset() > 0.0f);
    assert(shell.runtime().findControl(shell.id("interior"))->value().boolean);
    assert(shell.runtime().findControl(shell.id("intensity"))->value().text == "1.5");
    input::InputFrame search;
    search.add(input::InputEvent::pointerMoved(105, 95));
    search.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
    search.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
    search.add(input::InputEvent::textInput("ReflectionProbe"));
    assert(shell.runtime().dispatchFrame(search, {0, 32}).ok());
    assert(!shell.runtime().canvas().findWidget(shell.id("node_Core"))->visible());
    assert(shell.runtime().canvas().findWidget(shell.id("node_ReflectionProbe1"))->visible());
    shell.activate("file_root");
    assert(shell.runtime().findControl(shell.id("files_path"))->value().text == "res://");
    shell.activate("dock_Signals");
    assert(!shell.runtime().canvas().findWidget(shell.id("properties"))->visible());
    assert(shell.drawCommands().ok());
    shell.activate("dock_Groups");
    assert(shell.runtime().canvas().findWidget(shell.id("groups_content"))->visible());
    assert(shell.drawCommands().ok());
    shell.activate("dock_Inspector");
    shell.activate("bottom_Output");
    assert(shell.outputOpen());
    assert(shell.runtime().findControl(shell.id("output_status"))->value().text
        == "Output dock opened");
    shell.activate("close_menu");
    assert(shell.diagnostic().code == ui::UiDiagnosticCode::None);
    editor::EditorShell scrolling;
    assert(scrolling.load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui")
            .ok());
    auto beforeScroll = scrolling.runtime().canvas().resolveLayout();
    assert(beforeScroll.ok());
    const auto* beforeTransform = beforeScroll.findWidget(scrolling.id("transform_header"));
    const auto* initialThumb = beforeScroll.findWidget(scrolling.id("inspector_scroll"));
    assert(beforeTransform && initialThumb);
    const float initialTransformY = beforeTransform->rect.y;
    const float initialThumbY = initialThumb->rect.y;
    input::InputFrame drag;
    drag.add(input::InputEvent::pointerMoved(1902.0f, initialThumbY + 4.0f));
    drag.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
    drag.add(input::InputEvent::pointerMoved(1902.0f, initialThumbY + 36.0f));
    drag.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
    assert(scrolling.runtime().dispatchFrame(drag, {0, 16}).ok());
    assert(scrolling.inspectorScrollOffset() > 0.0f);
    auto afterDrag = scrolling.runtime().canvas().resolveLayout();
    const auto* afterTransform = afterDrag.findWidget(scrolling.id("transform_header"));
    const auto* movedThumb = afterDrag.findWidget(scrolling.id("inspector_scroll"));
    assert(afterTransform && movedThumb);
    assert(afterTransform->rect.y < initialTransformY);
    assert(movedThumb->rect.y > initialThumbY);
    const auto clip = afterDrag.clipRect(scrolling.id("transform_header"));
    assert(clip.y >= 185.0f && clip.y + clip.height <= 1025.0f);
    input::InputFrame wheel;
    wheel.add(input::InputEvent::pointerMoved(1750.0f, 500.0f));
    wheel.add(input::InputEvent::mouseWheel(-12.0f));
    assert(scrolling.runtime().dispatchFrame(wheel, {0, 8}).ok());
    assert(scrolling.inspectorScrollOffset() > 0.0f);
    assert(scrolling.drawCommands().ok());
    editor::EditorShell hovering;
    assert(hovering.load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui")
            .ok());
    input::InputFrame hoverFrame;
    hoverFrame.add(input::InputEvent::pointerMoved(100.0f, 150.0f)); // Core scene row
    assert(hovering.runtime().dispatchFrame(hoverFrame, {0, 8}).ok());
    assert(hovering.runtime().hoveredControl() == hovering.id("node_Core"));
    const auto hoverDraw = hovering.drawCommands();
    assert(hoverDraw.ok());
    const auto roundedHoverCount =
        std::count_if(hoverDraw.commands().begin(), hoverDraw.commands().end(), [&](const auto& c) {
            return c.widgetId == hovering.id("node_Core")
                && c.textureResourceId == "texture/editor-reference"
                && c.kind == renderer::ui::UiDrawCommandKind::Image;
        });
    assert(roundedHoverCount == 9);
    const auto selectedPatch =
        std::find_if(drawn.commands().begin(), drawn.commands().end(), [&](const auto& c) {
            return c.widgetId == shell.id("node_ReflectionProbe2")
                && c.textureResourceId == "texture/editor-reference";
        });
    const auto hoverPatch =
        std::find_if(hoverDraw.commands().begin(), hoverDraw.commands().end(), [&](const auto& c) {
            return c.widgetId == hovering.id("node_Core")
                && c.textureResourceId == "texture/editor-reference";
        });
    assert(selectedPatch != drawn.commands().end() && hoverPatch != hoverDraw.commands().end());
    for (int channel = 0; channel < 4; ++channel)
        assert(std::abs(selectedPatch->color[channel] - hoverPatch->color[channel]) < 0.00001f);
    const auto tempRoot = std::filesystem::temp_directory_path()
        / ("arp_editor_fs_"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempRoot / "content" / "scripts");
    for (int i = 0; i < 12; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "scroll_%02d.arp", i);
        std::ofstream(tempRoot / name) << "pass\n";
    }
    for (int i = 0; i < 8; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "extra_%02d", i);
        std::filesystem::create_directory(tempRoot / name);
    }
    const auto sampleScript = tempRoot / "content" / "scripts" / "demo.arp";
    const auto secondScript = tempRoot / "content" / "scripts" / "second.arp";
    std::ofstream(secondScript) << "pass\n";
    const std::string cjkText = "\xE4\xB8\xAD\xE6\x96\x87";
    {
        std::ofstream file(sampleScript, std::ios::binary);
        file
            << "# syntax test " << cjkText
            << "\nclass SampleType:\n    pass\ndef test_value(value: SampleType) -> bool:\n    var amount: "
               "int = 42\n    return amount\n";
        for (int i = 0; i < 100; ++i)
            file << "# overflow line " << i << "\n";
    }
    editor::EditorShell fileEditor;
    assert(fileEditor
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", tempRoot)
            .ok());
    // The preview host must stay idle without a process and reset shell launch
    // state after a missing executable, allowing another explicit attempt.
    editor::GamePreviewHost preview;
    assert(preview.update(nullptr, 0, 0, false) == editor::GamePreviewHost::Update::Idle);
    assert(!preview.requestStop());
    for (int attempt = 0; attempt < 2; ++attempt) {
        assert(!preview.synchronize(fileEditor, 1440, 810, false));
        fileEditor.activate("run_play");
        assert(preview.synchronize(fileEditor, 1440, 810, false));
        assert(!fileEditor.gamePreviewRunning());
        assert(!preview.synchronize(fileEditor, 1440, 810, false));
    }
    fileEditor.activate("bottom_Output");

    ui::UiFontFaceDesc codeMetricFace;
    codeMetricFace.tokenId = "font.code";
    codeMetricFace.family = "Consolas";
    codeMetricFace.sourcePath = "C:/Windows/Fonts/consola.ttf";
    codeMetricFace.sizePx = 48;
    codeMetricFace.lineHeightPx = 48;
    codeMetricFace.renderMode = "sdf";
    ui::UiFontRasterizeDesc codeMetricDesc;
    codeMetricDesc.face = codeMetricFace;
    codeMetricDesc.text = "MMMM";
    codeMetricDesc.originX = 0.0f;
    codeMetricDesc.originY = 0.0f;
    codeMetricDesc.maxX = 4096.0f;
    codeMetricDesc.maxY = 128.0f;
    const auto codeMetric = ui::UiFontTextRasterizer().rasterize(codeMetricDesc);
    assert(codeMetric.ok());
    fileEditor.setCodeColumnWidth(codeMetric.text().advanceX / 4.0f * (16.0f / 48.0f));
    assert(fileEditor.fileEntryCount() == 21);
    const auto initialFileBarDraw = fileEditor.drawCommands();
    assert(initialFileBarDraw.ok());
    const auto fileScrollbarBoxes = std::count_if(initialFileBarDraw.commands().begin(),
        initialFileBarDraw.commands().end(),
        [&](const auto& command) {
            return command.widgetId == fileEditor.id("files_view")
                && command.kind == renderer::ui::UiDrawCommandKind::Box;
        });
    assert(fileScrollbarBoxes == 2);
    platform::WindowInputEvent fileThumbDown;
    fileThumbDown.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
    fileThumbDown.mouseButton = platform::WindowMouseButton::Left;
    fileThumbDown.x = 276.0f;
    fileThumbDown.y = 800.0f;
    platform::WindowInputEvent fileThumbMove;
    fileThumbMove.kind = platform::WindowInputEvent::Kind::PointerMoved;
    fileThumbMove.x = 276.0f;
    fileThumbMove.y = 989.0f;
    platform::WindowInputEvent fileThumbUp = fileThumbDown;
    fileThumbUp.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
    fileEditor.handlePlatformInput(fileThumbDown);
    fileEditor.handlePlatformInput(fileThumbMove);
    fileEditor.handlePlatformInput(fileThumbUp);
    const auto maxFileOffset = fileEditor.fileEntryCount() - 11;
    assert(fileEditor.fileScrollOffset() == maxFileOffset);
    platform::WindowInputEvent fileWheel;
    fileWheel.kind = platform::WindowInputEvent::Kind::MouseWheel;
    fileWheel.x = 120.0f;
    fileWheel.y = 800.0f;
    fileWheel.wheelDelta = 1.0f;

    input::InputFrame scrolledFileClick;
    scrolledFileClick.add(input::InputEvent::pointerMoved(80.0f, 738.0f));
    scrolledFileClick.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
    scrolledFileClick.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
    assert(fileEditor.runtime().dispatchFrame(scrolledFileClick, {0, 16}).ok());
    assert(fileEditor.selectedFilePath() == tempRoot / "scroll_01.arp");
    const auto selectedFileDraw = fileEditor.drawCommands();
    assert(selectedFileDraw.ok());
    assert(std::any_of(selectedFileDraw.commands().begin(),
        selectedFileDraw.commands().end(),
        [&](const auto& command) {
            return command.widgetId == fileEditor.id("folder_door")
                && command.kind == renderer::ui::UiDrawCommandKind::Image
                && std::abs(command.color[0] - 69.0f / 255.0f) < 0.001f;
        }));
    fileEditor.handlePlatformInput(fileWheel);
    assert(fileEditor.fileScrollOffset() == maxFileOffset - 3);
    for (int i = 0; i < 4; ++i)
        fileEditor.handlePlatformInput(fileWheel);
    assert(fileEditor.fileScrollOffset() == 0);
    assert(fileEditor.runtime()
               .canvas()
               .findWidget(fileEditor.id("folder_icon_door"))
               ->desc()
               .textureResourceId
        == "icon-folder");
    assert(fileEditor.runtime()
               .canvas()
               .findWidget(fileEditor.id("asset_icon_0"))
               ->desc()
               .textureResourceId
        == "icon-folder");
    assert(fileEditor.runtime()
               .canvas()
               .findWidget(fileEditor.id("asset_icon_2"))
               ->desc()
               .textureResourceId
        == "icon-workspace-script");
    const auto folderIconDraw = fileEditor.drawCommands();
    assert(folderIconDraw.ok());
    assert(std::any_of(folderIconDraw.commands().begin(),
        folderIconDraw.commands().end(),
        [&](const auto& command) {
            return command.widgetId == fileEditor.id("asset_icon_0")
                && command.kind == renderer::ui::UiDrawCommandKind::Image
                && command.textureResourceId == "icon-folder";
        }));
    fileEditor.activate("folder_door");
    assert(fileEditor.runtime().findControl(fileEditor.id("files_path"))->value().text
        == "res://content/");
    fileEditor.activate("folder_door");
    assert(fileEditor.runtime().findControl(fileEditor.id("files_path"))->value().text
        == "res://content/scripts/");
    fileEditor.activate("folder_door");
    fileEditor.activate("folder_door");
    assert(fileEditor.scriptPath() == sampleScript);
    assert(fileEditor.workspace() == "Script" && !fileEditor.scriptDirty());
    const auto* gameToolbar = fileEditor.runtime().canvas().findWidget(fileEditor.id("toolbar"));
    const auto* gameTabRow = fileEditor.runtime().canvas().findWidget(fileEditor.id("scene_tabs"));
    const auto* scriptViewportInitial =
        fileEditor.runtime().canvas().findWidget(fileEditor.id("viewport"));
    assert(gameToolbar && !gameToolbar->visible());
    assert(gameTabRow && gameTabRow->visible());
    assert(scriptViewportInitial && scriptViewportInitial->desc().localRect.y == 77.0f);
    const auto initialScriptTabs = fileEditor.drawCommands();
    assert(initialScriptTabs.ok());
    assert(std::any_of(initialScriptTabs.commands().begin(),
        initialScriptTabs.commands().end(),
        [&](const auto& command) {
            return command.widgetId == fileEditor.id("scene_tabs")
                && command.kind == renderer::ui::UiDrawCommandKind::Text
                && command.text == "demo.arp";
        }));
    auto scriptDraw = fileEditor.drawCommands();
    assert(scriptDraw.ok());
    assert(std::any_of(
        scriptDraw.commands().begin(), scriptDraw.commands().end(), [](const auto& command) {
            return command.kind == renderer::ui::UiDrawCommandKind::Text
                && command.styleId == "code-keyword" && command.text == "def";
        }));
    assert(std::any_of(
        scriptDraw.commands().begin(), scriptDraw.commands().end(), [&](const auto& command) {
            return command.kind == renderer::ui::UiDrawCommandKind::Text
                && command.styleId == "code-comment" && command.fontToken == "font.code.cjk"
                && std::string(command.text.data(), command.text.size()) == cjkText;
        }));
    const auto* cjkComment = [&]() -> const renderer::ui::UiDrawCommand* {
        const auto found = std::find_if(
            scriptDraw.commands().begin(), scriptDraw.commands().end(), [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.styleId == "code-comment" && command.fontToken == "font.code.cjk";
            });
        return found == scriptDraw.commands().end() ? nullptr : &*found;
    }();
    const auto* latinComment = [&]() -> const renderer::ui::UiDrawCommand* {
        const auto found = std::find_if(
            scriptDraw.commands().begin(), scriptDraw.commands().end(), [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.styleId == "code-comment" && command.fontToken == "font.code";
            });
        return found == scriptDraw.commands().end() ? nullptr : &*found;
    }();
    assert(cjkComment && latinComment && cjkComment->rect.y == latinComment->rect.y - 4.0f);
    const auto hasCodeType = [&](const char* token) {
        return std::any_of(
            scriptDraw.commands().begin(), scriptDraw.commands().end(), [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.styleId == "code-type" && command.text == token;
            });
    };
    assert(hasCodeType("SampleType"));
    assert(hasCodeType("bool"));
    assert(hasCodeType("int"));
    const auto findScriptToken = [&](const char* token) -> const renderer::ui::UiDrawCommand* {
        const auto found = std::find_if(
            scriptDraw.commands().begin(), scriptDraw.commands().end(), [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == token;
            });
        return found == scriptDraw.commands().end() ? nullptr : &*found;
    };
    const auto* defToken = findScriptToken("def");
    const auto* nameToken = findScriptToken("test_value");
    assert(defToken && nameToken);
    const auto parenIt = std::find_if(
        scriptDraw.commands().begin(), scriptDraw.commands().end(), [&](const auto& command) {
            return command.kind == renderer::ui::UiDrawCommandKind::Text
                && command.styleId == "code-default"
                && std::abs(command.rect.y - nameToken->rect.y) < 0.01f
                && command.text.find('(') != core::pmr::String::npos;
        });
    const auto* parenToken = parenIt == scriptDraw.commands().end() ? nullptr : &*parenIt;
    assert(parenToken);
    const float codeAdvance = codeMetric.text().advanceX / 4.0f * (16.0f / 48.0f);
    assert(std::abs((nameToken->rect.x - defToken->rect.x) - codeAdvance * 4.0f) < 0.01f);
    const auto parenOffset = parenToken->text.find('(');
    const float parenX = parenToken->rect.x + static_cast<float>(parenOffset) * codeAdvance;
    assert(std::abs((parenX - nameToken->rect.x) - codeAdvance * 10.0f) < 0.01f);
    const auto codeLayout = fileEditor.runtime().canvas().resolveLayout();
    assert(codeLayout.ok());
    const auto* scriptViewport = codeLayout.findWidget(fileEditor.id("viewport"));
    assert(scriptViewport);
    assert(std::abs((defToken->rect.y - (scriptViewport->rect.y + 34.0f + 66.0f)) - 4.0f) < 0.01f);
    const std::string originalScript(
        fileEditor.scriptText().data(), fileEditor.scriptText().size());
    auto scriptView = [&]() {
        return std::string_view(fileEditor.scriptText().data(), fileEditor.scriptText().size());
    };
    auto sendKey = [&](platform::WindowInputEvent::Kind kind, platform::WindowKey key) {
        platform::WindowInputEvent event;
        event.kind = kind;
        event.key = key;
        fileEditor.handlePlatformInput(event);
    };
    auto sendText = [&](const char* value) {
        platform::WindowInputEvent event;
        event.kind = platform::WindowInputEvent::Kind::TextInput;
        event.text = value;
        fileEditor.handlePlatformInput(event);
    };
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::LeftControl);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::A);
    assert(fileEditor.scriptSelectionStart() == 0
        && fileEditor.scriptSelectionEnd() == originalScript.size());
    sendKey(platform::WindowInputEvent::Kind::KeyUp, platform::WindowKey::LeftControl);
    sendText("one atomic edit");
    assert(scriptView() == "one atomic edit");
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::LeftControl);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Z);
    assert(scriptView() == originalScript); // one undo restores the whole replaced document
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Y);
    assert(scriptView() == "one atomic edit");
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Z);
    assert(scriptView() == originalScript);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::LeftShift);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Z);
    assert(scriptView() == "one atomic edit");
    sendKey(platform::WindowInputEvent::Kind::KeyUp, platform::WindowKey::LeftShift);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Z);
    assert(scriptView() == originalScript);
    sendKey(platform::WindowInputEvent::Kind::KeyUp, platform::WindowKey::LeftControl);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::End);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::LeftShift);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::ArrowLeft);
    assert(fileEditor.scriptSelectionEnd() - fileEditor.scriptSelectionStart() == 1);
    sendKey(platform::WindowInputEvent::Kind::KeyUp, platform::WindowKey::LeftShift);
    sendText("!");
    assert(scriptView() == originalScript.substr(0, originalScript.size() - 1) + "!");
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::LeftControl);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Z);
    assert(scriptView() == originalScript);
    sendKey(platform::WindowInputEvent::Kind::KeyUp, platform::WindowKey::LeftControl);
    // Double-click selects the whole identifier under the pointer.
    auto clickCode = [&](float x, float y) {
        platform::WindowInputEvent down;
        down.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
        down.mouseButton = platform::WindowMouseButton::Left;
        down.x = x;
        down.y = y;
        platform::WindowInputEvent up = down;
        up.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
        fileEditor.handlePlatformInput(down);
        fileEditor.handlePlatformInput(up);
    };
    const float syntaxWordX = 354.0f + 3.5f * codeAdvance;
    const float firstCodeY = scriptViewport->rect.y + 34.0f;
    clickCode(syntaxWordX, firstCodeY);
    clickCode(syntaxWordX, firstCodeY);
    assert(scriptView().substr(fileEditor.scriptSelectionStart(),
               fileEditor.scriptSelectionEnd() - fileEditor.scriptSelectionStart())
        == "syntax");
    platform::WindowInputEvent dragDown;
    dragDown.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
    dragDown.mouseButton = platform::WindowMouseButton::Left;
    dragDown.x = 354.0f;
    dragDown.y = firstCodeY;
    platform::WindowInputEvent dragMove;
    dragMove.kind = platform::WindowInputEvent::Kind::PointerMoved;
    dragMove.x = 354.0f + 3.5f * codeAdvance;
    dragMove.y = firstCodeY;
    platform::WindowInputEvent dragUp = dragDown;
    dragUp.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
    fileEditor.handlePlatformInput(dragDown);
    fileEditor.handlePlatformInput(dragMove);
    fileEditor.handlePlatformInput(dragUp);
    assert(fileEditor.scriptSelectionEnd() - fileEditor.scriptSelectionStart() == 4);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::End);
    // Preserve the legacy two-byte fixture exactly; this is not a Unicode literal.
    sendText("\xD6\xD0");
    assert(scriptView().find("\xD6\xD0") != std::string_view::npos);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Backspace);
    assert(scriptView().find("\xD6\xD0") == std::string_view::npos);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::LeftControl);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Z);
    assert(scriptView().find("\xD6\xD0") != std::string_view::npos);
    sendKey(platform::WindowInputEvent::Kind::KeyDown, platform::WindowKey::Z);
    assert(scriptView() == originalScript);
    sendKey(platform::WindowInputEvent::Kind::KeyUp, platform::WindowKey::LeftControl);
    platform::WindowInputEvent endKey;
    endKey.kind = platform::WindowInputEvent::Kind::KeyDown;
    endKey.key = platform::WindowKey::End;
    fileEditor.handlePlatformInput(endKey);
    platform::WindowInputEvent typed;
    typed.kind = platform::WindowInputEvent::Kind::TextInput;
    typed.text = " # edited";
    fileEditor.handlePlatformInput(typed);
    assert(fileEditor.scriptDirty());
    platform::WindowInputEvent controlDown;
    controlDown.kind = platform::WindowInputEvent::Kind::KeyDown;
    controlDown.key = platform::WindowKey::LeftControl;
    platform::WindowInputEvent saveKey;
    saveKey.kind = platform::WindowInputEvent::Kind::KeyDown;
    saveKey.key = platform::WindowKey::S;
    fileEditor.handlePlatformInput(controlDown);
    fileEditor.handlePlatformInput(saveKey);
    assert(!fileEditor.scriptDirty());
    {
        std::ifstream file(sampleScript, std::ios::binary);
        const std::string saved(
            (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        assert(saved.find("# edited") != std::string::npos);
    }
    platform::WindowInputEvent controlUp;
    controlUp.kind = platform::WindowInputEvent::Kind::KeyUp;
    controlUp.key = platform::WindowKey::LeftControl;
    fileEditor.handlePlatformInput(controlUp);
    fileEditor.handlePlatformInput(controlDown);
    platform::WindowInputEvent undoSaved;
    undoSaved.kind = platform::WindowInputEvent::Kind::KeyDown;
    undoSaved.key = platform::WindowKey::Z;
    fileEditor.handlePlatformInput(undoSaved);
    assert(fileEditor.scriptDirty());
    platform::WindowInputEvent redoSaved = undoSaved;
    redoSaved.key = platform::WindowKey::Y;
    fileEditor.handlePlatformInput(redoSaved);
    assert(!fileEditor.scriptDirty());
    fileEditor.handlePlatformInput(controlUp);
    const auto overflowDraw = fileEditor.drawCommands();
    assert(overflowDraw.ok());
    const auto scriptScrollbarBoxes = std::count_if(
        overflowDraw.commands().begin(), overflowDraw.commands().end(), [&](const auto& command) {
            return command.widgetId == fileEditor.id("viewport")
                && command.kind == renderer::ui::UiDrawCommandKind::Box
                && std::abs(command.rect.width - 6.0f) < 0.01f;
        });
    assert(scriptScrollbarBoxes == 2);
    const auto overflowLayout = fileEditor.runtime().canvas().resolveLayout();
    const auto* overflowViewport = overflowLayout.findWidget(fileEditor.id("viewport"));
    assert(overflowViewport);
    const float scrollTrackY = overflowViewport->rect.y + 34.0f;
    const float scrollTrackHeight = overflowViewport->rect.height - 58.0f;
    const auto visibleCodeLines =
        static_cast<std::size_t>((overflowViewport->rect.height - 42.0f) / 22.0f);
    const auto totalCodeLines = static_cast<std::size_t>(
        std::count(fileEditor.scriptText().begin(), fileEditor.scriptText().end(), '\n') + 1);
    const float thumbHeight = std::max(32.0f,
        scrollTrackHeight * static_cast<float>(visibleCodeLines)
            / static_cast<float>(totalCodeLines));
    platform::WindowInputEvent scriptThumbDown;
    scriptThumbDown.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
    scriptThumbDown.mouseButton = platform::WindowMouseButton::Left;
    scriptThumbDown.x = overflowViewport->rect.x + overflowViewport->rect.width - 5.0f;
    scriptThumbDown.y = scrollTrackY + thumbHeight * 0.5f;
    platform::WindowInputEvent scriptThumbMove;
    scriptThumbMove.kind = platform::WindowInputEvent::Kind::PointerMoved;
    scriptThumbMove.x = scriptThumbDown.x;
    scriptThumbMove.y = scrollTrackY + scrollTrackHeight - 1.0f;
    platform::WindowInputEvent scriptThumbUp = scriptThumbDown;
    scriptThumbUp.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
    fileEditor.handlePlatformInput(scriptThumbDown);
    fileEditor.handlePlatformInput(scriptThumbMove);
    fileEditor.handlePlatformInput(scriptThumbUp);
    assert(fileEditor.scriptScrollLine() == totalCodeLines - visibleCodeLines);
    platform::WindowInputEvent scriptWheel;
    scriptWheel.kind = platform::WindowInputEvent::Kind::MouseWheel;
    scriptWheel.x = overflowViewport->rect.x + 100.0f;
    scriptWheel.y = overflowViewport->rect.y + 100.0f;
    scriptWheel.wheelDelta = 1.0f;
    fileEditor.handlePlatformInput(scriptWheel);
    assert(fileEditor.scriptScrollLine() + 3 == totalCodeLines - visibleCodeLines);
    assert(fileEditor.openScriptFile(secondScript));
    assert(fileEditor.openScriptCount() == 2 && fileEditor.scriptPath() == secondScript);
    auto switchTab = [&](float x) {
        platform::WindowInputEvent down;
        down.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
        down.mouseButton = platform::WindowMouseButton::Left;
        down.x = x;
        down.y = 60.0f;
        auto up = down;
        up.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
        fileEditor.handlePlatformInput(down);
        fileEditor.handlePlatformInput(up);
    };
    platform::WindowInputEvent appendSecond;
    appendSecond.kind = platform::WindowInputEvent::Kind::TextInput;
    appendSecond.text = "# dirty";
    fileEditor.handlePlatformInput(appendSecond);
    assert(fileEditor.scriptDirty());
    switchTab(300.0f);
    assert(fileEditor.scriptPath() == sampleScript && !fileEditor.scriptDirty());
    assert(fileEditor.scriptText().find("# edited") != core::pmr::String::npos);
    switchTab(440.0f);
    assert(fileEditor.scriptPath() == secondScript && fileEditor.scriptDirty());
    switchTab(557.0f);
    assert(fileEditor.openScriptCount() == 2 && fileEditor.scriptDirty());
    fileEditor.handlePlatformInput(controlDown);
    fileEditor.handlePlatformInput(saveKey);
    fileEditor.handlePlatformInput(controlUp);
    assert(!fileEditor.scriptDirty());
    switchTab(557.0f);
    assert(fileEditor.openScriptCount() == 1 && fileEditor.scriptPath() == sampleScript);
    switchTab(419.0f);
    assert(fileEditor.openScriptCount() == 0 && fileEditor.workspace() == "Game");
    assert(fileEditor.runtime().canvas().findWidget(fileEditor.id("toolbar"))->visible());
    std::error_code cleanupError;
    std::filesystem::remove_all(tempRoot, cleanupError);
    assert(!cleanupError);
    assert(shell.drawCommands().ok());
    std::printf(
        "PASS: editor selection, filesystem, script editing, controls and draggable inspector "
        "scrolling (%zu controls)\n",
        shell.runtime().controlCount());
}
