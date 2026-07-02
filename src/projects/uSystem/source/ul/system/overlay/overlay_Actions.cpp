// overlay_Actions.cpp — atomic flag definitions for the overlay bridge.
// See overlay_Actions.hpp for design rationale.

#include <ul/system/overlay/overlay_Actions.hpp>

namespace ul::system::overlay {

    std::atomic<bool> g_RequestClose    { false };
    std::atomic<bool> g_RequestMinimize { false };

}  // namespace ul::system::overlay
