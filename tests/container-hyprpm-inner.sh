#!/usr/bin/env bash
# Run as non-root (hyprtest) inside the Arch container smoke test.
set -euo pipefail
export OMARCHY_MAC_SCROLLING_REPO="${OMARCHY_MAC_SCROLLING_REPO:-/work}"
export OMARCHY_MAC_SCROLLING_NO_RELOAD=1

_hl_json="$(Hyprland --version-json)"
read -r _COMMIT _ABI < <(python3 -c 'import json,sys; j=json.load(sys.stdin); print(j["commit"], j["abiHash"])' <<<"$_hl_json")
_ts="$(date +%s)"
export HYPRLAND_INSTANCE_SIGNATURE="${_COMMIT}_${_ts}_ci00000001"

python3 /work/tests/hyprland-fake-ipc.py &
IPCPID=$!
trap 'kill "${IPCPID}" 2>/dev/null || true' EXIT
sleep 0.5
test -S "${XDG_RUNTIME_DIR}/hypr/${HYPRLAND_INSTANCE_SIGNATURE}/.socket.sock"

_headers="/var/cache/hyprpm/$(id -un)/headersRoot/share/pkgconfig/hyprland.pc"
hyprpm update -f -n || true
test -f "${_headers}"

/work/bin/omarchy-mac-scrolling-install
test -f "${HOME}/.config/omarchy/mac-scrolling.conf"
/work/bin/omarchy-mac-scrolling-install
/work/bin/omarchy-mac-scrolling-uninstall
test ! -f "${HOME}/.config/omarchy/mac-scrolling.conf"
! rg -q mac-scrolling.conf "${HOME}/.config/hypr/hyprland.conf"
hyprpm list