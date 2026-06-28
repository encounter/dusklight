#pragma once

#include "mods/api.h"

#define UI_SERVICE_ID "dev.twilitrealm.dusklight.ui"
#define UI_SERVICE_MAJOR 1u
#define UI_SERVICE_MINOR 0u

typedef void* PanelHandle;
typedef void* ElemHandle;

typedef ModResult (*UiBuildFn)(
    ModContext* ctx, PanelHandle panel, void* userdata, ModError* out_error);
typedef ModResult (*UiUpdateFn)(ModContext* ctx, void* userdata, ModError* out_error);
typedef void (*UiButtonFn)(ModContext* ctx, void* userdata);

typedef struct UiTab {
    uint32_t struct_size;
    UiBuildFn build;
    UiUpdateFn update;
    void* userdata;
} UiTab;

#define UI_TAB_INIT {sizeof(UiTab), NULL, NULL, NULL}

typedef struct UiService {
    ServiceHeader header;

    ModResult (*register_tab)(ModContext* ctx, const UiTab* tab);

    ModResult (*panel_add_section)(ModContext* ctx, PanelHandle panel, const char* text);
    ModResult (*panel_add_button)(
        ModContext* ctx, PanelHandle panel, const char* label, UiButtonFn callback, void* userdata);
    ModResult (*panel_add_badge_row)(
        ModContext* ctx, PanelHandle panel, const char* label, int ok, ElemHandle* out_elem);
    ModResult (*panel_add_dyn_text)(
        ModContext* ctx, PanelHandle panel, const char* text, ElemHandle* out_elem);
    ModResult (*panel_add_progress)(
        ModContext* ctx, PanelHandle panel, float value, ElemHandle* out_elem);

    ModResult (*elem_set_badge)(ModContext* ctx, ElemHandle elem, int ok);
    ModResult (*elem_set_text)(ModContext* ctx, ElemHandle elem, const char* text);
    ModResult (*elem_set_progress)(ModContext* ctx, ElemHandle elem, float value);
} UiService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<UiService> {
    static constexpr const char* id = UI_SERVICE_ID;
    static constexpr uint16_t major_version = UI_SERVICE_MAJOR;
};
#endif
