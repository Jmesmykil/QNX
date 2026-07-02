import React from "react";

/**
 * DockTile — a tile in the bottom dock band. Holds a built-in app icon or a
 * minimized-window snapshot; an accent dot marks the active/running state.
 */
export function DockTile({
  label = "",
  glyph,
  active = false,
  running = false,
  size = 64,
  style = {},
  ...rest
}) {
  const [hover, setHover] = React.useState(false);

  return (
    <div
      title={label}
      onMouseEnter={() => setHover(true)}
      onMouseLeave={() => setHover(false)}
      style={{
        position: "relative",
        width: size, height: size,
        borderRadius: 14,
        background: active ? "rgba(125,211,252,0.16)" : "rgba(24,24,48,0.7)",
        border: `1px solid ${active ? "var(--accent)" : "rgba(125,211,252,0.16)"}`,
        display: "grid", placeItems: "center",
        cursor: "pointer",
        transform: hover ? "translateY(-4px) scale(1.04)" : "none",
        transition: "transform .14s cubic-bezier(.2,.8,.2,1), background .18s ease, border-color .18s ease",
        boxShadow: hover ? "var(--glow-accent)" : "none",
        ...style,
      }}
      {...rest}
    >
      <span style={{
        fontFamily: "var(--font-sans)", fontSize: size * 0.4, fontWeight: 700, lineHeight: 1,
        color: "var(--accent)",
      }}>{glyph}</span>
      {(running || active) && (
        <span style={{
          position: "absolute", bottom: -9, left: "50%", transform: "translateX(-50%)",
          width: 5, height: 5, borderRadius: 999, background: "var(--accent)",
          boxShadow: "0 0 8px 1px var(--accent)",
        }} />
      )}
    </div>
  );
}
