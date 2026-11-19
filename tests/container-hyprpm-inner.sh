#!/usr/bin/env bash
# Run as non-root (hyprtest) inside the Arch container smoke test.
set -euo pipefail
export OMARCHY_MAC_SCROLLING_REPO="${OMARCHY_MAC_SCROLLING_REPO:-/work}"
export WLR_BACKENDS="${WLR_BACKENDS:-headless}"
export WLR_RENDERER="${WLR_RENDERER:-pixman}"
export HYPRLAND_NO_SD_NOTIFY="${HYPRLAND_NO_SD_NOTIFY:-1}"

printf '%s
' 'monitor=,preferred,auto,1' >"${HOME}/.config/hypr/headless.conf"
Hyprland -c "${HOME}/.config/hypr/headless.conf" &
HL_PID=$!
trap 'kill "${HL_PID}" 2>/dev/null || true' EXIT

for _ in $(seq 1 120); do
  if compgen -G "${XDG_RUNTIME_DIR}/hypr/*" >/dev/null; then
    export HYPRLAND_INSTANCE_SIGNATURE="$(basename "$(ls -d "${XDG_RUNTIME_DIR}/hypr/"* | head -1)")"
    break
  fi
  sleep 0.25
done
test -n "${HYPRLAND_INSTANCE_SIGNATURE:-}"

hyprpm update -n || true
/work/bin/omarchy-mac-scrolling-install
test -f "${HOME}/.config/omarchy/mac-scrolling.conf"
/work/bin/omarchy-mac-scrolling-install
/work/bin/omarchy-mac-scrolling-uninstall
test ! -f "${HOME}/.config/omarchy/mac-scrolling.conf"
! rg -q mac-scrolling.conf "${HOME}/.config/hypr/hyprland.conf"
hyprpm list
