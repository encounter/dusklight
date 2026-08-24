#pragma once

#include <dolphin/types.h>

#include <string_view>

namespace randomizer::item {
void exec_item_get(u8 item_no);
int check_item_get(u8 item_no, int param);

void apply_item_data_tables(std::string_view seedHash);
void restore_item_data_tables();
} // namespace randomizer::item
