#include "scratchpad.hpp"
#include "globals.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/supplementary/executor/Executor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include <hyprutils/utils/ScopeGuard.hpp>

#include <csignal>
#include <cstring>
#include <tuple>

using namespace Summon;

static constexpr const char* SPECIAL_PREFIX = "special:";

static std::string           trim(const std::string& s) {
    const auto B = s.find_first_not_of(" \t");
    if (B == std::string::npos)
        return "";
    return s.substr(B, s.find_last_not_of(" \t") - B + 1);
}

static SDispatchResult fail(std::string msg) {
    return SDispatchResult{.success = false, .error = std::move(msg)};
}

// Keyed by workspace NAME, not id: a special workspace is destroyed with its
// last window and returns with a fresh id.
static SScratchpad* padForWorkspace(const PHLWORKSPACE& ws) {
    if (!ws || !ws->m_isSpecialWorkspace || !ws->m_name.starts_with(SPECIAL_PREFIX))
        return nullptr;

    const auto IT = g_pGlobalState->pads.find(ws->m_name.substr(strlen(SPECIAL_PREFIX)));
    return IT == g_pGlobalState->pads.end() ? nullptr : &IT->second;
}

// Resolve "special:<name>", creating it if absent. getWorkspaceIDNameFromString
// returns an existing id or allocates one, so create() only runs for the latter.
static PHLWORKSPACE resolveWorkspace(const std::string& name, const PHLMONITOR& monitor) {
    const auto& [ID, WSNAME, IS_AUTO] = getWorkspaceIDNameFromString(std::string{SPECIAL_PREFIX} + name);

    if (ID == WORKSPACE_INVALID || !State::workspaceState()->isSpecial(ID))
        return nullptr;

    if (auto ws = State::workspaceState()->query().id(ID).run())
        return ws;

    return State::workspaceState()->create(ID, monitor->m_id, WSNAME);
}

// Size and centre a pad's floating windows against the monitor showing it.
// Geometry is a fraction because a fixed pixel size is a different share of
// every display. m_size is the LOGICAL size, so this is scale-correct.
static void applyGeometry(const PHLWORKSPACE& ws, const PHLMONITOR& monitor, const SScratchpad& pad) {
    const float W = pad.width.value_or(g_pGlobalState->config.width->value());
    const float H = pad.height.value_or(g_pGlobalState->config.height->value());

    if (W <= 0.F || H <= 0.F)
        return;

    const Vector2D SIZE = {monitor->m_size.x * W, monitor->m_size.y * H};
    const Vector2D POS  = monitor->m_position + (monitor->m_size - SIZE) / 2.F;

    for (auto const& w : Desktop::windowState()->windows()) {
        // Tiled windows belong to the layout.
        if (!w || w->m_workspace != ws || !w->m_isMapped || !w->m_isFloating)
            continue;

        // Best effort: these refuse on a fullscreen window, deliberately.
        std::ignore = Config::Actions::resize(SIZE, false, w);
        std::ignore = Config::Actions::move(POS, false, w);
    }
}

static bool processAlive(uint64_t pid) {
    return pid != 0 && ::kill(sc<pid_t>(pid), 0) == 0;
}

SDispatchResult Summon::dispatch(std::string arg) {
    // "<name>" or "<name>, <command>" - the inline form is for .conf users.
    std::string name    = trim(arg);
    std::string command = "";

    if (const auto COMMA = arg.find(','); COMMA != std::string::npos) {
        name    = trim(arg.substr(0, COMMA));
        command = trim(arg.substr(COMMA + 1));
    }

    if (name.empty())
        return fail("summon: expected a scratchpad name");

    auto IT = g_pGlobalState->pads.find(name);

    if (IT == g_pGlobalState->pads.end()) {
        if (command.empty())
            return fail("summon: unknown scratchpad \"" + name + "\" - define it with hl.plugin.hyprsummon.define{} or pass \"" + name + ", <command>\"");

        IT = g_pGlobalState->pads.emplace(name, SScratchpad{.name = name, .command = command}).first;
    } else if (!command.empty())
        IT->second.command = command;

    auto&      pad = IT->second;

    const auto PMONITOR = Desktop::focusState()->monitor();
    if (!PMONITOR)
        return fail("summon: no focused monitor");

    const auto WS = resolveWorkspace(name, PMONITOR);
    if (!WS)
        return fail("summon: could not resolve special workspace for \"" + name + "\"");

    // Empty pad: lazy launch. This is the case togglespecialworkspace cannot
    // serve - it shows an empty workspace and leaves you there.
    if (WS->getWindowCount() == 0) {
        if (pad.command.empty())
            return fail("summon: scratchpad \"" + name + "\" has no windows and no command to launch");

        // No duplicates while an app is still starting; a dead pid means the
        // last launch failed and the pad is retryable.
        if (!processAlive(pad.launchPid)) {
            // spawnWithRules passes the workspace to the child through
            // HL_INITIAL_WORKSPACE_TOKEN, so the window is placed before its
            // first frame rather than moved after mapping.
            pad.launchPid = Config::Supplementary::executor()->spawnWithRules(pad.command, WS).value_or(0);

            if (pad.launchPid == 0)
                return fail("summon: failed to launch \"" + pad.command + "\"");
        }

        // Show it here so the window arrives on the focused monitor.
        if (PMONITOR->m_activeSpecialWorkspace != WS)
            PMONITOR->setSpecialWorkspace(WS);

        return {};
    }

    // Already showing here: put it away.
    if (PMONITOR->m_activeSpecialWorkspace == WS) {
        PMONITOR->setSpecialWorkspace(nullptr);
        return {};
    }

    // setSpecialWorkspace steals the pad off another monitor and reassigns its
    // windows, so there is no move-then-show race.
    PMONITOR->setSpecialWorkspace(WS);
    applyGeometry(WS, PMONITOR, pad);

    if (g_pGlobalState->config.focusOnSummon->value()) {
        if (const auto PLAST = WS->getLastFocusedWindow())
            Desktop::focusState()->fullWindowFocus(PLAST, Desktop::FOCUS_REASON_TOGGLE_SPECIAL_WORKSPACE);
    }

    return {};
}

void Summon::onWindowOpen(PHLWINDOW window) {
    // g_pGlobalState is gone between PLUGIN_EXIT and the .so unloading, which
    // is every `hyprpm reload`.
    if (!window || !g_pGlobalState)
        return;

    auto* pad = padForWorkspace(window->m_workspace);
    if (!pad)
        return;

    pad->launchPid = 0;

    if (pad->makeFloating && !window->m_isFloating)
        std::ignore = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, window);

    const auto PMONITOR = window->m_monitor.lock() ? window->m_monitor.lock() : Desktop::focusState()->monitor();
    if (PMONITOR)
        applyGeometry(window->m_workspace, PMONITOR, *pad);
}

void Summon::onWindowActive(PHLWINDOW window, Desktop::eFocusReason) {
    if (!window || !g_pGlobalState || g_pGlobalState->handlingFocus)
        return;

    // Hiding a pad moves focus, re-emitting this event inside itself.
    g_pGlobalState->handlingFocus = true;
    Hyprutils::Utils::CScopeGuard x([] { g_pGlobalState->handlingFocus = false; });

    for (auto const& m : State::monitorState()->monitors()) {
        const auto SPECIAL = m->m_activeSpecialWorkspace;
        if (!SPECIAL || SPECIAL == window->m_workspace)
            continue;

        auto* pad = padForWorkspace(SPECIAL);
        if (!pad)
            continue;

        if (!pad->hideOnFocusLoss.value_or(g_pGlobalState->config.hideOnFocusLoss->value()))
            continue;

        m->setSpecialWorkspace(nullptr);
    }
}
