**StatusRow** — a line in a `MenuPanel` dropdown. Read-only status rows show a label + mono value and render muted; `action` rows are interactive with a hover highlight.

```jsx
<StatusRow label="Network" value="Wi-Fi (82%)" />
<StatusRow label="Sleep" action />
<StatusRow label="Reboot to Hekate" action danger />
```

Status rows first (Battery / Time / Network / Volume), then a divider, then actions. 48px tall.
