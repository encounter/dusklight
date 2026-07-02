#pragma once

#include "component.hpp"
#include "ui.hpp"

#include <span>

namespace dusk::ui {

class Document {
public:
    explicit Document(const Rml::String& source, bool passive = false,
        DocumentScope scope = DocumentScope::None);
    virtual ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    virtual void show();
    virtual void hide(bool close);
    virtual void update();
    virtual bool focus();
    virtual bool visible() const;
    virtual bool active() const;
    // Cooperative close request (e.g. from the mod API): document types that
    // can be dismissed override this and return true.
    virtual bool request_dismiss() { return false; }

    DocumentScope scope() const { return mScope; }
    // Replaces this document's own extra RCSS sheet (empty string clears it)
    // and restyles. Returns false if the RCSS fails to parse.
    bool set_document_styles(const Rml::String& rcss);
    // Reapplies the document's styles as base sheet + scoped sheets (in order)
    // + the per-document sheet.
    void restyle(std::span<const Rml::StyleSheetContainer* const> sheets);

    void listen(Rml::Element* element, Rml::EventId event, ScopedEventListener::Callback callback,
        bool capture = false);
    void listen(Rml::Element* element, const Rml::String& event,
        ScopedEventListener::Callback callback, bool capture = false);
    void listen(Rml::EventId event, ScopedEventListener::Callback callback, bool capture = false) {
        listen(mDocument, event, std::move(callback), capture);
    }
    void listen(
        const Rml::String& event, ScopedEventListener::Callback callback, bool capture = false) {
        listen(mDocument, event, std::move(callback), capture);
    }
    void toggle() {
        if (visible()) {
            hide(false);
        } else {
            show();
        }
    }
    void push(std::unique_ptr<Document> document) {
        push_document(std::move(document));
        hide(false);
    }
    void pop(bool show = true) {
        hide(true);
        focus_top_document(show);
    }

    bool closed() const { return mClosed; }

    bool handle_nav_event(Rml::Event& event);

protected:
    virtual bool handle_nav_command(Rml::Event& event, NavCommand cmd);

    Rml::ElementDocument* mDocument;
    std::vector<std::unique_ptr<ScopedEventListener> > mListeners;
    // Pristine style sheets from the document source, snapshotted at load
    Rml::SharedPtr<Rml::StyleSheetContainer> mBaseStyleSheets;
    // Extra sheet applied to this document only, after the scoped sheets
    Rml::SharedPtr<Rml::StyleSheetContainer> mDocumentStyleSheets;
    DocumentScope mScope = DocumentScope::None;
    bool mPendingClose = false;
    bool mClosed = false;
    bool mPassive = false;
    bool mRestyled = false;
};

}  // namespace dusk::ui
