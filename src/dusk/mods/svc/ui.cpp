#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

#include <RmlUi/Core.h>

#include <string>

namespace dusk::mods::svc {
namespace {

std::string escape_rml(const char* text) {
    std::string out;
    if (text == nullptr) {
        return out;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += *p;
            break;
        }
    }
    return out;
}

class ModClickListener final : public Rml::EventListener {
public:
    ModClickListener(ModContext* context, UiButtonFn callback, void* userdata)
        : m_context{context}, m_callback{callback}, m_userdata{userdata} {}

    void ProcessEvent(Rml::Event&) override {
        const auto* mod = mod_from_context(m_context);
        if (mod != nullptr && mod->active && m_callback != nullptr) {
            m_callback(m_context, m_userdata);
        }
    }

    void OnDetach(Rml::Element*) override { delete this; }

private:
    ModContext* m_context = nullptr;
    UiButtonFn m_callback = nullptr;
    void* m_userdata = nullptr;
};

ModResult ui_register_tab(ModContext* context, const UiTab* tab) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || tab == nullptr || tab->build == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    mod->uiTabs.push_back(ModUiTabCallback{context, *tab});
    return MOD_OK;
}

ModResult ui_panel_add_section(ModContext*, PanelHandle panel, const char* text) {
    auto* pane = static_cast<Rml::Element*>(panel);
    if (pane == nullptr || text == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    auto el = pane->GetOwnerDocument()->CreateElement("div");
    el->SetClass("section-heading", true);
    el->SetInnerRML(escape_rml(text));
    pane->AppendChild(std::move(el));
    return MOD_OK;
}

ModResult ui_panel_add_button(ModContext* context, PanelHandle panel, const char* label,
    UiButtonFn callback, void* userdata) {
    auto* pane = static_cast<Rml::Element*>(panel);
    if (pane == nullptr || label == nullptr || callback == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    auto btn = pane->GetOwnerDocument()->CreateElement("button");
    btn->SetInnerRML(escape_rml(label));
    btn->AddEventListener(Rml::EventId::Click, new ModClickListener(context, callback, userdata));
    pane->AppendChild(std::move(btn));
    return MOD_OK;
}

ModResult ui_panel_add_badge_row(
    ModContext*, PanelHandle panel, const char* label, const int ok, ElemHandle* outElem) {
    auto* pane = static_cast<Rml::Element*>(panel);
    if (outElem != nullptr) {
        *outElem = nullptr;
    }
    if (pane == nullptr || label == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    auto* doc = pane->GetOwnerDocument();

    auto row = doc->CreateElement("div");
    row->SetClass("mod-info-row", true);

    auto badge = doc->CreateElement("span");
    badge->SetClass("achievement-badge", true);
    badge->SetClass(ok != 0 ? "unlocked" : "locked", true);
    badge->SetInnerRML(ok != 0 ? "PASS" : "WAIT");
    Rml::Element* badgePtr = row->AppendChild(std::move(badge));

    auto lbl = doc->CreateElement("span");
    lbl->SetClass("mod-info-value", true);
    lbl->SetInnerRML(escape_rml(label));
    row->AppendChild(std::move(lbl));

    pane->AppendChild(std::move(row));
    if (outElem != nullptr) {
        *outElem = static_cast<ElemHandle>(badgePtr);
    }
    return MOD_OK;
}

ModResult ui_panel_add_dyn_text(
    ModContext*, PanelHandle panel, const char* text, ElemHandle* outElem) {
    auto* pane = static_cast<Rml::Element*>(panel);
    if (outElem != nullptr) {
        *outElem = nullptr;
    }
    if (pane == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    auto el = pane->GetOwnerDocument()->CreateElement("div");
    el->SetInnerRML(escape_rml(text));
    Rml::Element* ptr = pane->AppendChild(std::move(el));
    if (outElem != nullptr) {
        *outElem = static_cast<ElemHandle>(ptr);
    }
    return MOD_OK;
}

ModResult ui_panel_add_progress(
    ModContext*, PanelHandle panel, const float value, ElemHandle* outElem) {
    auto* pane = static_cast<Rml::Element*>(panel);
    if (outElem != nullptr) {
        *outElem = nullptr;
    }
    if (pane == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    auto el = pane->GetOwnerDocument()->CreateElement("progress");
    el->SetClass("progress-health", true);
    el->SetAttribute("value", value);
    Rml::Element* ptr = pane->AppendChild(std::move(el));
    if (outElem != nullptr) {
        *outElem = static_cast<ElemHandle>(ptr);
    }
    return MOD_OK;
}

ModResult ui_elem_set_badge(ModContext*, ElemHandle elem, const int ok) {
    auto* el = static_cast<Rml::Element*>(elem);
    if (el == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    el->SetClass("unlocked", ok != 0);
    el->SetClass("locked", ok == 0);
    el->SetInnerRML(ok != 0 ? "PASS" : "WAIT");
    return MOD_OK;
}

ModResult ui_elem_set_text(ModContext*, ElemHandle elem, const char* text) {
    auto* el = static_cast<Rml::Element*>(elem);
    if (el == nullptr || text == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    el->SetInnerRML(escape_rml(text));
    return MOD_OK;
}

ModResult ui_elem_set_progress(ModContext*, ElemHandle elem, const float value) {
    auto* el = static_cast<Rml::Element*>(elem);
    if (el == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    el->SetAttribute("value", value);
    return MOD_OK;
}

constexpr UiService s_uiService{
    .header = SERVICE_HEADER(UiService, UI_SERVICE_MAJOR, UI_SERVICE_MINOR),
    .register_tab = ui_register_tab,
    .panel_add_section = ui_panel_add_section,
    .panel_add_button = ui_panel_add_button,
    .panel_add_badge_row = ui_panel_add_badge_row,
    .panel_add_dyn_text = ui_panel_add_dyn_text,
    .panel_add_progress = ui_panel_add_progress,
    .elem_set_badge = ui_elem_set_badge,
    .elem_set_text = ui_elem_set_text,
    .elem_set_progress = ui_elem_set_progress,
};

}  // namespace

const UiService& ui_service() {
    return s_uiService;
}

}  // namespace dusk::mods::svc
