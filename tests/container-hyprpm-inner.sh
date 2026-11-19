#!/usr/bin/env bash
# Run as non-root (hyprtest) inside the Arch container smoke test.
set -euo pipefail
export OMARCHY_MAC_SCROLLING_REPO="${OMARCHY_MAC_SCROLLING_REPO:-/work}"
unset HYPRLAND_INSTANCE_SIGNATURE

Hyprland --version-json | python3 -c 'import json,sys; json.load(sys.stdin)'

_headers="/var/cache/hyprpm/$(id -un)/headersRoot/share/pkgconfig/hyprland.pc"
hyprpm update -f -n || true
if [[ ! -f "${_headers}" ]]; then
  hyprpm update -f -n || true
fi
test -f "${_headers}"

/work/bin/omarchy-mac-scrolling-install
test -f "${HOME}/.config/omarchy/mac-scrolling.conf"
/work/bin/omarchy-mac-scrolling-install
/work/bin/omarchy-mac-scrolling-uninstall
test ! -f "${HOME}/.config/omarchy/mac-scrolling.conf"
! rg -q mac-scrolling.conf "${HOME}/.config/hypr/hyprland.conf"
hyprpm list
