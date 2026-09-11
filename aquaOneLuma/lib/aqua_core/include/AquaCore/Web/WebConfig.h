#pragma once

#include <stdint.h>

namespace AquaCore {
namespace Web {

enum class NavigationSection : uint8_t {
    Dashboard = 0U,
    Control,
    Automation,
    Settings,
    Diagnostics,
    System
};

constexpr uint32_t navigationSectionMask(NavigationSection section) {
    return 1UL << static_cast<uint8_t>(section);
}

constexpr uint32_t ALL_NAVIGATION_SECTIONS =
    navigationSectionMask(NavigationSection::Dashboard) |
    navigationSectionMask(NavigationSection::Control) |
    navigationSectionMask(NavigationSection::Automation) |
    navigationSectionMask(NavigationSection::Settings) |
    navigationSectionMask(NavigationSection::Diagnostics) |
    navigationSectionMask(NavigationSection::System);

struct WebConfig {
    bool enabled = false;
    uint16_t port = 80U;
    uint32_t navigationMask =
        navigationSectionMask(NavigationSection::Dashboard) |
        navigationSectionMask(NavigationSection::Diagnostics) |
        navigationSectionMask(NavigationSection::System);
};

} // namespace Web
} // namespace AquaCore
