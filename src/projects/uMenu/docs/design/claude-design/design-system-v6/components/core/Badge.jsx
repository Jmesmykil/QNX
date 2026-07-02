import React from "react";

/**
 * Badge — the README-style mono status pill (version, HW state, firmware).
 * Tone maps to a semantic color; `solid` fills, otherwise it's a tinted outline.
 */
export function Badge({ children, tone = "accent", solid = false, style = {}, ...rest }) {
  const tones = {
    accent: "var(--accent)",
    cyan: "var(--brand-cyan)",
    magenta: "var(--brand-magenta)",
    lavender: "var(--brand-lavender)",
    ok: "var(--status-ok)",
    warn: "var(--status-warn)",
    error: "var(--status-error)",
    neutral: "var(--text-secondary)",
  };
  const c = tones[tone] || tones.accent;

  return (
    <span
      style={{
        display: "inline-flex",
        alignItems: "center",
        gap: 6,
        height: 22,
        padding: "0 10px",
        fontFamily: "var(--font-mono)",
        fontSize: 11,
        fontWeight: 500,
        letterSpacing: "0.06em",
        textTransform: "uppercase",
        borderRadius: 6,
        whiteSpace: "nowrap",
        color: solid ? "#0A0A14" : c,
        background: solid ? c : `color-mix(in srgb, ${c} 14%, transparent)`,
        border: solid ? "1px solid transparent" : `1px solid color-mix(in srgb, ${c} 45%, transparent)`,
        ...style,
      }}
      {...rest}
    >
      {children}
    </span>
  );
}
