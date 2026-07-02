import * as React from "react";

export interface DockTileProps extends React.HTMLAttributes<HTMLDivElement> {
  /** Tooltip / aria label. */
  label?: string;
  /** Icon glyph (node / letter / emoji). */
  glyph?: React.ReactNode;
  /** Focused dock tile — accent fill + border. @default false */
  active?: boolean;
  /** Show the running-state dot. @default false */
  running?: boolean;
  /** Edge length in px. @default 64 */
  size?: number;
}

/**
 * A tile in the bottom dock band — app launcher or minimized-window snapshot.
 */
export function DockTile(props: DockTileProps): JSX.Element;
