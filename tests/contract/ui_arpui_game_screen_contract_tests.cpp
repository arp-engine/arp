#include "ui/ui_document.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::ui::UiGameScreenDesc validScreen()
{
    arp::ui::UiGameScreenDesc screen;
    screen.screenId = text("pika.main_menu");
    screen.viewId = text("main_menu.default");
    screen.stateId = text("state.menu");
    screen.safeAreaPolicy = arp::ui::UiGameSafeAreaPolicy::RespectPlatformSafeArea;
    screen.modalLayer = arp::ui::UiGameModalLayer::Base;

    arp::ui::UiGameInputAction startAction;
    startAction.actionId = text("input.accept");
    startAction.commandRole = text("command.start_game");
    screen.inputActions.push_back(std::move(startAction));

    arp::ui::UiGameCommandBinding startCommand;
    startCommand.commandRole = text("command.start_game");
    startCommand.gameBinding = text("pika.session.start");
    screen.commandBindings.push_back(std::move(startCommand));

    arp::ui::UiGameDataBinding healthBinding;
    healthBinding.bindingId = text("binding.player.health");
    healthBinding.gameBinding = text("pika.player.health");
    healthBinding.visibilityCondition = text("state.in_game");
    screen.dataBindings.push_back(std::move(healthBinding));

    arp::ui::UiGameNavigationEdge edge;
    edge.fromViewId = text("main_menu.default");
    edge.toViewId = text("settings.default");
    edge.inputAction = text("input.open_settings");
    screen.navigation.push_back(std::move(edge));

    arp::ui::UiGameFocusScope focus;
    focus.scopeId = text("main_menu.focus");
    focus.defaultViewId = text("main_menu.default");
    screen.focusScopes.push_back(std::move(focus));

    arp::ui::UiGameTransition transition;
    transition.fromStateId = text("state.menu");
    transition.toStateId = text("state.in_game");
    transition.commandRole = text("command.start_game");
    screen.transitions.push_back(std::move(transition));
    return screen;
}

void valid_game_screen_contract_records_explicit_semantics()
{
    arp::ui::UiGameScreenDocument document;
    document.addScreen(validScreen());

    const auto result = arp::ui::UiGameScreenValidator().validate(document);
    assert(result.ok());
    assert(document.screens().size() == 1u);
    assert(document.stableHash() == arp::ui::UiGameScreenDocument(document).stableHash());
}

void diagnostics_are_deterministic_for_invalid_game_screens()
{
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.screenId.clear();
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiGameScreenDiagnosticCode::UnknownScreenOrState);
    }
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.commandBindings.clear();
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiGameScreenDiagnosticCode::MissingCommandBinding);
    }
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.inputActions[0].actionId = text("accept");
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiGameScreenDiagnosticCode::InvalidInputAction);
    }
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.navigation[0].toViewId = screen.navigation[0].fromViewId;
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiGameScreenDiagnosticCode::CyclicNavigation);
    }
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.modalLayer = arp::ui::UiGameModalLayer::Invalid;
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiGameScreenDiagnosticCode::ModalLayerConflict);
    }
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.dataBindings[0].gameBinding.clear();
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(result.diagnostic().code == arp::ui::UiGameScreenDiagnosticCode::MissingGameBinding);
    }
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.safeAreaPolicy = arp::ui::UiGameSafeAreaPolicy::Invalid;
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(
            result.diagnostic().code == arp::ui::UiGameScreenDiagnosticCode::InvalidSafeAreaPolicy);
    }
    {
        arp::ui::UiGameScreenDocument document;
        auto screen = validScreen();
        screen.allowLayerNameSemanticInference = true;
        document.addScreen(std::move(screen));
        const auto result = arp::ui::UiGameScreenValidator().validate(document);
        assert(!result.ok());
        assert(result.diagnostic().code
            == arp::ui::UiGameScreenDiagnosticCode::LayerNameSemanticInference);
    }
}

} // namespace

int main()
{
    valid_game_screen_contract_records_explicit_semantics();
    diagnostics_are_deterministic_for_invalid_game_screens();
    return 0;
}
