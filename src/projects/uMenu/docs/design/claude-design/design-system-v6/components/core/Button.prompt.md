**Button** — the primary action control; use for any tap/click commitment in a Q OS surface (launch, apply theme, reboot, confirm).

```jsx
<Button variant="primary" onClick={apply}>Apply Theme</Button>
<Button variant="secondary" size="sm">Cancel</Button>
<Button variant="danger" iconLeft="⏻">Reboot to Hekate</Button>
```

Variants: `primary` (cyan fill, the default CTA), `secondary` (glass surface + accent hairline), `ghost` (text-only, low-emphasis), `danger` (red — destructive/power actions). Sizes `sm`/`md`/`lg`. Pass `block` to fill width, `iconLeft`/`iconRight` for glyphs.
