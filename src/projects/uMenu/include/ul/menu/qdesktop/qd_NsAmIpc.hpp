// qd_NsAmIpc.hpp — clean-room `ns:am` IApplicationManagerInterface IPC wrapper
// for the NSP installer's application-record push/delete.
//
// ───────────────────────────────────────────────────────────────────────────
// WHY THIS FILE EXISTS
// ───────────────────────────────────────────────────────────────────────────
// This tree's libnx binds `ns` for read/list (nsListApplicationRecord, etc.)
// but exposes NEITHER `nsPushApplicationRecord` NOR `nsDeleteApplicationRecord`.
// The NSP installer's Register/Rollback steps need exactly those two commands on
// IApplicationManagerInterface. We hand-roll them clean-room over PUBLIC libnx
// plumbing: we reuse libnx's own `nsInitialize` + the public Service getters to
// obtain the IApplicationManagerInterface session, then `serviceDispatch` the two
// missing commands. No proprietary headers, no leaked SDK code.
//
// ───────────────────────────────────────────────────────────────────────────
// LEGAL / SAFETY POSTURE
// ───────────────────────────────────────────────────────────────────────────
//   * PushApplicationRecord makes an INSTALLED title visible to HOME by writing
//     an application record pointing at content the user already placed. We push
//     records that describe the user's OWN local content only.
//   * Both calls mutate the system title database. They are WRITE-CLASS and
//     NAND-adjacent. The NSP installer keeps every call into this wrapper GATED
//     behind `QdNspInstaller_NotImplemented`; this file only makes the calls
//     *expressible*, it does not enable any install.
//   * Command ids (16 = PushApplicationRecord, 21 = DeleteApplicationRecord on
//     IApplicationManagerInterface) are the publicly-documented values
//     (switchbrew). A careless push can SHADOW an existing install — see the
//     design doc risks. Confirm cmd ids against the running firmware before
//     ungating.
//
// GPL-2 — Q OS uMenu qdesktop subsystem.

#pragma once

#ifdef QDESKTOP_MODE

#include <switch.h>   // Service, Result, NcmContentMetaKey, NcmStorageId
#include <cstdint>

namespace ul::menu::qdesktop::nsam {

    // One entry in the buffer PushApplicationRecord takes: a content-meta key
    // plus the storage the content lives on. Layout matches the system ABI
    // (`ns::ContentStorageRecord`): the 0x10-byte NcmContentMetaKey, then a u8
    // storage id, then 7 padding bytes (the struct is 0x18 bytes).
    //
    // We define it locally because libnx in this tree does not.
    typedef struct {
        NcmContentMetaKey meta_key;   ///< 0x10 bytes (id, version, type, ...).
        u8                storage_id; ///< NcmStorageId of the content.
        u8                padding[7]; ///< must be zero.
    } ContentStorageRecord;

    static_assert(sizeof(ContentStorageRecord) == 0x18,
                  "ContentStorageRecord must be 0x18 bytes (ns ABI)");

    // Acquire the IApplicationManagerInterface session (idempotent). Internally
    // calls libnx `nsInitialize` and then resolves the AppManager Service* in a
    // firmware-version-tolerant way (the [3.0.0+] getter cmd, falling back to the
    // pre-3.0.0 direct session). Returns ResultSuccess on success.
    //
    // NOTE: as with es, uMenu's process permissions may not allow the record-push
    // commands even if the session opens; probe on HW before ungating.
    Result Initialize();

    // Release the session (idempotent; safe if never initialized).
    void Finalize();

    // True if the AppManager session is currently held.
    bool IsActive();

    // Push (create/replace) an application record so HOME sees the title.
    //   app_id              — the title's ApplicationId.
    //   last_modified_event — the "last event" tag (installers use 0x3).
    //   records / count     — the ContentStorageRecord array describing content.
    // Maps to IApplicationManagerInterface cmd 16 (PushApplicationRecord):
    //   raw in  = { u8 last_modified_event; u64 application_id; }
    //   buffer  = records[] as one In map-alias buffer.
    //
    // ⚠ WRITE-CLASS: mutates the title database. Installer calls this GATED only.
    Result PushApplicationRecord(u64 app_id, u8 last_modified_event,
                                 const ContentStorageRecord *records, s32 count);

    // Delete an application record (the Rollback half of the push above).
    // Maps to IApplicationManagerInterface cmd 21 (DeleteApplicationRecord):
    //   raw in = { u64 application_id; }
    //
    // ⚠ WRITE-CLASS: mutates the title database. Installer calls this GATED only.
    Result DeleteApplicationRecord(u64 app_id);

}  // namespace ul::menu::qdesktop::nsam

#endif  // QDESKTOP_MODE
