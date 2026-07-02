import * as React from "react";

export interface WindowFrameProps extends React.HTMLAttributes<HTMLDivElement> {
  /** Window title shown in the titlebar. @default "Window" */
  title?: string;
  /** Optional icon node before the title. */
  icon?: React.ReactNode;
  /** Active (focused) window draws the accent border + lit titlebar. @default true */
  active?: boolean;
  /** Mono instruction text in the bottom hint strip. */
  hint?: string;
  /** Frame width in px. @default 520 */
  width?: number;
  /** Frame height in px. @default 340 */
  height?: number;
  /** Close button (×, TL) handler. */
  onClose?: (e: React.MouseEvent) => void;
  /** Minimize button (–, BL) handler. */
  onMinimize?: (e: React.MouseEvent) => void;
  /** Maximize button (□, TR) handler. */
  onMaximize?: (e: React.MouseEvent) => void;
  /** Resize control (⤡, BR) handler. */
  onResize?: (e: React.MouseEvent) => void;
  /** Mouse-down on the titlebar — wire this to start a drag. */
  onTitleMouseDown?: (e: React.MouseEvent) => void;
  children?: React.ReactNode;
}

/**
 * The signature Q OS window: titlebar, four colored corner buttons, glass body,
 * and a bottom hint strip with the control instructions.
 *
 * @startingPoint section="Window" subtitle="Draggable windowed-OS chrome with corner controls" viewport="700x420"
 */
export function WindowFrame(props: WindowFrameProps): JSX.Element;
