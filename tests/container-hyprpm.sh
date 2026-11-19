#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${OMARCHY_MAC_SCROLL_CONTAINER_IMAGE:-archlinux:latest}"
if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "SKIP: docker required for container-hyprpm" >&2
  exit 127
fi
HYPR_PKG="${HYPR_PKG_URL:-https://archive.archlinux.org/packages/h/hyprland/hyprland-0.57.2-1-x86_64.pkg.tar.zst}"
docker run --rm -v "${ROOT}:/repo:ro" "${IMG}" bash -exc "
  pacman -Syu --noconfirm
  pacman -S --needed --noconfirm base-devel libinput libdrm pixman lua git curl sudo ripgrep
  curl -fsSL -o /tmp/hyprland.pkg.tar.zst '${HYPR_PKG}'
  pacman -S --needed --noconfirm hyprpm
  pacman -U --noconfirm /tmp/hyprland.pkg.tar.zst || {
    pacman -S --needed --noconfirm aquamarine hyprutils hyprlang hyprcursor hyprgraphics wayland wayland-protocols xcb-util-errors xcb-util-renderutil libxcb
    pacman -U --noconfirm /tmp/hyprland.pkg.tar.zst
  }
  cp -a /repo /work
  cd /work
  make clean all
  test -f momentum.so
  chmod +x bin/omarchy-mac-scrolling-install bin/omarchy-mac-scrolling-uninstall
  useradd -m -u 1001 hyprtest
  chown -R hyprtest:hyprtest /work
  install -d -o hyprtest -g hyprtest -m 700 /tmp/xdg-hyprtest
  install -d -o hyprtest -g hyprtest /home/hyprtest/.config/hypr
  printf '%s\n' 'source = /home/hyprtest/.config/omarchy/mac-scrolling.conf' > /home/hyprtest/.config/hypr/hyprland.conf
  chown hyprtest:hyprtest /home/hyprtest/.config/hypr/hyprland.conf
  sudo -u hyprtest env HOME=/home/hyprtest XDG_RUNTIME_DIR=/tmp/xdg-hyprtest OMARCHY_MAC_SCROLLING_REPO=/work bash -exc '
    /work/bin/omarchy-mac-scrolling-install
    test -f /home/hyprtest/.config/omarchy/mac-scrolling.conf
    /work/bin/omarchy-mac-scrolling-install
    /work/bin/omarchy-mac-scrolling-uninstall
    test ! -f /home/hyprtest/.config/omarchy/mac-scrolling.conf
    ! rg -q mac-scrolling.conf /home/hyprtest/.config/hypr/hyprland.conf
    hyprpm list
  '
  echo PASS
"