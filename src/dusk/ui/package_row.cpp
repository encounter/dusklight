#include "package_row.hpp"

#include "fmt/format.h"

namespace dusk::ui {
namespace {

Rml::Element* create_row(Rml::Element* parent) {
    auto element = parent->GetOwnerDocument()->CreateElement("package-row");
    return parent->AppendChild(std::move(element));
}

}  // namespace

std::string format_bytes(uint64_t bytes) {
    constexpr double kiB = 1024.0;
    constexpr double miB = kiB * 1024.0;
    constexpr double giB = miB * 1024.0;
    if (bytes >= static_cast<uint64_t>(giB)) {
        return fmt::format("{:.1f} GiB", static_cast<double>(bytes) / giB);
    }
    if (bytes >= static_cast<uint64_t>(miB)) {
        return fmt::format("{:.1f} MiB", static_cast<double>(bytes) / miB);
    }
    if (bytes >= static_cast<uint64_t>(kiB)) {
        return fmt::format("{:.0f} KiB", static_cast<double>(bytes) / kiB);
    }
    return fmt::format("{} B", bytes);
}

const char* state_class(mods::queue::State state) {
    using enum mods::queue::State;
    switch (state) {
    case Downloading:
        return "downloading";
    case Paused:
        return "paused";
    case Retrying:
        return "retrying";
    case Verifying:
    case Installing:
    case Activating:
    case Uninstalling:
        return "installing";
    case Installed:
        return "installed";
    case Failed:
    case InstallFailed:
    case ActivationFailed:
        return "failed";
    case Canceled:
        return "canceled";
    case Queued:
        return "queued";
    }
    return "queued";
}

std::string state_label(const mods::queue::Item& item) {
    using enum mods::queue::State;
    switch (item.state) {
    case Queued:
        return "Queued";
    case Downloading:
        return "Downloading";
    case Paused:
        return "Paused";
    case Retrying:
        return fmt::format("Retrying in {}s", item.retrySeconds);
    case Verifying:
        return "Verifying";
    case Installing:
        return "Installing";
    case Activating:
        return "Activating";
    case Installed:
        return "Installed";
    case Failed:
        return item.local ? "Package failed" : "Download failed";
    case InstallFailed:
        return "Install failed";
    case ActivationFailed:
        return "Activation failed";
    case Uninstalling:
        return "Uninstalling";
    case Canceled:
        return "Canceled";
    }
    return {};
}

PackageRow::PackageRow(Rml::Element* parent) : Component{create_row(parent)} {
    auto* heading = append(mRoot, "package-row-heading");
    mName = append(heading, "package-row-name");
    mState = append(heading, "package-row-state");
    mProgress = append(mRoot, "progress");
    mDetail = append(mRoot, "package-row-detail");
}

void PackageRow::set_package(std::string name, std::string status, std::string detail,
    std::string stateClass, std::optional<float> progress) {
    mRoot->SetClassNames(fmt::format("package-row {}", stateClass));
    set_text_content(mName, name);
    set_text_content(mState, status);
    set_text_content(mDetail, detail);
    if (progress) {
        mProgress->SetProperty("display", "block");
        mProgress->SetAttribute("value", *progress);
    } else {
        mProgress->SetProperty("display", "none");
    }
}

Rml::Element* PackageRow::actions_root() {
    if (mActions == nullptr) {
        mActions = append(mRoot, "package-row-actions");
    }
    return mActions;
}

}  // namespace dusk::ui
