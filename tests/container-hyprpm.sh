#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${OMARCHY_MAC_SCROLL_CONTAINER_IMAGE:-archlinux:latest}"
RUNNER=(docker)
if ! command -v docker >/dev/null 2>&1; then
  echo "SKIP: no docker/podman on host" >&2
  exit 127
fi
if ! docker info >/dev/null 2>&1; then
  echo "SKIP: docker daemon unavailable" >&2
  exit 127
fi
"${RUNNER[@]}" run --rm -v "${ROOT}:/repo:ro" "${IMG}" bash -exc "
  pacman -Syu --noconfirm
  pacman -S --needed --noconfirm base-devel hyprland libinput libdrm pixman lua git
  cp -a /repo /work && cd /work
  make clean all && test -f momentum.so
  export HOME=/root XDG_RUNTIME_DIR=/tmp/xdg
  mkdir -p \$XDG_RUNTIME_DIR
  hyprpm add /work && hyprpm enable momentum && hyprpm disable momentum
  hyprpm remove omarchy-mac-scrolling
  chmod +x bin/omarchy-mac-scrolling-uninstall && ./bin/omarchy-mac-scrolling-uninstall
  echo PASS
"
