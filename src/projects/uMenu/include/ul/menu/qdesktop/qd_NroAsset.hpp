// qd_NroAsset.hpp — NRO/ASET icon extractor for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/nro_asset.rs.
#pragma once
#include <pu/Plutonium>
#include <cstddef>
#include <cstdint>

namespace ul::menu::qdesktop {

// NRO ASET icon extraction constants.
// All values confirmed against nro_asset.rs v1.1.12.
static constexpr size_t NRO_OFFSET_NRO_SIZE   = 0x18; // v1.8.17 B65: file offset; NroStart 0x10 base + size@0x08 in NroHeader = 0x18 (absolute)
static constexpr size_t ASET_HEADER_SIZE       = 0x38;
static constexpr size_t MAX_JPEG_BYTES         = 4 * 1024 * 1024;
static constexpr size_t MAX_PIXEL_BYTES        = 512 * 512 * 4;

// ASET magic bytes: "ASET"
static constexpr u8 ASET_MAGIC[4] = { 'A','S','E','T' };

// NRO0 magic bytes: "NRO0" (at file[0x10..0x14], NOT file[0x00])
static constexpr u8 NRO0_MAGIC[4] = { 'N','R','O','0' };

// Result of icon extraction.
struct NroIconResult {
    // RGBA pixel data, 256×256 (or smaller), 4 bytes per pixel.
    // Width/height reported by SDL2_image JPEG decode.
    u8 *pixels;       // heap-allocated; caller must free with delete[]
    s32 width;
    s32 height;
    bool valid;       // false if extraction failed; pixels is still a valid fallback
                      // buffer (64×64 colour-coded RGBA) — caller MUST call FreeNroIcon
                      // in BOTH the valid==true and valid==false branches (F-05 fix).
};

// Extract the 256×256 JPEG icon from the ASET section of an NRO file.
// Falls back to a flat #3A3A3A neutral-gray icon on any parse/decode failure.
//
// nro_path: null-terminated path on sdmc: (e.g. "sdmc:/switch/hbmenu.nro")
//
// Algorithm:
//   1. Read NRO header; verify NRO0 magic at hdr[0x10..0x14].
//   2. nro_size = *(u32 *)(hdr + NRO_OFFSET_NRO_SIZE)  [little-endian]
//   3. Seek to nro_size; read ASET_HEADER_SIZE bytes.
//   4. Verify ASET magic at aset[0x00..0x04].
//   5. icon_off  = *(u64 *)(aset + 0x08)  (relative to ASET header start)
//   6. icon_size = *(u64 *)(aset + 0x10)
//   7. Seek to (nro_size + icon_off); read icon_size bytes of JPEG.
//   8. Decode JPEG with SDL2_image → SDL_Surface; convert to RGBA.
// Fallback:
//   9. Allocate 64×64 RGBA buffer filled with solid #3A3A3A (R=0x3A G=0x3A B=0x3A A=0xFF).
//      (Previous DJB2/HSL color-derivation removed; neutral gray avoids palette noise.)
//
// Returns NroIconResult with valid=true on success, valid=false on failure.
NroIconResult ExtractNroIcon(const char *nro_path);

// Free pixels allocated by ExtractNroIcon.
inline void FreeNroIcon(NroIconResult &res) {
    if (res.pixels) {
        delete[] res.pixels;
        res.pixels = nullptr;
    }
    res.valid = false;
}

// DJB2 hash of a null-terminated string (u32 accumulator, matches desktop_icons.rs).
// h = 5381; for each byte b: h = h*33 + b
u32 Djb2Hash32(const char *str);

// HSL → RGB conversion, matching desktop_icons.rs::hsl_to_rgb exactly.
// h in [0,360), s and l in [0.0, 1.0].
// Returns (r,g,b) each in [0,255].
void HslToRgb(u32 h_deg, float s, float l,
              u8 &out_r, u8 &out_g, u8 &out_b);

// Produce the fallback 64×64 RGBA icon for a given NRO path.
// Allocates 64*64*4 bytes (caller must free with delete[]).
u8 *MakeFallbackIcon(const char *nro_path);

// Fix C (v1.6.12): resolve a creator-provided icon for a payload or system tool.
// Tries candidate paths in order using fopen existence checks:
//   1. sdmc:/bootloader/payloads/<stem>.jpg / .bmp / .png
//   2. sdmc:/switch/<stem>/icon.jpg
//   3. sdmc:/atmosphere/config/reboot_to_payload/icons/<stem>.jpg
// where <stem> is payload_name with any .bin / .nro / .kip extension stripped.
//
// Returns the first path that exists, or an empty string if none found.
// The returned string is suitable for passing to LoadJpegIconToCache.
std::string ResolvePayloadIcon(const char *payload_name);

} // namespace ul::menu::qdesktop
