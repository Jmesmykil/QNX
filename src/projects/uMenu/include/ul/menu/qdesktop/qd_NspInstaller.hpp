// qd_NspInstaller.hpp — Goldleaf-style LOCAL NSP/NSZ/XCI installer (v3.6 absorb wave 2).
//
// ───────────────────────────────────────────────────────────────────────────
// PURPOSE
// ───────────────────────────────────────────────────────────────────────────
// Install a Switch title from a container file ALREADY ON THE SD CARD into the
// console's content storage (NCM), so uMenu becomes a self-contained OS that
// can install titles without Goldleaf/Tinfoil/DBI.
//
//   sdmc:/<something>.nsp   ──parse──>  NCAs + CNMT + (optional) ticket/cert
//                           ──place──>  NCM content storage (SD or System)
//                           ──import─>  ES ticket (only if NSP carries one)
//                           ──register> ns application record + content-meta DB
//                           ──verify──>  NCM has every NCA the CNMT lists
//
// ───────────────────────────────────────────────────────────────────────────
// LEGAL / CLEAN-ROOM POSTURE  (see docs/NSP-INSTALLER-DESIGN.md §Legal)
// ───────────────────────────────────────────────────────────────────────────
//   * LOCAL SD FILES ONLY. No network fetch of titles, keys, or tickets.
//   * NO title-key DUMPING and NO key DERIVATION. We never read prod.keys, never
//     compute a title key, never decrypt NCA content. We move ENCRYPTED NCAs
//     byte-for-byte and let Horizon/FS decrypt at runtime using keys the console
//     already owns (exactly how a legitimately-purchased install works).
//   * The ticket, if present in the NSP, is imported VERBATIM via ES — we do not
//     forge, generate, or "fix" a ticket. A title with no ticket and no existing
//     rights id on the console will simply fail to launch (by design).
//   * This is the same content-pipeline shape libnx/Atmosphère expose publicly;
//     no proprietary Nintendo headers, no leaked SDK code.
//
// ───────────────────────────────────────────────────────────────────────────
// SAFETY POSTURE  (v3.6 scaffold — Prime-Directive PD-4 / PD-11 gated)
// ───────────────────────────────────────────────────────────────────────────
// THE PARSE / VALIDATE / VERIFY / FREE-SPACE PATHS ARE READ-ONLY AND FULLY
// IMPLEMENTED. They touch only the SD card and query-only NCM/NS APIs.
//
// EVERY PATH THAT WRITES TO NAND OR NCM CONTENT STORAGE IS *STUBBED* behind a
// `// GATED: live NAND write — do not enable without review` guard and returns
// Qd_NspInstaller_NotImplemented. Do NOT remove a gate without James's explicit
// authorization (PD-11) AND the open questions in the design doc resolved.
//
// Additionally, the libnx in this tree exposes NEITHER an `es` service binding
// NOR `nsPushApplicationRecord`, so the register/import steps are not even
// callable yet — see design doc §"libnx gaps". The stubs document exactly which
// custom IPC wrappers a future, reviewed change would have to add.
//
// ───────────────────────────────────────────────────────────────────────────
// THREADING / PROGRESS  (mirrors qd_CheatsInstaller)
// ───────────────────────────────────────────────────────────────────────────
//   1. Construct QdNspInstaller.
//   2. OpenAndParse(path) — SYNCHRONOUS, read-only, safe. Populates the entry
//      list + detects the CNMT/tickets. Call on a worker thread if the file is
//      large (PFS0 string-table parse is cheap; this is mostly for symmetry).
//   3. (future) StartInstall() — spawns a worker thread, drives the state
//      machine. Currently the write states return NotImplemented immediately.
//   4. Poll GetProgress() each frame; render the phase + percent.
//   5. Stop() in the layout dtor.
//
// GPL-2 — Q OS uMenu qdesktop subsystem.

#pragma once

#ifdef QDESKTOP_MODE

#include <ul/ul_Result.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

#include <switch.h>  // Thread, Mutex, u64, s64, Result, Ncm* types

namespace ul::menu::qdesktop {

// ── Result codes ───────────────────────────────────────────────────────────
//
// We reuse Module_Libnx with libnx error enums for transport-shaped failures
// (bad input, IO error) so callers can treat them uniformly with the rest of
// qdesktop. The one bespoke value is the GATE sentinel, which deliberately uses
// a distinctive description string in logs so a "NotImplemented" can never be
// mistaken for a real success or a real libnx error.
//
//   QdNspInstaller_NotImplemented — returned by every GATED write stub. If you
//   ever see this Result reach the UI, the installer did NOT write anything.
//
// NOTE: this tree's libnx has no LibnxError_NotImplemented, so we mint a bespoke
// description (350) inside Module_Libnx that is deliberately OUTSIDE the libnx
// LibnxError_* enum range — it reads as a distinctive 0x...15E in logs and can
// never collide with a real libnx error code or with ResultSuccess (0).
inline constexpr Result QdNspInstaller_NotImplemented =
    MAKERESULT(Module_Libnx, 350);

// ── Container kind ───────────────────────────────────────────────────────────

enum class NspContainerKind : uint8_t {
    Unknown = 0,
    Nsp,   ///< PFS0 archive of NCAs (+ cnmt.nca + optional .tik/.cert). SCAFFOLDED.
    Nsz,   ///< Like NSP but NCAs are zstd-compressed (.ncz). DEFERRED (decompress step).
    Xci,   ///< Gamecard image: HFS0 root → "secure" partition holds a PFS0-like set. DEFERRED.
};

// ── A single entry inside the container's PFS0 ───────────────────────────────

struct NspFileEntry {
    std::string name;        ///< e.g. "abcd...0123.nca", "abcd...0123.cnmt.nca", "....tik"
    u64         data_offset; ///< absolute offset of this file's data within the .nsp
    u64         size;        ///< byte length of this file's data

    // Convenience classification (filled by the parser from the name suffix).
    bool is_nca    = false;  ///< name ends with ".nca" (and not ".cnmt.nca")
    bool is_cnmt   = false;  ///< name ends with ".cnmt.nca" — the content-meta NCA
    bool is_ncz    = false;  ///< name ends with ".ncz" — compressed NCA (NSZ)
    bool is_ticket = false;  ///< name ends with ".tik"
    bool is_cert   = false;  ///< name ends with ".cert"
};

// ── Parsed-container summary ──────────────────────────────────────────────────

struct NspContainer {
    std::string               path;          ///< sdmc: path the parse opened.
    NspContainerKind          kind = NspContainerKind::Unknown;
    std::vector<NspFileEntry> entries;        ///< every file in the PFS0.

    // Derived roll-ups (read-only; computed by OpenAndParse).
    u64  total_content_bytes = 0;  ///< sum of all NCA/NCZ entry sizes.
    int  nca_count           = 0;  ///< plain .nca entries (excludes .cnmt.nca).
    int  ncz_count           = 0;  ///< compressed .ncz entries (NSZ indicator).
    bool has_cnmt            = false;
    bool has_ticket          = false;
    bool has_cert            = false;

    // The index into `entries` of the cnmt NCA, or -1 if absent.
    int  cnmt_entry_index    = -1;
};

// ── State machine ─────────────────────────────────────────────────────────────
//
// The states intentionally map 1:1 onto the design-doc pipeline so logs and the
// UI phase label read identically to the document.
enum class NspInstallState : uint8_t {
    Idle         = 0,
    Parse,          ///< OPEN .nsp, read PFS0 header + entry/string tables.     [SAFE]
    Validate,       ///< CNMT present, free-space ok, content-count sane.       [SAFE]
    PlaceContent,   ///< stream each NCA into an NCM placeholder, register it.  [GATED]
    ImportTicket,   ///< es-import the .tik (only if the NSP carried one).      [GATED]
    Register,       ///< write content-meta DB row + ns application record.     [GATED]
    Verify,         ///< NCM has every content id the CNMT enumerated.          [SAFE]
    Done,
    Rollback,       ///< delete partially-placed NCAs / meta on any failure.    [GATED]
    Failed,
};

const char *NspInstallStateName(NspInstallState s);  ///< for logs / UI label.

// ── Progress snapshot (UI thread reads this; worker writes it under a mutex) ──

struct NspInstallProgress {
    NspInstallState state          = NspInstallState::Idle;
    int             percent        = 0;   ///< [0,100] within the current content copy.
    int             ncas_done      = 0;   ///< NCAs fully placed + registered.
    int             ncas_total     = 0;   ///< NCAs the CNMT says to place.
    u64             bytes_done     = 0;
    u64             bytes_total    = 0;
    char            step[128]      = {};  ///< human label of the current step.
    char            error[256]     = {};  ///< populated when state == Failed.
};

// ── QdNspInstaller ────────────────────────────────────────────────────────────

class QdNspInstaller {
public:
    QdNspInstaller()  = default;
    ~QdNspInstaller();

    // Owns a Thread + Mutex once StartInstall lands — keep it pinned.
    QdNspInstaller(const QdNspInstaller&)            = delete;
    QdNspInstaller& operator=(const QdNspInstaller&) = delete;

    // ── READ-ONLY, SAFE TO USE TODAY ────────────────────────────────────────

    /// Open a local container file and parse its PFS0 directory into `out`.
    /// Reads only `path` (must be an sdmc: path). Does NOT touch NCM/NS/ES and
    /// writes nothing. This is the fully-implemented half of the installer.
    ///
    /// @return ResultSuccess and fills `out`; or:
    ///   MAKERESULT(Module_Libnx, LibnxError_BadInput)  — null out / empty path.
    ///   MAKERESULT(Module_Libnx, LibnxError_IoError)   — open/read/seek failed,
    ///                                                    or magic != "PFS0", or
    ///                                                    header self-inconsistent.
    static Result OpenAndParse(const std::string &path, NspContainer *out);

    /// Detect the container kind purely from the on-disk magic + extension,
    /// without a full parse. Read-only. Returns NspContainerKind::Unknown on any
    /// read error or unrecognized magic.
    static NspContainerKind DetectKind(const std::string &path);

    /// Free space (bytes) available on the destination storage, for the
    /// pre-install Validate gate. READ-ONLY (query-only NCM call).
    ///   storage_id — NcmStorageId_SdCard or NcmStorageId_BuiltInUser.
    /// Returns ResultSuccess + *out_free on success; forwards the ncm Result
    /// otherwise. (Opens, queries, and closes the content storage; writes nothing.)
    static Result QueryFreeSpace(NcmStorageId storage_id, s64 *out_free);

    /// Read-only verify: does NCM already hold every NCA content-id enumerated
    /// by the parsed CNMT? Used as the post-install Verify step AND as a safe
    /// "is this title already installed?" probe. Implemented against query-only
    /// NCM (ncmContentStorageHas / ncmContentMetaDatabaseHas).
    ///
    /// NOTE: the scaffold parses the PFS0 envelope; cracking the *encrypted* CNMT
    /// NCA to enumerate inner content-ids requires the NCA/PFS0-section reader
    /// that lands with the (gated) PlaceContent work — until then this returns
    /// QdNspInstaller_NotImplemented. It performs NO writes regardless.
    Result VerifyInstalled(const NspContainer &container,
                           NcmStorageId        storage_id,
                           bool               *out_all_present);

    // ── INSTALL DRIVER (write path is GATED) ─────────────────────────────────

    /// Spawn the install worker. SCAFFOLD STATUS: the worker walks Parse →
    /// Validate (both safe) then hits PlaceContent, which is GATED and returns
    /// QdNspInstaller_NotImplemented, transitioning the state machine to Failed
    /// with a clear "live install disabled" message. NOTHING is written.
    void StartInstall(const std::string &path, NcmStorageId storage_id);

    /// Thread-safe progress snapshot for the UI thread.
    NspInstallProgress GetProgress() const;

    /// Signal the worker to abort and join. Idempotent; safe before StartInstall.
    void Stop();

private:
    // ── Worker / state machine ───────────────────────────────────────────────
    static void WorkerEntry(void *arg);
    void        RunInstall();

    // Each step returns a Result. The SAFE steps are real; the GATED steps are
    // stubs that return QdNspInstaller_NotImplemented (see the .cpp).
    Result StepParse();
    Result StepValidate();
    Result StepPlaceContent();   ///< GATED
    Result StepImportTicket();   ///< GATED
    Result StepRegister();       ///< GATED
    Result StepVerify();
    Result StepRollback();       ///< GATED

    // ── Progress helpers ─────────────────────────────────────────────────────
    void SetState(NspInstallState s, const char *step);
    void SetPercent(int pct);
    void Fail(const char *fmt, ...);

    // ── State ─────────────────────────────────────────────────────────────────
    std::string        path_{};
    NcmStorageId       storage_id_ = NcmStorageId_SdCard;
    NspContainer       container_{};

    Thread             thread_{};
    bool               thread_started_ = false;
    // NOTE: must be the libnx `::Mutex` (a u32), NOT uCommon's RAII `ul::Mutex`.
    // This header pulls in <ul/ul_Result.hpp> → <ul/ul_Include.hpp>, which defines
    // `ul::Mutex`; inside `namespace ul::menu::qdesktop` an unqualified `Mutex`
    // would resolve to that enclosing-namespace class and break the libnx
    // mutexLock/mutexUnlock(&progress_lock_) calls in the .cpp. Qualify it.
    mutable ::Mutex    progress_lock_{};
    NspInstallProgress progress_{};
    std::atomic<bool>  abort_{false};

    // Placeholders we created during a (future) PlaceContent, tracked so
    // Rollback can delete exactly what we wrote and nothing else.
    std::vector<NcmPlaceHolderId> created_placeholders_{};
    std::vector<NcmContentId>     registered_contents_{};

    // 512 KiB worker stack — NCA streaming copies in large blocks; matches the
    // CheatsInstaller's 256 KiB plus headroom for the future zstd window (NSZ).
    static constexpr size_t kStackSize = 512 * 1024;
    alignas(0x1000) uint8_t stack_[kStackSize] = {};
};

}  // namespace ul::menu::qdesktop

#endif  // QDESKTOP_MODE
