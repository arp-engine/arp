#pragma once

#include "core/cpu_allocator.h"
#include "core/math.h"
#include "scene/component.h"
#include "scene/ids.h"

#include <cstddef>

namespace arp::scene {
class Scene;
}

namespace arp::input {

enum class InputKey {
    W,
    A,
    S,
    D,
    Space,
    // Appended neutral keys bridged from the platform window event pump
    // (platform/input_bridge.h). Existing enumerators keep their order.
    Escape,
    Enter,
    Tab,
    Backspace,
    Delete,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    // Full WindowKey mirror: every non-Unknown WindowKey maps 1:1 through
    // windowKeyToInputKey, so the bridge never drops a mapped platform key.
    B,
    C,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    T,
    U,
    V,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    PageUp,
    PageDown,
    Home,
    End,
    Insert,
    RightAlt,
    LeftSuper,
    RightSuper,
    CapsLock,
    NumLock,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,
    Numpad0,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,
    NumpadAdd,
    NumpadSubtract,
    NumpadMultiply,
    NumpadDivide,
    NumpadDecimal,
    NumpadEnter,
    PrintScreen,
    ScrollLock,
    Pause,
    Menu,
    NonUsBackslash,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
};

enum class InputMouseButton {
    Left,
    Right,
    Middle,
};

enum class InputDiagnosticCode {
    None,
    MissingActionMap,
    DuplicateBinding,
    InvalidPlayerSlot,
    InvalidEventConsumption,
    InvalidEventMetadata,
};

struct InputDiagnostic {
    InputDiagnosticCode code = InputDiagnosticCode::None;
    arp::core::pmr::String message;
};

class InputResult {
public:
    static InputResult success();
    static InputResult failure(InputDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == InputDiagnosticCode::None; }

    const InputDiagnostic& diagnostic() const { return diagnostic_; }

private:
    explicit InputResult(InputDiagnostic diagnostic);

    InputDiagnostic diagnostic_;
};

enum class InputEventKind {
    KeyPressed,
    KeyReleased,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseMoved,
    MouseWheel,
    PointerMoved,
    TextInput,
    FocusLost,
};

struct InputEvent {
    InputEventKind kind = InputEventKind::KeyPressed;
    uint64_t windowId = 0;
    bool hasPosition = false;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    InputKey key = InputKey::W;
    InputMouseButton mouseButton = InputMouseButton::Left;
    float x = 0.0f;
    float y = 0.0f;
    arp::core::pmr::String text;

    static InputEvent keyPressed(InputKey key);
    static InputEvent keyReleased(InputKey key);
    static InputEvent mouseButtonPressed(InputMouseButton button);
    static InputEvent mouseButtonReleased(InputMouseButton button);
    static InputEvent mouseMoved(float deltaX, float deltaY);
    static InputEvent mouseWheel(float delta);
    static InputEvent pointerMoved(float x, float y);
    static InputEvent textInput(arp::core::pmr::String text);
};

class InputFrame {
public:
    void add(InputEvent event);

    const arp::core::pmr::Vector<InputEvent>& events() const { return events_; }

private:
    arp::core::pmr::Vector<InputEvent> events_;
};

enum class InputBindingKind {
    KeyButton,
    Axis1D,
    MouseDeltaX,
    MouseDeltaY,
    MouseWheel,
};

struct InputBinding {
    InputBindingKind kind = InputBindingKind::KeyButton;
    InputKey keyCode = InputKey::W;
    InputKey negativeKey = InputKey::A;
    InputKey positiveKey = InputKey::D;

    static InputBinding key(InputKey key);
    static InputBinding axis1D(InputKey negativeKey, InputKey positiveKey);
    static InputBinding mouseDeltaX();
    static InputBinding mouseDeltaY();
    static InputBinding mouseWheel();
};

class InputActionMap {
public:
    explicit InputActionMap(arp::core::pmr::String id);

    const arp::core::pmr::String& id() const { return id_; }

    InputResult bindButton(arp::core::pmr::String action, InputBinding binding);
    InputResult bindAxis1D(arp::core::pmr::String action, InputBinding binding);

    const arp::core::pmr::UnorderedMap<arp::core::pmr::String, InputBinding>& bindings() const
    {
        return bindings_;
    }

private:
    InputResult bind(arp::core::pmr::String action, InputBinding binding);

    arp::core::pmr::String id_;
    arp::core::pmr::UnorderedMap<arp::core::pmr::String, InputBinding> bindings_;
};

class InputActionState {
public:
    static InputActionState success();
    static InputActionState failure(InputDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == InputDiagnosticCode::None; }

    const InputDiagnostic& diagnostic() const { return diagnostic_; }

    bool pressed(const arp::core::pmr::String& action) const;
    bool held(const arp::core::pmr::String& action) const;
    bool released(const arp::core::pmr::String& action) const;
    float axis(const arp::core::pmr::String& action) const;

    bool exposesRawKeycodes() const { return false; }

private:
    friend class InputSystem;

    struct ButtonState {
        bool pressed = false;
        bool held = false;
        bool released = false;
    };

    explicit InputActionState(InputDiagnostic diagnostic);
    void setButton(arp::core::pmr::String action, ButtonState state);
    void setAxis(arp::core::pmr::String action, float value);

    InputDiagnostic diagnostic_;
    arp::core::pmr::UnorderedMap<arp::core::pmr::String, ButtonState> buttons_;
    arp::core::pmr::UnorderedMap<arp::core::pmr::String, float> axes_;
};

struct InputPointerSnapshot {
    float x = 0.0f;
    float y = 0.0f;
    bool hasPosition = false;
    bool primaryPressed = false;
    bool primaryHeld = false;
    bool primaryReleased = false;
};

class InputConsumption {
public:
    static InputConsumption none();
    static InputConsumption pointer();

    void consumePointer();
    void consumeKeyboard();
    void consumeText();

    bool pointerConsumed() const { return pointerConsumed_; }

    bool keyboardConsumed() const { return keyboardConsumed_; }

    bool textConsumed() const { return textConsumed_; }

    bool anyConsumed() const { return pointerConsumed_ || keyboardConsumed_ || textConsumed_; }

private:
    bool pointerConsumed_ = false;
    bool keyboardConsumed_ = false;
    bool textConsumed_ = false;
};

struct InputSystemDesc {
    size_t maxPlayers = 1;
    uint64_t windowId = 0;
};

class InputSystem {
public:
    explicit InputSystem(InputSystemDesc desc);

    InputResult registerActionMap(InputActionMap map);
    InputResult bindPlayerActionMap(size_t playerSlot, const arp::core::pmr::String& actionMapId);
    InputResult beginFrame(const InputFrame& frame);
    InputResult beginFrame(
        const InputFrame& frame, const arp::core::pmr::Vector<InputConsumption>& eventConsumption);
    InputResult applyConsumption(const InputConsumption& consumption);
    InputActionState actionState(size_t playerSlot) const;
    InputPointerSnapshot pointer() const;

    const arp::core::pmr::String& textInput() const { return textInput_; }

    bool hasTextInput() const { return !textInput_.empty(); }

private:
    struct PlayerBinding {
        arp::core::pmr::String actionMapId;
        InputActionState state = InputActionState::failure(
            InputDiagnosticCode::MissingActionMap, "Player has no action map binding.");
    };

    InputResult beginFrameImpl(
        const InputFrame& frame, const arp::core::pmr::Vector<InputConsumption>* eventConsumption);
    uint64_t windowId_ = 0;
    arp::core::pmr::Vector<InputKey> physicalHeldKeys_;
    arp::core::pmr::Vector<InputMouseButton> physicalHeldMouseButtons_;
    bool keyHeld(InputKey key) const;
    bool keyPressed(InputKey key) const;
    bool keyReleased(InputKey key) const;
    bool mouseHeld(InputMouseButton button) const;
    bool mousePressed(InputMouseButton button) const;
    bool mouseReleased(InputMouseButton button) const;
    InputActionState buildActionState(const InputActionMap& map) const;
    void rebuildPlayerStates();

    arp::core::pmr::Vector<PlayerBinding> players_;
    arp::core::pmr::UnorderedMap<arp::core::pmr::String, InputActionMap> maps_;
    arp::core::pmr::Vector<InputKey> heldKeys_;
    arp::core::pmr::Vector<InputKey> pressedKeys_;
    arp::core::pmr::Vector<InputKey> releasedKeys_;
    arp::core::pmr::Vector<InputMouseButton> heldMouseButtons_;
    arp::core::pmr::Vector<InputMouseButton> pressedMouseButtons_;
    arp::core::pmr::Vector<InputMouseButton> releasedMouseButtons_;
    float mouseDeltaX_ = 0.0f;
    float mouseDeltaY_ = 0.0f;
    float mouseWheel_ = 0.0f;
    float pointerX_ = 0.0f;
    float pointerY_ = 0.0f;
    bool pointerValid_ = false;
    arp::core::pmr::String textInput_;
};

struct PlayerInputComponentDesc {
    InputSystem* inputSystem = nullptr;
    size_t playerSlot = 0;
    arp::core::pmr::String actionMapId;
};

class PlayerInputComponent final : public arp::scene::Component {
public:
    explicit PlayerInputComponent(PlayerInputComponentDesc desc);

    void onUpdate(const arp::scene::SceneUpdateContext&) override;

    const InputActionState& actionState() const { return actionState_; }

    size_t playerSlot() const { return desc_.playerSlot; }

    const arp::core::pmr::String& actionMapId() const { return desc_.actionMapId; }

    bool exposesRawKeycodes() const { return false; }

private:
    PlayerInputComponentDesc desc_;
    InputActionState actionState_ = InputActionState::failure(
        InputDiagnosticCode::MissingActionMap, "Player input has not been updated.");
};

struct CharacterControllerComponentDesc {
    arp::scene::Scene* scene = nullptr;
    PlayerInputComponent* playerInput = nullptr;
    arp::core::pmr::String moveXAction;
    arp::core::pmr::String moveYAction;
    float moveSpeed = 1.0f;
};

class CharacterControllerComponent final : public arp::scene::Component {
public:
    explicit CharacterControllerComponent(CharacterControllerComponentDesc desc);

    void onUpdate(const arp::scene::SceneUpdateContext& context) override;

    arp::core::Vec3 lastMovement() const { return lastMovement_; }

    bool consumesRawInput() const { return false; }

    bool readsCameraProjectionState() const { return false; }

private:
    CharacterControllerComponentDesc desc_;
    arp::core::Vec3 lastMovement_{};
};

struct CameraFollowComponentDesc {
    arp::scene::Scene* scene = nullptr;
    PlayerInputComponent* playerInput = nullptr;
    arp::scene::GameObjectId target;
    arp::core::pmr::String lookXAction;
    arp::core::pmr::String zoomAction;
    arp::core::Vec3 followOffset{0.0f, 0.0f, 0.0f};
    float lookSensitivity = 0.0f;
};

class CameraFollowComponent final : public arp::scene::Component {
public:
    explicit CameraFollowComponent(CameraFollowComponentDesc desc);

    void onLateUpdate(const arp::scene::SceneUpdateContext& context) override;
    void setFollowOffset(arp::core::Vec3 followOffset);

    uint64_t lateUpdateFrame() const { return lateUpdateFrame_; }

    arp::core::Vec3 lastTargetTranslation() const { return lastTargetTranslation_; }

    float lastLookAxis() const { return lastLookAxis_; }

    float lastZoomAxis() const { return lastZoomAxis_; }

    bool consumesRawInput() const { return false; }

private:
    CameraFollowComponentDesc desc_;
    uint64_t lateUpdateFrame_ = 0;
    arp::core::Vec3 lastTargetTranslation_{};
    float lastLookAxis_ = 0.0f;
    float lastZoomAxis_ = 0.0f;
};

} // namespace arp::input
