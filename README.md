# omarchy-mac-scrolling

Opt-in Omarchy package: **macOS-style trackpad momentum scrolling** for Hyprland.

Plugin core from [zoan37/hypr-momentum](https://github.com/zoan37/hypr-momentum) (MIT). See [UPSTREAM.md](UPSTREAM.md).

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/joshuaswarren/omarchy-mac-scrolling/HEAD/bin/omarchy-mac-scrolling-install | bash
# or from a clone:
./bin/omarchy-mac-scrolling-install
```

Equivalent manual steps:

```sh
hyprpm add https://github.com/joshuaswarren/omarchy-mac-scrolling
hyprpm enable momentum
hyprpm reload
```

## Opt in (one config line)

Stock Omarchy is unchanged unless you add **one** line to `hyprland.conf`:

```conf
source = ~/.config/omarchy/mac-scrolling.conf
```

The install script creates that snippet only under your home directory.

Tune at runtime (plugins load after config parse):

```sh
hyprctl eval "hl.plugin.momentum.set('decay', 0.9985)"
```

## Uninstall

```sh
./bin/omarchy-mac-scrolling-uninstall
```

Removes the hyprpm repository and `~/.config/omarchy/mac-scrolling.conf`. Remove the `source = ...` line you added. No stock Omarchy paths are edited.

## Test

```sh
./tests/container-hyprpm.sh
```

Builds the plugin and exercises `hyprpm add` / `enable` / `disable` / `remove` in a throwaway Arch container.

## Plugin options

See upstream README in commit history; keys live under `plugin:momentum:` (`enabled`, `decay`, `boost`, etc.).

Built for Hyprland **0.57.x** (0.57.2 fixes from w7C). After a Hyprland upgrade run `hyprpm update` then `hyprpm reload`.
