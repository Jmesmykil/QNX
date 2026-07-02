import * as React from "react";

export interface CardProps extends React.HTMLAttributes<HTMLDivElement> {
  /** Optional mono eyebrow above the title. */
  eyebrow?: string;
  /** Optional bold title row. */
  title?: string;
  /** Brighten the hairline to an accent ring. @default false */
  accent?: boolean;
  /** Inner padding in px. @default 20 */
  padding?: number;
  children?: React.ReactNode;
}

/**
 * Translucent glass surface with accent hairline — the base content container.
 */
export function Card(props: CardProps): JSX.Element;
