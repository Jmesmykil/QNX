// qd_BDSPSaveParser.hpp — Brilliant Diamond / Shining Pearl save parser
//                          + PB8 decoder (v3.6 SCAFFOLD, read-only).
//
// STATUS: CLEAN-ROOM SCAFFOLD.  This is the first slice of a multi-week
//   reverse-engineering effort.  It compiles and exposes the SAME interface
//   shape as QdSwShSaveParser so the save-editor wiring is a trivial later
//   step, but several BDSP-specific details are still marked TODO(bdsp) and
//   return best-effort / placeholder values.  DO NOT treat this as a complete
//   BDSP decoder — see the "What is NOT done yet" list below.
//
// ── Why a separate parser from SwSh ──────────────────────────────────────────
//   SwSh (Sav8SWSH) uses the SwishCrypto block container: a stream of SCBlocks,
//   each XorShift32-encrypted, terminated by a salted SHA-256 hash, with party
//   data living in a keyed block (kKeyParty).  See qd_SwishCrypto.{hpp,cpp}.
//
//   BDSP (Sav8BS) is FUNDAMENTALLY DIFFERENT: it is a FLAT save image.  There is
//   no SCBlock hash table and no SwishCrypto layer.  Game data lives at fixed
//   absolute byte offsets within the decrypted save image.  Therefore this
//   parser does NOT include qd_SwishCrypto and does NOT walk a block stream —
//   it reads directly from constant offsets.
//
// ── Public sources (clean-room; numeric facts only) ──────────────────────────
//   PKHeX/PKHeX.Core/Saves/SAV8BS.cs           (GPL-2.0-or-later) — block offsets
//   PKHeX/PKHeX.Core/Saves/Substructures/Gen8b/Party8b.cs        — party layout
//   PKHeX/PKHeX.Core/PKM/PB8.cs                (GPL-2.0-or-later) — PB8 : G8PKM
//   PKHeX/PKHeX.Core/PKM/Shared/G8PKM.cs       (GPL-2.0-or-later) — field offsets
//   PKHeX/PKHeX.Core/PKM/Util/PokeCrypto.cs    (GPL-2.0-or-later) — Decrypt8/Shuffle8
//   project-pokemon.org BDSP save-structure RE notes              — flat layout
//
// Legal posture (identical to qd_SwShSaveParser.hpp):
//   Byte offsets, block offsets, slot sizes, and shuffle tables are numeric
//   facts embedded in the Pokémon save format by Game Freak.  They are not
//   copyrightable expression and appear verbatim across PKHeX and every
//   independent RE writeup.  Reimplemented here from those public facts; no
//   closed-source code is copied.
//
// v3.6 is READ-ONLY.  No save write-back is exposed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ul::menu::qdesktop {

// ── BDSP result codes ─────────────────────────────────────────────────────────
//
// Deliberately a SEPARATE enum from SwishResult.  BDSP's integrity scheme is also
// different from SwSh: NOT SwishCrypto's per-block salted SHA-256, but a SINGLE
// MD5 over the whole save image stored at a fixed offset (0xE9818 == kSizeG8Bdsp0
// - 0x10; PKHeX SAV8BS.SetChecksums()).  ChecksumMismatch IS therefore meaningful
// for BDSP — see ChecksumValid().  The orchestrator maps BdspResult → the editor's
// display string in qd_SaveEditorLayout.

enum class BdspResult : uint8_t {
    Ok               = 0,  ///< Parse succeeded.
    BufferTooSmall   = 1,  ///< File shorter than the minimum BDSP save structure.
    NotBdspSave      = 2,  ///< File size does not match a known Sav8BS length.
    ReadError        = 3,  ///< File could not be opened / read.
    ChecksumMismatch = 4,  ///< Stored MD5 (@0xE9818) ≠ recomputed (corrupt save).
};

// ── PB8 size constants ────────────────────────────────────────────────────────
//
// Source: PokeCrypto.cs — PB8 shares the Gen-8 sizes with PK8.
//   SIZE_8STORED = 0x148 (344 bytes), SIZE_8PARTY = 0x158 (360 bytes).
// PB8 is byte-compatible with PK8 at the field level (PB8 : G8PKM); the only
// real differences are the embedding save (Sav8BS vs Sav8SWSH) and a couple of
// BDSP-only flags we do not yet surface.
static constexpr size_t kPB8StoredSize = 0x148;  ///< Stored (box) slot size.
static constexpr size_t kPB8PartySize  = 0x158;  ///< Party slot size (w/ stats).

// ── Sav8BS flat-save block offsets ────────────────────────────────────────────
//
// Source: PKHeX SAV8BS.cs (Initialize / substructure construction).
// These are ABSOLUTE byte offsets into the decrypted BDSP save image — there is
// no block-key indirection (contrast SwSh's kKeyParty lookup).
//
// Verified 2026-06-12 against PKHeX master SAV8BS.cs:
//   PartyInfo = new Party8b(this, Raw.Slice(0x14098, Party8b.SIZE));
//   Items     @ 0x0563C   BoxLayout @ 0x148AA   Box(0) @ 0x14EF4
//   MyStatus  @ 0x79BB4
//
// TODO(bdsp): these offsets are correct for the retail BDSP 1.x/2.x save image
//   as decoded by current PKHeX.  Confirm they are stable across the game
//   versions we care about (the creator's cartridge dump) before trusting blind.
static constexpr size_t kBsOffsetParty    = 0x14098;  ///< Party8b substructure.
static constexpr size_t kBsOffsetItems    = 0x0563C;  ///< Item pouches.
static constexpr size_t kBsOffsetBoxLayout = 0x148AA; ///< Box names / wallpapers.
static constexpr size_t kBsOffsetBox0     = 0x14EF4;  ///< First PC box slot 0.
static constexpr size_t kBsOffsetMyStatus = 0x79BB4;  ///< Trainer card block.

// ── Sav8BS save sizes + integrity hash (PKHeX SaveUtil.cs / SAV8BS.cs) ──────────
//
// BDSP grew across game updates; a save image is exactly one of these lengths.
// The integrity hash is a SINGLE MD5 over the entire image, stored at a FIXED
// offset (SIZE_G8BDSP_0 - 0x10) regardless of revision — so on v1.1+ saves the
// 0x10-byte hash sits in the MIDDLE of the file, not the end.
static constexpr size_t kSizeG8Bdsp0  = 0xE9828;  ///< v1.0
static constexpr size_t kSizeG8Bdsp1  = 0xEDC20;  ///< v1.1
static constexpr size_t kSizeG8Bdsp2  = 0xEED8C;  ///< v1.2
static constexpr size_t kSizeG8Bdsp3  = 0xEF0A4;  ///< v1.3 (the creator's cartridge dump)
static constexpr size_t kBsHashOffset = 0xE9818;  ///< MD5 hash region (== kSizeG8Bdsp0 - 0x10).
static constexpr size_t kBsHashLen    = 0x10;     ///< MD5 digest length.

// ── Party8b substructure layout ───────────────────────────────────────────────
//
// Source: PKHeX Party8b.cs (Gen8b substructure).
//
// The Party8b block begins with a small header, then up to 6 PB8 PARTY-format
// slots laid out back-to-back.  In BDSP the party count is stored as an int32
// at the START of the block, and each slot is kPB8PartySize (0x158) bytes.
//
// TODO(bdsp): VERIFY the exact party-count field offset and slot base.
//   This scaffold assumes:
//       count   = int32 at  kBsOffsetParty + 0x00
//       slot[i] = PB8 at     kBsOffsetParty + kPartyHeaderSize + i*kPB8PartySize
//   PKHeX's Party8b uses an explicit GetPartyOffset(slot); the header size below
//   (4 bytes for the count int32) is the most common community value but was NOT
//   re-confirmed against Party8b.cs in this pass (the raw file 404'd during RE).
//   Until confirmed, ParseFile clamps and bounds-checks so a wrong header size
//   degrades to "0 party / empty" rather than reading garbage.
static constexpr size_t kPartyHeaderSize = 0;     ///< VERIFIED: slots start at 0x14098, no count prefix.
static constexpr int    kMaxPartySlots   = 6;     ///< Hard cap (party is always ≤6).

// ── Box storage ───────────────────────────────────────────────────────────────
// VERIFIED 2026-06-12 two ways: (1) PKHeX SAV8BS.cs `Box = 0x14EF4;` and
// (2) a real-save scan found a clean 1200-slot valid-or-empty run at 0x14EF4 with
// stride kPB8PartySize (0x158).  Box slots use the SAME 0x158 stride as party.
static constexpr size_t kBsOffsetBox  = 0x14EF4;  ///< Box[40] storage base.
static constexpr int    kBoxCount     = 40;       ///< 40 PC boxes.
static constexpr int    kSlotsPerBox  = 30;       ///< 30 slots per box (6×5 grid).

// ── Bag / inventory (MyItem8b) ──────────────────────────────────────────────
// VERIFIED 2026-06-13: PKHeX SAV8BS.cs:23 `Items = MyItem8b @ 0x0563C`; the bag
// is a FLAT array indexed by item ID, entry size 0x10 (InventoryItem8b.SIZE),
// count = u32 at entry+0 (Index is positional, not stored).  Confirmed against a
// real save (id 4 = 999 Poke Balls, id 17 = 999 Potions).
static constexpr size_t kBsOffsetBag   = 0x0563C; ///< MyItem8b block base.
static constexpr size_t kBagEntrySize  = 0x10;    ///< per-item slot size.
static constexpr int    kBagMaxItemId  = 3000;    ///< 0xBB80/0x10 slots.

// ── PB8 field offsets (within a decrypted 0x158 slot) ─────────────────────────
//
// Source: PKHeX G8PKM.cs (PB8 : G8PKM — identical field offsets to PK8).
// Verified 2026-06-12 against PKHeX master G8PKM.cs.  These are the SAME offsets
// the SwSh PK8 decoder uses; they are reproduced here as named constants so the
// BDSP code is self-documenting and does not depend on the SwSh translation unit.
//
// All offsets are little-endian reads at the given byte position.
static constexpr size_t kPB8OffEncryptionConstant = 0x00;  ///< u32 EC (== shuffle seed source).
static constexpr size_t kPB8OffSpecies            = 0x08;  ///< u16 National Dex #.
static constexpr size_t kPB8OffHeldItem           = 0x0A;  ///< u16 held item ID.
static constexpr size_t kPB8OffId32               = 0x0C;  ///< u32 (TID16 | SID16<<16).
static constexpr size_t kPB8OffExp                = 0x10;  ///< u32 experience.
static constexpr size_t kPB8OffAbility            = 0x14;  ///< u16 ability ID.
static constexpr size_t kPB8OffPid                = 0x1C;  ///< u32 personality value.
static constexpr size_t kPB8OffNature             = 0x20;  ///< u8 nature (0-24).
static constexpr size_t kPB8OffGender             = 0x22;  ///< byte: bits[3:2] gender.
static constexpr size_t kPB8OffForm               = 0x24;  ///< u8 form index.
static constexpr size_t kPB8OffMove1              = 0x72;  ///< u16 move 1.
static constexpr size_t kPB8OffMove2              = 0x74;  ///< u16 move 2.
static constexpr size_t kPB8OffMove3              = 0x76;  ///< u16 move 3.
static constexpr size_t kPB8OffMove4              = 0x78;  ///< u16 move 4.
static constexpr size_t kPB8OffIv32               = 0x8C;  ///< u32 packed IVs + flags.
static constexpr size_t kPB8OffNickname           = 0x58;  ///< UTF-16LE, 26 bytes (12 chars + NUL).
static constexpr size_t kPB8OffOtName             = 0xF8;  ///< UTF-16LE, 26 bytes.
static constexpr size_t kPB8OffStatLevel          = 0x148; ///< u8 current level (party region).
static constexpr size_t kPB8NameTrashBytes        = 26;    ///< Nickname/OT trash byte length.

// ── PB8 struct ────────────────────────────────────────────────────────────────
//
// Field-for-field mirror of SwSh's PK8 (qd_SwShSaveParser.hpp), so the save
// editor's party-display code can consume either with no changes.  Kept as a
// SEPARATE type (not a typedef of PK8) to avoid a hard include dependency on the
// SwSh translation unit and to leave room for BDSP-only fields later.

/// Decoded fields of a PB8 Pokémon entry (0x148 stored / 0x158 party bytes).
struct PB8 {
    uint16_t species;        ///< 0x08 — National Dex number.
    uint16_t held_item;      ///< 0x0A — Held item ID.
    uint32_t id32;           ///< 0x0C — Trainer ID (TID16 | SID16<<16).
    uint32_t exp;            ///< 0x10 — Experience points.
    uint16_t ability;        ///< 0x14 — Ability ID.
    uint32_t pid;            ///< 0x1C — Personality value.
    uint8_t  nature;         ///< 0x20 — Nature (0-24).
    uint8_t  gender;         ///< 0x22 bits[3:2] — 0=M, 1=F, 2=Unknown.
    uint8_t  form;           ///< 0x24 — Form index.
    uint16_t moves[4];       ///< 0x72/74/76/78 — Move IDs.
    uint32_t iv32;           ///< 0x8C — Packed IVs (5 bits each + flags).
    char     nickname[26];   ///< 0x58 — UTF-16LE nickname trash (26 bytes).
    char     ot_name[26];    ///< 0xF8 — UTF-16LE OT name trash (26 bytes).
    uint8_t  stat_level;     ///< 0x148 — Current level (party slot).
    bool     is_egg;         ///< IV32 bit 30.
    bool     is_nicknamed;   ///< IV32 bit 31.
    bool     is_shiny;       ///< (PID>>16 ^ PID&0xFFFF ^ TID16 ^ SID16) < 16. TODO(bdsp): confirm threshold.

    // Convenience: decoded IV values extracted from iv32.
    uint8_t  iv_hp;   ///< bits [4:0]
    uint8_t  iv_atk;  ///< bits [9:5]
    uint8_t  iv_def;  ///< bits [14:10]
    uint8_t  iv_spe;  ///< bits [19:15]
    uint8_t  iv_spa;  ///< bits [24:20]
    uint8_t  iv_spd;  ///< bits [29:25]

    // Decoded display name (nickname if nicknamed, else empty so the caller can
    // substitute a species name).  UTF-8 for SDL text rendering.
    char     display_name[32];
    char     ot_display[24];  ///< OT name decoded to UTF-8.

    // Constructor: zero-initialise all POD fields (mirrors PK8::PK8).
    PB8() : species(0), held_item(0), id32(0), exp(0), ability(0), pid(0),
            nature(0), gender(0), form(0), iv32(0), stat_level(0),
            is_egg(false), is_nicknamed(false), is_shiny(false),
            iv_hp(0), iv_atk(0), iv_def(0), iv_spe(0), iv_spa(0), iv_spd(0)
    {
        memset(moves,        0, sizeof(moves));
        memset(nickname,     0, sizeof(nickname));
        memset(ot_name,      0, sizeof(ot_name));
        memset(display_name, 0, sizeof(display_name));
        memset(ot_display,   0, sizeof(ot_display));
    }
};

// ── BDSPSave ─────────────────────────────────────────────────────────────────
//
// Top-level decoded BDSP save.  Same shape as SwShSave (v3.6 — party only for
// now) so the editor's party panel binds to it identically.

/// Top-level decoded BDSP save data (scaffold — party only).
struct BDSPSave {
    char     trainer_name[24];  ///< OT name (UTF-8). MyStatus8b @ 0x79BB4+0x00.
    uint32_t trainer_id;        ///< TID16 (id32 & 0xFFFF). MyStatus8b @ +0x1C.
    uint16_t trainer_sid;       ///< SID16 (id32 >> 16). MyStatus8b @ +0x1E.
    uint8_t  gender;            ///< 0=M, 1=F. MyStatus8b male-flag @ +0x24 (1=male).
    uint32_t money;             ///< Money (max 999999). MyStatus8b @ +0x20.
    PB8      party[6];          ///< Current party slots.
    int      party_count;       ///< Number of occupied party slots (0-6).

    BDSPSave() : trainer_id(0), trainer_sid(0), gender(0), money(0), party_count(0)
    {
        memset(trainer_name, 0, sizeof(trainer_name));
        for (int i = 0; i < 6; ++i) party[i] = PB8();
    }
};

// ── BoxSlotLite ────────────────────────────────────────────────────────────────
//
// Lightweight decoded box slot for the save-editor box grid.  Only the fields the
// viewer renders — a full PB8 × 1200 would copy ~150 KB; this keeps the box list
// to a handful of bytes per occupied slot (empties are omitted entirely).

struct BoxSlotLite {
    uint8_t box;   ///< 0..39
    uint8_t slot;  ///< 0..29
    PB8     pk;    ///< full decoded slot — drives both the box list and the detail view
};

// One held bag item (count > 0).  id maps directly to item_name() (PKHeX item ID).
struct BagItemLite {
    uint16_t id;
    uint32_t count;
};

// ── QdBDSPSaveParser ─────────────────────────────────────────────────────────

/// Reads a BDSP (Sav8BS) save file and decodes party Pokémon.
///
/// Interface mirrors QdSwShSaveParser so qd_SaveEditorLayout can branch to it
/// with minimal new code (see the integration notes at the bottom of the .cpp).
class QdBDSPSaveParser {
public:
    /// Read a BDSP save file from `path`, parse it, and fill `out_save`.
    /// On failure, out_save is left default-constructed and an error is returned.
    ///
    /// NOTE: unlike QdSwShSaveParser::ParseFile, there is NO SwishDecrypt step —
    /// the BDSP save is read directly at fixed offsets.  (If the on-disk image
    /// turns out to carry an outer encryption layer for the cartridge dumps we
    /// target, that step is added here — see TODO(bdsp) in the .cpp.)
    static BdspResult ParseFile(const std::string &path, BDSPSave &out_save);

    /// Parse an already-in-memory BDSP save image.  Exposed (like the SwSh
    /// helpers) so host unit tests can feed a fixture buffer without file I/O.
    static BdspResult ParseBuffer(const uint8_t *buf, size_t size,
                                  BDSPSave &out_save);

    /// Parse all 40×30 PC box slots from the save file.  Occupied slots (valid
    /// species 1..1025) are appended to `out_slots`; `out_box_counts[box]` gets
    /// the per-box occupied count (caller must provide an array of kBoxCount).
    /// Reuses the SAME decrypt path as party.  Returns Ok even if boxes are empty.
    static BdspResult ParseBoxesFromFile(const std::string &path,
                                         std::vector<BoxSlotLite> &out_slots,
                                         int out_box_counts[kBoxCount]);

    /// Parse the bag (MyItem8b @ 0x0563C) — appends every item with count > 0 to
    /// out_items (id maps to item_name()).  Returns Ok even if the bag is empty.
    static BdspResult ParseBagFromFile(const std::string &path,
                                       std::vector<BagItemLite> &out_items);

    /// Decrypt + un-shuffle a single PB8 party slot (0x158 bytes) IF BDSP stores
    /// its slots encrypted.  Mirrors PokeCrypto.Decrypt8 + Shuffle8.
    ///
    /// TODO(bdsp): CONFIRM whether BDSP stores party/box PB8 slots ENCRYPTED in
    ///   the save image.  PKHeX's Sav8BS appears to keep them DECRYPTED in-place
    ///   (it does not re-run PokeCrypto on read), whereas SwSh stores them
    ///   encrypted inside the party SCBlock.  If BDSP slots are already plaintext
    ///   this routine is a NO-OP passthrough (current behaviour).  The full
    ///   Decrypt8/Shuffle8 implementation is provided behind kBdspSlotsEncrypted
    ///   so flipping one flag enables it once verified.
    static void DecryptPB8(const uint8_t *in_slot, uint8_t *out_raw);

    /// Inverse of DecryptPB8 — re-shuffle + re-encrypt a CANONICAL plaintext slot
    /// back to its on-save encrypted form.  WRITE-BACK primitive (basis of
    /// moving/storing Pokémon).  Host-verified (/tmp, real save): round-trip is
    /// bit-identical 9/9 AND the two-call crypt matches PKHeX (party levels decode
    /// sane).  Still has zero in-tree callers, but the save-integrity guard it
    /// depends on (RecomputeChecksum) is now implemented + host-verified.
    static void EncryptPB8(const uint8_t *in_canonical, uint8_t *out_encrypted);

    /// Recompute the Sav8BS integrity hash IN-PLACE: zero [0xE9818,0xE9828), MD5
    /// the WHOLE buffer, write the 16-byte digest back at 0xE9818.  MUST be called
    /// after ANY edit, immediately before the save is written, or the game treats
    /// the file as corrupt.  `size` must be a recognised Sav8BS length (else
    /// NotBdspSave — guards against stamping a hash onto a truncated write).
    /// Host-verified (2026-06-14) to reproduce openssl/PKHeX on a real v1.3 save.
    static BdspResult RecomputeChecksum(uint8_t *buf, size_t size);

    /// Verify the stored Sav8BS MD5 without mutating `buf`.  Returns Ok if the
    /// stored digest matches a fresh recompute, ChecksumMismatch if not, or a
    /// size/arg error.  (Read-side counterpart of RecomputeChecksum.)
    static BdspResult ChecksumValid(const uint8_t *buf, size_t size);

    /// Decode a raw (already-plaintext, un-shuffled) PB8 slot into a PB8 struct.
    static void DecodePB8(const uint8_t *raw, PB8 &out);

private:
    /// Read up to max_size bytes from path into a heap buffer.
    /// Returns the number of bytes read, or 0 on error.  Caller delete[]s.
    static size_t ReadFile(const std::string &path,
                           uint8_t **out_buf, size_t max_size);

    /// Decode a UTF-16LE trash string to a NUL-terminated UTF-8 buffer.
    /// (Same algorithm as the SwSh parser; duplicated to keep this TU standalone.)
    static void DecodeUtf16LeToUtf8(const uint8_t *src, size_t src_bytes,
                                    char *dst, size_t dst_chars);
};

} // namespace ul::menu::qdesktop
