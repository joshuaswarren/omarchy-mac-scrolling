# hypr-momentum

macOS-style momentum scrolling for touchpads, at the compositor level.

Momentum scrolling on Linux is per-application: libinput deliberately doesn't
generate it, each app is expected to compute its own fling. Chrome does;
terminals don't. On macOS the *window server* emits the momentum events, so
every app gets identical inertia. This plugin does the same job for Hyprland:
it watches raw touchpad scroll events, and when the fingers lift with speed it
keeps emitting synthetic scroll events that decay the way a Mac trackpad does.

## Feel

- **Time-based physics.** Glide velocity decays as `v(t) = v₀ · 0.998^t_ms` —
  the default is Apple's `UIScrollViewDecelerationRateNormal`, so the curve is
  the one your fingers already know. Decay is computed from measured elapsed
  time, so the feel is independent of touchpad report rate and timer cadence.
- **Real lift detection.** The glide launches on libinput's scroll-end event
  (the same "gesture ended" signal macOS keys off), not on timer heuristics.
  Resting your fingers before lifting doesn't fling; a flick does.
- **The catch.** Touching the pad during a glide stops it dead, exactly like
  macOS. So does clicking, scrolling another device, or moving the pointer to
  a different window.
- **Refresh-locked emission.** Synthetic scrolls are emitted at the refresh
  rate of the monitor being scrolled (8ms ticks on 120Hz), not a fixed 60Hz.
- **Correct velocity.** Tracking estimates true velocity (px/ms from delta/dt,
  EMA with a 30ms time constant) and applies your `scroll_factor` — including
  per-window `scroll_touchpad` rules — at emission, matching what the app saw
  during the live scroll. No fudge multipliers needed.

Browsers are skipped by default: they ship their own fling and gliding on top
of it double-scrolls.

## Install

```sh
hyprpm add https://github.com/zoan37/hypr-momentum
hyprpm enable momentum
hyprpm reload
```

The plugin refuses to load against a Hyprland it wasn't built for (commit and
dependency versions both checked) instead of risking a compositor crash. After
a Hyprland update, `hyprpm update` rebuilds it.

## Configuration

All values live under `plugin:momentum:` and every key is a valid Lua
identifier:

| key | default | meaning |
|---|---|---|
| `enabled` | `1` | master switch |
| `decay` | `0.998` | velocity retained per ms of glide (higher = floatier) |
| `boost` | `1.0` | multiplier applied to the lift velocity |
| `fling_threshold` | `80.0` | minimum lift speed (px/s) that starts a glide |
| `min_velocity` | `15.0` | glide stops below this speed (px/s) |
| `max_velocity` | `8000.0` | launch velocity cap (px/s) |
| `disable_in_browser` | `1` | skip apps with a native fling |
| `disabled_classes` | `""` | space/comma separated window classes to skip |
| `debug` | `0` | `1` = log to Hyprland log, `2` = also notify |

### Runtime API (no reload, no rebuild)

Everything is also tunable live, which matters on setups (like Omarchy's Lua
config) where plugin config values can't be set at parse time because plugins
load after the config:

```sh
hyprctl eval "hl.plugin.momentum.set('decay', 0.9985)"   # floatier
hyprctl eval "hl.plugin.momentum.set('boost', 1.3)"      # stronger launch
hyprctl eval "hl.plugin.momentum.unset('decay')"         # back to config value
hyprctl eval "hl.plugin.momentum.disable('mpv')"         # per-app off
hyprctl eval "hl.plugin.momentum.enable('firefox')"      # per-app on (beats browser detection)
hyprctl eval "hl.plugin.momentum.reset_rules()"          # clear per-app rules
hyprctl eval "hl.plugin.momentum.stop()"                 # kill a running glide
```

Runtime `set()` values override the config file until `unset()`.

## Notes

- Touchpads only (`FINGER` axis source). Mouse wheels don't glide — mice with
  momentum do it in hardware.
- A touchpad hot-plugged after the plugin loads scrolls and glides fine, but
  the touch-to-catch listeners attach on plugin load; `hyprpm reload` picks it
  up.
- Built and tested against Hyprland v0.56.2.
