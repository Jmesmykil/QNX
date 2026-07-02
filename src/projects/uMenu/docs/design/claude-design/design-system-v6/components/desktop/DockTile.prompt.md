**DockTile** — a single tile in the bottom dock band. Acts as a taskbar entry: built-in app launcher or a live minimized-window snapshot.

```jsx
<DockTile glyph="▤" label="Vault" running />
<DockTile glyph="◷" label="Monitor" active />
```

`active` lights the accent fill/border; `running` (or `active`) shows the state dot beneath it. Tapping a singleton tile twice focuses the existing window rather than spawning a duplicate.
