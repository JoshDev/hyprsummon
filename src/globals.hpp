#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

inline HANDLE PHANDLE = nullptr;

// One named scratchpad. Optional fields fall back to the plugin:hyprsummon:*
// globals.
struct SScratchpad {
    std::string          name;
    std::string          command;

    std::optional<float> width;
    std::optional<float> height;
    std::optional<bool>  hideOnFocusLoss;

    bool                 makeFloating = true;

    // PID of an in-flight launch, 0 when idle. A liveness check on it rather
    // than a bool, so a launch that dies without mapping a window leaves the
    // pad retryable instead of wedged.
    uint64_t launchPid = 0;
};

struct SGlobalState {
    std::unordered_map<std::string, SScratchpad> pads;

    struct {
        SP<Config::Values::CFloatValue> width;
        SP<Config::Values::CFloatValue> height;
        SP<Config::Values::CBoolValue>  hideOnFocusLoss;
        SP<Config::Values::CBoolValue>  focusOnSummon;
    } config;

    // Hiding a pad moves focus, which re-emits window.active into the listener
    // that is still on the stack.
    bool handlingFocus = false;
};

inline UP<SGlobalState> g_pGlobalState;
