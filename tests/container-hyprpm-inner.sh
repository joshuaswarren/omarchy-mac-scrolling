#!/usr/bin/env bash
# Run as non-root (hyprtest) inside the Arch container smoke test.
set -euo pipefail
export OMARCHY_MAC_SCROLLING_REPO="${OMARCHY_MAC_SCROLLING_REPO:-/work}"

Hyprland --version-json | python3 -c 'import json,sys; json.load(sys.stdin)'

_headers="/var/cache/hyprpm/$(id -un)/headersRoot/share/pkgconfig/hyprland.pc"
hyprpm update -f -n || true
test -f "${_headers}"

export HYPRLAND_INSTANCE_SIGNATURE="smoke00000000000000000000000000001"
python3 /work/tests/hyprland-fake-ipc.py &
IPCPID=$!
trap 'kill "${IPCPID}" 2>/dev/null || true' EXIT
sleep 0.5
hyprctl -j version | python3 -c 'import json,sys; json.load(sys.stdin)'

/work/bin/omarchy-mac-scrolling-install
test -f "${HOME}/.config/omarchy/mac-scrolling.conf"
/work/bin/omarchy-mac-scrolling-install
/work/bin/omarchy-mac-scrolling-uninstall
test ! -f "${HOME}/.config/omarchy/mac-scrolling.conf"
! rg -q mac-scrolling.conf "${HOME}/.config/hypr/hyprland.conf"
hyprpm list