import React from "react";

/**
 * MenuPanel — the hot-corner dropdown surface. A cyan/accent 1px border ring
 * around a translucent navy fill with an 8px radius. Mirrors the two-pass
 * paint in qd_HotCornerRightDropdown (accent outer rect, navy inner inset).
 */
export function MenuPanel({ children, width = 320, title, style = {}, ...rest }) {
  return (
    <div
      style={{
        width,
        background: "rgba(18,18,42,0.92)",
        border: "1px solid var(--accent)",
        borderRadius: 8,
        boxShadow: "var(--shadow-panel)",
        backdropFilter: "blur(16px) saturate(1.1)",
        overflow: "hidden",
        padding: 8,
        ...style,
      }}
      {...rest}
    >
      {title && (
        <div style={{
          fontFamily: "var(--font-mono)", fontSize: 10, letterSpacing: "0.14em",
          textTransform: "uppercase", color: "var(--text-secondary)",
          padding: "6px 8px 8px",
        }}>{title}</div>
      )}
      {children}
    </div>
  );
}
