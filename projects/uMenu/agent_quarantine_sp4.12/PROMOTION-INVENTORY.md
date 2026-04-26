# Promotion Inventory — agent_quarantine_sp4.12
Generated: 2026-04-25T00:00:00Z
Track: K-cycle Track A

## Files to Promote (Track A)

| File | Size (bytes) | Public class(es) | Destination |
|------|-------------|------------------|-------------|
| qd_AboutLayout.hpp | 6,538 | QdAboutElement, QdAboutLayout | include/ul/menu/qdesktop/ |
| qd_AboutLayout.cpp | 23,336 | QdAboutElement, QdAboutLayout | source/ul/menu/qdesktop/ |
| qd_LockscreenLayout.hpp | 6,801 | QdLockscreenElement, QdLockscreenLayout | include/ul/menu/qdesktop/ |
| qd_LockscreenLayout.cpp | 16,045 | QdLockscreenElement, QdLockscreenLayout | source/ul/menu/qdesktop/ |
| qd_MonitorLayout.hpp | 6,227 | QdMonitorLayout | include/ul/menu/qdesktop/ |
| qd_MonitorLayout.cpp | 20,099 | QdMonitorLayout | source/ul/menu/qdesktop/ |
| qd_SettingsLayout.hpp | 11,817 | QdSettingsElement, QdSettingsLayout | include/ul/menu/qdesktop/ |
| qd_SettingsLayout.cpp | 42,640 | QdSettingsElement, QdSettingsLayout | source/ul/menu/qdesktop/ |
| qd_TerminalLayout.hpp | 5,989 | QdTerminalLayout | include/ul/menu/qdesktop/ |
| qd_TerminalLayout.cpp | 18,864 | QdTerminalLayout | source/ul/menu/qdesktop/ |

## Files Held (NOT Promoted)

| File | Size (bytes) | Reason |
|------|-------------|--------|
| qd_Launchpad.hpp | 14,298 | Track D scope — separate promotion cycle |
| qd_Launchpad.cpp | 34,931 | Track D scope — separate promotion cycle |

## Audit Results (pre-promotion)

- Namespace: all 5 pairs verified `namespace ul::menu::qdesktop` — correct.
- Includes: all already use angle-bracket form `#include <ul/menu/qdesktop/...>` — no fixup needed.
- Stubs/TODOs/FIXMEs: zero across all 10 files.
- External dependencies identified:
  - `<ul/menu/bt/bt_Manager.hpp>` and `ul::menu::bt::GetConnectedAudioDevice()` — MonitorLayout only.
  - `<ul/acc/acc_Accounts.hpp>` and `acc::GetAccountName()` — LockscreenLayout, SettingsLayout.
  - `ShowSettingsMenu()` from `ul::menu::ui` — SettingsLayout only.
  - psm/nifm services assumed already open by host environment — LockscreenLayout.
- Missing helpers (RenderTextAutoFit, TryFindLoadImage, TryGetActiveThemeResource): NOT referenced in any of the 5 pairs — no action needed.
