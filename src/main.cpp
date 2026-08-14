#define WLR_USE_UNSTABLE

#include "globals.hpp"
#include "scratchpad.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static void notify(const std::string& msg, const CHyprColor& color = CHyprColor{1.0, 0.4, 0.2, 1.0}) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprsummon] " + msg, color, 5000);
}

// Lua bindings, exposed as hl.plugin.hyprsummon.*. These do not exist during
// the cold config parse - see the README on wrapping calls in pcall.

static std::optional<float> optFloatField(lua_State* L, int idx, const char* field) {
    lua_getfield(L, idx, field);
    std::optional<float> out;
    if (lua_isnumber(L, -1))
        out = sc<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return out;
}

static std::optional<bool> optBoolField(lua_State* L, int idx, const char* field) {
    lua_getfield(L, idx, field);
    std::optional<bool> out;
    if (lua_isboolean(L, -1))
        out = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return out;
}

static std::string stringField(lua_State* L, int idx, const char* field) {
    lua_getfield(L, idx, field);
    std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return out;
}

static int luaDefine(lua_State* L) {
    if (!lua_istable(L, 1))
        return luaL_error(L, "hyprsummon.define: expected a table { name, command, width, height, hide_on_focus_loss, float }");

    SScratchpad pad;
    pad.name    = stringField(L, 1, "name");
    pad.command = stringField(L, 1, "command");

    if (pad.name.empty())
        return luaL_error(L, "hyprsummon.define: 'name' is required and must be a string");

    if (pad.name.starts_with("special:"))
        return luaL_error(L, "hyprsummon.define: 'name' is the bare pad name, without the \"special:\" prefix");

    if (pad.command.empty())
        return luaL_error(L, "hyprsummon.define: 'command' is required and must be a string");

    pad.width           = optFloatField(L, 1, "width");
    pad.height          = optFloatField(L, 1, "height");
    pad.hideOnFocusLoss = optBoolField(L, 1, "hide_on_focus_loss");
    pad.makeFloating    = optBoolField(L, 1, "float").value_or(true);

    for (const auto& [FIELD, VAL] : {std::pair{"width", pad.width}, std::pair{"height", pad.height}}) {
        if (VAL && (*VAL <= 0.F || *VAL > 1.F))
            return luaL_error(L, "hyprsummon.define: '%s' is a fraction of the monitor and must be in (0, 1]", FIELD);
    }

    // Keep any in-flight launch, so a config reload mid-start does not spawn a
    // second copy.
    if (const auto IT = g_pGlobalState->pads.find(pad.name); IT != g_pGlobalState->pads.end())
        pad.launchPid = IT->second.launchPid;

    g_pGlobalState->pads.insert_or_assign(pad.name, std::move(pad));

    return 0;
}

static int luaSummon(lua_State* L) {
    if (!lua_isstring(L, 1))
        return luaL_error(L, "hyprsummon.summon: expected a scratchpad name");

    const auto RESULT = Summon::dispatch(lua_tostring(L, 1));

    // A notification rather than a raised error: this runs on a keypress, and a
    // typo'd pad name should not throw a traceback on every repeat.
    if (!RESULT.success)
        notify(RESULT.error);

    return 0;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprsummon] Failure in initialization: Version mismatch (headers ver is not equal to running hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprsummon] Version mismatch");
    }

    g_pGlobalState = makeUnique<SGlobalState>();

    g_pGlobalState->config.width  = makeShared<Config::Values::CFloatValue>("plugin:hyprsummon:width", "Default scratchpad width as a fraction of the monitor", 0.6F,
                                                                            Config::Values::SFloatValueOptions{.min = 0.05F, .max = 1.F});
    g_pGlobalState->config.height = makeShared<Config::Values::CFloatValue>("plugin:hyprsummon:height", "Default scratchpad height as a fraction of the monitor", 0.7F,
                                                                            Config::Values::SFloatValueOptions{.min = 0.05F, .max = 1.F});
    g_pGlobalState->config.hideOnFocusLoss =
        makeShared<Config::Values::CBoolValue>("plugin:hyprsummon:hide_on_focus_loss", "Hide a scratchpad when focus moves to a window outside it", false);
    g_pGlobalState->config.focusOnSummon = makeShared<Config::Values::CBoolValue>("plugin:hyprsummon:focus_on_summon", "Focus the scratchpad's window when it is summoned", true);

    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.width);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.height);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.hideOnFocusLoss);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_pGlobalState->config.focusOnSummon);

    HyprlandAPI::addDispatcherV2(PHANDLE, "summon", [](std::string arg) { return Summon::dispatch(std::move(arg)); });

    HyprlandAPI::addLuaFunction(PHANDLE, "hyprsummon", "define", ::luaDefine);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprsummon", "summon", ::luaSummon);

    // static: the handles must outlive pluginInit or the subscription drops.
    static auto P1 = Event::bus()->m_events.window.open.listen([](PHLWINDOW w) { Summon::onWindowOpen(w); });
    static auto P2 = Event::bus()->m_events.window.active.listen([](PHLWINDOW w, Desktop::eFocusReason r) { Summon::onWindowActive(w, r); });

    HyprlandAPI::reloadConfig();

    return {"hyprsummon", "Lazy-launching, monitor-aware scratchpads for Hyprland", "JoshDev", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_pGlobalState.reset();
}
