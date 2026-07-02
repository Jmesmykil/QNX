import React from "react";

/**
 * Button — Q OS primary action control.
 * Cyan-fill primary, glass secondary, ghost, and danger variants; three sizes.
 */
export function Button({
  children,
  variant = "primary",
  size = "md",
  disabled = false,
  block = false,
  iconLeft = null,
  iconRight = null,
  style = {},
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const [press, setPress] = React.useState(false);

  const sizes = {
    sm: { padding: "0 12px", height: 32, fontSize: 13, radius: 6, gap: 6 },
    md: { padding: "0 18px", height: 40, fontSize: 14, radius: 8, gap: 8 },
    lg: { padding: "0 26px", height: 48, fontSize: 16, radius: 10, gap: 10 },
  };
  const s = sizes[size] || sizes.md;

  const variants = {
    primary: {
      background: "var(--accent)",
      color: "var(--text-on-accent)",
      border: "1px solid transparent",
      boxShadow: hover ? "var(--glow-accent)" : "none",
    },
    secondary: {
      background: "var(--surface-glass)",
      color: "var(--text-primary)",
      border: "1px solid rgba(125,211,252,0.30)",
      boxShadow: hover ? "0 0 0 1px rgba(125,211,252,0.45)" : "none",
    },
    ghost: {
      background: hover ? "rgba(125,211,252,0.10)" : "transparent",
      color: "var(--accent)",
      border: "1px solid transparent",
      boxShadow: "none",
    },
    danger: {
      background: "var(--btn-close)",
      color: "#1A0606",
      border: "1px solid transparent",
      boxShadow: hover ? "0 0 22px -4px rgba(248,113,113,0.6)" : "none",
    },
  };
  const v = variants[variant] || variants.primary;

  return (
    <button
      type="button"
      disabled={disabled}
      onMouseEnter={() => setHover(true)}
      onMouseLeave={() => { setHover(false); setPress(false); }}
      onMouseDown={() => setPress(true)}
      onMouseUp={() => setPress(false)}
      style={{
        display: block ? "flex" : "inline-flex",
        width: block ? "100%" : "auto",
        alignItems: "center",
        justifyContent: "center",
        gap: s.gap,
        height: s.height,
        padding: s.padding,
        fontFamily: "var(--font-sans)",
        fontSize: s.fontSize,
        fontWeight: 600,
        lineHeight: 1,
        letterSpacing: "0.01em",
        borderRadius: s.radius,
        cursor: disabled ? "not-allowed" : "pointer",
        opacity: disabled ? 0.4 : 1,
        transform: press && !disabled ? "scale(0.97)" : "scale(1)",
        transition: "transform .08s ease, box-shadow .18s ease, background .18s ease",
        whiteSpace: "nowrap",
        ...v,
        ...style,
      }}
      {...rest}
    >
      {iconLeft}
      {children}
      {iconRight}
    </button>
  );
}
