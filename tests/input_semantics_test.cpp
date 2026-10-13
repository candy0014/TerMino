#include "tetr/input/action.h"
#include "tetr/input/services.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/engine/player_input.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/runtime/game_context.h"

#include <cassert>
#include <type_traits>
#include <sstream>
#include <iostream>

int main() {
    using Input::Action;
    {
        Input::FocusState focus;
        Input::Services first,second;
        first.focusSource=&focus;
        Input::share_key_state(second,first);
        std::array<int,Input::kBindableActionCount> keys;
        keys.fill(-1); keys[0]=30; keys[3]=31;
        Input::refresh_bindings(first,keys); Input::refresh_bindings(second,keys);
        first.begin_gameplay_input(); second.begin_gameplay_input();
        focus.update(30,true);
        first.poll_gameplay_events(); first.consume_gameplay_event();
        assert(first.gameplay_key_down(30) && second.key_down(30));
        // A pending hard drop must not survive losing and regaining focus.
        focus.update(31,true);
        first.poll_gameplay_events();
        assert(first.next_gameplay_event());
        focus.set_focused(false);
        assert(!first.gameplay_key_down(30) && !first.next_gameplay_event());
        assert(!first.key_down(30) && !second.control_key_pressed(31));
        focus.update(32,true); focus.update(32,false); // Background short tap.
        focus.set_focused(true);
        first.poll_gameplay_events(); second.poll_gameplay_events();
        assert(!first.gameplay_key_down(30) && !second.key_down(31));
        assert(!first.control_key_pressed(32));
        while (first.next_gameplay_event()) {
            assert(!first.next_gameplay_event()->down); first.consume_gameplay_event();
        }
        focus.update(30,false); focus.update(30,true);
        assert(first.key_down(30) && second.key_down(30));
        // The shared option permits new background presses, and turning it
        // off while unfocused releases all accepted keys immediately.
        second.set_continue_input_unfocused(true);
        focus.set_focused(false);
        focus.update(31,false); focus.update(31,true);
        assert(first.key_down(30) && second.key_down(31));
        first.set_continue_input_unfocused(false);
        assert(!first.key_down(30) && !second.key_down(31));
        focus.set_focused(true);
        assert(!first.key_down(30) && !second.key_down(31));
    }
    static_assert(!Input::capture_key_code(255,Input::KeyPlatform::Windows));
    static_assert(!Input::capture_key_code(256,Input::KeyPlatform::Windows));
    static_assert(Input::capture_key_code(255,Input::KeyPlatform::Evdev));
    static_assert(Input::rotation_delta(Action::Rotate180) == 2);
    static_assert(Input::rotation_delta(Action::RotateClockwise) == 1);
    static_assert(Input::rotation_delta(Action::RotateCounterclockwise) == -1);
    static_assert(Input::horizontal_delta(Action::MoveLeft) == -1);
    static_assert(Input::horizontal_delta(Action::MoveRight) == 1);
    static_assert(!std::is_convertible_v<int, Engine::KeyPhase>);

    std::array<int, Input::kBindableActionCount> keyCodes{};
    keyCodes.fill(-1);
    keyCodes[4] = 4;   // CW
    keyCodes[8] = 8;   // retry
    keyCodes[3] = 700; // hard drop
    const auto bindings = Input::BindingSet::from_key_codes(keyCodes);
    assert(bindings.action_for(4) == Action::RotateClockwise);
    assert(bindings.action_for(8) == Action::Retry);
    assert(bindings.action_for(12) == Action::Unknown);
    assert(bindings.action_for(700) == Action::HardDrop);
    std::array<std::vector<int>,Input::kBindableActionCount> additional;
    additional[4]={44,45};
    additional[3]={701};
    Input::Services configured;
    Input::refresh_bindings(configured,keyCodes,additional);
    assert(configured.bindings.action_for(4)==Action::RotateClockwise);
    assert(configured.bindings.action_for(44)==Action::RotateClockwise);
    assert(configured.bindings.action_for(45)==Action::RotateClockwise);
    assert(configured.bindings.action_for(700)==Action::HardDrop);
    assert(configured.bindings.action_for(701)==Action::HardDrop);
    keyCodes[0]=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x1b : 1;
    additional[0]={Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x0d : 28};
    Input::refresh_bindings(configured,keyCodes,additional);
    assert(configured.bindings.action_for(keyCodes[0])==Action::MoveLeft);
    assert(configured.bindings.action_for(additional[0][0])==Action::MoveLeft);
    // The menu actions reject conflicting primary and alternate keys, without
    // shadowing the same keys assigned to ordinary gameplay actions.
    const int tab=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x09 : 15;
    keyCodes[8]=keyCodes[0];additional[8]={tab};
    keyCodes[9]=additional[0][0];additional[9]={tab};
    Input::refresh_bindings(configured,keyCodes,additional);
    assert(configured.bindings.action_for(keyCodes[0])==Action::MoveLeft);
    assert(configured.bindings.action_for(additional[0][0])==Action::MoveLeft);
    assert(configured.bindings.action_for(tab)==Action::Unknown);

#ifdef _WIN32
    keyCodes[0]=255;
    additional[0]={255,37};
    Input::refresh_bindings(configured,keyCodes,additional);
    assert(configured.bindings.action_for(255)==Action::Unknown);
    assert(configured.bindings.action_for(37)==Action::MoveLeft);
#endif

    Engine::MovementInputState input;
    assert(input.phase(1) == Engine::KeyPhase::Released);
    input.setPhase(700, Engine::KeyPhase::Pressed);
    assert(input.phase(700) == Engine::KeyPhase::Pressed);
    input.setPhase(1, Engine::KeyPhase::AwaitRelease);
    assert(input.consumeReleaseDebounce(1, true));
    assert(input.phase(1) == Engine::KeyPhase::AwaitRelease);
    assert(input.consumeReleaseDebounce(1, false));
    assert(input.phase(1) == Engine::KeyPhase::Released);

    Input::Services firstServices;
    Input::Services secondServices;
    Input::share_key_state(secondServices, firstServices);
    assert(Input::shares_key_state_with(secondServices, firstServices));
    assert(secondServices.initialized);

    // Both physical keys drive the same movement action independently.
    keyCodes.fill(-1);
    keyCodes[0]=1;
    additional={}; additional[0]={2};
    const auto movementBindings=Input::BindingSet::from_key_codes(keyCodes,additional);
    Engine::MovementState state;
    state.pose.position={8,5};
    Engine::MovementController movement(state,
        [](Piece::Pose pose) { return pose.position.row<=18 && pose.position.col>=0 && pose.position.col<10; },
        [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
    Engine::MovementInputState movementInput;
    Engine::PlayerInputConfig config;
    config.das=Runtime::Duration::from_seconds(.1);
    config.arr=Runtime::Duration::from_seconds(.01);
    const auto frame=[&](int pressed) {
        return Engine::process_player_input_frame(movementInput,movement,movementBindings,{},{1,2},config,
            [&](int key) { return key==pressed; },[] { return Runtime::TimePoint{}; });
    };
    frame(1); assert(state.pose.position.col==4);
    frame(-1);
    frame(2); assert(state.pose.position.col==3);

    // An unpressed alias cannot release suppression for a held retry key.
    keyCodes.fill(-1); keyCodes[8]=31;
    additional={}; additional[8]={32};
    Input::refresh_bindings(configured,keyCodes,additional);
    int primaryReads=0,aliasReads=0;
    configured.keyStateProvider=[&](int key) {
        if (key==31) return ++primaryReads<=5;
        if (key==32) return ++aliasReads>=6;
        return false;
    };
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    context.attach_input(configured);
    context.config.mode.mode=Runtime::GameMode::Racing;
    std::ostringstream rendered;
    auto* old=std::cout.rdbuf(rendered.rdbuf());
    assert(Engine::run_start_countdown(context,session.player,session.opponent)==Engine::CountdownResult::Retry);
    std::cout.rdbuf(old);
    assert(primaryReads>=6 && aliasReads>=6);
}
