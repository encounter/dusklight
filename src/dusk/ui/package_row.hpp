#pragma once

#include "component.hpp"
#include "dusk/mods/queue.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace dusk::ui {

std::string format_bytes(uint64_t bytes);
const char* state_class(mods::queue::State state);
std::string state_label(const mods::queue::Item& item);

class PackageRow : public Component {
public:
    explicit PackageRow(Rml::Element* parent);

    void set_package(std::string name, std::string status, std::string detail,
        std::string stateClass, std::optional<float> progress = {});
    Rml::Element* actions_root();

private:
    Rml::Element* mName = nullptr;
    Rml::Element* mState = nullptr;
    Rml::Element* mProgress = nullptr;
    Rml::Element* mDetail = nullptr;
    Rml::Element* mActions = nullptr;
};

}  // namespace dusk::ui
