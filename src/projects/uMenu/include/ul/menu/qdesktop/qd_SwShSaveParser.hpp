// qd_SwShSaveParser.hpp — SwSh SCBlock parser + PK8 decoder (v3.5, read-only).
//
// Parses a SwishCrypto-decrypted save buffer into useful game data.
//
// Sources:
//   PKHeX/PKHeX.Core/Saves/Encryption/SwishCrypto/SCBlock.cs  (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/PKM/Shared/G8PKM.cs                      (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/PKM/Util/PokeCrypto.cs                   (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/PKM/PK8.cs                               (GPL-2.0-or-later)
//
// Legal posture:
//   Block keys, byte offsets, and shuffle tables are numeric facts embedded in
//   the Pokemon save format by Game Freak.  They are not copyrightable expression.
//
// v3.5 is READ-ONLY.  No save write-back is exposed in the UI.
//   SwishEncrypt() exists in qd_SwishCrypto but is not called here.
#pragma once

#include <ul/menu/qdesktop/qd_SwishCrypto.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ul::menu::qdesktop {

// ── SCBlock type codes ────────────────────────────────────────────────────────
//
// Source: PKHeX SCTypeCode.cs (numeric fact constants).

enum class SCTypeCode : uint8_t {
    None   = 0,
    Bool1  = 1,
    Bool2  = 2,
    Bool3  = 3,
    Object = 4,
    Array  = 5,
    U8     = 8,
    U16    = 9,
    U32    = 10,
    U64    = 11,
    I8     = 12,
    I16    = 13,
    I32    = 14,
    I64    = 15,
    F32    = 16,
    F64    = 17,
};

// ── Well-known SCBlock keys ───────────────────────────────────────────────────
//
// Source: PKHeX SaveBlockAccessor8SWSH.cs — numeric fact constants.

static constexpr uint32_t kKeyMyStatus  = 0x6c1cf0bf;  ///< Trainer info block.
static constexpr uint32_t kKeyMyItem    = 0x4c2eb6e1;  ///< Item bag block.
static constexpr uint32_t kKeyParty     = 0xa9e72a90;  ///< Current party block.
// Box keys: KBox1 = 0x2A7B33C8, KBox2 = 0x2A7B33C9, …  KBox32 = 0x2A7B33E7
// (sequential, not used in the v3.5 party-only viewer)

// ── PK8 struct ────────────────────────────────────────────────────────────────
//
// Fields extracted from G8PKM.cs and PokeCrypto.cs (PKHeX, GPL-2.0-or-later).
// Byte offsets are format facts; field names follow PKHeX convention.

/// Decoded fields of a PK8 Pokémon entry (344 bytes stored / 360 bytes party).
struct PK8 {
    uint16_t species;           ///< 0x08 — National Dex number.
    uint16_t held_item;         ///< 0x0A — Held item ID.
    uint32_t id32;              ///< 0x0C — Trainer ID (TID16 | SID16<<16).
    uint32_t exp;               ///< 0x10 — Experience points.
    uint16_t ability;           ///< 0x14 — Ability ID.
    uint32_t pid;               ///< 0x1C — Personality value.
    uint8_t  nature;            ///< 0x20 — Nature (0-24).
    uint8_t  gender;            ///< 0x22 bits[3:2] — 0=M, 1=F, 2=Unknown.
    uint8_t  form;              ///< 0x24 — Form index.
    uint16_t moves[4];          ///< 0x72/74/76/78 — Move IDs.
    uint32_t iv32;              ///< 0x8C — Packed IVs (5 bits each + flags).
    char     nickname[26];      ///< 0x58 — UTF-16LE nickname trash (26 bytes).
    char     ot_name[26];       ///< 0xF8 — UTF-16LE OT name trash (26 bytes).
    uint8_t  stat_level;        ///< 0x148 — Current level (party slot).
    bool     is_egg;            ///< IV32 bit 30.
    bool     is_nicknamed;      ///< IV32 bit 31.
    bool     is_shiny;          ///< (PID>>16 ^ PID&0xFFFF ^ TID16 ^ SID16) < 16.

    // Convenience: decoded IV values extracted from iv32.
    uint8_t  iv_hp;   ///< bits [4:0]
    uint8_t  iv_atk;  ///< bits [9:5]
    uint8_t  iv_def;  ///< bits [14:10]
    uint8_t  iv_spe;  ///< bits [19:15]
    uint8_t  iv_spa;  ///< bits [24:20]
    uint8_t  iv_spd;  ///< bits [29:25]

    // Decoded display name (nickname if nicknamed, else species fallback).
    // Filled by DecodePK8 as a UTF-8 string for SDL text rendering.
    char     display_name[32];
    char     ot_display[24];    ///< OT name decoded to UTF-8.

    // Constructor: zero-initialise all POD fields.
    PK8() : species(0), held_item(0), id32(0), exp(0), ability(0), pid(0),
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

// ── SwShSave ─────────────────────────────────────────────────────────────────

/// Top-level decoded save data (v3.5 — trainer + party only).
struct SwShSave {
    char     trainer_name[24];   ///< OT name decoded to UTF-8.
    uint32_t trainer_id;         ///< TID16 (lower 16 bits of ID32).
    uint8_t  gender;             ///< 0=M, 1=F.
    uint32_t money;              ///< Money (from MyStatus block).
    PK8      party[6];           ///< Current party slots.
    int      party_count;        ///< Number of occupied party slots (0-6).

    SwShSave() : trainer_id(0), gender(0), money(0), party_count(0)
    {
        memset(trainer_name, 0, sizeof(trainer_name));
        for (int i = 0; i < 6; ++i) party[i] = PK8();
    }
};

// ── QdSwShSaveParser ─────────────────────────────────────────────────────────

/// Reads a SwSh save file, decrypts it, parses SCBlocks, and decodes party Pokémon.
class QdSwShSaveParser {
public:
    /// Read a save file from path, decrypt, parse, and return SwShSave.
    /// On failure, out_save is unmodified and the SwishResult error is returned.
    static SwishResult ParseFile(const std::string &path, SwShSave &out_save);

    /// Find the first SCBlock with the given key in the decrypted buffer.
    /// Returns nullptr if not found.  *out_block_size is set to the data payload
    /// size of the block (excluding the per-block XorShift header bytes).
    static const uint8_t* FindBlock(const uint8_t *buf, size_t size, uint32_t key,
                                    size_t *out_block_size);

    /// Decrypt and un-shuffle a single PK8 party entry (0x158 bytes).
    /// Source: PokeCrypto.cs Decrypt8 + Shuffle8.
    ///   seed = EncryptionConstant (u32 at offset 0)
    ///   sv   = (seed >> 13) & 31
    ///   CryptArray(data[8..0x148], seed) then Shuffle8(data[8..0x148], sv)
    static void DecryptPK8(const uint8_t *encrypted, uint8_t *out_raw);

    /// Decode a raw (decrypted+unshuffled) PK8 buffer into a PK8 struct.
    static void DecodePK8(const uint8_t *raw, PK8 &out);

private:
    /// Read up to max_size bytes from path into a heap buffer.
    /// Returns number of bytes read, or 0 on error.
    static size_t ReadFile(const std::string &path,
                           uint8_t **out_buf, size_t max_size);

    /// Decode a UTF-16LE string (trash bytes) to a null-terminated UTF-8 buffer.
    /// src_bytes: byte length of source (e.g. 26).  dst: output buffer.
    /// dst_chars: capacity of dst in bytes.
    static void DecodeUtf16LeToUtf8(const uint8_t *src, size_t src_bytes,
                                    char *dst, size_t dst_chars);
};

} // namespace ul::menu::qdesktop
