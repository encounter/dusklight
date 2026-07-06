#pragma once
// Ported from the randomizer branch's getLayerNo_common_common override ladder.
// Returns true and writes out_layer (0..14, or -1 = keep previous layer) when the
// randomizer determines the layer for (stage_name, room_no); false = fall through
// to the vanilla event-bit ladder.
namespace randomizer::layers {
bool resolve_layer(const char* stage_name, int room_no, int* out_layer);
}
