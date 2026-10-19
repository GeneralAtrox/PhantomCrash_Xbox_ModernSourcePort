#include "test_support.hpp"
#include "../src/desktop_input.hpp"

#include <cmath>

using namespace phantomcrash;

namespace {
struct Sdl {
  Sdl() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
  }
  ~Sdl() {
    SDL_Quit();
  }
};
void pump(DesktopInput &input) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    input.handle(event);
  }
}
} // namespace

TEST_CASE("default desktop bindings resolve to actual SDL keys and controller inputs") {
  Sdl sdl;
  for (const HostBinding &binding : HostControls{}.bindings) {
    for (const auto &key : binding.keys) {
      CHECK(SDL_GetScancodeFromName(key.c_str()) != SDL_SCANCODE_UNKNOWN);
    }
    std::string pad = binding.pad;
    if (pad.ends_with('+') || pad.ends_with('-')) {
      pad.pop_back();
      CHECK(SDL_GetGamepadAxisFromString(pad.c_str()) != SDL_GAMEPAD_AXIS_INVALID);
    } else {
      CHECK(SDL_GetGamepadButtonFromString(pad.c_str()) != SDL_GAMEPAD_BUTTON_INVALID);
    }
  }
}

TEST_CASE("a reassigned keyboard press reaches its new action only once") {
  Sdl sdl;
  DesktopInput input;
  HostControls controls;
  bind_host_key(controls, "left_trigger", "W");
  input.configure(controls);
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.scancode = SDL_SCANCODE_W;
  input.handle(event);
  const InputFrame pressed = input.sample(true, Viewport{});
  CHECK(pressed.any_pressed(button::left_trigger));
  CHECK(!pressed.any_pressed(button::up));
  CHECK(!input.sample(true, Viewport{}).any_pressed(button::left_trigger));
}

TEST_CASE("a virtual controller holds remapped movement and fire across successive ticks") {
  Sdl sdl;
  DesktopInput input;
  HostControls controls;
  bind_host_pad(controls, "left_trigger", "a");
  input.configure(controls);
  SDL_VirtualJoystickDesc desc{};
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.axis_mask = (1U << SDL_GAMEPAD_AXIS_COUNT) - 1;
  desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  CHECK(id != 0);
  SDL_Joystick *joystick = SDL_OpenJoystick(id);
  CHECK(joystick != nullptr);
  pump(input);
  CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -32767));
  CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
  pump(input);
  for (int tick = 0; tick < 120; ++tick) {
    const InputFrame frame = input.sample(true, Viewport{});
    CHECK(frame.left_stick[1] > 0.99F);
    CHECK((frame.held & button::left_trigger) != 0);
    CHECK((frame.held & button::a) == 0);
  }
  const InputFrame unfocused = input.sample(false, Viewport{});
  CHECK((unfocused.left_stick == std::array<float, 2>{}));
  CHECK(unfocused.held == 0);
  CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0));
  CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false));
  pump(input);
  const InputFrame released = input.sample(true, Viewport{});
  CHECK(released.left_stick[1] == 0.0F);
  CHECK((released.held & button::left_trigger) == 0);
  SDL_CloseJoystick(joystick);
  CHECK(SDL_DetachVirtualJoystick(id));
  pump(input);
}

TEST_CASE("relative mouse motion is consumed once and F10 releases gameplay input") {
  Sdl sdl;
  SDL_Window *window = SDL_CreateWindow("Input test", 320, 240, SDL_WINDOW_HIDDEN);
  CHECK(window != nullptr);
  DesktopInput input;
  input.configure(HostControls{});
  input.capture_mouse(window, true);
  SDL_Event motion{};
  motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.motion.windowID = SDL_GetWindowID(window);
  motion.motion.xrel = 20.0F;
  motion.motion.yrel = -10.0F;
  input.handle(motion);
  const InputFrame aimed = input.sample(true, Viewport{});
  CHECK(aimed.mouse_aim[0] > 0.0F);
  CHECK(aimed.mouse_aim[1] > 0.0F);
  CHECK((input.sample(true, Viewport{}).mouse_aim == std::array<float, 2>{}));
  SDL_Event release{};
  release.type = SDL_EVENT_KEY_DOWN;
  release.key.scancode = SDL_SCANCODE_F10;
  input.handle(release);
  input.handle(motion);
  CHECK((input.sample(true, Viewport{}).mouse_aim == std::array<float, 2>{}));
  input.capture_mouse(window, false);
  SDL_DestroyWindow(window);
}
