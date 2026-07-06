#pragma once

#include <string>

#include "../../generator/seedgen/config.hpp"
#include "../../generator/seedgen/settings.hpp"

// Non-UI pieces of the branch's rando_config.cpp: the persistent generator config
// (settings.yaml / preferences.yaml under the randomizer data dir) and setting lookup.

namespace randomizer::ui {

seedgen::config::Config& GetRandomizerConfig();
void SaveRandomizerConfig();
// nullptr when the key is unknown (the branch used DuskLog.fatal here).
seedgen::settings::Setting* FindSetting(const std::string& key);
// Seeds the config with a random seed string when empty; true if one was created.
bool TryCreateRandomSeed();

}  // namespace randomizer::ui
