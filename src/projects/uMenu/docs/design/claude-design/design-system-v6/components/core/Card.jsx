import React from "react";

/**
 * Card — a translucent glass surface with an accent hairline. The base
 * container for About panels, settings groups, and content blocks.
 */
export function Card({ children, title, eyebrow, accent = false, padding = 20, style = {}, ...rest }) {
  return (
    <div
      style={{
        background: "rgba(18,18,42,0.92)",
        border: accent
          ? "1px solid rgba(125,211,252,0.45)"
          : "1px solid rgba(125,211,252,0.16)",
        borderRadius: 12,
        boxShadow: "var(--shadow-panel)",
        backdropFilter: "blur(14px) saturate(1.1)",
        padding,
        color: "var(--text-primary)",
        ...style,
      }}
      {...rest}
    >
      {eyebrow && (
        <div style={{
          fontFamily: "var(--font-mono)", fontSize: 11, letterSpacing: "0.12em",
          textTransform: "uppercase", color: "var(--accent)", marginBottom: 8,
        }}>{eyebrow}</div>
      )}
      {title && (
        <div style={{
          fontFamily: "var(--font-sans)", fontSize: 18, fontWeight: 600,
          color: "var(--text-primary)", marginBottom: children ? 12 : 0,
        }}>{title}</div>
      )}
      {children}
    </div>
  );
}
