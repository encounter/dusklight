#include "drop.hpp"

#include "borealis/io.hpp"
#include "saves_window.hpp"
#include "ui.hpp"

#include <SDL3/SDL_events.h>

#include <filesystem>

namespace dusk::ui::drop {

void handle_event(const SDL_Event& event) noexcept {
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS)
    if (event.type != SDL_EVENT_DROP_FILE || event.drop.data == nullptr) {
        return;
    }
    if (!is_prelaunch_open()) {
        push_toast({
            .title = "Save Import",
            .content = "Return to the main menu before importing save files.",
            .duration = std::chrono::seconds(4),
        });
        return;
    }

    const std::string location = event.drop.data;
    const std::string displayName = borealis::io::display_name(location);
    if (borealis::io::fs_path_from_utf8(displayName).extension() == ".dusk") {
        // Mod bundle installation can be added here when ModLoader exposes an install API.
        push_toast({
            .title = "Mod Install",
            .content = "Drag-and-drop mod installation is not available yet.",
            .duration = std::chrono::seconds(4),
        });
        return;
    }
    import_save_location(location);
#else
    (void)event;
#endif
}

}  // namespace dusk::ui::drop
