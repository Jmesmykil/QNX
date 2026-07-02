// qd_SwishCrypto.hpp — SwSh-era SwishCrypto round-trip (host-buildable spike).
//
// This header declares a minimal SwishCrypto interface derived from the
// constants visible verbatim in PKHeX's SwishCrypto.cs and the matching
// reference in pkHouse (both GPL-2.0 / GPL-2.0-or-later).
//
// Legal posture:
//   The XOR pad, hash salts, and block sizes are numeric facts embedded in the
//   Pokemon save format by Game Freak.  They are not copyrightable expression;
//   they appear verbatim in PKHeX, pkHouse, and every independent RE writeup.
//   Including them as static constants here is the same posture taken by
//   pkHouse's own implementation and does not create a GPL boundary issue with
//   the uMenu GPL-2.0 project.
//
// Compile note:
//   This file and qd_SwishCrypto.cpp have NO libnx #include and NO Switch SDK
//   dependency.  They are designed to build on the host (macOS/Linux) for unit
//   testing the round-trip before wiring into the on-device layer in the
//   follow-up PR.  On-device callers will include this header as-is once the
//   follow-up PR adds the save-open / commit path in qd_SaveEditorLayout.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ul::menu::qdesktop {

// ── SwishCrypto constants (SwSh era, Block 0 / SCBlock header) ───────────────
//
// Source: PKHeX/PKHeX.Core/Saves/Util/SwishCrypto.cs
//         and pkHouse/source/crypto.cpp (GPL-2.0 / GPL-2.0-or-later).
//
// The intro/outro hash salts and the 127-byte xorpad are part of the public
// Pokemon save-format specification derived by the community.

/// Magic value at offset 0 of a SwSh / BDSP / PLA save file.
/// NOTE: SwSh saves do NOT embed a magic at offset 0 in the traditional sense —
/// the magic here is the first u32 of the first SCBlock key stream, which
/// always resolves to 0x88BBFA5B after XorShift32 decryption.  We use this
/// as a quick sanity check before attempting full decryption.
/// (Verified against PKHeX SwishCrypto.cs, 2026-05-27.)
static constexpr uint32_t kSwishMagic = 0x88BBFA5B;

/// Size of the SCBlock key prefix (u32 key before each block's type byte).
static constexpr size_t kSCBlockKeySize = 4;

/// Byte length of the SHA-256 hash appended at the end of the save file.
/// Source: PKHeX SwishCrypto.cs — SIZE_HASH = SHA256.HashSizeInBytes = 0x20.
static constexpr size_t kHashLen = 0x20;  // 32 bytes (full SHA-256)

/// Effective step of the repeating XOR pad per chunk.
/// PKHeX advances by 0x7F (127) bytes per iteration — the 128th byte of the
/// pad is 0x00 (alignment only) and is intentionally skipped.
static constexpr size_t kXorPadStep = 0x7F;

/// Full allocated length of the XOR pad table (pad step + 1 alignment byte).
static constexpr size_t kXorPadLen = 128;

/// The XOR pad used by SwSh SwishCrypto.
/// Source: PKHeX SwishCrypto.cs StaticXorpad (128 bytes, including 0x00 at [127]).
/// These are numeric constants embedded in the Pokemon save format by Game Freak.
extern const uint8_t kSwishXorPad[kXorPadLen];

/// 64-byte intro salt prepended when computing the save hash.
/// Source: PKHeX SwishCrypto.cs IntroHashBytes.
extern const uint8_t kSwishIntroHash[64];

/// 64-byte outro salt appended when computing the save hash.
/// Source: PKHeX SwishCrypto.cs OutroHashBytes.
extern const uint8_t kSwishOutroHash[64];

// ── SwishCrypto result codes ──────────────────────────────────────────────────

enum class SwishResult : uint8_t {
    Ok            = 0,   ///< Operation succeeded.
    InvalidMagic  = 1,   ///< File does not start with kSwishMagic.
    BufferTooSmall = 2,  ///< Buffer is shorter than the minimum save structure.
    HashMismatch  = 3,   ///< SHA-256 verification failed after decryption.
};

// ── Public API ────────────────────────────────────────────────────────────────

/// Decrypt a SwSh-era save buffer in-place.
///
/// @param buf   Pointer to the raw save data (read from nsam::ReadSaveData).
/// @param size  Byte length of the buffer.
/// @returns     SwishResult::Ok on success; error code otherwise.
///
/// The buffer is mutated in-place.  On failure the buffer contents are
/// undefined — callers should keep a backup copy before calling.
SwishResult SwishDecrypt(uint8_t *buf, size_t size);

/// Re-encrypt a previously-decrypted buffer in-place (round-trip inverse).
///
/// Calling SwishDecrypt followed by SwishEncrypt on the same buffer must
/// produce bit-identical output to the original raw save bytes.
///
/// @param buf   Pointer to decrypted save data.
/// @param size  Byte length of the buffer.
/// @returns     SwishResult::Ok on success.
SwishResult SwishEncrypt(uint8_t *buf, size_t size);

/// Verify the SHA-256 hash embedded in a decrypted save buffer.
///
/// @param buf   Pointer to the decrypted save data (after SwishDecrypt).
/// @param size  Byte length of the buffer.
/// @returns     true if the stored hash matches the computed hash.
bool SwishVerifyHash(const uint8_t *buf, size_t size);

} // namespace ul::menu::qdesktop
