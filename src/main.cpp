#include "globals.hpp"
#include "Momentum.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/version.h>

#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

static Hyprutils::Signal::CHyprSignalListener              g_axisListener;
static Hyprutils::Signal::CHyprSignalListener              g_buttonListener;
static Hyprutils::Signal::CHyprSignalListener              g_focusListener;
static std::vector<Hyprutils::Signal::CHyprSignalListener> g_touchpadListeners;

static void registerConfig() {
    using namespace Config::Values;
    // All keys are valid Lua identifiers on purpose, so
    // hl.config({ plugin = { momentum = { ... } } }) has a chance of working
    // in Lua-configured setups, and hl.plugin.momentum.set() always works.
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CIntValue>("plugin:momentum:enabled", "Enable momentum scrolling", 1));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CFloatValue>("plugin:momentum:decay", "Velocity retained per millisecond of glide (0.998 = macOS feel)", 0.998F));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CFloatValue>("plugin:momentum:boost", "Multiplier applied to the lift velocity", 1.0F));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CFloatValue>("plugin:momentum:fling_threshold", "Minimum lift speed (px/s) that starts a glide", 80.0F));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CFloatValue>("plugin:momentum:min_velocity", "Glide stops below this speed (px/s)", 15.0F));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CFloatValue>("plugin:momentum:max_velocity", "Launch velocity cap (px/s)", 8000.0F));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CIntValue>("plugin:momentum:disable_in_browser", "Skip apps that have their own fling", 1));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CStringValue>("plugin:momentum:disabled_classes", "Space/comma separated window classes to skip", ""));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<CIntValue>("plugin:momentum:debug", "1 = log to Hyprland log, 2 = also notify", 0));
}

// ---- hl.plugin.momentum.* -------------------------------------------------

static int luaSet(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    if (lua_isnumber(L, 2))
        g_numOverrides[key] = lua_tonumber(L, 2);
    else
        g_strOverrides[key] = luaL_checkstring(L, 2);
    return 0;
}

static int luaUnset(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    g_numOverrides.erase(key);
    g_strOverrides.erase(key);
    return 0;
}

static int luaEnable(lua_State* L) {
    if (g_pMomentum)
        g_pMomentum->setAppRule(luaL_checkstring(L, 1), true);
    return 0;
}

static int luaDisable(lua_State* L) {
    if (g_pMomentum)
        g_pMomentum->setAppRule(luaL_checkstring(L, 1), false);
    return 0;
}

static int luaResetRules(lua_State* /*L*/) {
    if (g_pMomentum)
        g_pMomentum->resetAppRules();
    return 0;
}

static int luaStop(lua_State* /*L*/) {
    if (g_pMomentum)
        g_pMomentum->stop("lua stop");
    return 0;
}

static void registerLua() {
    HyprlandAPI::addLuaFunction(PHANDLE, "momentum", "set", luaSet);
    HyprlandAPI::addLuaFunction(PHANDLE, "momentum", "unset", luaUnset);
    HyprlandAPI::addLuaFunction(PHANDLE, "momentum", "enable", luaEnable);
    HyprlandAPI::addLuaFunction(PHANDLE, "momentum", "disable", luaDisable);
    HyprlandAPI::addLuaFunction(PHANDLE, "momentum", "reset_rules", luaResetRules);
    HyprlandAPI::addLuaFunction(PHANDLE, "momentum", "stop", luaStop);
}

// ---- plugin lifecycle -------------------------------------------------------

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Refuse to load against a Hyprland we weren't built for. A stale plugin
    // that loads anyway can take the whole compositor down. Compares commit
    // hash plus dependency versions (aquamarine, hyprutils, ...) on both sides.
    const std::string SERVER = __hyprland_api_get_hash();
    const std::string CLIENT = __hyprland_api_get_client_hash();
    if (SERVER != CLIENT)
        throw std::runtime_error("[momentum] built for a different Hyprland (" + CLIENT + " vs running " + SERVER + "), rebuild the plugin");

    try {
        registerConfig();

        g_pMomentum = new CMomentum();

        registerLua();

        g_axisListener = Event::bus()->m_events.input.mouse.axis.listen([](const IPointer::SAxisEvent& e, Event::SCallbackInfo&) {
            if (g_pMomentum)
                g_pMomentum->onAxis(e);
            // Never cancelled: the live scroll always passes through to the app.
        });

        g_buttonListener = Event::bus()->m_events.input.mouse.button.listen([](const IPointer::SButtonEvent& e, Event::SCallbackInfo&) {
            if (g_pMomentum && e.state == WL_POINTER_BUTTON_STATE_PRESSED)
                g_pMomentum->onButtonPress();
        });

        g_focusListener = g_pSeatManager->m_events.pointerFocusChange.listen([] {
            if (g_pMomentum)
                g_pMomentum->onPointerFocusChange();
        });

        // The "catch": touching the pad during a glide stops it. Scroll events
        // are handled by onAxis; these cover cursor motion and resting fingers.
        for (const auto& pointer : g_pInputManager->m_pointers) {
            if (!pointer || !pointer->m_isTouchpad)
                continue;

            g_touchpadListeners.emplace_back(pointer->m_pointerEvents.motion.listen([](const IPointer::SMotionEvent&) {
                if (g_pMomentum)
                    g_pMomentum->onTouchpadContact();
            }));
            g_touchpadListeners.emplace_back(pointer->m_pointerEvents.holdBegin.listen([](const IPointer::SHoldBeginEvent&) {
                if (g_pMomentum)
                    g_pMomentum->onTouchpadContact();
            }));
        }
    } catch (...) {
        g_axisListener.reset();
        g_buttonListener.reset();
        g_focusListener.reset();
        g_touchpadListeners.clear();
        delete g_pMomentum;
        g_pMomentum = nullptr;
        throw;
    }

    return {"hypr-momentum", "macOS-style momentum scrolling for touchpads", "zoan37", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_axisListener.reset();
    g_buttonListener.reset();
    g_focusListener.reset();
    g_touchpadListeners.clear();

    delete g_pMomentum;
    g_pMomentum = nullptr;
}
