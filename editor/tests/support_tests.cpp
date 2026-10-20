#include "core/filesystem_path.h"
#include "core/utf8_cursor.h"
#include "editor/workspace/editor_presentation_state.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/game_preview_model.h"
#include "editor/workspace/project_browser_model.h"
#include "editor/workspace/script_document_session.h"
#include "ui/scrollbar_geometry.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <fstream>
#include <string>

int main()
{
    using namespace arp;
    editor::GamePreviewModel preview;
    using PreviewRequest = editor::GamePreviewModel::Request;
    assert(preview.requestStart());
    assert(!preview.requestStart());
    assert(preview.consumeRequest() == PreviewRequest::Start);
    assert(preview.starting());
    assert(preview.requestStop());
    assert(preview.consumeRequest() == PreviewRequest::Stop);
    preview.setState(false, false);
    assert(!preview.running() && !preview.starting());
    assert(!preview.requestStop());
    editor::GamePreviewModel pendingPreview;
    assert(pendingPreview.requestStart());
    assert(pendingPreview.requestStop());
    assert(pendingPreview.consumeRequest() == PreviewRequest::Stop);
    assert(pendingPreview.starting());

    editor::EditorPresentationState presentation;
    assert(presentation.selection() == "ReflectionProbe2");
    assert(presentation.workspace() == "Game" && presentation.dock() == "Inspector");
    presentation.select("Camera3D");
    presentation.setWorkspace("Script");
    presentation.setDock("Signals");
    assert(presentation.selection() == "Camera3D" && presentation.workspace() == "Script");
    assert(presentation.dock() == "Signals");
    presentation.registerSource(
        17, core::pmr::String("node_Camera3D", &core::defaultCpuMemoryResource()));
    assert(presentation.sourceFor(17) == "node_Camera3D");
    assert(presentation.toggleDisabledMask("layers_2"));
    assert(presentation.isDisabledMask("layers_2"));
    assert(!presentation.toggleDisabledMask("layers_2"));
    assert(!presentation.isDisabledMask("layers_2"));
    assert(presentation.advanceUpdateMode() == 1);
    assert(presentation.advanceUpdateMode() == 2);
    assert(!presentation.outputOpen());
    presentation.openOutput();
    assert(presentation.outputOpen());
    presentation.toggleMenu();
    assert(presentation.menuOpen());
    presentation.closeMenu();
    assert(!presentation.menuOpen());

    editor::EditorSceneDocument sceneDocument;
    auto* initialObject = sceneDocument.scene().findObject(sceneDocument.selectedObject());
    assert(initialObject && initialObject->name() == "MeshInstance");
    assert(initialObject->components().size() == 2);
    assert(sceneDocument.setSelectedTranslation(1.0f, 2.0f, 3.0f));
    assert(initialObject->transform().localTransform().translation.x == 1.0f);
    assert(sceneDocument.addComponent("MeshComponent") != nullptr);
    assert(initialObject->components().size() == 3);
    assert(sceneDocument.removeLastSelectedComponent());
    assert(initialObject->components().size() == 2);
    assert(!sceneDocument.addComponent("UnknownComponent"));
    assert(!sceneDocument.renameSelected(""));
    assert(sceneDocument.destroySelected());
    assert(sceneDocument.scene().findObject(sceneDocument.selectedObject()) != nullptr);

    const std::string_view mixed = "A\xE4\xB8\xAD\xF0\x9F\x98\x80";
    assert(core::Utf8Cursor::next(mixed, 0) == 1);
    assert(core::Utf8Cursor::next(mixed, 1) == 4);
    assert(core::Utf8Cursor::next(mixed, 4) == mixed.size());
    assert(core::Utf8Cursor::previous(mixed, mixed.size()) == 4);
    assert(core::Utf8Cursor::previous(mixed, 4) == 1);
    assert(core::Utf8Cursor::previous(mixed, 1) == 0);
    assert(core::Utf8Cursor::previous(mixed, 999) == 4);
    const auto han = core::Utf8Cursor::at(mixed, 1);
    assert(han.valid && han.value == 0x4e2du && han.byteLength == 3);
    assert(core::Utf8Cursor::at(mixed, 4).value == 0x1f600u);
    assert(!core::Utf8Cursor::at("\xC0\xAF", 0).valid);
    assert(!core::Utf8Cursor::at("\xED\xA0\x80", 0).valid);
    assert(!core::Utf8Cursor::at("\xE4\xB8", 0).valid);
    assert(core::Utf8Cursor::next({}, 0) == 0);
    assert(core::Utf8Cursor::previous({}, 0) == 0);

    const auto root = std::filesystem::current_path() / "project";
    assert(core::FilePath::contains(root, root));
    assert(core::FilePath::contains(root, root / "scripts/main.arp"));
    assert(!core::FilePath::contains(root, root.parent_path() / "project-other/main.arp"));
    assert(!core::FilePath::contains(root, (root / "../outside.arp").lexically_normal()));
    assert(core::FilePath::utf8(std::filesystem::u8path("\xE4\xB8\xAD.arp")) == "\xE4\xB8\xAD.arp");

    assert(!ui::ScrollbarGeometry::thumb(10, 100, 3, 3, 0, 8).visible);
    assert(!ui::ScrollbarGeometry::thumb(10, 100, 9, 0, 0, 8).visible);
    assert(!ui::ScrollbarGeometry::thumb(10, 0, 9, 3, 0, 8).visible);
    const auto first = ui::ScrollbarGeometry::thumb(10, 100, 100, 10, 0, 20);
    const auto last = ui::ScrollbarGeometry::thumb(10, 100, 100, 10, 999, 20);
    assert(first.visible && first.maxOffset == 90 && first.height == 20);
    assert(first.y == 10 && last.y + last.height == 110);
    const auto oversize = ui::ScrollbarGeometry::thumb(0, 10, 100, 10, 90, 20);
    assert(oversize.height == 10 && oversize.y == 0);

    const auto tempRoot = std::filesystem::temp_directory_path()
        / ("arp-editor-browser-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project = tempRoot / "project";
    std::filesystem::create_directories(project / "Alpha");
    std::filesystem::create_directories(project / "Sub" / "Child");
    std::filesystem::create_directories(tempRoot / "outside");
    std::ofstream(project / "beta.arp").put('x');
    std::ofstream(project / "Gamma.txt").put('x');

    editor::ProjectBrowserModel browser;
    browser.setRoot(project);
    assert(browser.refresh());
    assert(browser.entries().size() == 4);
    assert(browser.entries()[0].directory && browser.entries()[0].name == "Alpha");
    assert(browser.entries()[1].directory && browser.entries()[1].name == "Sub");
    browser.setSearch("aLp");
    assert(browser.visibleEntryIndices().size() == 1);
    assert(browser.entries()[browser.visibleEntryIndices()[0]].name == "Alpha");
    browser.setSearch("");

    using Navigation = editor::ProjectBrowserModel::NavigationResult;
    assert(browser.navigate(project / "Alpha") == Navigation::Navigated);
    assert(browser.navigate(project / "Sub") == Navigation::Navigated);
    assert(browser.back());
    assert(browser.currentDirectory() == std::filesystem::weakly_canonical(project / "Alpha"));
    assert(browser.back());
    assert(browser.currentDirectory() == std::filesystem::weakly_canonical(project));
    assert(browser.forward());
    assert(browser.currentDirectory() == std::filesystem::weakly_canonical(project / "Alpha"));
    assert(browser.navigate(tempRoot / "outside") == Navigation::Rejected);
    assert(browser.currentDirectory() == std::filesystem::weakly_canonical(project / "Alpha"));
    std::filesystem::remove_all(tempRoot);

    editor::ScriptDocumentSession documents;
    editor::ScriptDocumentSession::Document firstDocument;
    firstDocument.path = "first.arp";
    const auto firstIndex = documents.append(std::move(firstDocument));
    const auto firstId = documents.documents()[firstIndex].id;
    editor::ScriptDocumentSession::Document secondDocument;
    secondDocument.path = "second.arp";
    const auto secondIndex = documents.append(std::move(secondDocument));
    const auto secondId = documents.documents()[secondIndex].id;
    assert(firstId != 0 && secondId > firstId);
    assert(documents.setActiveIndex(secondIndex));
    assert(documents.activeId() == secondId);
    documents.active()->text = "base";
    documents.active()->savedText = "base";
    documents.active()->cursor = 4;
    documents.active()->anchor = 4;
    using EditResult = editor::ScriptDocumentSession::EditResult;
    assert(documents.replaceActiveRange(4, 0, "!", 64) == EditResult::Applied);
    assert(documents.active()->text == "base!" && documents.active()->dirty());
    assert(documents.undoActive());
    assert(documents.active()->text == "base" && !documents.active()->dirty());
    assert(documents.activeCursor() == 4 && documents.activeAnchor() == 4);
    assert(documents.redoActive());
    assert(documents.active()->text == "base!" && documents.active()->dirty());
    assert(documents.erase(firstIndex));
    assert(documents.activeIndex() == 0 && documents.activeId() == secondId);
    assert(!documents.setActiveIndex(1));

    const auto fileRoot = std::filesystem::temp_directory_path()
        / ("arp-editor-documents-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto documentRoot = fileRoot / "project";
    std::filesystem::create_directories(documentRoot);
    std::filesystem::create_directories(fileRoot / "outside");
    const auto crlfPath = documentRoot / "crlf.arp";
    const auto secondPath = documentRoot / "second.arp";
    const auto outsidePath = fileRoot / "outside" / "outside.arp";
    {
        std::ofstream output(crlfPath, std::ios::binary);
        output.write("alpha\r\nbeta\r\n", 13);
    }
    std::ofstream(secondPath, std::ios::binary) << "second\nline\n";
    std::ofstream(outsidePath, std::ios::binary) << "outside\n";
    editor::ScriptDocumentSession fileSession;
    using FileCode = editor::ScriptDocumentSession::FileResultCode;
    const auto openedCrlf = fileSession.openFile(documentRoot, crlfPath);
    assert(openedCrlf.code == FileCode::Opened);
    assert(fileSession.setActiveIndex(openedCrlf.documentIndex));
    assert(fileSession.active()->text == "alpha\nbeta\n");
    assert(fileSession.active()->newline == "\r\n" && !fileSession.active()->dirty());
    fileSession.setActiveSelection(3, 1);
    assert(
        fileSession.replaceActiveRange(1, 2, "XYZ", editor::ScriptDocumentSession::maxScriptBytes)
        == EditResult::Applied);
    assert(fileSession.activeCursor() == 4 && fileSession.activeAnchor() == 4);
    assert(fileSession.undoActive());
    assert(fileSession.active()->text == "alpha\nbeta\n");
    assert(fileSession.activeCursor() == 3 && fileSession.activeAnchor() == 1);
    assert(fileSession.redoActive());
    assert(fileSession.active()->text == "aXYZha\nbeta\n");
    const auto openedSecond = fileSession.openFile(documentRoot, secondPath);
    assert(openedSecond.code == FileCode::Opened);
    assert(fileSession.setActiveIndex(openedSecond.documentIndex));
    fileSession.setActiveSelection(2, 5);
    assert(fileSession.setActiveIndex(openedCrlf.documentIndex));
    assert(fileSession.activeCursor() == 4 && fileSession.activeAnchor() == 4);
    assert(fileSession.openFile(documentRoot, crlfPath).code == FileCode::AlreadyOpen);
    assert(fileSession.openFile(documentRoot, outsidePath).code == FileCode::InvalidPath);
    const auto tooLargePath = documentRoot / "too_large.arp";
    {
        std::ofstream output(tooLargePath, std::ios::binary);
        core::pmr::String oversized(editor::ScriptDocumentSession::maxScriptBytes + 1,
            'x',
            &core::defaultCpuMemoryResource());
        output.write(oversized.data(), static_cast<std::streamsize>(oversized.size()));
    }
    assert(fileSession.openFile(documentRoot, tooLargePath).code == FileCode::TooLarge);
    const auto saved = fileSession.saveActiveFile(documentRoot);
    assert(saved.code == FileCode::Saved && !fileSession.active()->dirty());
    {
        std::ifstream input(crlfPath, std::ios::binary);
        const std::string bytes(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        assert(bytes == "aXYZha\r\nbeta\r\n");
    }
    editor::ScriptDocumentSession reopened;
    const auto reopenedResult = reopened.openFile(documentRoot, crlfPath);
    assert(reopenedResult.code == FileCode::Opened);
    assert(reopened.setActiveIndex(reopenedResult.documentIndex));
    assert(reopened.active()->text == "aXYZha\nbeta\n");
    assert(reopened.active()->newline == "\r\n" && !reopened.active()->dirty());
    std::filesystem::remove_all(fileRoot);
}
