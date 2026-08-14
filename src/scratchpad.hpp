#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>

#include <string>

namespace Summon {
    // The `summon` dispatcher. Arg is "<name>" or "<name>, <command>".
    SDispatchResult dispatch(std::string arg);

    // Event bus listeners.
    void onWindowOpen(PHLWINDOW window);
    void onWindowActive(PHLWINDOW window, Desktop::eFocusReason reason);
}
