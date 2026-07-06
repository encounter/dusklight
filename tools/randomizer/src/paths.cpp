#include "paths.hpp"

#include "dusk/data.hpp"

namespace randomizer::paths {

std::filesystem::path GetRandomizerPath() {
    return dusk::data::configured_data_path() / "randomizer";
}

std::filesystem::path GetRandomizerSettingsPath() {
    return GetRandomizerPath() / "settings.yaml";
}

std::filesystem::path GetRandomizerPreferencesPath() {
    return GetRandomizerPath() / "preferences.yaml";
}

std::filesystem::path GetRandomizerSeedsPath() {
    return GetRandomizerPath() / "seeds";
}

}  // namespace randomizer::paths
