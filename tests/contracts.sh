#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf -- "$t"' EXIT
status=0
fail() { printf 'FAIL: %s\n' "$*" >&2; status=1; }

# The real add/update path is exercised by container-hyprpm.
if grep -Eq '^[[:space:]]*hyprpm (enable|reload)' bin/omarchy-mac-scrolling-install; then
  fail 'install must not enable or load the plugin'
fi

# Exercise uninstall against an isolated HOME and a recorded hyprpm boundary.
export HOME="$t/home" HYPR_CONF="$t/home/.config/hypr/hyprland.conf" CALLS="$t/calls"
mkdir -p "$HOME/.config/omarchy" "$(dirname "$HYPR_CONF")" "$t/bin"
cat > "$t/bin/hyprpm" <<'SH'
#!/bin/bash
printf '%s\n' "$*" >> "$CALLS"
case "$1" in
  list) echo omarchy-mac-scrolling ;;
  disable|reload) ;;
  remove) read -r answer; [[ $answer = y ]] ;;
  *) exit 2 ;;
esac
SH
chmod +x "$t/bin/hyprpm"
printf '%s\n' '# mentions mac-scrolling.conf' 'source = /somewhere/mac-scrolling.conf' > "$t/config"
cp "$t/config" "$t/config.expected"
ln -s "$t/config" "$HYPR_CONF"
printf 'user setting\n' > "$HOME/.config/omarchy/mac-scrolling.conf"
PATH="$t/bin:$PATH" bash bin/omarchy-mac-scrolling-uninstall
if [[ ! -L "$HYPR_CONF" ]] || ! cmp -s "$t/config.expected" "$HYPR_CONF"; then
  fail 'uninstall altered user-owned Hyprland config'
fi
[[ $(cat "$HOME/.config/omarchy/mac-scrolling.conf" 2>/dev/null) = 'user setting' ]] || fail 'uninstall removed a user-edited snippet'
for call in 'disable momentum' 'remove omarchy-mac-scrolling' 'reload -n'; do
  grep -Fxq "$call" "$CALLS" || fail "uninstall did not call hyprpm $call"
done

# Guard the integration point; runtime input behavior needs the Wayland trace.
if grep -Fq 'sendPointerAxis(' src/Momentum.cpp || ! grep -Fq 'onMouseWheel(' src/Momentum.cpp || ! grep -Fq 'onPointerFrame(' src/Momentum.cpp; then
  fail 'synthetic axes must enter Hyprland via its input manager and frame path'
fi
if ! sed -n '/void PLUGIN_EXIT()/,$p' src/main.cpp | grep -Fq 'stop("unload")'; then
  fail 'live unload must close the synthetic finger axes before teardown'
fi
exit "$status"
