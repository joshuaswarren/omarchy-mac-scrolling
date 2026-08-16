#pragma once
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <string>
#include <unordered_map>

inline HANDLE PHANDLE = nullptr;

class CMomentum;
inline CMomentum* g_pMomentum = nullptr;

// Runtime overrides set via `hl.plugin.momentum.set("key", value)`. They beat
// the registered config values, which Omarchy's Lua config can't reach because
// plugins load after the config is parsed (hyprpm reload runs in autostart).
inline std::unordered_map<std::string, double>      g_numOverrides;
inline std::unordered_map<std::string, std::string> g_strOverrides;

inline double cfgFloat(const std::string& name, double fallback) {
    if (const auto it = g_numOverrides.find(name); it != g_numOverrides.end())
        return it->second;

    const auto OPTION = CConfigValue<Config::FLOAT>("plugin:momentum:" + name);
    if (!OPTION.good())
        return fallback;

    return *OPTION;
}

inline int cfgInt(const std::string& name, int fallback) {
    if (const auto it = g_numOverrides.find(name); it != g_numOverrides.end())
        return static_cast<int>(it->second);

    const auto OPTION = CConfigValue<Config::INTEGER>("plugin:momentum:" + name);
    if (!OPTION.good())
        return fallback;

    return *OPTION;
}

inline std::string cfgString(const std::string& name, const std::string& fallback) {
    if (const auto it = g_strOverrides.find(name); it != g_strOverrides.end())
        return it->second;

    const auto OPTION = CConfigValue<Config::STRING>("plugin:momentum:" + name);
    if (!OPTION.good())
        return fallback;

    return *OPTION;
}
