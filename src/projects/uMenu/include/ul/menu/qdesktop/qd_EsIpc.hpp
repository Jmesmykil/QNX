// qd_EsIpc.hpp — clean-room `es` (ETicket) IPC wrapper for the NSP installer.
//
// ───────────────────────────────────────────────────────────────────────────
// WHY THIS FILE EXISTS
// ───────────────────────────────────────────────────────────────────────────
// This tree's libnx exposes NO `es` service binding (there is no `<switch/
// services/es.h>` and no `esImportTicket`). The NSP installer's ImportTicket
// step needs exactly one ES call — import a ticket the NSP already carries —
// so we hand-roll a minimal, clean-room wrapper using only the PUBLIC libnx
// service plumbing (`smGetService` + `serviceDispatch`), the same way Goldleaf
// and other open homebrew do. No proprietary headers, no leaked SDK code.
//
// ───────────────────────────────────────────────────────────────────────────
// LEGAL / SAFETY POSTURE
// ───────────────────────────────────────────────────────────────────────────
//   * We IMPORT a ticket VERBATIM (the bytes already present in the user's NSP).
//     We never forge, derive, common-key-wrap, or "fix" a ticket.
//   * `esImportTicket` writes to system state (the ETicket store). It is
//     therefore a NAND-adjacent, write-class call. The NSP installer keeps every
//     call into this wrapper GATED behind `QdNspInstaller_NotImplemented` — this
//     file only makes the call *expressible*, it does not enable any install.
//   * Command id (1 = ImportTicket on `es`) is the publicly-documented value
//     (switchbrew). Confirm against the running firmware before ungating.
//
// GPL-2 — Q OS uMenu qdesktop subsystem.

#pragma once

#ifdef QDESKTOP_MODE

#include <switch.h>   // Service, Result, smGetService, serviceDispatch
#include <cstddef>

namespace ul::menu::qdesktop::es {

    // Acquire the `es` service session (idempotent). Returns ResultSuccess if the
    // session is already (or now) active; forwards the sm Result otherwise.
    //
    // NOTE: opening `es` from uMenu (a qlaunch replacement) may be DENIED by the
    // process's SAC/permission list — that is expected and must be probed on HW
    // before any live install. A denial surfaces here as a normal Result.
    Result Initialize();

    // Close the `es` session (idempotent; safe if never initialized).
    void Finalize();

    // True if the `es` session is currently held.
    bool IsActive();

    // Import a ticket + its certificate chain VERBATIM into the ETicket store.
    //   tik  / tik_size  — the .tik bytes from the NSP (typically 0x2C0).
    //   cert / cert_size — the .cert chain bytes from the NSP.
    // Maps to `es` cmd 1 (ImportTicket): two In map-alias buffers, no raw input.
    //
    // ⚠ WRITE-CLASS: this mutates system ETicket state. The installer only calls
    // this from a GATED path. Returns the raw `es` Result.
    Result ImportTicket(const void *tik,  size_t tik_size,
                        const void *cert, size_t cert_size);

}  // namespace ul::menu::qdesktop::es

#endif  // QDESKTOP_MODE
