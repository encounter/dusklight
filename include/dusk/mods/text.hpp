#pragma once

#include <cstdint>

// Message-text overrides for the mod text service, applied from
// JMessage::TControl::setMessageCode_inSequence_ right after the control's text pointers are
// set from the resource cache. Kept free of mods/svc/text.h and JMessage headers so the seam
// stays a one-liner; the registry lives in src/dusk/mods/loader/text.cpp.

namespace dusk::mods {

// Apply a registered override to the control (JMessage::TControl*), if any. groupID/index are
// the runtime message-code pair; the stable (group, message id) key is derived host-side from
// the processor's (JMessage::TProcessor const*) message entry. Overridden text is copied into
// host storage owned per control, so registry changes never dangle a displayed message.
void text_apply_override(void* control, const void* processor, int groupID, int index);

// Resolve an override by stable key without a control: returns the resolved bytes (valid until
// the next call from any caller) or nullptr when no active mod overrides the message. original
// is handed to resolver callbacks. Exposed for tests and host UI reuse.
const char* text_lookup(uint16_t group, uint16_t messageId, const char* original);

}  // namespace dusk::mods
