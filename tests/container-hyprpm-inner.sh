#!/usr/bin/env bash
# Run as non-root (hyprtest) inside the Arch container smoke test.
set -euo pipefail
export OMARCHY_MAC_SCROLLING_REPO="${OMARCHY_MAC_SCROLLING_REPO:-/work}"

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

conf="$HOME/.config/hypr/hyprland.conf"
cp "$conf" "$conf.before"
/work/bin/omarchy-mac-scrolling-install
state=$(hyprpm list)
grep -Fq 'Plugin momentum' <<< "$state"
grep -Eq 'enabled:.*false' <<< "$state"
hyprpm enable momentum
/work/bin/omarchy-mac-scrolling-install
state=$(hyprpm list)
grep -Eq 'enabled:.*true' <<< "$state"
/work/bin/omarchy-mac-scrolling-uninstall
test ! -f "${HOME}/.config/omarchy/mac-scrolling.conf"
cmp "$conf.before" "$conf"
state=$(hyprpm list)
if grep -Fq 'Plugin momentum' <<< "$state"; then
  exit 1
fi