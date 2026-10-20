#include "input/input.h"

#include "scene/scene.h"

#include <algorithm>
#include <utility>

namespace arp::input {
namespace {

template <typename Value>
bool containsValue(const arp::core::pmr::Vector<Value>& values, Value value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

void eraseValue(arp::core::pmr::Vector<InputKey>& values, InputKey value)
{
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

void eraseValue(arp::core::pmr::Vector<InputMouseButton>& values, InputMouseButton value)
{
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

} // namespace

InputResult::InputResult(InputDiagnostic diagnostic) : diagnostic_(std::move(diagnostic))
{}

InputResult InputResult::success()
{
    return InputResult(InputDiagnostic{});
}

InputResult InputResult::failure(InputDiagnosticCode code, arp::core::pmr::String message)
{
    return InputResult(InputDiagnostic{code, std::move(message)});
}

InputEvent InputEvent::keyPressed(InputKey keyValue)
{
    InputEvent event;
    event.kind = InputEventKind::KeyPressed;
    event.key = keyValue;
    return event;
}

InputEvent InputEvent::keyReleased(InputKey keyValue)
{
    InputEvent event;
    event.kind = InputEventKind::KeyReleased;
    event.key = keyValue;
    return event;
}

InputEvent InputEvent::mouseButtonPressed(InputMouseButton button)
{
    InputEvent event;
    event.kind = InputEventKind::MouseButtonPressed;
    event.mouseButton = button;
    return event;
}

InputEvent InputEvent::mouseButtonReleased(InputMouseButton button)
{
    InputEvent event;
    event.kind = InputEventKind::MouseButtonReleased;
    event.mouseButton = button;
    return event;
}

InputEvent InputEvent::mouseMoved(float deltaX, float deltaY)
{
    InputEvent event;
    event.kind = InputEventKind::MouseMoved;
    event.x = deltaX;
    event.y = deltaY;
    return event;
}

InputEvent InputEvent::mouseWheel(float delta)
{
    InputEvent event;
    event.kind = InputEventKind::MouseWheel;
    event.y = delta;
    return event;
}

InputEvent InputEvent::pointerMoved(float xValue, float yValue)
{
    InputEvent event;
    event.kind = InputEventKind::PointerMoved;
    event.x = xValue;
    event.y = yValue;
    return event;
}

InputEvent InputEvent::textInput(arp::core::pmr::String textValue)
{
    InputEvent event;
    event.kind = InputEventKind::TextInput;
    event.text = std::move(textValue);
    return event;
}

void InputFrame::add(InputEvent event)
{
    events_.push_back(event);
}

InputBinding InputBinding::key(InputKey keyValue)
{
    InputBinding binding;
    binding.kind = InputBindingKind::KeyButton;
    binding.keyCode = keyValue;
    return binding;
}

InputBinding InputBinding::axis1D(InputKey negativeKeyValue, InputKey positiveKeyValue)
{
    InputBinding binding;
    binding.kind = InputBindingKind::Axis1D;
    binding.negativeKey = negativeKeyValue;
    binding.positiveKey = positiveKeyValue;
    return binding;
}

InputBinding InputBinding::mouseDeltaX()
{
    InputBinding binding;
    binding.kind = InputBindingKind::MouseDeltaX;
    return binding;
}

InputBinding InputBinding::mouseDeltaY()
{
    InputBinding binding;
    binding.kind = InputBindingKind::MouseDeltaY;
    return binding;
}

InputBinding InputBinding::mouseWheel()
{
    InputBinding binding;
    binding.kind = InputBindingKind::MouseWheel;
    return binding;
}

InputActionMap::InputActionMap(arp::core::pmr::String id) : id_(std::move(id))
{}

InputResult InputActionMap::bindButton(arp::core::pmr::String action, InputBinding binding)
{
    binding.kind = InputBindingKind::KeyButton;
    return bind(std::move(action), binding);
}

InputResult InputActionMap::bindAxis1D(arp::core::pmr::String action, InputBinding binding)
{
    return bind(std::move(action), binding);
}

InputResult InputActionMap::bind(arp::core::pmr::String action, InputBinding binding)
{
    if (bindings_.find(action) != bindings_.end()) {
        return InputResult::failure(
            InputDiagnosticCode::DuplicateBinding, "InputActionMap action already has a binding.");
    }
    bindings_.emplace(std::move(action), binding);
    return InputResult::success();
}

InputActionState::InputActionState(InputDiagnostic diagnostic) : diagnostic_(std::move(diagnostic))
{}

InputActionState InputActionState::success()
{
    return InputActionState(InputDiagnostic{});
}

InputActionState InputActionState::failure(InputDiagnosticCode code, arp::core::pmr::String message)
{
    return InputActionState(InputDiagnostic{code, std::move(message)});
}

bool InputActionState::pressed(const arp::core::pmr::String& action) const
{
    const auto found = buttons_.find(action);
    return found != buttons_.end() && found->second.pressed;
}

bool InputActionState::held(const arp::core::pmr::String& action) const
{
    const auto found = buttons_.find(action);
    return found != buttons_.end() && found->second.held;
}

bool InputActionState::released(const arp::core::pmr::String& action) const
{
    const auto found = buttons_.find(action);
    return found != buttons_.end() && found->second.released;
}

float InputActionState::axis(const arp::core::pmr::String& action) const
{
    const auto found = axes_.find(action);
    return found != axes_.end() ? found->second : 0.0f;
}

void InputActionState::setButton(arp::core::pmr::String action, ButtonState state)
{
    buttons_[std::move(action)] = state;
}

void InputActionState::setAxis(arp::core::pmr::String action, float value)
{
    axes_[std::move(action)] = value;
}

InputConsumption InputConsumption::none()
{
    return InputConsumption{};
}

InputConsumption InputConsumption::pointer()
{
    InputConsumption consumption;
    consumption.consumePointer();
    return consumption;
}

void InputConsumption::consumePointer()
{
    pointerConsumed_ = true;
}

void InputConsumption::consumeKeyboard()
{
    keyboardConsumed_ = true;
}

void InputConsumption::consumeText()
{
    textConsumed_ = true;
}

InputSystem::InputSystem(InputSystemDesc desc) : players_(desc.maxPlayers), windowId_(desc.windowId)
{
    const auto keyCount = static_cast<size_t>(InputKey::F24) + 1u;
    physicalHeldKeys_.reserve(keyCount);
    heldKeys_.reserve(keyCount);
    pressedKeys_.reserve(keyCount);
    releasedKeys_.reserve(keyCount);
    physicalHeldMouseButtons_.reserve(3u);
    heldMouseButtons_.reserve(3u);
    pressedMouseButtons_.reserve(3u);
    releasedMouseButtons_.reserve(3u);
}

InputResult InputSystem::registerActionMap(InputActionMap map)
{
    maps_.insert_or_assign(map.id(), std::move(map));
    return InputResult::success();
}

InputResult InputSystem::bindPlayerActionMap(
    size_t playerSlot, const arp::core::pmr::String& actionMapId)
{
    if (playerSlot >= players_.size()) {
        return InputResult::failure(InputDiagnosticCode::InvalidPlayerSlot,
            "Player slot is outside the configured input player range.");
    }
    if (maps_.find(actionMapId) == maps_.end()) {
        return InputResult::failure(
            InputDiagnosticCode::MissingActionMap, "Cannot bind player to an unknown action map.");
    }
    players_[playerSlot].actionMapId = actionMapId;
    players_[playerSlot].state = buildActionState(maps_.at(actionMapId));
    return InputResult::success();
}

InputResult InputSystem::beginFrame(const InputFrame& frame)
{
    return beginFrameImpl(frame, nullptr);
}

InputResult InputSystem::beginFrame(
    const InputFrame& frame, const arp::core::pmr::Vector<InputConsumption>& consumption)
{
    return beginFrameImpl(frame, &consumption);
}

InputResult InputSystem::beginFrameImpl(
    const InputFrame& frame, const arp::core::pmr::Vector<InputConsumption>* consumption)
{
    if (consumption && consumption->size() != frame.events().size())
        return InputResult::failure(
            InputDiagnosticCode::InvalidEventConsumption, "INPUT-CONSUMPTION-COUNT-MISMATCH");
    for (const auto& event : frame.events()) {
        if ((event.kind == InputEventKind::KeyPressed || event.kind == InputEventKind::KeyReleased)
            && static_cast<size_t>(event.key) > static_cast<size_t>(InputKey::F24))
            return InputResult::failure(
                InputDiagnosticCode::InvalidEventMetadata, "INPUT-KEY-INVALID");
        if ((event.kind == InputEventKind::MouseButtonPressed
                || event.kind == InputEventKind::MouseButtonReleased)
            && static_cast<size_t>(event.mouseButton)
                > static_cast<size_t>(InputMouseButton::Middle))
            return InputResult::failure(
                InputDiagnosticCode::InvalidEventMetadata, "INPUT-MOUSE-BUTTON-INVALID");
    }
    pressedKeys_.clear();
    releasedKeys_.clear();
    pressedMouseButtons_.clear();
    releasedMouseButtons_.clear();
    mouseDeltaX_ = 0.0f;
    mouseDeltaY_ = 0.0f;
    mouseWheel_ = 0.0f;
    textInput_.clear();
    for (size_t index = 0; index < frame.events().size(); ++index) {
        const auto& event = frame.events()[index];
        if (event.windowId != windowId_)
            continue;
        const auto consumed = consumption ? (*consumption)[index] : InputConsumption::none();
        switch (event.kind) {
        case InputEventKind::KeyPressed:
            if (std::find(physicalHeldKeys_.begin(), physicalHeldKeys_.end(), event.key)
                == physicalHeldKeys_.end()) {
                physicalHeldKeys_.push_back(event.key);
                if (!consumed.keyboardConsumed()) {
                    pressedKeys_.push_back(event.key);
                    heldKeys_.push_back(event.key);
                }
            }
            break;
        case InputEventKind::KeyReleased:
            // Release clears the press owner's state even if UI focus changed.
            if (keyHeld(event.key))
                releasedKeys_.push_back(event.key);
            eraseValue(physicalHeldKeys_, event.key);
            eraseValue(heldKeys_, event.key);
            break;
        case InputEventKind::MouseButtonPressed:
            if (std::find(physicalHeldMouseButtons_.begin(),
                    physicalHeldMouseButtons_.end(),
                    event.mouseButton)
                == physicalHeldMouseButtons_.end()) {
                physicalHeldMouseButtons_.push_back(event.mouseButton);
                if (!consumed.pointerConsumed()) {
                    pressedMouseButtons_.push_back(event.mouseButton);
                    heldMouseButtons_.push_back(event.mouseButton);
                }
            }
            break;
        case InputEventKind::MouseButtonReleased:
            if (mouseHeld(event.mouseButton))
                releasedMouseButtons_.push_back(event.mouseButton);
            eraseValue(physicalHeldMouseButtons_, event.mouseButton);
            eraseValue(heldMouseButtons_, event.mouseButton);
            break;
        case InputEventKind::MouseMoved:
            if (!consumed.pointerConsumed()) {
                mouseDeltaX_ += event.x;
                mouseDeltaY_ += event.y;
            }
            break;
        case InputEventKind::MouseWheel:
            if (!consumed.pointerConsumed())
                mouseWheel_ += event.y;
            break;
        case InputEventKind::PointerMoved:
            pointerX_ = event.x;
            pointerY_ = event.y;
            pointerValid_ = true;
            break;
        case InputEventKind::TextInput:
            if (!consumed.textConsumed())
                textInput_ += event.text;
            break;
        case InputEventKind::FocusLost:
            for (const auto key : heldKeys_)
                if (!keyReleased(key))
                    releasedKeys_.push_back(key);
            for (const auto button : heldMouseButtons_)
                if (!mouseReleased(button))
                    releasedMouseButtons_.push_back(button);
            heldKeys_.clear();
            physicalHeldKeys_.clear();
            heldMouseButtons_.clear();
            physicalHeldMouseButtons_.clear();
            pointerValid_ = false;
            break;
        }
    }
    rebuildPlayerStates();
    return InputResult::success();
}

InputResult InputSystem::applyConsumption(const InputConsumption& consumption)
{
    if (consumption.pointerConsumed()) {
        pressedMouseButtons_.clear();
        releasedMouseButtons_.clear();
        mouseDeltaX_ = 0.0f;
        mouseDeltaY_ = 0.0f;
        mouseWheel_ = 0.0f;
        heldMouseButtons_.clear();
    }
    if (consumption.keyboardConsumed()) {
        pressedKeys_.clear();
        releasedKeys_.clear();
        heldKeys_.clear();
    }
    if (consumption.textConsumed()) {
        textInput_.clear();
    }
    rebuildPlayerStates();
    return InputResult::success();
}

InputActionState InputSystem::actionState(size_t playerSlot) const
{
    if (playerSlot >= players_.size()) {
        return InputActionState::failure(InputDiagnosticCode::InvalidPlayerSlot,
            "Player slot is outside the configured input player range.");
    }
    return players_[playerSlot].state;
}

InputPointerSnapshot InputSystem::pointer() const
{
    return InputPointerSnapshot{
        pointerX_,
        pointerY_,
        pointerValid_,
        mousePressed(InputMouseButton::Left),
        mouseHeld(InputMouseButton::Left),
        mouseReleased(InputMouseButton::Left),
    };
}

bool InputSystem::keyHeld(InputKey key) const
{
    return containsValue(heldKeys_, key);
}

bool InputSystem::keyPressed(InputKey key) const
{
    return containsValue(pressedKeys_, key);
}

bool InputSystem::keyReleased(InputKey key) const
{
    return containsValue(releasedKeys_, key);
}

bool InputSystem::mouseHeld(InputMouseButton button) const
{
    return containsValue(heldMouseButtons_, button);
}

bool InputSystem::mousePressed(InputMouseButton button) const
{
    return containsValue(pressedMouseButtons_, button);
}

bool InputSystem::mouseReleased(InputMouseButton button) const
{
    return containsValue(releasedMouseButtons_, button);
}

InputActionState InputSystem::buildActionState(const InputActionMap& map) const
{
    auto state = InputActionState::success();
    for (const auto& binding : map.bindings()) {
        switch (binding.second.kind) {
        case InputBindingKind::KeyButton:
            state.setButton(binding.first,
                InputActionState::ButtonState{
                    keyPressed(binding.second.keyCode),
                    keyHeld(binding.second.keyCode),
                    keyReleased(binding.second.keyCode),
                });
            break;
        case InputBindingKind::Axis1D: {
            const auto negative = keyHeld(binding.second.negativeKey) ? -1.0f : 0.0f;
            const auto positive = keyHeld(binding.second.positiveKey) ? 1.0f : 0.0f;
            state.setAxis(binding.first, negative + positive);
            break;
        }
        case InputBindingKind::MouseDeltaX:
            state.setAxis(binding.first, mouseDeltaX_);
            break;
        case InputBindingKind::MouseDeltaY:
            state.setAxis(binding.first, mouseDeltaY_);
            break;
        case InputBindingKind::MouseWheel:
            state.setAxis(binding.first, mouseWheel_);
            break;
        }
    }
    return state;
}

void InputSystem::rebuildPlayerStates()
{
    for (auto& player : players_) {
        if (player.actionMapId.empty()) {
            player.state = InputActionState::failure(
                InputDiagnosticCode::MissingActionMap, "Player has no action map binding.");
            continue;
        }
        const auto found = maps_.find(player.actionMapId);
        if (found == maps_.end()) {
            player.state = InputActionState::failure(InputDiagnosticCode::MissingActionMap,
                "Player action map binding no longer resolves.");
            continue;
        }
        player.state = buildActionState(found->second);
    }
}

PlayerInputComponent::PlayerInputComponent(PlayerInputComponentDesc desc) : desc_(std::move(desc))
{}

void PlayerInputComponent::onUpdate(const arp::scene::SceneUpdateContext&)
{
    if (desc_.inputSystem == nullptr) {
        actionState_ = InputActionState::failure(
            InputDiagnosticCode::MissingActionMap, "PlayerInputComponent requires an InputSystem.");
        return;
    }
    actionState_ = desc_.inputSystem->actionState(desc_.playerSlot);
}

CharacterControllerComponent::CharacterControllerComponent(CharacterControllerComponentDesc desc)
    : desc_(std::move(desc))
{}

void CharacterControllerComponent::onUpdate(const arp::scene::SceneUpdateContext& context)
{
    lastMovement_ = {};
    if (desc_.scene == nullptr || desc_.playerInput == nullptr
        || !desc_.playerInput->actionState().ok()) {
        return;
    }
    auto* owner = desc_.scene->findObject(ownerId());
    if (owner == nullptr) {
        return;
    }

    const auto& state = desc_.playerInput->actionState();
    lastMovement_.x = state.axis(desc_.moveXAction) * desc_.moveSpeed * context.deltaSeconds;
    lastMovement_.y = state.axis(desc_.moveYAction) * desc_.moveSpeed * context.deltaSeconds;

    auto local = owner->transform().localTransform();
    local.translation.x += lastMovement_.x;
    local.translation.y += lastMovement_.y;
    owner->transform().setLocalTransform(local);
}

CameraFollowComponent::CameraFollowComponent(CameraFollowComponentDesc desc)
    : desc_(std::move(desc))
{}

void CameraFollowComponent::setFollowOffset(arp::core::Vec3 followOffset)
{
    desc_.followOffset = followOffset;
}

void CameraFollowComponent::onLateUpdate(const arp::scene::SceneUpdateContext& context)
{
    lateUpdateFrame_ = context.frameIndex;
    lastTargetTranslation_ = {};
    lastLookAxis_ = 0.0f;
    lastZoomAxis_ = 0.0f;
    if (desc_.scene == nullptr) {
        return;
    }

    const auto* target = desc_.scene->findObject(desc_.target);
    auto* owner = desc_.scene->findObject(ownerId());
    if (target == nullptr || owner == nullptr) {
        return;
    }

    if (desc_.playerInput != nullptr && desc_.playerInput->actionState().ok()) {
        lastLookAxis_ = desc_.playerInput->actionState().axis(desc_.lookXAction);
        lastZoomAxis_ = desc_.playerInput->actionState().axis(desc_.zoomAction);
    }

    lastTargetTranslation_ = target->transform().localTransform().translation;
    auto local = owner->transform().localTransform();
    local.translation = {
        lastTargetTranslation_.x + desc_.followOffset.x + lastLookAxis_ * desc_.lookSensitivity,
        lastTargetTranslation_.y + desc_.followOffset.y,
        lastTargetTranslation_.z + desc_.followOffset.z,
    };
    owner->transform().setLocalTransform(local);
}

} // namespace arp::input
