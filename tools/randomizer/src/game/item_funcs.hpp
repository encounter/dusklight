#pragma once

#include <dolphin/types.h>

namespace randomizer::item_funcs {

// Randomizer-aware replacements for the d_item.cpp dispatch, used from
// execItemGet/checkItemGet pre-hooks while a seed is active.
void exec_item_get(u8 item_no);
int check_item_get(u8 item_no, int param);

} // namespace randomizer::item_funcs
