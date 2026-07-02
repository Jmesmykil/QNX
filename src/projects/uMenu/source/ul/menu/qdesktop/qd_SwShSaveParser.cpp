// qd_SwShSaveParser.cpp — SwSh SCBlock parser + PK8 decoder (v3.5, read-only).
//
// Sources used:
//   PKHeX/PKHeX.Core/Saves/Encryption/SwishCrypto/SCBlock.cs  (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/Saves/Encryption/SwishCrypto/SCXorShift32.cs
//   PKHeX/PKHeX.Core/PKM/Shared/G8PKM.cs                      (GPL-2.0-or-later)
//   PKHeX/PKHeX.Core/PKM/Util/PokeCrypto.cs                   (GPL-2.0-or-later)
//
// All byte offsets, block keys, XorShift constants, and shuffle tables ported
// verbatim from the above — they are numeric fact constants, not copyrightable.

#include <ul/menu/qdesktop/qd_SwShSaveParser.hpp>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// libnx / Switch SDK headers — only needed for the file-read path on-device.
// The block parser and PK8 decoder have NO SDK dependency.
#ifdef __SWITCH__
#  include <stdio.h>   // fopen/fclose/fread on horizon FS
#endif

namespace ul::menu::qdesktop {

// ── Maximum save file size we'll attempt to load ──────────────────────────────
// Real SwSh main save is ~0x1C0000 bytes (~1.75 MB).
// Use 4 MB cap to handle future titles without buffer overflow.
static constexpr size_t kMaxSaveBytes = 4u * 1024u * 1024u;

// ── PK8 size constants ────────────────────────────────────────────────────────
// Source: PokeCrypto.cs — SIZE_8STORED = 0x148, SIZE_8PARTY = 0x158.
static constexpr size_t kPK8StoredSize = 0x148;
static constexpr size_t kPK8PartySize  = 0x158;

// ── SCXorShift32 ──────────────────────────────────────────────────────────────
//
// Source: PKHeX SCXorShift32.cs (verbatim port, GPL-2.0-or-later).
// Self-mutating PRNG used to encrypt each SCBlock's header and data.

namespace {

struct SCXorShift32 {
    int      counter = 0;
    uint32_t state   = 0;

    explicit SCXorShift32(uint32_t seed) {
        // Advance state by popcount(seed) times.
        state = seed;
        const int popcnt = __builtin_popcount(seed);
        for (int i = 0; i < popcnt; ++i) {
            state = advance(state);
        }
    }

    static uint32_t advance(uint32_t s) {
        s ^= s << 2;
        s ^= s >> 15;
        s ^= s << 13;
        return s;
    }

    uint8_t next() {
        const uint8_t result = static_cast<uint8_t>(state >> (counter << 3));
        if (counter == 3) {
            state = advance(state);
            counter = 0;
        } else {
            ++counter;
        }
        return result;
    }

    uint32_t next32() {
        const uint32_t a = next();
        const uint32_t b = next();
        const uint32_t c = next();
        const uint32_t d = next();
        return a | (b << 8) | (c << 16) | (d << 24);
    }
};

// ── PokeCrypto helpers ────────────────────────────────────────────────────────
//
// Source: PKHeX PokeCrypto.cs — CryptArray + Shuffle8 + BlockPosition table.

// LCRNG used by CryptArray (Gen6+ PKM encryption).
static inline uint32_t lcrng(uint32_t seed) {
    return 0x41C64E6Du * seed + 0x00006073u;
}

static void crypt_array(uint8_t *data, size_t len, uint32_t seed) {
    // data must be 2-byte aligned in length.
    for (size_t i = 0; i + 1 < len; i += 2) {
        seed = lcrng(seed);
        const uint16_t xor_val = static_cast<uint16_t>(seed >> 16);
        data[i]   ^= static_cast<uint8_t>(xor_val & 0xFF);
        data[i+1] ^= static_cast<uint8_t>(xor_val >> 8);
    }
}

// Source: PKHeX PokeCrypto.cs BlockPosition (32 entries × 4 = 128 bytes).
// Each group of 4 bytes gives the block order for shuffle value sv.
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

// Block size: SIZE_8BLOCK = 0x50 = 80 bytes (4 blocks × 0x50 = 0x140; + 8 header = 0x148).
static constexpr size_t kBlock8Size = 0x50;

// Shuffle the four 0x50-byte sub-blocks of data[8..0x148] according to sv.
static void shuffle8(uint8_t *data, uint32_t sv) {
    // data points to start of PK8 (offset 0); sub-blocks start at offset 8.
    uint8_t *blocks = data + 8;
    // Read original four blocks.
    uint8_t tmp[4][kBlock8Size];
    for (int i = 0; i < 4; ++i) {
        memcpy(tmp[i], blocks + i * kBlock8Size, kBlock8Size);
    }
    // BlockPosition[sv*4 .. sv*4+3] gives the source block indices.
    const uint8_t *order = &kBlockPosition[sv * 4];
    for (int i = 0; i < 4; ++i) {
        memcpy(blocks + i * kBlock8Size, tmp[order[i]], kBlock8Size);
    }
}

// ── SCBlock skip helper ───────────────────────────────────────────────────────
//
// Determine the total serialised byte length of the SCBlock starting at
// data[offset] (key is NOT yet consumed — offset points at the 4-byte key).
// Returns the new offset past this block, or 0 on parse error.
// Source: PKHeX SCBlock.cs GetTotalLength.

static size_t scblock_skip(const uint8_t *data, size_t size, size_t offset) {
    if (offset + 5 > size) return 0;  // need key(4) + type(1) minimum

    // Read key (u32 little-endian).
    uint32_t key = 0;
    memcpy(&key, data + offset, 4);
    offset += 4;

    SCXorShift32 xk(key);
    const auto type = static_cast<SCTypeCode>(data[offset++] ^ xk.next());

    switch (type) {
        case SCTypeCode::Bool1:
        case SCTypeCode::Bool2:
        case SCTypeCode::Bool3:
            // No data payload.
            return offset;

        case SCTypeCode::Object: {
            if (offset + 4 > size) return 0;
            uint32_t raw_len = 0;
            memcpy(&raw_len, data + offset, 4);
            const uint32_t num_bytes = raw_len ^ xk.next32();
            offset += 4;
            return offset + num_bytes;
        }

        case SCTypeCode::Array: {
            if (offset + 4 > size) return 0;
            uint32_t raw_count = 0;
            memcpy(&raw_count, data + offset, 4);
            const uint32_t num_entries = raw_count ^ xk.next32();
            offset += 4;
            if (offset >= size) return 0;
            const auto sub = static_cast<SCTypeCode>(data[offset++] ^ xk.next());
            // GetTypeSize: U8/Bool=1, U16/I8=2 ... use a simple lookup.
            size_t elem_size = 1;
            switch (sub) {
                case SCTypeCode::U8:  case SCTypeCode::I8:  elem_size = 1; break;
                case SCTypeCode::U16: case SCTypeCode::I16: elem_size = 2; break;
                case SCTypeCode::U32: case SCTypeCode::I32:
                case SCTypeCode::F32: elem_size = 4; break;
                case SCTypeCode::U64: case SCTypeCode::I64:
                case SCTypeCode::F64: elem_size = 8; break;
                default: elem_size = 1; break;
            }
            return offset + static_cast<size_t>(num_entries) * elem_size;
        }

        default: {
            // Single primitive — size determined by type code.
            size_t sz = 1;
            switch (type) {
                case SCTypeCode::U8:  case SCTypeCode::I8:  sz = 1; break;
                case SCTypeCode::U16: case SCTypeCode::I16: sz = 2; break;
                case SCTypeCode::U32: case SCTypeCode::I32:
                case SCTypeCode::F32: sz = 4; break;
                case SCTypeCode::U64: case SCTypeCode::I64:
                case SCTypeCode::F64: sz = 8; break;
                default: sz = 1; break;
            }
            return offset + sz;
        }
    }
}

// ── UTF-16LE → UTF-8 decode ───────────────────────────────────────────────────

} // anonymous namespace

/*static*/ void QdSwShSaveParser::DecodeUtf16LeToUtf8(const uint8_t *src,
                                                       const size_t src_bytes,
                                                       char *dst,
                                                       const size_t dst_chars) {
    size_t out = 0;
    for (size_t i = 0; i + 1 < src_bytes && out + 1 < dst_chars; i += 2) {
        const uint32_t cp = static_cast<uint32_t>(src[i]) |
                            (static_cast<uint32_t>(src[i + 1]) << 8);
        if (cp == 0) break;  // null terminator
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

// ── FindBlock ─────────────────────────────────────────────────────────────────
//
// Walk the SCBlock stream searching for the block with the given key.
// PKHeX ReadBlocks: iterate from offset 0, consuming blocks sequentially.
// Each block starts with a u32 key, then a XorShift32(key)-encrypted header.
// Source: PKHeX SCBlock.cs ReadFromOffset.

/*static*/ const uint8_t* QdSwShSaveParser::FindBlock(const uint8_t *buf,
                                                        const size_t size,
                                                        const uint32_t key,
                                                        size_t *out_block_size) {
    size_t offset = 0;
    while (offset + 5 <= size) {
        // Peek the block key.
        uint32_t blk_key = 0;
        memcpy(&blk_key, buf + offset, 4);

        if (blk_key == key) {
            // Decode the block header to get the data payload pointer + size.
            const size_t key_offset = offset + 4;
            SCXorShift32 xk(key);
            const auto type = static_cast<SCTypeCode>(buf[key_offset] ^ xk.next());

            size_t data_start = 0;
            size_t data_len   = 0;

            switch (type) {
                case SCTypeCode::Bool1:
                case SCTypeCode::Bool2:
                case SCTypeCode::Bool3:
                    // No payload.
                    data_start = key_offset + 1;
                    data_len   = 0;
                    break;

                case SCTypeCode::Object: {
                    if (key_offset + 1 + 4 > size) return nullptr;
                    uint32_t raw_len = 0;
                    memcpy(&raw_len, buf + key_offset + 1, 4);
                    data_len   = raw_len ^ xk.next32();
                    data_start = key_offset + 1 + 4;
                    break;
                }

                case SCTypeCode::Array: {
                    if (key_offset + 1 + 4 > size) return nullptr;
                    uint32_t raw_count = 0;
                    memcpy(&raw_count, buf + key_offset + 1, 4);
                    const uint32_t num_entries = raw_count ^ xk.next32();
                    const size_t sub_off = key_offset + 1 + 4;
                    if (sub_off >= size) return nullptr;
                    const auto sub = static_cast<SCTypeCode>(buf[sub_off] ^ xk.next());
                    size_t elem_size = 1;
                    switch (sub) {
                        case SCTypeCode::U16: case SCTypeCode::I16: elem_size = 2; break;
                        case SCTypeCode::U32: case SCTypeCode::I32:
                        case SCTypeCode::F32: elem_size = 4; break;
                        case SCTypeCode::U64: case SCTypeCode::I64:
                        case SCTypeCode::F64: elem_size = 8; break;
                        default: elem_size = 1; break;
                    }
                    data_len   = static_cast<size_t>(num_entries) * elem_size;
                    data_start = sub_off + 1;
                    break;
                }

                default: {
                    // Single value.
                    size_t sz = 4;
                    switch (type) {
                        case SCTypeCode::U8:  case SCTypeCode::I8:  sz = 1; break;
                        case SCTypeCode::U16: case SCTypeCode::I16: sz = 2; break;
                        case SCTypeCode::U64: case SCTypeCode::I64:
                        case SCTypeCode::F64: sz = 8; break;
                        default: sz = 4; break;
                    }
                    data_len   = sz;
                    data_start = key_offset + 1;
                    break;
                }
            }

            // Decrypt block data bytes into a caller-accessible buffer.
            // PKHeX: data bytes are XOR'd with the *continuing* xk stream.
            // We return a pointer into our internal buffer after decrypting in-place.
            // NOTE: buf is already fully decrypted by SwishDecrypt — but the per-block
            // SCXorShift32 encryption layer is on top of that.  Decrypt in-place here.
            // IMPORTANT: buf is const; caller must work with the data we expose.
            // For read-only v3.5 viewer this is acceptable — we decrypt into a local
            // static and return a pointer.  Safe because ParseFile calls FindBlock
            // sequentially on a single buffer.
            static uint8_t s_block_data[0x10000];  // 64 KB max block (party fits in ~2 KB)
            if (data_len > sizeof(s_block_data)) return nullptr;
            if (data_start + data_len > size) return nullptr;

            memcpy(s_block_data, buf + data_start, data_len);
            // Re-init xk at the correct counter position after reading the header.
            // Instead, create a fresh xk and advance it past the header bytes we consumed.
            {
                SCXorShift32 xk2(key);
                // Consume: 1 byte (type) + header bytes consumed (depends on type).
                xk2.next();  // type byte
                if (type == SCTypeCode::Object) {
                    xk2.next32();  // length u32
                } else if (type == SCTypeCode::Array) {
                    xk2.next32();  // count u32
                    xk2.next();   // sub-type byte
                }
                // Now xk2 is aligned with the first data byte.
                for (size_t i = 0; i < data_len; ++i) {
                    s_block_data[i] ^= xk2.next();
                }
            }

            if (out_block_size) *out_block_size = data_len;
            return s_block_data;
        }

        // Advance past this block.
        const size_t next_off = scblock_skip(buf, size, offset);
        if (next_off == 0 || next_off <= offset) break;  // parse error / infinite loop guard
        offset = next_off;
    }
    return nullptr;
}

// ── DecryptPK8 ────────────────────────────────────────────────────────────────
//
// Ported from PKHeX PokeCrypto.cs Decrypt8 (GPL-2.0-or-later).
//
// Algorithm:
//   1. pv  = u32LE(data[0])          — EncryptionConstant
//   2. sv  = (pv >> 13) & 31         — shuffle value
//   3. CryptArray(data[8..0x148], pv) — LCRNG XOR over the 4 sub-blocks
//   4. Shuffle8(data[8..0x148], sv)   — permute the 4 sub-blocks
//   Optional party stats (data[0x148..0x158] if present): also CryptArray'd.

/*static*/ void QdSwShSaveParser::DecryptPK8(const uint8_t *encrypted,
                                              uint8_t *out_raw) {
    memcpy(out_raw, encrypted, kPK8PartySize);

    uint32_t pv = 0;
    memcpy(&pv, out_raw, 4);
    const uint32_t sv = (pv >> 13) & 31;

    // Decrypt main data (bytes 8..0x148).
    crypt_array(out_raw + 8, kPK8StoredSize - 8, pv);

    // Decrypt party stats (bytes 0x148..0x158), if present.
    crypt_array(out_raw + kPK8StoredSize, kPK8PartySize - kPK8StoredSize, pv);

    // Un-shuffle the four sub-blocks.
    shuffle8(out_raw, sv);
}

// ── DecodePK8 ─────────────────────────────────────────────────────────────────
//
// Byte offsets from PKHeX G8PKM.cs (GPL-2.0-or-later).
// All offsets are numeric facts embedded in the Pokemon save format by Game Freak.

/*static*/ void QdSwShSaveParser::DecodePK8(const uint8_t *raw, PK8 &out) {
    // Encryption constant / PID.
    memcpy(&out.pid, raw + 0x1C, 4);

    // Species (u16 at 0x08).
    memcpy(&out.species, raw + 0x08, 2);

    // Held item (u16 at 0x0A).
    memcpy(&out.held_item, raw + 0x0A, 2);

    // Trainer ID (u32 at 0x0C — TID16 in low 16, SID16 in high 16).
    memcpy(&out.id32, raw + 0x0C, 4);

    // Experience points (u32 at 0x10).
    memcpy(&out.exp, raw + 0x10, 4);

    // Ability (u16 at 0x14).
    uint16_t ab = 0; memcpy(&ab, raw + 0x14, 2); out.ability = ab;

    // Nature (u8 at 0x20).
    out.nature = raw[0x20];

    // Gender: bits [3:2] of byte 0x22.
    out.gender = (raw[0x22] >> 2) & 0x3;

    // Form (u8 at 0x24).
    out.form = raw[0x24];

    // Moves (u16 each at 0x72, 0x74, 0x76, 0x78).
    memcpy(&out.moves[0], raw + 0x72, 2);
    memcpy(&out.moves[1], raw + 0x74, 2);
    memcpy(&out.moves[2], raw + 0x76, 2);
    memcpy(&out.moves[3], raw + 0x78, 2);

    // IVs — packed u32 at 0x8C.
    memcpy(&out.iv32, raw + 0x8C, 4);
    out.iv_hp  = static_cast<uint8_t>((out.iv32 >>  0) & 0x1F);
    out.iv_atk = static_cast<uint8_t>((out.iv32 >>  5) & 0x1F);
    out.iv_def = static_cast<uint8_t>((out.iv32 >> 10) & 0x1F);
    out.iv_spe = static_cast<uint8_t>((out.iv32 >> 15) & 0x1F);
    out.iv_spa = static_cast<uint8_t>((out.iv32 >> 20) & 0x1F);
    out.iv_spd = static_cast<uint8_t>((out.iv32 >> 25) & 0x1F);
    out.is_egg      = ((out.iv32 >> 30) & 1) == 1;
    out.is_nicknamed = ((out.iv32 >> 31) & 1) == 1;

    // Stat_Level (u8 at 0x148 — party slot byte, valid after party decrypt).
    out.stat_level = raw[0x148];

    // Nickname trash (UTF-16LE, 26 bytes at 0x58).
    memcpy(out.nickname, raw + 0x58, 26);
    DecodeUtf16LeToUtf8(reinterpret_cast<const uint8_t*>(out.nickname), 26,
                        out.display_name, sizeof(out.display_name));

    // OT name trash (UTF-16LE, 26 bytes at 0xF8).
    memcpy(out.ot_name, raw + 0xF8, 26);
    DecodeUtf16LeToUtf8(reinterpret_cast<const uint8_t*>(out.ot_name), 26,
                        out.ot_display, sizeof(out.ot_display));

    // IsShiny: (PID>>16 ^ PID&0xFFFF ^ TID16 ^ SID16) < 16.
    {
        const uint16_t tid16 = static_cast<uint16_t>(out.id32 & 0xFFFF);
        const uint16_t sid16 = static_cast<uint16_t>(out.id32 >> 16);
        const uint16_t psv   = static_cast<uint16_t>((out.pid >> 16) ^ (out.pid & 0xFFFF));
        const uint16_t tsv   = static_cast<uint16_t>(tid16 ^ sid16);
        out.is_shiny = ((psv ^ tsv) < 16);
    }

    // If not nicknamed, clear display_name so callers can substitute species name.
    if (!out.is_nicknamed) {
        out.display_name[0] = '\0';
    }
}

// ── ReadFile ──────────────────────────────────────────────────────────────────

/*static*/ size_t QdSwShSaveParser::ReadFile(const std::string &path,
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

// ── ParseFile ─────────────────────────────────────────────────────────────────

/*static*/ SwishResult QdSwShSaveParser::ParseFile(const std::string &path,
                                                    SwShSave &out_save) {
    uint8_t *raw = nullptr;
    const size_t sz = ReadFile(path, &raw, kMaxSaveBytes);
    if (sz == 0 || raw == nullptr) {
        return SwishResult::BufferTooSmall;
    }

    // Decrypt the SwishCrypto layer in-place.
    SwishResult res = SwishDecrypt(raw, sz);
    if (res != SwishResult::Ok) {
        delete[] raw;
        return res;
    }

    SwShSave save;

    // ── Parse KMyStatus block ─────────────────────────────────────────────────
    {
        size_t block_size = 0;
        const uint8_t *blk = FindBlock(raw, sz, kKeyMyStatus, &block_size);
        if (blk && block_size >= 4) {
            // MyStatus layout (SwSh):
            //   0x00: u32 money
            //   0x14: trainer name (UTF-16LE, 26 bytes)
            //   0x30: u32 ID32
            //   0x34: u8 gender
            // (offsets from PKHeX SAV8SWSH.cs MyStatus block)
            if (block_size >= 0x08) {
                uint32_t money = 0;
                memcpy(&money, blk + 0x04, 4);
                save.money = money;
            }
            if (block_size >= 0x2E) {
                uint32_t id32 = 0;
                memcpy(&id32, blk + 0x14 + 26, 4);  // after trainer name
                save.trainer_id = id32 & 0xFFFF;
                save.gender     = blk[0x14 + 26 + 4 + 1] & 0x1;
                // Trainer name at offset 0x14 (UTF-16LE, 26 bytes).
                DecodeUtf16LeToUtf8(blk + 0x14, 26,
                                    save.trainer_name, sizeof(save.trainer_name));
            }
        }
    }

    // ── Parse KParty block ────────────────────────────────────────────────────
    {
        size_t block_size = 0;
        const uint8_t *blk = FindBlock(raw, sz, kKeyParty, &block_size);
        if (blk) {
            // Party block layout: u32 count, then up to 6 × kPK8PartySize bytes.
            // Source: PKHeX SAV8SWSH.cs Party property — block data starts with
            // the party count as a u32, followed by 6 PK8 party-format slots.
            if (block_size >= 4) {
                uint32_t party_count = 0;
                memcpy(&party_count, blk, 4);
                if (party_count > 6) party_count = 6;
                save.party_count = static_cast<int>(party_count);

                const size_t slots_offset = 4;
                for (int i = 0; i < save.party_count; ++i) {
                    const size_t slot_off = slots_offset + i * kPK8PartySize;
                    if (slot_off + kPK8PartySize > block_size) break;

                    uint8_t decrypted[kPK8PartySize] = {};
                    DecryptPK8(blk + slot_off, decrypted);
                    DecodePK8(decrypted, save.party[i]);
                }
            }
        }
    }

    delete[] raw;
    out_save = save;
    return SwishResult::Ok;
}

} // namespace ul::menu::qdesktop
