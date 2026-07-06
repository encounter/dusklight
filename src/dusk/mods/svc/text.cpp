#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

namespace dusk::mods::svc {
namespace {

ModResult text_override_message_(
    ModContext* context, uint16_t group, uint16_t messageId, const char* text) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || text == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return text_override_message(*mod, group, messageId, text);
}

ModResult text_override_message_fn_(
    ModContext* context, uint16_t group, uint16_t messageId, TextMessageFn fn, void* userData) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || fn == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return text_override_message_fn(*mod, group, messageId, fn, userData);
}

ModResult text_clear_message_override_(ModContext* context, uint16_t group, uint16_t messageId) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return text_clear_override(*mod, group, messageId);
}

constexpr TextService s_textService{
    .header = SERVICE_HEADER(TextService, TEXT_SERVICE_MAJOR, TEXT_SERVICE_MINOR),
    .override_message = text_override_message_,
    .override_message_fn = text_override_message_fn_,
    .clear_message_override = text_clear_message_override_,
};

}  // namespace

const TextService& text_service() {
    return s_textService;
}
}  // namespace dusk::mods::svc
