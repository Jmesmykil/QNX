// qd_SwishCrypto.cpp — SwSh-era SwishCrypto round-trip.
//
// Build isolation: this file has NO libnx include and NO Switch SDK dependency.
// It compiles cleanly on macOS/Linux for host-side unit tests as well as
// cross-compiled for aarch64 Nintendo Switch.
//
// Sources used:
//   PKHeX/PKHeX.Core/Saves/Encryption/SwishCrypto/SwishCrypto.cs (GPL-2.0-or-later)
//   Community RE notes — nhc/swsh-save-structure  (public domain)
//
// Real PKHeX algorithm (verified from source 2026-05-27):
//   1. The 32-byte SHA-256 hash at the end is computed over
//      IntroHashBytes(64) + payload(all bytes before hash) + OutroHashBytes(64).
//   2. The XOR step advances by 0x7F (127) bytes per chunk, NOT 0x80.
//      The 128th pad byte (index 127) is 0x00 (alignment) and is never used
//      as an XOR key — it just pads the table to a power-of-two size.
//   3. Hash size is 32 bytes (full SHA-256), not 20 bytes.
//
// Round-trip contract:
//   SwishDecrypt(buf, size)           → decrypted plaintext + hash verified
//   SwishEncrypt(decrypted_buf, size) → bit-identical to original ciphertext
//   SwishVerifyHash(buf, size)        → true iff stored SHA matches recomputed
//
// NOTE: The SHA-256 implementation is a minimal standalone C implementation
// (no external library) so this file stays self-contained.

#include <ul/menu/qdesktop/qd_SwishCrypto.hpp>
#include <cstring>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── kSwishXorPad ──────────────────────────────────────────────────────────────
//
// The 127-byte XOR pad for the SwSh SCBlock encryption.
// Source: PKHeX SwishCrypto.cs, array literal lines 21-30.
// These are numeric constants derived from the Pokemon save format spec;
// not copyrightable expression.

// Source: PKHeX SwishCrypto.cs StaticXorpad (128 bytes, verbatim).
// Note: The xorpad advances by 0x7F (127) per iteration, not 0x80 —
// the 128th element (index 127) is 0x00 and serves as alignment only.
// Both the byte values and the 0x7F-step are format facts, not copyrightable.
const uint8_t kSwishXorPad[kXorPadLen] = {
    0xA0, 0x92, 0xD1, 0x06, 0x07, 0xDB, 0x32, 0xA1, 0xAE, 0x01, 0xF5, 0xC5,
    0x1E, 0x84, 0x4F, 0xE3, 0x53, 0xCA, 0x37, 0xF4, 0xA7, 0xB0, 0x4D, 0xA0,
    0x18, 0xB7, 0xC2, 0x97, 0xDA, 0x5F, 0x53, 0x2B, 0x75, 0xFA, 0x48, 0x16,
    0xF8, 0xD4, 0x8A, 0x6F, 0x61, 0x05, 0xF4, 0xE2, 0xFD, 0x04, 0xB5, 0xA3,
    0x0F, 0xFC, 0x44, 0x92, 0xCB, 0x32, 0xE6, 0x1B, 0xB9, 0xB1, 0x2E, 0x01,
    0xB0, 0x56, 0x53, 0x36, 0xD2, 0xD1, 0x50, 0x3D, 0xDE, 0x5B, 0x2E, 0x0E,
    0x52, 0xFD, 0xDF, 0x2F, 0x7B, 0xCA, 0x63, 0x50, 0xA4, 0x67, 0x5D, 0x23,
    0x17, 0xC0, 0x52, 0xE1, 0xA6, 0x30, 0x7C, 0x2B, 0xB6, 0x70, 0x36, 0x5B,
    0x2A, 0x27, 0x69, 0x33, 0xF5, 0x63, 0x7B, 0x36, 0x3F, 0x26, 0x9B, 0xA3,
    0xED, 0x7A, 0x53, 0x00, 0xA4, 0x48, 0xB3, 0x50, 0x9E, 0x14, 0xA0, 0x52,
    0xDE, 0x7E, 0x10, 0x2B, 0x1B, 0x77, 0x6E, 0x00, // 0x00 = alignment byte
};

// ── Minimal standalone SHA-256 ────────────────────────────────────────────────
//
// Self-contained 32-byte digest; no external library.
// RFC 6234 / FIPS 180-4 reference implementation style.

namespace {

static const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr32(const uint32_t x, const int n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i*4+0]) << 24) |
               (static_cast<uint32_t>(block[i*4+1]) << 16) |
               (static_cast<uint32_t>(block[i*4+2]) <<  8) |
               (static_cast<uint32_t>(block[i*4+3]));
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i-15], 7)  ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        const uint32_t s1 = rotr32(w[i-2],  17) ^ rotr32(w[i-2],  19) ^ (w[i-2]  >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1    = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch    = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0    = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/// Compute SHA-256 digest of `data` into `digest[32]`.
static void sha256(const uint8_t *data, const size_t len, uint8_t digest[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    const size_t blocks    = len / 64;
    const size_t remainder = len % 64;

    // Process complete 64-byte blocks.
    for (size_t i = 0; i < blocks; ++i) {
        sha256_compress(state, data + i * 64);
    }

    // Padding block.
    uint8_t pad[128] = {};
    memcpy(pad, data + blocks * 64, remainder);
    pad[remainder] = 0x80;

    const uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    if (remainder < 56) {
        // Length fits in this block.
        for (int i = 0; i < 8; ++i) {
            pad[56 + i] = static_cast<uint8_t>(bit_len >> (56 - 8*i));
        }
        sha256_compress(state, pad);
    } else {
        // Need an extra block.
        for (int i = 0; i < 8; ++i) {
            pad[120 + i] = static_cast<uint8_t>(bit_len >> (56 - 8*i));
        }
        sha256_compress(state, pad);
        sha256_compress(state, pad + 64);
    }

    // Write big-endian digest.
    for (int i = 0; i < 8; ++i) {
        digest[i*4+0] = static_cast<uint8_t>(state[i] >> 24);
        digest[i*4+1] = static_cast<uint8_t>(state[i] >> 16);
        digest[i*4+2] = static_cast<uint8_t>(state[i] >>  8);
        digest[i*4+3] = static_cast<uint8_t>(state[i]);
    }
}

} // anonymous namespace

// ── Intro/Outro hash salts ────────────────────────────────────────────────────
//
// Source: PKHeX SwishCrypto.cs IntroHashBytes / OutroHashBytes (64 bytes each).
// These are format constants derived from the Pokemon save format spec.

const uint8_t kSwishIntroHash[64] = {
    0x9E, 0xC9, 0x9C, 0xD7, 0x0E, 0xD3, 0x3C, 0x44,
    0xFB, 0x93, 0x03, 0xDC, 0xEB, 0x39, 0xB4, 0x2A,
    0x19, 0x47, 0xE9, 0x63, 0x4B, 0xA2, 0x33, 0x44,
    0x16, 0xBF, 0x82, 0xA2, 0xBA, 0x63, 0x55, 0xB6,
    0x3D, 0x9D, 0xF2, 0x4B, 0x5F, 0x7B, 0x6A, 0xB2,
    0x62, 0x1D, 0xC2, 0x1B, 0x68, 0xE5, 0xC8, 0xB5,
    0x3A, 0x05, 0x90, 0x00, 0xE8, 0xA8, 0x10, 0x3D,
    0xE2, 0xEC, 0xF0, 0x0C, 0xB2, 0xED, 0x4F, 0x6D,
};

const uint8_t kSwishOutroHash[64] = {
    0xD6, 0xC0, 0x1C, 0x59, 0x8B, 0xC8, 0xB8, 0xCB,
    0x46, 0xE1, 0x53, 0xFC, 0x82, 0x8C, 0x75, 0x75,
    0x13, 0xE0, 0x45, 0xDF, 0x32, 0x69, 0x3C, 0x75,
    0xF0, 0x59, 0xF8, 0xD9, 0xA2, 0x5F, 0xB2, 0x17,
    0xE0, 0x80, 0x52, 0xDB, 0xEA, 0x89, 0x73, 0x99,
    0x75, 0x79, 0xAF, 0xCB, 0x2E, 0x80, 0x07, 0xE6,
    0xF1, 0x26, 0xE0, 0x03, 0x0A, 0xE6, 0x6F, 0xF6,
    0x41, 0xBF, 0x7E, 0x59, 0xC2, 0xAE, 0x55, 0xFD,
};

// ── Minimum viable save-buffer size ──────────────────────────────────────────
//
// SwSh save: block stream + 32-byte trailing SHA-256 hash.
// Minimum: at least enough for one minimal SCBlock (4 bytes key + 1 byte type)
// plus the 32-byte hash = 37 bytes.

static constexpr size_t kMinSaveSize = kHashLen + 5;

// ── Internal: compute SwSh salted SHA-256 hash ────────────────────────────────
//
// PKHeX: SHA-256(IntroHashBytes || payload || OutroHashBytes)
// where payload is all bytes of the save file EXCEPT the trailing 32-byte hash.

namespace {

static void swish_compute_hash(const uint8_t *payload, size_t payload_len,
                               uint8_t out_digest[32]) {
    // We feed the hash in three segments.  Use a minimal incremental wrapper
    // around the existing sha256() that hashes a concatenated message.
    // Concatenate into a temporary buffer for simplicity (saves are <1 MB).
    const size_t total = 64 + payload_len + 64;
    // Allocate on the heap to avoid large stack frames.
    uint8_t *msg = new uint8_t[total];
    memcpy(msg,                  kSwishIntroHash, 64);
    memcpy(msg + 64,             payload,         payload_len);
    memcpy(msg + 64 + payload_len, kSwishOutroHash, 64);
    sha256(msg, total, out_digest);
    delete[] msg;
}

// Apply the 0x7F-step XOR pad to [data, data+len).
// PKHeX iterates in kXorPadStep (0x7F)-sized chunks, applying the full 0x80
// table to each chunk (vectorised).  The final partial chunk applies only as
// many bytes as remain.  The net effect is identical to:
//   data[i] ^= kSwishXorPad[i % kXorPadStep]   for all i in [0, len).
static void swish_xor_payload(uint8_t *data, size_t len) {
    size_t pos = 0;
    // Full 0x7F-byte chunks.
    while (pos + kXorPadStep <= len) {
        for (size_t i = 0; i < kXorPadStep; ++i) {
            data[pos + i] ^= kSwishXorPad[i];
        }
        pos += kXorPadStep;
    }
    // Remaining bytes.
    const size_t rem = len - pos;
    for (size_t i = 0; i < rem; ++i) {
        data[pos + i] ^= kSwishXorPad[i];
    }
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

SwishResult SwishDecrypt(uint8_t *buf, const size_t size) {
    if (size < kMinSaveSize) {
        return SwishResult::BufferTooSmall;
    }

    // Payload is everything before the trailing 32-byte hash.
    const size_t payload_len = size - kHashLen;

    // Verify hash before decryption (data is still encrypted at this point).
    // PKHeX: GetIsHashValid checks hash of the encrypted payload.
    {
        uint8_t computed[32] = {};
        swish_compute_hash(buf, payload_len, computed);
        if (memcmp(computed, buf + payload_len, kHashLen) != 0) {
            return SwishResult::HashMismatch;
        }
    }

    // Decrypt: XOR the payload in-place.
    swish_xor_payload(buf, payload_len);

    return SwishResult::Ok;
}

SwishResult SwishEncrypt(uint8_t *buf, const size_t size) {
    if (size < kMinSaveSize) {
        return SwishResult::BufferTooSmall;
    }

    const size_t payload_len = size - kHashLen;

    // Re-encrypt: XOR is self-inverse.
    swish_xor_payload(buf, payload_len);

    // Recompute and write the hash over the now-encrypted payload.
    swish_compute_hash(buf, payload_len, buf + payload_len);

    return SwishResult::Ok;
}

bool SwishVerifyHash(const uint8_t *buf, const size_t size) {
    if (size < kMinSaveSize) {
        return false;
    }
    const size_t payload_len = size - kHashLen;
    uint8_t computed[32] = {};
    swish_compute_hash(buf, payload_len, computed);
    return (memcmp(computed, buf + payload_len, kHashLen) == 0);
}

} // namespace ul::menu::qdesktop
