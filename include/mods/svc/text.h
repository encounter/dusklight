#pragma once

#include "mods/api.h"

#define TEXT_SERVICE_ID "dev.twilitrealm.dusklight.text"
#define TEXT_SERVICE_MAJOR 1u
#define TEXT_SERVICE_MINOR 0u

/*
 * Message-text overrides.
 *
 * Replaces the text of individual game messages at display time, keyed by the stable
 * (group, message id) pair from the message data: group 0 for the shared bmg (message ids
 * < 5000), the stage's message group otherwise. Text is the raw message byte string as the
 * game's processors expect it (including control/escape sequences), NUL-terminated; the host
 * copies it, and overridden text applied to a live textbox stays valid even if the override
 * is removed while displayed.
 *
 * The game applies overrides for the *current* message lookup regardless of language — a mod
 * shipping per-language text registers the strings for the language the game is running in
 * (query it via game linkage or re-register on change).
 *
 * When several mods override one message, the later-loaded mod wins (warning logged); a
 * resolver callback that returns NULL passes to the next-lower-priority override, then to the
 * vanilla text. Callbacks run on the game thread during message setup — the returned pointer
 * only needs to stay valid for the duration of the call.
 *
 * Registrations are owned by the calling mod and removed automatically when it is disabled,
 * reloaded, or fails.
 */

/* Resolver: return replacement bytes (copied during the call) or NULL to pass. */
typedef const char* (*TextMessageFn)(
    ModContext* ctx, uint16_t group, uint16_t message_id, const char* original, void* user_data);

typedef struct TextService {
    ServiceHeader header;

    /*
     * Override a message with fixed text (copied). Replaces the calling mod's previous
     * override for the same key, if any.
     */
    ModResult (*override_message)(
        ModContext* ctx, uint16_t group, uint16_t message_id, const char* text);

    /*
     * Override a message with a resolver callback (for dynamic text, e.g. live counters).
     * Replaces the calling mod's previous override for the same key, if any.
     */
    ModResult (*override_message_fn)(ModContext* ctx, uint16_t group, uint16_t message_id,
        TextMessageFn fn, void* user_data);

    /* Remove the calling mod's override for the key. */
    ModResult (*clear_message_override)(ModContext* ctx, uint16_t group, uint16_t message_id);
} TextService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<TextService> {
    static constexpr const char* id = TEXT_SERVICE_ID;
    static constexpr uint16_t major_version = TEXT_SERVICE_MAJOR;
};
#endif
