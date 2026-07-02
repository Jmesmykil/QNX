import * as React from "react";

export interface ButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  /** Visual style. @default "primary" */
  variant?: "primary" | "secondary" | "ghost" | "danger";
  /** Control size. @default "md" */
  size?: "sm" | "md" | "lg";
  /** Disable interaction + dim. @default false */
  disabled?: boolean;
  /** Stretch to fill container width. @default false */
  block?: boolean;
  /** Node rendered before the label. */
  iconLeft?: React.ReactNode;
  /** Node rendered after the label. */
  iconRight?: React.ReactNode;
  children?: React.ReactNode;
}

/**
 * The Q OS action button — cyan-fill primary plus glass / ghost / danger variants.
 *
 * @startingPoint section="Core" subtitle="Primary action control with 4 variants" viewport="700x150"
 */
export function Button(props: ButtonProps): JSX.Element;
