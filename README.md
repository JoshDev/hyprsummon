# hyprsummon

[![build](https://github.com/JoshDev/hyprsummon/actions/workflows/build.yml/badge.svg)](https://github.com/JoshDev/hyprsummon/actions/workflows/build.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)
![Hyprland 0.56.2+](https://img.shields.io/badge/Hyprland-0.56.2%2B-58E1FF)

Lazy-launching, monitor-aware scratchpads for [Hyprland](https://hypr.land).

Press a key, get your terminal. If it is not running yet, it starts - on the
right workspace, on the monitor you are looking at, at a size that suits that
monitor. Press again, it goes away.

Hyprland's built-in `togglespecialworkspace` already does more than it gets credit for: it
brings a special workspace to whichever monitor has focus, stealing it from another monitor
if it was open there. What it does not do is the other half of what a scratchpad needs.

`hyprsummon` adds one dispatcher, `summon`, that closes three gaps:

| Gap | Built-in behaviour | With `hyprsummon` |
|---|---|---|
| **Lazy launch** | Toggling an empty special workspace shows you an empty workspace. The usual workaround is to start every scratchpad app at login and pay for it in RAM. | The app is launched on first summon and placed on the pad as it maps. |
| **Adaptive geometry** | A window rule pins a fixed pixel size, so a pad tuned on one monitor is the wrong proportion on every other one. | Size is a fraction of whichever monitor the pad lands on, centred, and recomputed on every summon. |
| **Auto-hide** | The pad stays up until you toggle it again. | Optional `hide_on_focus_loss`, globally or per pad. |

The launch path matters more than it looks. `hyprsummon` hands the workspace to Hyprland's
own executor, which passes it to the child through `HL_INITIAL_WORKSPACE_TOKEN`, so the
window is placed on the pad *before its first frame*. A shell script that execs and then
polls for the window cannot make that promise, and the reflow is visible.

## Install

Requires Hyprland 0.56.2 or newer.

```sh
hyprpm add https://github.com/JoshDev/hyprsummon
hyprpm enable hyprsummon
```

Plugins are compiled against Hyprland's internal headers and there is no stable ABI, so
after any Hyprland upgrade run `hyprpm update` to rebuild. Until you do, the plugin is not
loaded and `summon` is simply absent.

## Usage

### Lua config

```lua
-- Define the pads once.
hl.plugin.hyprsummon.define{
    name    = "term",
    command = "foot --app-id=dropdown_term",
    width   = 0.6,   -- optional, fraction of the monitor
    height  = 0.5,   -- optional
}

hl.plugin.hyprsummon.define{ name = "files", command = "thunar" }

-- hl.bind accepts a plain Lua function as its action, so no dispatcher object
-- is needed.
hl.bind("SUPER + minus", function() hl.plugin.hyprsummon.summon("term") end, { desc = "Dropdown terminal" })
hl.bind("SUPER + N", function() hl.plugin.hyprsummon.summon("files") end, { desc = "File manager" })
```

**A plugin's Lua functions do not exist during the cold config parse.** Plugins load from
autostart, which runs after the config is first read, so on a cold start
`hl.plugin.hyprsummon` is `nil`. In a Lua config an unhandled error aborts the *whole* file,
not just one line, so guard the calls:

```lua
pcall(function()
    hl.plugin.hyprsummon.define{ name = "term", command = "foot --app-id=dropdown_term" }
end)
```

Hyprland re-applies the config after `hyprctl plugin load`, so the second pass is where the
definitions actually take. This is the same pattern any plugin-provided config needs.

### hyprland.conf

The dispatcher takes an optional inline command after a comma, so `.conf` users get the full
feature set without defining pads separately:

```ini
bind = SUPER, minus, summon, term, foot --app-id=dropdown_term
bind = SUPER, N, summon, files, thunar
```

Once a pad has been summoned with a command, later `summon term` calls without one reuse it.

## Configuration

Global defaults, overridable per pad in `define{}`:

| Option | Type | Default | Meaning |
|---|---|---|---|
| `plugin:hyprsummon:width` | float | `0.6` | Pad width as a fraction of the monitor |
| `plugin:hyprsummon:height` | float | `0.7` | Pad height as a fraction of the monitor |
| `plugin:hyprsummon:hide_on_focus_loss` | bool | `false` | Hide a pad when focus moves outside it |
| `plugin:hyprsummon:focus_on_summon` | bool | `true` | Focus the pad's window when summoned |

`define{}` fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `name` | string | yes | Pad name, without the `special:` prefix |
| `command` | string | yes | What to launch on first summon |
| `width` / `height` | float in `(0, 1]` | no | Per-pad geometry override |
| `hide_on_focus_loss` | bool | no | Per-pad auto-hide override |
| `float` | bool | no (default `true`) | Float the window when it maps into the pad |

Geometry is only applied to floating windows. Tiled windows belong to the layout, and
resizing them here would fight whatever algorithm owns the workspace.

## Behaviour notes

- Summoning a pad that is already visible **on the focused monitor** hides it. Summoning one
  that is visible on a *different* monitor pulls it over instead.
- A pad's special workspace is destroyed by Hyprland when its last window closes, and comes
  back with a fresh id next time. Pads are therefore tracked by name, not id.
- Mashing the keybind while an app is still starting will not spawn duplicates. The in-flight
  pid is tracked, and if that process dies without ever mapping a window (a bad command, a
  crash) the pad becomes launchable again rather than wedging.

## Compatibility

Built and tested against **Hyprland 0.56.2**.

Hyprland plugins compile against the compositor's internal headers and there is
no stable ABI, so a plugin built for one release will not load into another.
`hyprpm` enforces this: it refuses to load a `.so` whose build hash does not
match the running compositor.

`hyprpm.toml` carries no commit pins yet, so hyprpm builds from the default
branch. That is the normal fallback and works: a pin records "the author has
confirmed this Hyprland/plugin pair", not "this is the only pair allowed". What
actually decides is whether the plugin compiles against your installed headers.

CI builds weekly against whatever Hyprland Arch currently ships, so a breaking
release shows up as a red build rather than as a broken desktop.

## Contributing

Issues and pull requests welcome.

The `.clang-format` is [hyprwm/hyprland-plugins'](https://github.com/hyprwm/hyprland-plugins)
own, so a patch here lands looking like the rest of the ecosystem. CI checks it:

```sh
clang-format -i src/*.cpp src/*.hpp
make all
```

## Licence

BSD 3-Clause
