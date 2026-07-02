**WindowFrame** — the windowed-desktop chrome that defines Q OS. Use it to host any built-in app surface (Vault, Settings, Monitor, About).

```jsx
<WindowFrame title="Vault" hint="A Launch · Y Edit save · ZL Menu" width={560} height={360}>
  <FileList … />
</WindowFrame>
```

Four corner buttons each have their own color + glyph: close × (red, TL), maximize □ (green, TR), minimize – (amber, BL), plus a resize grip (BR). `active={false}` dims it to an inactive window. The instruction text lives IN the bottom strip — never on a separate bar.
