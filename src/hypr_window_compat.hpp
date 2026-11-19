#pragma once
#include <string>

#if __has_include(<hyprland/src/desktop/view/window/Window.hpp>)
#include <hyprland/src/desktop/view/window/Window.hpp>
inline std::string momentumWindowClass(PHLWINDOW PWIN) {
    return PWIN->metadata().appID();
}
#else
#include <hyprland/src/desktop/view/Window.hpp>
inline std::string momentumWindowClass(PHLWINDOW PWIN) {
    return PWIN->m_class;
}
#endif
