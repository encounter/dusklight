#include "queue_window.hpp"

#include "button.hpp"
#include "dusk/mods/queue.hpp"
#include "fmt/format.h"
#include "package_row.hpp"
#include "pane.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dusk::ui {
namespace {

class QueueRow final : public PackageRow {
public:
    QueueRow(Rml::Element* parent, std::string id) : PackageRow{parent}, mId{std::move(id)} {
        auto* actions = actions_root();
        auto pause = std::make_unique<Button>(actions, "Pause");
        mPause = pause.get();
        mPause->on_pressed([this] {
            const auto item = mods::queue::find(mId);
            if (!item) {
                return;
            }
            switch (item->state) {
            case mods::queue::State::Paused:
                mods::queue::resume(mId);
                break;
            case mods::queue::State::Failed:
            case mods::queue::State::InstallFailed:
            case mods::queue::State::ActivationFailed:
                mods::queue::retry(mId);
                break;
            default:
                mods::queue::pause(mId);
                break;
            }
        });
        mChildren.push_back(std::move(pause));

        auto cancel = std::make_unique<Button>(actions, "Cancel");
        mCancel = cancel.get();
        mCancel->on_pressed([this] {
            const auto item = mods::queue::find(mId);
            if (!item) {
                return;
            }
            mods::queue::cancel(mId);
            if (item->state == mods::queue::State::Installed ||
                item->state == mods::queue::State::Failed ||
                item->state == mods::queue::State::InstallFailed ||
                item->state == mods::queue::State::Canceled)
            {
                mods::queue::clear_finished();
            }
        });
        mChildren.push_back(std::move(cancel));
        update();
    }

    void update() override {
        const auto item = mods::queue::find(mId);
        if (!item) {
            mRoot->SetProperty("display", "none");
            return;
        }
        mRoot->SetProperty("display", "flex");
        const float progress =
            item->total == 0 ?
                0.0f :
                std::clamp(static_cast<float>(item->completed) / static_cast<float>(item->total),
                    0.0f, 1.0f);
        std::string detail;
        if (item->completed != 0 || item->state == mods::queue::State::Downloading ||
            item->state == mods::queue::State::Paused)
        {
            detail =
                fmt::format("{} / {}", format_bytes(item->completed), format_bytes(item->total));
        } else {
            detail = format_bytes(item->total);
        }
        if (!item->message.empty()) {
            detail = fmt::format("{} · {}", detail, item->message);
        }
        const auto name =
            item->version.empty() ? item->name : fmt::format("{} {}", item->name, item->version);
        set_package(name, state_label(*item), detail, state_class(item->state), progress);

        const bool pauseVisible = (!item->local && item->state == mods::queue::State::Queued) ||
                                  item->state == mods::queue::State::Downloading ||
                                  item->state == mods::queue::State::Paused ||
                                  item->state == mods::queue::State::Retrying ||
                                  item->state == mods::queue::State::Failed ||
                                  item->state == mods::queue::State::InstallFailed ||
                                  item->state == mods::queue::State::ActivationFailed;
        mPause->root()->SetProperty("display", pauseVisible ? "block" : "none");
        if (item->state == mods::queue::State::Paused) {
            mPause->set_text("Resume");
        } else if (item->state == mods::queue::State::Failed ||
                   item->state == mods::queue::State::InstallFailed ||
                   item->state == mods::queue::State::ActivationFailed)
        {
            mPause->set_text("Retry");
        } else {
            mPause->set_text("Pause");
        }

        const bool cancelVisible = item->state != mods::queue::State::Installing &&
                                   item->state != mods::queue::State::Activating &&
                                   item->state != mods::queue::State::Uninstalling;
        mCancel->root()->SetProperty("display", cancelVisible ? "block" : "none");
        mCancel->set_text(item->state == mods::queue::State::Installed ||
                                  item->state == mods::queue::State::Failed ||
                                  item->state == mods::queue::State::InstallFailed ||
                                  item->state == mods::queue::State::ActivationFailed ||
                                  item->state == mods::queue::State::Canceled ?
                              "Clear" :
                              "Cancel");
        Component::update();
    }

private:
    std::string mId;
    Button* mPause = nullptr;
    Button* mCancel = nullptr;
};

}  // namespace

QueueWindow::QueueWindow(std::string focusId)
    : Modal{Props{
          .title = "Installs",
          .bodyText = "Preparing installs...",
          .actions =
              {
                  ModalAction{"Close", [](Modal& modal) { modal.pop(); }, {}},
                  ModalAction{"Pause all", [](Modal&) { mods::queue::pause_all(); },
                      [] { return !mods::queue::has_active_items(); }},
                  ModalAction{"Clear finished", [](Modal&) { mods::queue::clear_finished(); }, {}},
              },
          .variant = "install-queue",
      }},
      mFocusId{std::move(focusId)} {
    content_pane();
    rebuild_rows();
}

void QueueWindow::update() {
    const auto queueItems = mods::queue::items();
    std::vector<std::string> ids;
    ids.reserve(queueItems.size());
    size_t active = 0;
    for (const auto& item : queueItems) {
        ids.push_back(item.id);
        if (item.state != mods::queue::State::Installed &&
            item.state != mods::queue::State::Failed &&
            item.state != mods::queue::State::InstallFailed &&
            item.state != mods::queue::State::ActivationFailed &&
            item.state != mods::queue::State::Canceled)
        {
            ++active;
        }
    }
    if (ids != mItemIds) {
        rebuild_rows();
    }
    set_body_text(fmt::format("{} active · {} total", active, queueItems.size()));
    Modal::update();
}

void QueueWindow::rebuild_rows() {
    auto& pane = content_pane();
    pane.clear();
    mItemIds.clear();

    const auto queueItems = mods::queue::items();
    if (queueItems.empty()) {
        pane.add_text("No installs.");
        return;
    }
    for (const auto& item : queueItems) {
        mItemIds.push_back(item.id);
        auto& row = pane.add_child<QueueRow>(item.id);
        if (!mFocusId.empty() && item.id == mFocusId) {
            row.focus();
            mFocusId.clear();
        }
    }
}

}  // namespace dusk::ui
