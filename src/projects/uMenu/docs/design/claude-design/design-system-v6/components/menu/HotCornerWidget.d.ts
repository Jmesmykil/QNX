import * as React from "react";

export interface HotCornerWidgetProps extends React.HTMLAttributes<HTMLDivElement> {
  /** Emblem glyph — defaults to the Q OS "Q". Themes swap this (⚡ ♥ 🔥 …). @default "Q" */
  glyph?: React.ReactNode;
  /** Which screen corner the widget anchors to. @default "tl" */
  corner?: "tl" | "tr" | "bl" | "br";
  /** Width in px. @default 96 */
  width?: number;
  /** Height in px. @default 72 */
  height?: number;
}

/**
 * Corner hot-zone marker carrying the theme's identity emblem.
 */
export function HotCornerWidget(props: HotCornerWidgetProps): JSX.Element;
