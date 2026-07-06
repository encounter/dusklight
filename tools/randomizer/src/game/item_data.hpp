#pragma once

#include "d/d_item_data.h"

namespace randomizer::item_data {

extern dItem_itemResource item_resource_randomizer[255];
extern dItem_fieldItemResource field_item_res_randomizer[255];
extern dItem_itemInfo item_info_randomizer[255];

// Copy the randomizer tables over the vanilla dItem_data tables (saving the vanilla
// contents first) / restore the saved vanilla contents.
void apply_tables();
void restore_tables();

}  // namespace randomizer::item_data
