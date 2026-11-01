# Changelog

## 0.1.1

- Build against Hyprland 0.57, which moved workspaces out of `desktop/` into
  `workspace/` and replaced the id-based special-workspace API: pads now resolve
  by address (`special:<name>`) through `query().address()` and are created with
  `createSpecial()`, special-ness comes from the workspace type, and a newly
  created pad gets `ready()` after it is shown. Window flags moved to
  `mapped()` and `isFloating()`.

## 0.1.0

First release.

- `summon <name>` dispatcher and `hl.plugin.hyprsummon.summon(name)` for Lua configs.
- Lazy launch: the app starts on first summon and is placed on its special
  workspace through `HL_INITIAL_WORKSPACE_TOKEN`, before its first frame.
- Monitor-relative geometry: floating pads are sized as a fraction of whichever
  monitor they land on, and re-sized on every summon.
- Optional `hide_on_focus_loss`, globally or per pad.
- In-flight pid tracking, so repeated presses during a slow launch cannot spawn
  duplicates and a launch that dies without mapping a window stays retryable.
