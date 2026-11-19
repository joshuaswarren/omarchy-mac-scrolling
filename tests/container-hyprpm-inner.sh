#!/usr/bin/env bash
# Run as non-root (hyprtest) inside the Arch container smoke test.
set -euo pipefail
export OMARCHY_MAC_SCROLLING_REPO="${OMARCHY_MAC_SCROLLING_REPO:-/work}"
unset HYPRLAND_INSTANCE_SIGNATURE
LOG="/tmp/hyprland-smoke.log"
WLOG="/tmp/weston-smoke.log"

Hyprland --version-json | python3 -c 'import json,sys; json.load(sys.stdin)'

_headers="/var/cache/hyprpm/$(id -un)/headersRoot/share/pkgconfig/hyprland.pc"
hyprpm update -f -n || true
test -f "${_headers}"

printf '%s\n' 'monitor=,preferred,auto,1' >"${HOME}/.config/hypr/headless.conf"

weston --backend=headless-backend.so --width=1280 --height=720 >"${WLOG}" 2>&1 &
WPID=$!
trap 'kill "${WPID}" "${HL_PID:-}" 2>/dev/null || true' EXIT
for _ in $(seq 1 80); do
  [[ -S "${XDG_RUNTIME_DIR}/wayland-0" ]] && break
  sleep 0.25
done
test -S "${XDG_RUNTIME_DIR}/wayland-0"
export WAYLAND_DISPLAY=wayland-0

dbus-run-session -- env WLR_RENDERER=pixman HYPRLAND_NO_SD_NOTIFY=1 \
  Hyprland -c "${HOME}/.config/hypr/headless.conf" >"${LOG}" 2>&1 &
HL_PID=$!

for _ in $(seq 1 240); do
  kill -0 "${HL_PID}" 2>/dev/null || { tail -80 "${LOG}" >&2; tail -40 "${WLOG}" >&2; exit 1; }
  for sigdir in "${XDG_RUNTIME_DIR}/hypr/"*; do
    [[ -d "${sigdir}" ]] || continue
    export HYPRLAND_INSTANCE_SIGNATURE="$(basename "${sigdir}")"
    if [[ -S "${sigdir}/.socket.sock" ]] && hyprctl version >/dev/null 2>&1; then
      break 2
    fi
    unset HYPRLAND_INSTANCE_SIGNATURE
  done
  sleep 0.25
done
test -n "${HYPRLAND_INSTANCE_SIGNATURE:-}"

/work/bin/omarchy-mac-scrolling-install
test -f "${HOME}/.config/omarchy/mac-scrolling.conf"
/work/bin/omarchy-mac-scrolling-install
/work/bin/omarchy-mac-scrolling-uninstall
test ! -f "${HOME}/.config/omarchy/mac-scrolling.conf"
! rg -q mac-scrolling.conf "${HOME}/.config/hypr/hyprland.conf"
hyprpm list