#pragma once

#include <cstdint>

// Extension dispatch + node overrides for the mod flow service, called from dMsgFlow_c
// (d_msg_flow.cpp) where message-flow query/event indices exceed the vanilla dispatch tables
// (mQueryList[53] / mEventList[43]) and where flow nodes may be redirected. Kept free of
// mods/svc/flow.h so game code does not pull the mod SDK; the registry lives in
// src/dusk/mods/loader/flow.cpp.

class fopAc_ac_c;

namespace dusk::mods {

// First extension index for each table (guarded by static_asserts in d_msg_flow.cpp).
inline constexpr uint16_t FlowFirstExtensionQuery = 53;
inline constexpr uint16_t FlowFirstExtensionEvent = 43;

// Dispatch an extension query. param is the branch node's decoded argument; mode is the call
// site's pass (0 = message-count scan, 1 = live branch). The return value selects the branch
// target (added to the node's next index). Unregistered ids warn once and return 0.
uint16_t flow_dispatch_query(uint16_t queryIdx, uint16_t param, fopAc_ac_c* speaker, int mode);

// Dispatch an extension event (side effects only; the flow advances to the next node
// regardless, mirroring the vanilla default case). Unregistered ids warn once.
void flow_dispatch_event(uint16_t eventIdx, const uint8_t params[4], fopAc_ac_c* speaker);

// Copy the winning 8-byte flow-node override for key into out_node and return true, or return
// false when no active mod overrides it. key = (message group << 16) | node index, with the
// shared bmg keyed under group 0 (see flow_node_override_key in d_msg_flow.cpp).
bool flow_node_override(uint32_t key, void* out_node);

}  // namespace dusk::mods
