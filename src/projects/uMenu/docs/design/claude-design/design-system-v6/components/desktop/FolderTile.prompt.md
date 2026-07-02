**FolderTile** — one of the six desktop folder tiles that auto-classify NROs (Games / Emulators / Tools / System / Q OS / Other). Glass body + category-colored ring.

```jsx
<FolderTile category="games" label="Games" count={42} />
<FolderTile category="system" glyph="⚙" />
```

`category` drives the ring color and the default letter glyph (the Glass pack literally draws the category letter). Pass `glyph` to override, `count` for a corner badge, `selected` for the focus ring.
