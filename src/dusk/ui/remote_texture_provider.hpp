#pragma once

#include <cstdint>
#include <string_view>

namespace dusk::ui {

void register_remote_texture_provider() noexcept;
void unregister_remote_texture_provider() noexcept;
void update_remote_texture_provider() noexcept;
void set_remote_texture_dimensions(
    std::string_view source, uint32_t width, uint32_t height) noexcept;

}  // namespace dusk::ui
