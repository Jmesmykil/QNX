import * as React from "react";

export interface FolderTileProps extends React.HTMLAttributes<HTMLDivElement> {
  /** Content category — sets the ring color + default glyph letter. @default "games" */
  category?: "games" | "homebrew" | "system" | "payloads" | "builtin" | "custom";
  /** Tile label below the glyph. Defaults to the capitalized category. */
  label?: string;
  /** Override the centered glyph (emoji / letter / node). */
  glyph?: React.ReactNode;
  /** Optional count badge (e.g. NRO count). */
  count?: number;
  /** Tile edge length in px. @default 96 */
  size?: number;
  /** Draw the selected ring. @default false */
  selected?: boolean;
}

/**
 * Desktop folder tile — glass body, category-colored ring, centered glyph.
 *
 * @startingPoint section="Desktop" subtitle="Auto-classified category folder tile" viewport="700x200"
 */
export function FolderTile(props: FolderTileProps): JSX.Element;
