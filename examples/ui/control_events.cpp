#include "examples/ui/control_events.h"

#include <cassert>
#include <cstdio>

int main()
{
    arp::examples::ControlEventsExample example;
    auto loaded =
        example.load(std::filesystem::path(ARP_SOURCE_DIR) / "examples/ui/control_events.arpui");
    if (!loaded.ok()) {
        std::fprintf(stderr, "%s\n", loaded.diagnostic().message.c_str());
        return 1;
    }
    auto dispatched = example.runScenario();
    if (!dispatched.ok()) {
        std::fprintf(stderr, "%s\n", dispatched.diagnostic.message.c_str());
        return 1;
    }
    assert(example.button()->clicks == 1 && example.slider()->lastValue == 0.75f);
    assert(example.input()->value().text == "Typed through InputFrame");
    assert(example.button()->diagnostic.code == arp::ui::UiDiagnosticCode::None);
    assert(example.slider()->diagnostic.code == arp::ui::UiDiagnosticCode::None);
    assert(example.input()->diagnostic.code == arp::ui::UiDiagnosticCode::None);
    assert(example.runtime().unload().ok());
    std::puts(
        "PASS: YAML sourceId factories + Button/Slider/TextInput inheritance through ordered InputFrame");
}
