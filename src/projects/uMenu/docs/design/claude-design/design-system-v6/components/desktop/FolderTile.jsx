import React from "react";

const CAT = {
  games:    { color: "var(--cat-games)",    letter: "G" },
  homebrew: { color: "var(--cat-homebrew)", letter: "H" },
  system:   { color: "var(--cat-system)",   letter: "S" },
  payloads: { color: "var(--cat-payloads)", letter: "P" },
  builtin:  { color: "var(--cat-builtin)",  letter: "B" },
  custom:   { color: "var(--cat-custom)",   letter: "F" },
};

/**
 * FolderTile — a desktop category folder tile. Glass body, a 2px ring in the
 * category color, and a centered glyph (Glass pack draws the category letter).
 * Mirrors qd_FolderTheme DrawPack0Glass.
 */
export function FolderTile({
  category = "games",
  label,
  glyph,
  count,
  size = 96,
  selected = false,
  style = {},
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const cat = CAT[category] || CAT.games;
  const name = label != null ? label : category.charAt(0).toUpperCase() + category.slice(1);

  return (
    <div
      onMouseEnter={() => setHover(true)}
      onMouseLeave={() => setHover(false)}
      style={{ display: "inline-flex", flexDirection: "column", alignItems: "center", gap: 8, ...style }}
      {...rest}
    >
      <div style={{
        position: "relative",
        width: size, height: size,
        borderRadius: 18,
        background: "rgba(16,16,40,0.82)",
        border: `2px solid ${cat.color}`,
        boxShadow: selected
          ? `0 0 0 3px color-mix(in srgb, ${cat.color} 50%, transparent), var(--shadow-tile)`
          : (hover ? `0 0 22px -4px ${cat.color}` : "var(--shadow-tile)"),
        display: "grid", placeItems: "center",
        transform: hover ? "translateY(-3px)" : "none",
        transition: "transform .14s ease, box-shadow .18s ease",
      }}>
        <span style={{
          fontFamily: glyph ? "inherit" : "var(--font-sans)",
          fontSize: glyph ? size * 0.42 : size * 0.40,
          fontWeight: 700, lineHeight: 1,
          color: cat.color,
        }}>{glyph || cat.letter}</span>
        {count != null && (
          <span style={{
            position: "absolute", top: 6, right: 6,
            minWidth: 18, height: 18, padding: "0 5px",
            borderRadius: 999, background: cat.color, color: "#0A0A14",
            fontFamily: "var(--font-mono)", fontSize: 10, fontWeight: 600,
            display: "grid", placeItems: "center",
          }}>{count}</span>
        )}
      </div>
      <span style={{
        fontFamily: "var(--font-sans)", fontSize: 13, fontWeight: 500,
        color: "var(--text-primary)",
      }}>{name}</span>
    </div>
  );
}
