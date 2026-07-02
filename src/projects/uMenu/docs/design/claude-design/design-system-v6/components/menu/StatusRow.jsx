import React from "react";

/**
 * StatusRow — a single row inside a MenuPanel dropdown. Status rows are
 * read-only (disabled, muted) and show a label + value; action rows are
 * interactive with a hover highlight. 48px tall to match kRowH.
 */
export function StatusRow({
  label,
  value,
  action = false,
  danger = false,
  icon = null,
  onClick,
  style = {},
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const interactive = action;

  return (
    <div
      onMouseEnter={() => setHover(true)}
      onMouseLeave={() => setHover(false)}
      onClick={interactive ? onClick : undefined}
      style={{
        display: "flex", alignItems: "center", gap: 10,
        height: 48, padding: "0 16px",
        borderRadius: 6,
        cursor: interactive ? "pointer" : "default",
        background: interactive && hover ? "var(--titlebar-inactive)" : "transparent",
        transition: "background .12s ease",
        ...style,
      }}
      {...rest}
    >
      {icon && <span style={{ display: "inline-flex", color: danger ? "var(--btn-close)" : "var(--accent)" }}>{icon}</span>}
      <span style={{
        flex: 1,
        fontFamily: "var(--font-sans)", fontSize: 14,
        fontWeight: interactive ? 500 : 400,
        color: danger ? "var(--btn-close)"
             : interactive ? "var(--text-primary)" : "var(--text-disabled)",
      }}>{label}</span>
      {value != null && (
        <span style={{
          fontFamily: "var(--font-mono)", fontSize: 12,
          color: "var(--text-secondary)",
        }}>{value}</span>
      )}
    </div>
  );
}
