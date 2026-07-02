**MenuPanel** — the rounded, accent-bordered dropdown that drops from the hot corners (status + quick actions on the right, app/search on the left). Fill it with `StatusRow`s.

```jsx
<MenuPanel width={320} title="System">
  <StatusRow label="Battery" value="87% (AC)" />
  <StatusRow label="Reboot to Hekate" action />
</MenuPanel>
```

48px rows, 8px radius, cyan hairline ring — match the real chrome.
