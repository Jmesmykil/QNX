import React from "react";

/**
 * WindowFrame — the signature Q OS windowed-OS chrome.
 * Titlebar with the window name, four corner controls each in its own color
 * (TL close ×, TR maximize □, BL minimize –, BR resize ⤡), a glass body, and a
 * bottom hint strip carrying the control instructions.
 */
export function WindowFrame({
  title = "Window",
  icon = null,
  active = true,
  hint = "B / +  Close   ·   Drag titlebar to move",
  width = 520,
  height = 340,
  onClose,
  onMinimize,
  onMaximize,
  onResize,
  onTitleMouseDown,
  children,
  style = {},
  ...rest
}) {
  const BTN = 22;
  const corner = (bg, glyph, label, pos, handler) => (
    <div
      title={label}
      onMouseDown={(e) => e.stopPropagation()}
      onClick={handler}
      style={{
        position: "absolute", zIndex: 2,
        width: BTN, height: BTN, borderRadius: 999,
        display: "grid", placeItems: "center",
        background: bg, color: "#0A0A14",
        fontFamily: "var(--font-sans)", fontSize: 14, fontWeight: 700, lineHeight: 1,
        cursor: "pointer", userSelect: "none",
        ...pos,
      }}
    >{glyph}</div>
  );

  const resizeGlyph = (
    <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="#0A0A14"
         strokeWidth="2.4" strokeLinecap="round" strokeLinejoin="round">
      <path d="M7 7 L17 17 M17 17 L17 12.5 M17 17 L12.5 17 M7 7 L7 11.5 M7 7 L11.5 7"/>
    </svg>
  );

  return (
    <div
      style={{
        position: "relative",
        width, height,
        display: "flex", flexDirection: "column",
        background: "rgba(18,18,42,0.94)",
        border: `1px solid ${active ? "var(--accent)" : "var(--titlebar-inactive)"}`,
        borderRadius: 12,
        boxShadow: "var(--shadow-window)",
        backdropFilter: "blur(16px) saturate(1.1)",
        overflow: "hidden",
        ...style,
      }}
      {...rest}
    >
      {/* Titlebar — title text clears the TL/TR corner controls */}
      <div
        onMouseDown={onTitleMouseDown}
        style={{
          height: 40, flex: "none",
          display: "flex", alignItems: "center", gap: 9,
          padding: "0 40px",
          background: active ? "rgba(125,211,252,0.10)" : "var(--titlebar-inactive)",
          borderBottom: "1px solid rgba(125,211,252,0.16)",
          cursor: onTitleMouseDown ? "grab" : "default",
        }}>
        {icon && <span style={{ display: "inline-flex" }}>{icon}</span>}
        <span style={{
          fontFamily: "var(--font-sans)", fontSize: 14, fontWeight: 600,
          color: active ? "var(--text-primary)" : "var(--text-secondary)",
          overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
        }}>{title}</span>
      </div>

      {/* Corner controls — TL close · TR maximize · BL minimize · BR resize */}
      {corner("var(--btn-close)", "\u00D7", "Close", { top: 9, left: 9 }, onClose)}
      {corner("var(--btn-maximize)", "\u25A1", "Maximize", { top: 9, right: 9 }, onMaximize)}
      {corner("var(--btn-minimize)", "\u2013", "Minimize", { bottom: 9, left: 9 }, onMinimize)}
      {corner("var(--accent)", resizeGlyph, "Resize", { bottom: 9, right: 9 }, onResize)}

      {/* Body */}
      <div style={{ flex: 1, minHeight: 0, overflow: "auto", padding: 18, color: "var(--text-primary)" }}>
        {children}
      </div>

      {/* Bottom hint strip — instructions live in the chrome, centered between BL/BR controls */}
      <div style={{
        flex: "none", height: 34,
        display: "flex", alignItems: "center", justifyContent: "center",
        padding: "0 44px",
        background: "rgba(10,10,20,0.5)",
        borderTop: "1px solid rgba(125,211,252,0.12)",
        fontFamily: "var(--font-mono)", fontSize: 11, color: "var(--text-secondary)",
        letterSpacing: "0.02em",
      }}>
        <span style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{hint}</span>
      </div>
    </div>
  );
}
