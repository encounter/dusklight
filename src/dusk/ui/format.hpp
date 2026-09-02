#pragma once

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dusk::ui {

struct ByteFormat {
    int gibFractionDigits = 1;
    int mibFractionDigits = 1;
};

inline std::string format_bytes(uint64_t bytes, ByteFormat options = {}) {
    constexpr double kiB = 1024.0;
    constexpr double miB = kiB * 1024.0;
    constexpr double giB = miB * 1024.0;
    if (bytes >= static_cast<uint64_t>(giB)) {
        return fmt::format(
            "{:.{}f} GiB", static_cast<double>(bytes) / giB, options.gibFractionDigits);
    }
    if (bytes >= static_cast<uint64_t>(miB)) {
        return fmt::format(
            "{:.{}f} MiB", static_cast<double>(bytes) / miB, options.mibFractionDigits);
    }
    if (bytes >= static_cast<uint64_t>(kiB)) {
        return fmt::format("{:.0f} KiB", static_cast<double>(bytes) / kiB);
    }
    return fmt::format("{} B", bytes);
}

// Truncates without splitting a UTF-8 sequence.
inline std::string snippet(std::string_view text, size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return std::string{text};
    }
    size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return fmt::format("{}...", text.substr(0, end));
}

}  // namespace dusk::ui
