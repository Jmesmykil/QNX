import * as React from "react";

export interface BadgeProps extends React.HTMLAttributes<HTMLSpanElement> {
  /** Semantic color. @default "accent" */
  tone?: "accent" | "cyan" | "magenta" | "lavender" | "ok" | "warn" | "error" | "neutral";
  /** Filled chip vs tinted outline. @default false */
  solid?: boolean;
  children?: React.ReactNode;
}

/**
 * Mono status pill — versions, firmware, HW-verified state, security flags.
 */
export function Badge(props: BadgeProps): JSX.Element;
