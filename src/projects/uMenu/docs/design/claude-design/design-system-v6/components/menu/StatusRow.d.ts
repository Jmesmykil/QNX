import * as React from "react";

export interface StatusRowProps extends React.HTMLAttributes<HTMLDivElement> {
  /** Row label. */
  label: string;
  /** Right-aligned mono value (status rows only). */
  value?: string;
  /** Interactive action row (hover highlight + pointer) vs read-only status. @default false */
  action?: boolean;
  /** Tint the label red for destructive/power actions. @default false */
  danger?: boolean;
  /** Optional leading icon. */
  icon?: React.ReactNode;
}

/**
 * A 48px row in a hot-corner dropdown — read-only status or interactive action.
 */
export function StatusRow(props: StatusRowProps): JSX.Element;
