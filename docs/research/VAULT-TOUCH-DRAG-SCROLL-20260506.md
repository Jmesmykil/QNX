# Vault Touch-Drag Scroll — 2026-05-06

Finger-flick scrolling for QdVaultLayout's entry grid, layered on top of
QdWindow's VSB scroll. Only the two scoped files were touched.

## New state members (qd_VaultLayout.hpp)

| Member | Purpose |
|---|---|
| `s32 drag_view_offset_y_` | Scroll offset, 0 = top |
| `bool drag_in_progress_` | Finger down inside grid area |
| `bool drag_passed_deadband_` | Movement crossed `DRAG_DEADBAND_PX` |
| `s32 drag_start_touch_x_/y_` | Touch pos at touch-down |
| `s32 drag_start_offset_y_` | Scroll offset at touch-down |
| `bool drag_was_touch_active_` | Prev-frame latch for edge detection |

`DRAG_DEADBAND_PX = 8`.

## State machine

```
no-touch -> rising in grid area  : in_progress=true, deadband=false
held + |max(dx,dy)| > 8           : deadband=true
held + deadband                   : offset = clamp(start - dy,
                                                  0, MaxScrollOffsetY)
falling + !deadband               : tap-dispatch at start_pos
falling + deadband                : consume, no tap
```

Edge detection mirrors `sb_was_touch_active_last_frame_`. Drag-active rect:
`x >= VAULT_SIDEBAR_W` and `y in [VAULT_BODY_TOP+VAULT_PATHBAR_H,
VAULT_BODY_TOP+VAULT_BODY_H)`. Sidebar / hot-corner taps run earlier and
early-return, preventing drag-arm.

## Render integration

`EntryRect()` bakes `-drag_view_offset_y_` into `out_y`. Render
(`RenderMainPane`) and hit-test read identical shifted coords. Pathbar,
sidebar, backdrop draw at unshifted y so only the grid scrolls.

## Deadband: 8 px

Movement under 8 px on touch-up dispatches the tap at touch-down position
via the existing `focus_idx_ = i; EnterFocused()` path. Above 8 px, touch-up
consumes the gesture.

## Existing inputs

* **QdWindow VSB / arrow scroll**: untouched. Effects sum with finger-drag.
* **D-pad, A, B, Y, Plus, ZL context menu**: button-driven, untouched.
* **Sidebar / hot-corner taps**: rising-edge, early-return before drag block.
* **Navigate()**: resets offset and clears drag state so a folder-open tap
  cannot leak into the post-navigation handler.

## Build

`make umenu` clean. uMenu.nso md5 `fe408f8f3b9bb2b7f8069fdf54dcc338`
(7,098,209 bytes). Not deployed.
