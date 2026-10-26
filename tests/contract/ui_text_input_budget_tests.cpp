#include "tests/fixtures/ui_control_event_fixture.h"
#include "ui/ui_document.h"

#include <cstdio>
#include <fstream>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::ui;
using F = arp::tests::UiControlEventFixture;

class ObservedInput final : public TextInput {
public:
    unsigned notifications = 0;

protected:
    void onValueChanged(const UiControlValue&, const UiControlValue&) override { ++notifications; }
};

// YAML remains an independent input to the same runtime contract.
core::pmr::String document(std::string_view metadata)
{
    // clang-format off
    core::pmr::String yaml = R"yaml(type: ui.document
contractVersion: ui.document/v1
canvas: {id: input-budget, width: 320, height: 180, dpi: 96}
root:
  id: root
  type: Panel
  layer: root
  rect: {x: 0, y: 0, width: 320, height: 180}
  style: root
  state: enabled
  event: none
  children:
    - id: input
      type: TextInput
      layer: input
      rect: {x: 4, y: 4, width: 100, height: 24}
      style: input
      state: enabled
      event: none
)yaml";
    // clang-format on
    yaml += metadata;
    return yaml;
}

void runtimeBoundaries()
{
    for (std::uint32_t budget : {64u, 128u, 512u}) {
        Runtime runtime("bounded-text", F::canvas());
        auto desc = F::control(UiControlType::TextInput);
        desc.maxTextBytes = budget;
        desc.initialValue = UiControlValue::fromText(core::pmr::String(budget + 1, 'x'));
        assert(!runtime.createControl(desc, runtime.rootId()).ok() && runtime.controlCount() == 0);
        desc.initialValue = UiControlValue::fromText(core::pmr::String(budget, 'x'));
        auto object = core::makeUnique<ObservedInput>();
        auto* observed = object.get();
        const auto created = runtime.createControl(std::move(object), desc, runtime.rootId());
        assert(created.ok());
        const auto id = created.widgetId();
        const auto original = runtime.findControl(id)->value().text;
        assert(runtime.findControlObject(id)->desc().maxTextBytes == budget);
        assert(!runtime
                .setValue(id,
                    UiControlValue::fromText(core::pmr::String(budget + 1, 'y')),
                    UiNotifyPolicy::Notify)
                .ok());
        assert(runtime.findControl(id)->value().text == original);
        assert(observed->notifications == 0);
        assert(runtime.requestFocus(id).ok());
        assert(!runtime.dispatch(UiEvent::textInput("x")).ok());
        assert(runtime.findControl(id)->value().text == original);
        assert(!runtime
                .interact(UiControlInteraction::editText(id, core::pmr::String(budget + 1, 'y')))
                .ok());
        assert(runtime.findControl(id)->value().text == original);
        for (const auto& invalid : {core::pmr::String("\x80"),
                 core::pmr::String("\xe4\xb8"),
                 core::pmr::String("a\nb"),
                 core::pmr::String("a\rb"),
                 core::pmr::String("a\0b", 3)}) {
            assert(!runtime.setValue(id, UiControlValue::fromText(invalid)).ok());
            assert(!runtime.interact(UiControlInteraction::editText(id, invalid)).ok());
            assert(!runtime.dispatch(UiEvent::textInput(invalid)).ok());
            assert(runtime.findControl(id)->value().text == original);
            desc.initialValue = UiControlValue::fromText(invalid);
            assert(!runtime.createControl(desc, runtime.rootId()).ok());
        }
        auto* input = static_cast<TextInput*>(runtime.findControlObject(id));
        UiControlValue detached = UiControlValue::fromText("retained");
        assert(!input->editText(detached, core::pmr::String(budget + 1, 'x')).ok());
        assert(detached.text == "retained");
        const core::pmr::String unicode = core::pmr::String(budget - 3, 'x') + u8"中";
        assert(input->editText(detached, unicode).ok());
        assert(runtime.setValue(id, detached).ok());
        assert(runtime.dispatch(UiEvent::backspace()).ok());
        assert(runtime.findControl(id)->value().text == core::pmr::String(budget - 3, 'x'));
        assert(runtime.dispatch(UiEvent::textInput(u8"中")).ok());
        assert(runtime.findControl(id)->value().text == unicode);
        assert(observed->notifications == 2);
    }
    Runtime runtime("bad-budget", F::canvas());
    auto desc = F::control(UiControlType::TextInput);
    desc.maxTextBytes = 0;
    assert(!runtime.createControl(desc, runtime.rootId()).ok());
    desc = F::control(UiControlType::Label);
    desc.maxTextBytes = 512;
    assert(!runtime.createControl(desc, runtime.rootId()).ok());
}

void yamlContract()
{
    UiArpuiRuntimeLoadDesc desc;
    desc.sourceYaml = document("");
    auto loaded = UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok());
    assert(loaded.runtime()
               .findControlObject(loaded.widgetIdForSourceNodeId("input"))
               ->desc()
               .maxTextBytes
        == 64);
    const auto oldHash = loaded.document().stableHash();
    desc.sourceYaml = document("      maxTextBytes: 512\n");
    loaded = UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok() && loaded.document().stableHash() != oldHash);
    const auto id = loaded.widgetIdForSourceNodeId("input");
    assert(loaded.runtime().findControlObject(id)->desc().maxTextBytes == 512);
    assert(
        loaded.runtime().setValue(id, UiControlValue::fromText(core::pmr::String(512, 'x'))).ok());
    for (const auto* value : {"0", "-1", "4294967296", "true", "'512'", "1.5", "null"}) {
        desc.sourceYaml = document(core::pmr::String("      maxTextBytes: ") + value + '\n');
        assert(!UiArpuiRuntimeLoader().load(desc).ok());
    }
    desc.sourceYaml = document("      maxTextBytes: 512\n");
    const auto at = desc.sourceYaml.find("type: TextInput");
    desc.sourceYaml.replace(at, 15, "type: Label");
    assert(!UiArpuiRuntimeLoader().load(desc).ok());
}

void irRoundTrip()
{
    // clang-format off
    const core::pmr::String yaml = R"yaml(type: ArpUiAuthoringIR
contractVersion: arpui.ir.v1
canvas: {id: budget-ir, width: 320, height: 180, dpi: 96}
nodes:
  - id: 1
    parent: 0
    type: Panel
    layer: root
    rect: {x: 0, y: 0, width: 320, height: 180}
    style: root
    state: enabled
    event: none
  - id: 2
    parent: 1
    type: TextInput
    layer: input
    rect: {x: 4, y: 4, width: 100, height: 24}
    style: input
    state: enabled
    event: none
    maxTextBytes: 512
)yaml";
    // clang-format on
    const std::filesystem::path path = "artifacts/ui_text_budget/input.arpui.ir.yaml";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(yaml.data(), yaml.size());
        assert(stream.good());
    }
    UiArpuiIrBuildDesc ir;
    ir.path = path;
    const auto built = UiArpuiIrBuilder().build(ir);
    assert(built.ok() && built.documentYaml().find("maxTextBytes: 512") != core::pmr::String::npos);
    UiArpuiRuntimeLoadDesc desc;
    desc.sourceYaml = built.documentYaml();
    auto loaded = UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok());
    const auto id = loaded.widgetIdForSourceNodeId("node_2");
    assert(loaded.runtime().findControlObject(id)->desc().maxTextBytes == 512);
    assert(
        loaded.runtime().setValue(id, UiControlValue::fromText(core::pmr::String(512, 'x'))).ok());
    assert(
        !loaded.runtime().setValue(id, UiControlValue::fromText(core::pmr::String(513, 'x'))).ok());
    DocumentDesc modern;
    modern.canvas = loaded.document().canvas();
    NodeDesc input;
    input.nodeId = NodeId::fromValue(1);
    input.controlType = UiControlType::TextInput;
    input.maxTextBytes = 128;
    modern.nodes.push_back(input);
    const Document document(std::move(modern));
    assert(document.nodes()[0].maxTextBytes == 128);
    std::filesystem::remove(path);
}

void lifecycle()
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            UiArpuiRuntimeLoadDesc desc;
            desc.sourceYaml = document("      maxTextBytes: 512\n");
            auto loaded = UiArpuiRuntimeLoader().load(desc);
            assert(loaded.ok());
            const auto id = loaded.widgetIdForSourceNodeId("input");
            assert(loaded.runtime().requestFocus(id).ok());
            assert(loaded.runtime().dispatch(UiEvent::textInput(core::pmr::String(512, 'x'))).ok());
            assert(!loaded.runtime().dispatch(UiEvent::textInput("overflow")).ok());
        }
        const auto released = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = released;
        assert(released.liveBytes == baseline.liveBytes);
        assert(released.allocationCount - released.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS text input budget CPU smoke: 20 warmed load/input/reject/destroy cycles; core live bytes/count delta = 0");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle();
    else {
        assert(argc == 1);
        runtimeBoundaries();
        yamlContract();
        irRoundTrip();
    }
}
