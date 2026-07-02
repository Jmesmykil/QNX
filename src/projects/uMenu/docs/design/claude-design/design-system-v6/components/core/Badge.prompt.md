**Badge** — the small uppercase-mono pill used for versions, firmware, and HW/security state (mirrors the README shields).

```jsx
<Badge tone="ok" solid>HW-verified</Badge>
<Badge tone="magenta">v3.5.0</Badge>
<Badge tone="error">Security gap</Badge>
```

`tone` picks a semantic color; `solid` fills it (default is a tinted outline). Always mono, always uppercase — keep labels to 1–3 short words.
