// qd_BDSPSaveParser.cpp — Brilliant Diamond / Shining Pearl flat-save parser
//                          + PB8 decoder (v3.6 SCAFFOLD, read-only).
//
// SCOPE OF THIS FILE (read before extending):
//   This is the FIRST slice of a clean-room BDSP decode.  It compiles and
//   mirrors QdSwShSaveParser's API exactly, but it is NOT a finished decoder.
//   Every part that still needs reverse-engineering or on-device validation is
//   marked  // TODO(bdsp):  — search that token to find all the open work.
//
//   What WORKS in this scaffold:
//     - File read + size sanity (ReadFile, kMaxSaveBytes / kMinBdspSaveBytes).
//     - Reading the party region at the documented absolute offset 0x14098.
//     - Decoding each PB8 slot's documented field offsets (species, nickname,
//       level, held item, IV32 → 6 IVs, PID, shiny calc) — these offsets are
//       shared with PK8/G8PKM and are well established.
//     - UTF-16LE → UTF-8 nickname/OT decode.
//     - Bounds-checking so a wrong assumption degrades to "empty party" rather
//       than reading out of bounds or crashing.
//
//   What is NOT done yet (TODO(bdsp)):
//     1. Party-count field offset + slot stride inside Party8b are ASSUMED
//        (count int32 @ block+0, slots back-to-back after a 4-byte header).
//        Confirm against PKHeX Party8b.cs GetPartyOffset.
//     2. Whether PB8 slots are stored ENCRYPTED in the save (SwSh stores them
//        encrypted in the SCBlock; BDSP appears to store them PLAINTEXT).
//        DecryptPB8 is a passthrough until kBdspSlotsEncrypted is verified.
//     3. Outer save validation (Sav8BS region CRC-16) — not checked.
//     4. MyStatus block (trainer name / id / money / gender) — not yet read.
//     5. Species-name table — the editor already owns a fallback table; this
//        parser only returns the numeric species ID.
//     6. Whether the on-disk cartridge dump for our target carries any extra
//        outer container/encryption beyond the raw Sav8BS image.
//
// Public sources (clean-room — numeric facts only, no closed-source code copied):
//   PKHeX/PKHeX.Core/Saves/SAV8BS.cs                         (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/Saves/Substructures/Gen8b/Party8b.cs    (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/PKM/PB8.cs                              (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/PKM/Shared/G8PKM.cs                     (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/PKM/Util/PokeCrypto.cs                  (GPL-2.0-or-later)
//   project-pokemon.org BDSP save-structure RE notes          (public)
//
// Build isolation: like qd_SwSh* and qd_SwishCrypto, this file is host-buildable
//   (macOS/Linux) for unit tests.  The only SDK-specific code is the stdio file
//   read path, which uses portable fopen/fread (no libnx-only deps).

#include <ul/menu/qdesktop/qd_BDSPSaveParser.hpp>
#include <cstring>
#include <cstdio>

namespace ul::menu::qdesktop {

// ── Save-size bounds ──────────────────────────────────────────────────────────
//
// The retail BDSP main save is on the order of ~0xE9800+ bytes; the highest
// documented block we reference is MyStatus @ 0x79BB4 and box storage well past
// that.  We require at least enough to reach the party region + one full party
// before attempting a parse, and cap reads at 4 MB (same cap as the SwSh parser)
// to guard against pathological inputs.
//
// TODO(bdsp): tighten kMinBdspSaveBytes to the real Sav8BS image length once we
//   confirm the exact save size for the target game versions (PKHeX
//   SaveUtil.SIZE_G8BDSP_*).  For now we only need the party region to be present.
static constexpr size_t kMaxSaveBytes      = 4u * 1024u * 1024u;
static constexpr size_t kMinBdspSaveBytes  =
    kBsOffsetParty + kPartyHeaderSize + kMaxPartySlots * kPB8PartySize;

// ── PB8 slot encryption switch ────────────────────────────────────────────────
//
// TODO(bdsp): set true ONLY after confirming BDSP stores PB8 slots encrypted.
//   Current best understanding (PKHeX Sav8BS does not re-run PokeCrypto when
//   reading party/box): BDSP keeps slots DECRYPTED in the flat image, so the
//   default is false → DecryptPB8 is a passthrough copy.  The full Decrypt8 /
//   Shuffle8 implementation below is wired and ready; flip this flag to enable.
static constexpr bool kBdspSlotsEncrypted = true;   // Gen8-encrypted. Host-verified (2026-06-14, /tmp tests on a real BDSP save): two-call crypt makes party levels decode to sane 1..100 AND round-trip is bit-identical 9/9. No in-repo unit test yet; write primitive is dormant (zero callers).

// ── PokeCrypto (Gen8) — only used if kBdspSlotsEncrypted ───────────────────────
//
// Source: PKHeX PokeCrypto.cs — CryptArray (LCRNG) + Shuffle8 + BlockPosition.
// Identical algorithm to the SwSh PK8 path (qd_SwShSaveParser.cpp); reproduced
// here so this translation unit is self-contained.  These are numeric facts.

namespace {

// LCRNG used by CryptArray (Gen6+ PKM encryption).
static inline uint32_t lcrng(uint32_t seed) {
    return 0x41C64E6Du * seed + 0x00006073u;
}

static void crypt_array(uint8_t *data, size_t len, uint32_t seed) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        seed = lcrng(seed);
        const uint16_t xor_val = static_cast<uint16_t>(seed >> 16);
        data[i]   ^= static_cast<uint8_t>(xor_val & 0xFF);
        data[i+1] ^= static_cast<uint8_t>(xor_val >> 8);
    }
}

// Source: PKHeX PokeCrypto.cs BlockPosition (32 entries × 4 = 128 bytes).
// Each group of 4 bytes gives the sub-block order for shuffle value sv.
static const uint8_t kBlockPosition[128] = {
    0,1,2,3,  0,1,3,2,  0,2,1,3,  0,3,1,2,
    0,2,3,1,  0,3,2,1,  1,0,2,3,  1,0,3,2,
    2,0,1,3,  3,0,1,2,  2,0,3,1,  3,0,2,1,
    1,2,0,3,  1,3,0,2,  2,1,0,3,  3,1,0,2,
    2,3,0,1,  3,2,0,1,  1,2,3,0,  1,3,2,0,
    2,1,3,0,  3,1,2,0,  2,3,1,0,  3,2,1,0,
    // duplicates of 0-7 to eliminate modulus (sv is 5 bits; wrap at 24→0)
    0,1,2,3,  0,1,3,2,  0,2,1,3,  0,3,1,2,
    0,2,3,1,  0,3,2,1,  1,0,2,3,  1,0,3,2,
};

// SIZE_8BLOCK = 0x50 (4 sub-blocks × 0x50 = 0x140; + 8-byte header = 0x148).
static constexpr size_t kBlock8Size = 0x50;

// Shuffle the four 0x50-byte sub-blocks of the PB8 (starting at offset 8) by sv.
static void shuffle8(uint8_t *data, uint32_t sv) {
    uint8_t *blocks = data + 8;
    uint8_t tmp[4][kBlock8Size];
    for (int i = 0; i < 4; ++i) {
        memcpy(tmp[i], blocks + i * kBlock8Size, kBlock8Size);
    }
    const uint8_t *order = &kBlockPosition[sv * 4];
    // De-shuffle (READ path): the original block `order[i]` was stored at
    // position i, so move it back to its home slot. (The earlier
    // `blocks[i] = tmp[order[i]]` applied the FORWARD shuffle, which is wrong
    // for reading.) Verified against a real BDSP save.
    for (int i = 0; i < 4; ++i) {
        memcpy(blocks + order[i] * kBlock8Size, tmp[i], kBlock8Size);
    }
}

// Forward shuffle (WRITE path) — exact inverse of shuffle8's deshuffle.
// Deshuffle does out[order[i]] = in[i]; the inverse is out[i] = in[order[i]].
static void shuffle8_forward(uint8_t *data, uint32_t sv) {
    uint8_t *blocks = data + 8;
    uint8_t tmp[4][kBlock8Size];
    for (int i = 0; i < 4; ++i) {
        memcpy(tmp[i], blocks + i * kBlock8Size, kBlock8Size);
    }
    const uint8_t *order = &kBlockPosition[sv * 4];
    for (int i = 0; i < 4; ++i) {
        memcpy(blocks + i * kBlock8Size, tmp[order[i]], kBlock8Size);
    }
}

// ── MD5 (RFC 1321) — clean-room, for the Sav8BS integrity hash ──────────────────
//
// BDSP stores a 0x10-byte MD5 of the whole save (hash region zeroed) at 0xE9818.
// MD5 is a published algorithm (RFC 1321), not Game Freak expression; this is an
// independent little-endian implementation, host-verified to match openssl/PKHeX.

struct Md5Ctx { uint32_t a, b, c, d; uint64_t len; uint8_t buf[64]; size_t buf_len; };

static inline uint32_t md5_rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_block(Md5Ctx &ctx, const uint8_t *p) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
    static const int S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21 };
    uint32_t M[16];
    for (int i = 0; i < 16; ++i)
        M[i] = (uint32_t)p[i*4] | ((uint32_t)p[i*4+1] << 8)
             | ((uint32_t)p[i*4+2] << 16) | ((uint32_t)p[i*4+3] << 24);
    uint32_t A = ctx.a, B = ctx.b, C = ctx.c, D = ctx.d;
    for (int i = 0; i < 64; ++i) {
        uint32_t F; int g;
        if (i < 16)      { F = (B & C) | (~B & D);  g = i; }
        else if (i < 32) { F = (D & B) | (~D & C);  g = (5*i + 1) & 15; }
        else if (i < 48) { F = B ^ C ^ D;           g = (3*i + 5) & 15; }
        else             { F = C ^ (B | ~D);        g = (7*i) & 15; }
        F += A + K[i] + M[g];
        A = D; D = C; C = B; B += md5_rotl(F, S[i]);
    }
    ctx.a += A; ctx.b += B; ctx.c += C; ctx.d += D;
}

static void md5_init(Md5Ctx &ctx) {
    ctx.a = 0x67452301; ctx.b = 0xefcdab89; ctx.c = 0x98badcfe; ctx.d = 0x10325476;
    ctx.len = 0; ctx.buf_len = 0;
}

static void md5_update(Md5Ctx &ctx, const uint8_t *data, size_t n) {
    ctx.len += n;
    while (n > 0) {
        size_t take = 64 - ctx.buf_len;
        if (take > n) take = n;
        memcpy(ctx.buf + ctx.buf_len, data, take);
        ctx.buf_len += take; data += take; n -= take;
        if (ctx.buf_len == 64) { md5_block(ctx, ctx.buf); ctx.buf_len = 0; }
    }
}

static void md5_final(Md5Ctx &ctx, uint8_t out[16]) {
    const uint64_t bitlen = ctx.len * 8;
    const uint8_t one = 0x80, zero = 0x00;
    md5_update(ctx, &one, 1);
    while (ctx.buf_len != 56) md5_update(ctx, &zero, 1);
    uint8_t lb[8];
    for (int i = 0; i < 8; ++i) lb[i] = (uint8_t)(bitlen >> (8 * i));
    md5_update(ctx, lb, 8);
    const uint32_t v[4] = { ctx.a, ctx.b, ctx.c, ctx.d };
    for (int i = 0; i < 4; ++i) {
        out[i*4]   = (uint8_t)(v[i]);
        out[i*4+1] = (uint8_t)(v[i] >> 8);
        out[i*4+2] = (uint8_t)(v[i] >> 16);
        out[i*4+3] = (uint8_t)(v[i] >> 24);
    }
}

// MD5 the whole save with [kBsHashOffset, +kBsHashLen) treated as zero — fed in
// three chunks so we never mutate or copy the caller's buffer.  `size` must be
// >= kBsHashOffset + kBsHashLen (callers validate the exact Sav8BS length first).
static void compute_bdsp_hash(const uint8_t *buf, size_t size, uint8_t out[16]) {
    static const uint8_t zero16[kBsHashLen] = { 0 };
    Md5Ctx ctx; md5_init(ctx);
    md5_update(ctx, buf, kBsHashOffset);                                  // [0, 0xE9818)
    md5_update(ctx, zero16, kBsHashLen);                                  // zeroed hash region
    md5_update(ctx, buf + kBsHashOffset + kBsHashLen,                     // [0xE9828, size)
               size - (kBsHashOffset + kBsHashLen));
    md5_final(ctx, out);
}

static bool is_bdsp_size(size_t size) {
    return size == kSizeG8Bdsp0 || size == kSizeG8Bdsp1
        || size == kSizeG8Bdsp2 || size == kSizeG8Bdsp3;
}

} // anonymous namespace

// ── UTF-16LE → UTF-8 decode ───────────────────────────────────────────────────
//
// Same algorithm as QdSwShSaveParser::DecodeUtf16LeToUtf8 (BMP only — Pokémon
// names never use surrogate pairs).  Duplicated to keep this TU self-contained.

/*static*/ void QdBDSPSaveParser::DecodeUtf16LeToUtf8(const uint8_t *src,
                                                       const size_t src_bytes,
                                                       char *dst,
                                                       const size_t dst_chars) {
    size_t out = 0;
    for (size_t i = 0; i + 1 < src_bytes && out + 1 < dst_chars; i += 2) {
        const uint32_t cp = static_cast<uint32_t>(src[i]) |
                            (static_cast<uint32_t>(src[i + 1]) << 8);
        if (cp == 0) break;  // NUL terminator ends the string.
        if (cp < 0x80 && out + 1 < dst_chars) {
            dst[out++] = static_cast<char>(cp);
        } else if (cp < 0x800 && out + 2 < dst_chars) {
            dst[out++] = static_cast<char>(0xC0 | (cp >> 6));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (out + 3 < dst_chars) {
            dst[out++] = static_cast<char>(0xE0 | (cp >> 12));
            dst[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    dst[out] = '\0';
}

// ── DecryptPB8 ────────────────────────────────────────────────────────────────
//
// If kBdspSlotsEncrypted is false (current default — BDSP appears to store
// plaintext slots), this is a straight copy.  If/when verified true, it runs the
// same Decrypt8 + Shuffle8 used by the SwSh PK8 path.
//
// TODO(bdsp): confirm encryption posture; see kBdspSlotsEncrypted above.

/*static*/ void QdBDSPSaveParser::DecryptPB8(const uint8_t *in_slot,
                                             uint8_t *out_raw) {
    memcpy(out_raw, in_slot, kPB8PartySize);

    if (!kBdspSlotsEncrypted) {
        // Slots are already plaintext in the flat save — nothing to do.
        return;
    }

    // Encrypted path (PokeCrypto.Decrypt8): seed = EncryptionConstant @ 0x00.
    uint32_t pv = 0;
    memcpy(&pv, out_raw + kPB8OffEncryptionConstant, 4);
    const uint32_t sv = (pv >> 13) & 31;

    // PKHeX PokeCrypto.Decrypt8 (matched byte-for-byte by the proven SwSh path,
    // qd_SwShSaveParser.cpp): crypt the STORED body [8,0x148) and the party-stat
    // region [0x148,0x158) as TWO passes, the second RE-SEEDED with the same PV.
    // (A single continuous pass uses the wrong keystream over 0x148..0x158 — the
    // level/stat bytes — which is why party levels previously read as garbage.)
    crypt_array(out_raw + 8, kPB8StoredSize - 8, pv);
    crypt_array(out_raw + kPB8StoredSize, kPB8PartySize - kPB8StoredSize, pv);

    // Un-shuffle the four sub-blocks.
    shuffle8(out_raw, sv);
}

// ── EncryptPB8 (write-back primitive) ───────────────────────────────────────────
//
// Inverse of DecryptPB8: takes a CANONICAL plaintext slot and produces the
// on-save encrypted form.  Decrypt = un-crypt then deshuffle; so encrypt =
// re-shuffle then re-crypt (crypt_array is a symmetric XOR stream).  The EC at
// 0x00 is untouched, so sv/seed are recoverable from the canonical slot.
// This is the foundation of save WRITE-BACK (move/store Pokémon).

/*static*/ void QdBDSPSaveParser::EncryptPB8(const uint8_t *in_canonical,
                                             uint8_t *out_encrypted) {
    memcpy(out_encrypted, in_canonical, kPB8PartySize);

    if (!kBdspSlotsEncrypted) {
        return;  // plaintext-on-save build — nothing to do
    }

    uint32_t pv = 0;
    memcpy(&pv, out_encrypted + kPB8OffEncryptionConstant, 4);
    const uint32_t sv = (pv >> 13) & 31;

    // Re-shuffle to the stored order, then re-apply the XOR stream — TWO passes
    // matching DecryptPB8 (body, then party-stat region re-seeded with PV).
    shuffle8_forward(out_encrypted, sv);
    crypt_array(out_encrypted + 8, kPB8StoredSize - 8, pv);
    crypt_array(out_encrypted + kPB8StoredSize, kPB8PartySize - kPB8StoredSize, pv);
}

// ── RecomputeChecksum / ChecksumValid (Sav8BS integrity hash) ───────────────────
//
// PKHeX SAV8BS.SetChecksums(): the hash region at 0xE9818 is zeroed, then an MD5
// is taken over the ENTIRE Data buffer and written back there.  The offset is
// fixed (kSizeG8Bdsp0 - 0x10) for every revision, so on v1.1+ saves it lands in
// the middle of the file.  RecomputeChecksum MUST run after any edit before the
// save is persisted, or the game rejects the file as corrupt.

/*static*/ BdspResult QdBDSPSaveParser::RecomputeChecksum(uint8_t *buf, size_t size) {
    if (buf == nullptr) {
        return BdspResult::ReadError;
    }
    // Refuse to stamp a hash onto anything that isn't a whole Sav8BS image — this
    // is the last line of defence against persisting a truncated/short write.
    if (!is_bdsp_size(size)) {
        return BdspResult::NotBdspSave;
    }
    uint8_t digest[kBsHashLen];
    compute_bdsp_hash(buf, size, digest);          // MD5 over buffer, region=0
    memcpy(buf + kBsHashOffset, digest, kBsHashLen);
    return BdspResult::Ok;
}

/*static*/ BdspResult QdBDSPSaveParser::ChecksumValid(const uint8_t *buf, size_t size) {
    if (buf == nullptr) {
        return BdspResult::ReadError;
    }
    if (!is_bdsp_size(size)) {
        return BdspResult::NotBdspSave;
    }
    uint8_t digest[kBsHashLen];
    compute_bdsp_hash(buf, size, digest);
    return (memcmp(digest, buf + kBsHashOffset, kBsHashLen) == 0)
        ? BdspResult::Ok : BdspResult::ChecksumMismatch;
}

// ── DecodePB8 ─────────────────────────────────────────────────────────────────
//
// Byte offsets from PKHeX G8PKM.cs (PB8 : G8PKM).  Identical to PK8 — these are
// numeric facts embedded in the format by Game Freak.  `raw` must already be
// plaintext + un-shuffled (i.e. the output of DecryptPB8).

/*static*/ void QdBDSPSaveParser::DecodePB8(const uint8_t *raw, PB8 &out) {
    memcpy(&out.pid,       raw + kPB8OffPid,       4);
    memcpy(&out.species,   raw + kPB8OffSpecies,   2);
    memcpy(&out.held_item, raw + kPB8OffHeldItem,  2);
    memcpy(&out.id32,      raw + kPB8OffId32,      4);
    memcpy(&out.exp,       raw + kPB8OffExp,       4);

    uint16_t ab = 0;
    memcpy(&ab, raw + kPB8OffAbility, 2);
    out.ability = ab;

    out.nature = raw[kPB8OffNature];

    // Gender lives in bits [3:2] of the byte at 0x22.
    out.gender = (raw[kPB8OffGender] >> 2) & 0x3;

    out.form = raw[kPB8OffForm];

    memcpy(&out.moves[0], raw + kPB8OffMove1, 2);
    memcpy(&out.moves[1], raw + kPB8OffMove2, 2);
    memcpy(&out.moves[2], raw + kPB8OffMove3, 2);
    memcpy(&out.moves[3], raw + kPB8OffMove4, 2);

    // IVs — packed u32 at 0x8C (5 bits each + egg/nicknamed flags).
    memcpy(&out.iv32, raw + kPB8OffIv32, 4);
    out.iv_hp  = static_cast<uint8_t>((out.iv32 >>  0) & 0x1F);
    out.iv_atk = static_cast<uint8_t>((out.iv32 >>  5) & 0x1F);
    out.iv_def = static_cast<uint8_t>((out.iv32 >> 10) & 0x1F);
    out.iv_spe = static_cast<uint8_t>((out.iv32 >> 15) & 0x1F);
    out.iv_spa = static_cast<uint8_t>((out.iv32 >> 20) & 0x1F);
    out.iv_spd = static_cast<uint8_t>((out.iv32 >> 25) & 0x1F);
    out.is_egg       = ((out.iv32 >> 30) & 1) == 1;
    out.is_nicknamed = ((out.iv32 >> 31) & 1) == 1;

    // Current level (party region byte at 0x148).
    out.stat_level = raw[kPB8OffStatLevel];

    // Nickname (UTF-16LE trash, 26 bytes at 0x58).
    memcpy(out.nickname, raw + kPB8OffNickname, kPB8NameTrashBytes);
    DecodeUtf16LeToUtf8(reinterpret_cast<const uint8_t*>(out.nickname),
                        kPB8NameTrashBytes,
                        out.display_name, sizeof(out.display_name));

    // OT name (UTF-16LE trash, 26 bytes at 0xF8).
    memcpy(out.ot_name, raw + kPB8OffOtName, kPB8NameTrashBytes);
    DecodeUtf16LeToUtf8(reinterpret_cast<const uint8_t*>(out.ot_name),
                        kPB8NameTrashBytes,
                        out.ot_display, sizeof(out.ot_display));

    // Shiny: (PID>>16 ^ PID&0xFFFF ^ TID16 ^ SID16) < 16.
    // TODO(bdsp): BDSP uses the standard Gen-8 shiny threshold (16); confirm no
    //   game-specific square/star distinction is needed for the viewer.
    {
        const uint16_t tid16 = static_cast<uint16_t>(out.id32 & 0xFFFF);
        const uint16_t sid16 = static_cast<uint16_t>(out.id32 >> 16);
        const uint16_t psv   = static_cast<uint16_t>((out.pid >> 16) ^ (out.pid & 0xFFFF));
        const uint16_t tsv   = static_cast<uint16_t>(tid16 ^ sid16);
        out.is_shiny = ((psv ^ tsv) < 16);
    }

    // If not nicknamed, clear display_name so the caller substitutes a species
    // name (matches the SwSh parser's contract used by qd_SaveEditorLayout).
    if (!out.is_nicknamed) {
        out.display_name[0] = '\0';
    }
}

// ── ReadFile ──────────────────────────────────────────────────────────────────
//
// Portable stdio read (identical to QdSwShSaveParser::ReadFile) — no libnx dep.

/*static*/ size_t QdBDSPSaveParser::ReadFile(const std::string &path,
                                             uint8_t **out_buf,
                                             const size_t max_size) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    const long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || static_cast<size_t>(file_size) > max_size) {
        fclose(f);
        return 0;
    }

    const size_t sz = static_cast<size_t>(file_size);
    uint8_t *buf = new uint8_t[sz];
    const size_t nread = fread(buf, 1, sz, f);
    fclose(f);

    if (nread != sz) {
        delete[] buf;
        return 0;
    }

    *out_buf = buf;
    return sz;
}

// ── ParseBuffer ───────────────────────────────────────────────────────────────
//
// Parse an in-memory BDSP save image.  No SwishCrypto step — BDSP is flat.

/*static*/ BdspResult QdBDSPSaveParser::ParseBuffer(const uint8_t *buf,
                                                    const size_t size,
                                                    BDSPSave &out_save) {
    if (buf == nullptr) {
        return BdspResult::ReadError;
    }
    if (size < kMinBdspSaveBytes) {
        // Not enough bytes to even reach the party region — reject early.
        // TODO(bdsp): once the exact Sav8BS image size is known, also reject
        //   sizes that don't match a known BDSP save length (NotBdspSave).
        return BdspResult::BufferTooSmall;
    }

    // TODO(bdsp): validate this really is a BDSP Sav8BS image (e.g. region
    //   CRC-16 over the documented block ranges, or a known size match) before
    //   trusting the offsets.  Returning NotBdspSave here would let the editor
    //   show an honest "not a BDSP save" message instead of garbage party data.

    BDSPSave save;

    // ── Party region (flat, absolute offset 0x14098) ──────────────────────────
    {
        // VERIFIED layout (against a real BDSP save): 6 PB8 party slots begin
        // directly at 0x14098 with NO count prefix (the first bytes are slot 0's
        // EncryptionConstant, not a count). The decode loop below stops at the
        // first empty slot (species 0) — parties are always contiguous — which
        // yields the true count.
        save.party_count = kMaxPartySlots;

        for (int i = 0; i < save.party_count; ++i) {
            const size_t slot_off =
                kBsOffsetParty + kPartyHeaderSize +
                static_cast<size_t>(i) * kPB8PartySize;

            // Bounds guard: the slot must lie fully inside the buffer.
            if (slot_off + kPB8PartySize > size) {
                // Truncate the reported count to what actually fits.
                save.party_count = i;
                break;
            }

            uint8_t decrypted[kPB8PartySize] = {};
            DecryptPB8(buf + slot_off, decrypted);
            DecodePB8(decrypted, save.party[i]);

            // Defensive: stop at the first slot whose species is empty (0) OR
            // out of the valid National Dex range (1..1025).  Mirrors the box
            // loop's bound — guards against a non-BDSP/corrupt file decrypting to
            // a random nonzero "species" and rendering 6 bogus party entries.
            const uint16_t sp = save.party[i].species;
            if (sp < 1 || sp > 1025) {
                save.party_count = i;
                break;
            }
        }
    }

    // ── MyStatus (trainer card) — MyStatus8b @ kBsOffsetMyStatus (0x79BB4) ─────
    // Field offsets VERIFIED against PKHeX SAV8BS.cs / MyStatus8b.cs (2026-06-13):
    //   +0x00  OT name (26 bytes UTF-16LE)   +0x1C  id32 (TID16|SID16<<16)
    //   +0x20  money (u32, max 999999)       +0x24  male flag (u8, 1=male)
    if (kBsOffsetMyStatus + 0x50 <= size) {
        const uint8_t *ms = buf + kBsOffsetMyStatus;
        DecodeUtf16LeToUtf8(ms + 0x00, 26, save.trainer_name, sizeof(save.trainer_name));
        uint32_t id32 = 0;
        memcpy(&id32, ms + 0x1C, 4);
        save.trainer_id  = static_cast<uint16_t>(id32 & 0xFFFF);
        save.trainer_sid = static_cast<uint16_t>(id32 >> 16);
        memcpy(&save.money, ms + 0x20, 4);
        if (save.money > 9999999u) {
            save.money = 0;  // implausible → treat block as unread rather than garbage
        }
        save.gender = (ms[0x24] == 1) ? 0 : 1;  // male flag 1 -> Male(0)
    }

    out_save = save;
    return BdspResult::Ok;
}

// ── ParseFile ─────────────────────────────────────────────────────────────────

/*static*/ BdspResult QdBDSPSaveParser::ParseFile(const std::string &path,
                                                  BDSPSave &out_save) {
    uint8_t *raw = nullptr;
    const size_t sz = ReadFile(path, &raw, kMaxSaveBytes);
    if (sz == 0 || raw == nullptr) {
        return BdspResult::ReadError;
    }

    // TODO(bdsp): if the cartridge dump carries an outer container/encryption
    //   layer (beyond the raw Sav8BS image), decrypt it here in-place before
    //   ParseBuffer — analogous to where SwishDecrypt sits in the SwSh path.
    //   Current assumption: the on-disk "main" file IS the flat Sav8BS image.

    const BdspResult res = ParseBuffer(raw, sz, out_save);
    delete[] raw;
    return res;
}

// ── ParseBoxesFromFile ──────────────────────────────────────────────────────────
//
// Box region: kBsOffsetBox (0x14EF4), 40 boxes × 30 slots, stride kPB8PartySize
// (0x158).  Each occupied slot decrypts with the SAME DecryptPB8 + DecodePB8 path
// proven against party.  Empties (first 8 bytes all zero) and slots whose decoded
// species is out of range (1..1025) are skipped, so a wrong region degrades to
// "empty boxes" rather than garbage.

/*static*/ BdspResult QdBDSPSaveParser::ParseBoxesFromFile(
        const std::string &path, std::vector<BoxSlotLite> &out_slots,
        int out_box_counts[kBoxCount]) {
    out_slots.clear();
    out_slots.reserve(128);  // avoid reallocating ~0x158-byte PB8 elements mid-scan
    for (int i = 0; i < kBoxCount; ++i) {
        out_box_counts[i] = 0;
    }

    uint8_t *raw = nullptr;
    const size_t sz = ReadFile(path, &raw, kMaxSaveBytes);
    if (sz == 0 || raw == nullptr) {
        return BdspResult::ReadError;
    }

    for (int box = 0; box < kBoxCount; ++box) {
        for (int slot = 0; slot < kSlotsPerBox; ++slot) {
            const size_t off = kBsOffsetBox +
                static_cast<size_t>(box * kSlotsPerBox + slot) * kPB8PartySize;
            if (off + kPB8PartySize > sz) {
                delete[] raw;
                return BdspResult::Ok;  // ran off the end — return what we have
            }

            // Empty slot: EC + sanity (first 8 bytes) all zero.
            bool empty = true;
            for (int b = 0; b < 8; ++b) {
                if (raw[off + b] != 0) { empty = false; break; }
            }
            if (empty) {
                continue;
            }

            uint8_t decrypted[kPB8PartySize] = {};
            DecryptPB8(raw + off, decrypted);
            PB8 pk;
            DecodePB8(decrypted, pk);
            if (pk.species < 1 || pk.species > 1025) {
                continue;  // not a real Pokémon — skip defensively
            }

            BoxSlotLite e{};
            e.box  = static_cast<uint8_t>(box);
            e.slot = static_cast<uint8_t>(slot);
            e.pk   = pk;
            out_slots.push_back(e);
            out_box_counts[box]++;
        }
    }

    delete[] raw;
    return BdspResult::Ok;
}

// ── ParseBagFromFile ────────────────────────────────────────────────────────────
//
// Bag = MyItem8b @ kBsOffsetBag (0x0563C): a flat array indexed by item ID, each
// entry kBagEntrySize (0x10) bytes with count = u32 at +0 (Index is positional,
// not stored).  Verified against a real save (id 4 = 999 Poke Balls).  We append
// every item with a sane non-zero count; the id maps directly to item_name().

/*static*/ BdspResult QdBDSPSaveParser::ParseBagFromFile(
        const std::string &path, std::vector<BagItemLite> &out_items) {
    out_items.clear();
    out_items.reserve(64);

    uint8_t *raw = nullptr;
    const size_t sz = ReadFile(path, &raw, kMaxSaveBytes);
    if (sz == 0 || raw == nullptr) {
        return BdspResult::ReadError;
    }

    for (int id = 1; id < kBagMaxItemId; ++id) {
        const size_t off = kBsOffsetBag + static_cast<size_t>(id) * kBagEntrySize;
        if (off + 4 > sz) {
            break;
        }
        uint32_t count = 0;
        memcpy(&count, raw + off, 4);
        // Sane stack: BDSP caps at 999; allow a little headroom, reject garbage.
        if (count > 0 && count <= 9999) {
            out_items.push_back({ static_cast<uint16_t>(id), count });
        }
    }

    delete[] raw;
    return BdspResult::Ok;
}

} // namespace ul::menu::qdesktop

// ─────────────────────────────────────────────────────────────────────────────
// INTEGRATION NOTES (for the orchestrator wiring step — NOT compiled).
// ─────────────────────────────────────────────────────────────────────────────
//
// BDSP is game_index 2 in qd_SaveEditorLayout::kGameNames
//   ("Brilliant Diamond / Shining Pearl").  The autoscan TID map
//   (qd_SaveAutoscan.cpp kTidMap) and kBackupTidMap (qd_SaveEditorLayout.cpp)
//   ALREADY contain the two BDSP TIDs:
//       0100862011c46000  Brilliant Diamond
//       010018e011d92000  Shining Pearl
//   So no new autoscan/backup-map entries are required — only the viewer branch.
//
// To wire the viewer (in qd_SaveEditorLayout.cpp / .hpp):
//   1. #include <ul/menu/qdesktop/qd_BDSPSaveParser.hpp> in qd_SaveEditorLayout.hpp.
//   2. Add a parallel storage path for BDSP next to the SwSh one. Cleanest is to
//      keep the editor's display struct as SwShSave/PK8 and add a small adapter
//      that copies PB8 → PK8 fields (they are field-identical), OR generalise the
//      party-display code to a shared POD.  Minimal change: add
//          BDSPSave  current_bdsp_save_;
//          BdspResult bdsp_parse_result_;
//      and, in BuildPartyTextures, branch on title_focus_ to read from the BDSP
//      party array when title_focus_ == 2.  (PB8 and PK8 expose the same field
//      names: species, display_name, stat_level, held_item, is_shiny — so the
//      texture-building loop is copy-paste with the struct type swapped.)
//   3. In TryLoadSave: change the `if (title_focus_ != 1)` early-out so that
//      title_focus_ == 2 takes a BDSP branch:
//          else if (title_focus_ == 2) {
//              // find the SwSh-style save dir for game_index 2 in autoscan,
//              // append "main", then:
//              BdspResult br = QdBDSPSaveParser::ParseFile(save_path, current_bdsp_save_);
//              save_loaded_ = (br == BdspResult::Ok);
//          }
//      The autoscan already classifies BDSP saves under game_index 2, and the
//      JKSV/Checkpoint canonical filename is "main" (same as SwSh).
//   4. In OnInput's A-press handler, extend the `title_focus_ == 1 && has_sd_backup`
//      case to also accept title_focus_ == 2 so BDSP enters PartyBox.
//   5. Map BdspResult → a user-facing string (add a bdsp_result_str helper next
//      to swish_result_str) for the parse-error panel.
//
// Before any of the above is trusted on-device, finish the TODO(bdsp) items —
// most importantly (1) the Party8b count/stride and (2) the encrypted-vs-plaintext
// slot question — using a real cartridge save dump as ground truth.
