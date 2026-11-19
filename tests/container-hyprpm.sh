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
  pacman -S --needed --noconfirm base-devel libinput libdrm pixman lua git curl
  curl -fsSL -o /tmp/hyprland.pkg.tar.zst '${HYPR_PKG}'
  pacman -U --noconfirm /tmp/hyprland.pkg.tar.zst || {
    pacman -S --needed --noconfirm aquamarine hyprutils hyprlang hyprcursor hyprgraphics wayland wayland-protocols xcb-util-errors xcb-util-renderutil libxcb
    pacman -U --noconfirm /tmp/hyprland.pkg.tar.zst
  }
  command -v hyprpm
  cp -a /repo /work && cd /work
  make clean all
  test -f momentum.so
  export HOME=/root XDG_RUNTIME_DIR=/tmp/xdg
  mkdir -p "\$XDG_RUNTIME_DIR"
  hyprpm add /work
  hyprpm enable momentum
  hyprpm disable momentum
  hyprpm remove omarchy-mac-scrolling
  chmod +x bin/omarchy-mac-scrolling-uninstall
  ./bin/omarchy-mac-scrolling-uninstall
  echo PASS
"
