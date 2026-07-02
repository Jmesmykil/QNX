import * as React from "react";

export interface MenuPanelProps extends React.HTMLAttributes<HTMLDivElement> {
  /** Panel width in px. @default 320 */
  width?: number;
  /** Optional mono section title. */
  title?: string;
  children?: React.ReactNode;
}

/**
 * Hot-corner dropdown surface — accent border ring + translucent navy fill.
 */
export function MenuPanel(props: MenuPanelProps): JSX.Element;
