#pragma once

union SDL_Event;

namespace dusk::ui::drop {

void handle_event(const SDL_Event& event) noexcept;

}  // namespace dusk::ui::drop
