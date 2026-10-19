#include "desktop_coop.hpp"
#include "desktop_text.hpp"
#include "phantomcrash/split_screen.hpp"
#include <algorithm>
#include <stdexcept>
#include <cctype>

namespace phantomcrash {
namespace {
std::string screen_label(std::string name) {
  if (name == "rumbling_battle") {
    return "Rumble";
  }
  if (name == "match_result") {
    return "Results";
  }
  if (name.starts_with("event_")) {
    return "Story";
  }
  if (name.starts_with("dealer_")) {
    name.erase(0, 7);
  }
  bool capital = true;
  for (auto &c : name) {
    if (c == '_') {
      c = ' ';
    }
    if (capital) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    capital = c == ' ';
  }
  return name;
}
std::vector<SplitScreen> coop_panes(const int count, const int width, const int height) {
  auto panes = split_screens(count, width, height);
  // Side-by-side keeps town menus legible while either player is shopping.
  if (count == 2) {
    const float half = static_cast<float>(width / 2);
    panes[0].rect = {0, 0, half, static_cast<float>(height)};
    panes[1].rect = {half, 0, static_cast<float>(width) - half, static_cast<float>(height)};
  }
  return panes;
}
InputFrame all_ports_frame(const InputFrame &input) {
  InputFrame out{input.held, input.pressed, 1};
  out.left_stick = input.left_stick;
  out.right_stick = input.right_stick;
  out.ports[0] = {true, input.held, input.pressed, input.left_stick, input.right_stick};
  return out;
}
// Local split-screen panes reserve a status bar above each player's game.
// Online play has one full-window pane; its peer status is overlaid instead.
int status_bar(const int width, const bool reserved) {
  return reserved ? width / 16 : 0;
}
Viewport player_canvas(const int width, const int height, const bool reserved) {
  return fit_canvas(width, height - status_bar(width, reserved));
}
} // namespace

void CoopScreen::text(const Viewport &v, const std::string &line, const float x, const float y,
                      const float size, const SDL_Color colour) {
  auto glyphs = layout_run(font_, glyph_run(font_, line), 0, 0);
  const float scale = size / font_.cell;
  for (auto &glyph : glyphs) {
    glyph.x = x + glyph.x * scale;
    glyph.y = y + glyph.y * scale;
    glyph.width *= scale;
    glyph.height *= scale;
  }
  const auto rgb = (static_cast<std::uint32_t>(colour.r) << 16) |
                   (static_cast<std::uint32_t>(colour.g) << 8) | colour.b;
  draw_glyphs(textures_, font_, glyphs, v, rgb, std::nullopt, colour.a / 255.0F);
}

InputFrame CoopScreen::player_input(const InputFrame &input, const int slot) const {
  // An online machine has one local player, who uses every local device: the
  // keyboard is port 0 and a lone gamepad port 1, so a single port would
  // leave the gamepad (or the keyboard) dead.
  InputFrame own = state_->online ? all_ports_frame(input) : port_frame(input, players_[slot].port);
  if (!own.controllers) {
    return {};
  }
  // The keyboard/mouse belongs to its joined port. Controllers never inherit
  // another player's analog axes or a global mouse click. Typing goes the
  // same way, so a name entry hears the keyboard only on that pilot's port.
  if (players_[slot].port == 0) {
    own.typed = input.typed;
    own.erased = input.erased;
    own.text_submit = input.text_submit;
  }
  if (players_[slot].port == 0 && input.pointer) {
    const int count = state_->online ? 1
                      : playing_     ? static_cast<int>(std::ranges::count_if(
                                       players_, [](const Player &p) { return p.port >= 0; }))
                                     : 4;
    const auto panes = coop_panes(count, viewport_.pixel_width, viewport_.pixel_height);
    int pane = 0;
    for (int i = 0; i < slot; ++i) {
      if (!state_->online && (!playing_ || players_[i].port >= 0)) {
        ++pane;
      }
    }
    const auto &rect = panes.at(pane).rect;
    const float x = viewport_.to_pixel_x((*input.pointer)[0]) - viewport_.origin_x;
    const float y = viewport_.to_pixel_y((*input.pointer)[1]) - viewport_.origin_y;
    if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height) {
      const bool bar = playing_ && !state_->online;
      const int width = static_cast<int>(rect.width);
      const auto local = player_canvas(width, static_cast<int>(rect.height), bar);
      own.pointer =
          std::array{(x - rect.x - local.offset_x) / local.scale,
                     (y - rect.y - status_bar(width, bar) - local.offset_y) / local.scale};
      own.pointer_moved = input.pointer_moved;
      own.clicked = input.clicked;
      own.right_clicked = input.right_clicked;
      own.mouse_left_held = input.mouse_left_held;
      own.mouse_wheel = input.mouse_wheel;
    }
  }
  return own;
}

float CoopScreen::text_width(const std::string &line, const float size) const {
  return run_width(font_, glyph_run(font_, line)) * size / font_.cell;
}

void CoopScreen::draw_presence(const int slot, const Viewport &v) {
  const auto &self = players_[slot];
  if (!self.runner) {
    return;
  }
  auto header = v;
  header.scale = static_cast<float>(v.pixel_width) / 640.0F;
  header.offset_x = header.offset_y = header.origin_y = 0;
  SDL_Rect content;
  SDL_GetRenderViewport(renderer_, &content);
  SDL_SetRenderViewport(renderer_, nullptr);
  float y = 14;
  std::vector<PeerView> peers;
  if (state_->online) {
    // Online play has no status bar: the lines overlay the top-left corner.
    if (!self.connected) {
      text(header, "CONTROLLER DISCONNECTED", 8, 1, 10, {255, 230, 80, 255});
    } else {
      y = 2;
    }
    peers = online_peers();
  } else {
    SDL_SetRenderDrawColor(renderer_, 10, 14, 22, 255);
    const SDL_FRect bar{0, 0, static_cast<float>(v.pixel_width), static_cast<float>(content.y)};
    SDL_RenderFillRect(renderer_, &bar);
    text(header,
         "P" + std::to_string(slot + 1) + (self.connected ? "" : " CONTROLLER DISCONNECTED"), 8, 1,
         10, {255, 230, 80, 255});
    for (int other = 0; other < 4; ++other) {
      const auto &peer = players_[other];
      if (other != slot && peer.runner) {
        const auto &save = peer.runner->quest_save();
        peers.push_back({other, save.player_name, save.day, peer.runner->screen(),
                         peer.runner->coop_location(), peer.runner->coop_marks()});
      }
    }
  }
  for (const auto &peer : peers) {
    draw_peer(peer, *self.runner, content, v, header, y);
  }
}

void CoopScreen::draw_peer(const PeerView &peer, const ScreenRunner &self, const SDL_Rect &content,
                           const Viewport &v, const Viewport &header, float &y) {
  const bool together = self.screen() == peer.screen;
  const auto marked = [&](const std::string &screen) -> std::optional<MenuPresence> {
    for (const auto &[name, mark] : peer.marks) {
      if (name == screen) {
        return mark;
      }
    }
    return std::nullopt;
  };
  const auto selection = marked(peer.screen);
  std::optional<std::array<float, 2>> at;
  bool busy = !together;
  if (self.screen() == "town_map") {
    if (peer.screen == "town_map" && selection) {
      at = selection->position;
    } else {
      for (const auto &place : map_.places) {
        if (place.next == peer.location) {
          at = place.rider;
        }
      }
    }
  } else if (const auto presence = marked(self.screen())) {
    if (const auto placed = self.active_screen()->peer_presence(*presence)) {
      at = placed->position;
      busy = placed->busy;
    }
  }
  if (at) {
    Sprite rider = map_.rider;
    rider.position = {(*at)[0] - 18.0F + 14.0F * peer.slot, (*at)[1] - 20.0F};
    rider.size = std::array{24.0F, 28.0F};
    rider.angle = 0;
    const std::uint32_t colours[] = {0x80ddff, 0xffb080, 0xb0ff80, 0xff80e0};
    SDL_SetRenderViewport(renderer_, &content);
    draw_sprite(textures_, rider, v, 0, 1, busy ? 0x888888 : colours[peer.slot]);
    text(v, "P" + std::to_string(peer.slot + 1), rider.position[0], rider.position[1] - 9, 9);
    SDL_SetRenderViewport(renderer_, nullptr);
  }
  const auto line = "P" + std::to_string(peer.slot + 1) + " " + peer.name + " - " +
                    screen_label(peer.screen) +
                    (together && selection ? ": " + screen_label(selection->selection) : "") +
                    " / DAY " + std::to_string(peer.day + 1);
  constexpr float size = 8;
  if (state_->online) {
    // A translucent backing keeps the overlay readable over the game.
    const float width = run_width(font_, glyph_run(font_, line)) * size / font_.cell;
    const SDL_FRect backing{6 * header.scale, (y - 1) * header.scale, (width + 4) * header.scale,
                            (size + 2) * header.scale};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 10, 14, 22, 170);
    SDL_RenderFillRect(renderer_, &backing);
  }
  text(header, line, 8, y, size,
       together ? SDL_Color{100, 220, 255, 255} : SDL_Color{160, 160, 160, 255});
  y += 9;
}

void CoopScreen::draw(const Viewport &viewport) {
  viewport_ = viewport;
  if (draw_connection_menu(viewport) || draw_online_wait(viewport)) {
    return;
  }
  const int count = state_->online ? 1
                    : playing_     ? static_cast<int>(std::ranges::count_if(
                                     players_, [](const Player &p) { return p.port >= 0; }))
                                   : 4;
  if (!count) {
    return;
  }
  const auto panes = coop_panes(count, viewport.pixel_width, viewport.pixel_height);
  SDL_Texture *previous = SDL_GetRenderTarget(renderer_);
  int pane = 0;
  for (int slot = 0; slot < 4; ++slot) {
    auto &player = players_[slot];
    if ((state_->online && slot != online_slot_) ||
        (!state_->online && playing_ && player.port < 0)) {
      continue;
    }
    const auto &rect = panes.at(pane++).rect;
    const int width = static_cast<int>(rect.width), height = static_cast<int>(rect.height);
    if (!player.target || player.width != width || player.height != height) {
      SDL_DestroyTexture(player.target);
      player.target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET, width, height);
      if (!player.target) {
        throw std::runtime_error(SDL_GetError());
      }
      SDL_SetTextureBlendMode(player.target, SDL_BLENDMODE_NONE);
      player.width = width;
      player.height = height;
    }
    SDL_SetRenderTarget(renderer_, player.target);
    SDL_SetRenderDrawColor(renderer_, 10, 14, 22, 255);
    SDL_RenderClear(renderer_);
    const Viewport local = player_canvas(width, height, playing_ && !state_->online);
    const SDL_Rect content{0, height - local.pixel_height, width, local.pixel_height};
    SDL_SetRenderViewport(renderer_, &content);
    if (player.runner && !player.save_error && (playing_ || !player.ready)) {
      player.runner->draw(player.runner->placed(local));
      if (playing_) {
        draw_presence(slot, local);
      }
    } else {
      draw_lobby(slot, local);
    }
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderTarget(renderer_, previous);
    const SDL_FRect target{rect.x + viewport.origin_x, rect.y + viewport.origin_y, rect.width,
                           rect.height};
    SDL_RenderTexture(renderer_, player.target, nullptr, &target);
  }
}

} // namespace phantomcrash
