import React from "react";

/**
 * HotCornerWidget — a corner hot-zone marker. Top-left carries the active
 * theme's identity emblem (Q for Glass); the right side surfaces live status.
 * Glass box with accent borders on the inner edges.
 */
export function HotCornerWidget({
  glyph = "Q",
  corner = "tl",
  width = 96,
  height = 72,
  style = {},
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const borders =
    corner === "tl" ? { borderRight: "1px solid var(--accent)", borderBottom: "1px solid var(--accent)", borderTopLeftRadius: 0 }
  : corner === "tr" ? { borderLeft: "1px solid var(--accent)", borderBottom: "1px solid var(--accent)" }
  : corner === "bl" ? { borderRight: "1px solid var(--accent)", borderTop: "1px solid var(--accent)" }
  :                   { borderLeft: "1px solid var(--accent)", borderTop: "1px solid var(--accent)" };

  return (
    <div
      onMouseEnter={() => setHover(true)}
      onMouseLeave={() => setHover(false)}
      style={{
        width, height,
        display: "grid", placeItems: "center",
        background: hover ? "rgba(125,211,252,0.18)" : "rgba(125,211,252,0.10)",
        ...borders,
        cursor: "pointer",
        transition: "background .15s ease",
        ...style,
      }}
      {...rest}
    >
      <span style={{
        fontFamily: "var(--font-display)", fontStyle: "italic", fontWeight: 600,
        fontSize: height * 0.5, lineHeight: 1, color: "var(--accent)",
        textShadow: "0 0 16px color-mix(in srgb, var(--accent) 60%, transparent)",
      }}>{glyph}</span>
    </div>
  );
}
