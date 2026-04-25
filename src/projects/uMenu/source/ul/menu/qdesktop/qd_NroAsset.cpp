// qd_NroAsset.cpp — NRO ASET icon extractor for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/nro_asset.rs.
//
// Algorithm mirror (authoritative):
//   1. Open NRO file; verify NRO0 magic at hdr[0x10..0x14].
//   2. nro_size = *(u32 LE)(hdr + 0x28).  (v1.1.12 fix: was 0x18 in v1.1.11.)
//   3. Seek past NRO body to nro_size; read ASET_HEADER_SIZE bytes.
//   4. Verify ASET magic at aset[0x00..0x04].
//   5. icon_off  = *(u64 LE)(aset + 0x08), icon_size = *(u64 LE)(aset + 0x10).
//   6. Skip any gap between ASET header end and icon_off.
//   7. Read icon_size bytes of JPEG blob.
//   8. Decode JPEG with SDL2_image IMG_Load_RW → SDL_Surface → RGBA copy.
// Fallback (any failure): DJB2 hash of nro_path → hue → HSL(hue,0.55,0.40) → RGB;
//   allocate 64×64 solid-colour RGBA.

#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

namespace ul::menu::qdesktop {

// ── Internal helpers ───────────────────────────────────────────────────────

namespace {

// Read u32 little-endian from buf at offset off.
// Caller must guarantee buf[off..off+3] is valid.
static inline u32 ReadU32LE(const u8 *buf, size_t off) {
    return  static_cast<u32>(buf[off])
         | (static_cast<u32>(buf[off + 1]) << 8)
         | (static_cast<u32>(buf[off + 2]) << 16)
         | (static_cast<u32>(buf[off + 3]) << 24);
}

// Read u64 little-endian from buf at offset off.
// Caller must guarantee buf[off..off+7] is valid.
static inline u64 ReadU64LE(const u8 *buf, size_t off) {
    return  static_cast<u64>(buf[off])
         | (static_cast<u64>(buf[off + 1]) <<  8)
         | (static_cast<u64>(buf[off + 2]) << 16)
         | (static_cast<u64>(buf[off + 3]) << 24)
         | (static_cast<u64>(buf[off + 4]) << 32)
         | (static_cast<u64>(buf[off + 5]) << 40)
         | (static_cast<u64>(buf[off + 6]) << 48)
         | (static_cast<u64>(buf[off + 7]) << 56);
}

// Drain `n` bytes from f by reading in 4 KiB chunks.
// Returns true on success, false on premature EOF.
static bool DrainBytes(FILE *f, size_t n) {
    u8 buf[4096];
    while (n > 0) {
        size_t chunk = (n < sizeof(buf)) ? n : sizeof(buf);
        size_t got = fread(buf, 1, chunk, f);
        if (got == 0) return false;
        n -= got;
    }
    return true;
}

} // anonymous namespace

// ── DJB2 u32 hash ─────────────────────────────────────────────────────────
// Matches desktop_icons.rs::hash_to_color DJB2: h = 5381; h = h*33 + b.
// Input: null-terminated ASCII string.
u32 Djb2Hash32(const char *str) {
    u32 h = 5381;
    const u8 *p = reinterpret_cast<const u8 *>(str);
    while (*p) {
        h = h * 33u + static_cast<u32>(*p);
        ++p;
    }
    return h;
}

// ── HSL → RGB ──────────────────────────────────────────────────────────────
// Exact C++ port of desktop_icons.rs::hsl_to_rgb.
// h_deg in [0,360), s and l in [0.0, 1.0].
void HslToRgb(u32 h_deg, float s, float l,
              u8 &out_r, u8 &out_g, u8 &out_b) {
    const float h = static_cast<float>(h_deg % 360u);
    const float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    // x = c * (1 - |((h/60) mod 2) - 1|)
    const float h60 = h / 60.0f;
    const float mod2 = h60 - 2.0f * floorf(h60 / 2.0f); // h60 mod 2
    const float x = c * (1.0f - fabsf(mod2 - 1.0f));
    const float m = l - c / 2.0f;
    float r1, g1, b1;
    if      (h < 60.0f)  { r1 = c; g1 = x; b1 = 0.0f; }
    else if (h < 120.0f) { r1 = x; g1 = c; b1 = 0.0f; }
    else if (h < 180.0f) { r1 = 0.0f; g1 = c; b1 = x; }
    else if (h < 240.0f) { r1 = 0.0f; g1 = x; b1 = c; }
    else if (h < 300.0f) { r1 = x; g1 = 0.0f; b1 = c; }
    else                  { r1 = c; g1 = 0.0f; b1 = x; }
    out_r = static_cast<u8>((r1 + m) * 255.0f);
    out_g = static_cast<u8>((g1 + m) * 255.0f);
    out_b = static_cast<u8>((b1 + m) * 255.0f);
}

// ── Fallback icon ──────────────────────────────────────────────────────────
// 64×64 solid-colour RGBA pixel buffer from DJB2(nro_path) → HSL(hue,0.55,0.40).
// Caller must free with delete[].
u8 *MakeFallbackIcon(const char *nro_path) {
    u32 h = Djb2Hash32(nro_path);
    u32 hue = h % 360u;
    u8 r, g, b;
    HslToRgb(hue, 0.55f, 0.40f, r, g, b);
    constexpr size_t N = 64 * 64 * 4;
    u8 *buf = new u8[N];
    for (size_t i = 0; i < 64 * 64; ++i) {
        buf[i * 4 + 0] = r;
        buf[i * 4 + 1] = g;
        buf[i * 4 + 2] = b;
        buf[i * 4 + 3] = 0xFF;
    }
    return buf;
}

// ── ExtractNroIcon ─────────────────────────────────────────────────────────
// Extract the JPEG icon from the ASET section of an NRO file.
// Falls back to MakeFallbackIcon on any parse/decode failure.
NroIconResult ExtractNroIcon(const char *nro_path) {
    // ── 1. Open file ──────────────────────────────────────────────────────
    FILE *f = fopen(nro_path, "rb");
    if (!f) {
        // Fallback: solid colour from DJB2.
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 2. Read NRO header (0x80 bytes) ──────────────────────────────────
    constexpr size_t HDR_SIZE = 0x80;
    u8 hdr[HDR_SIZE];
    if (fread(hdr, 1, HDR_SIZE, f) < HDR_SIZE) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    // Validate NRO0 magic at file[0x10..0x14].
    if (memcmp(&hdr[0x10], NRO0_MAGIC, 4) != 0) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    const u32 nro_size = ReadU32LE(hdr, NRO_OFFSET_NRO_SIZE);
    if (nro_size < HDR_SIZE) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 3. Drain bytes from end of header to nro_size ────────────────────
    // We already consumed HDR_SIZE bytes.  Drain the remainder of the NRO body.
    constexpr size_t MAX_NRO_BODY = 8 * 1024 * 1024;
    if (nro_size > MAX_NRO_BODY) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    const size_t remaining = static_cast<size_t>(nro_size) - HDR_SIZE;
    if (!DrainBytes(f, remaining)) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 4. Read ASET header (0x38 bytes) ─────────────────────────────────
    u8 aset_hdr[ASET_HEADER_SIZE];
    if (fread(aset_hdr, 1, ASET_HEADER_SIZE, f) < ASET_HEADER_SIZE) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    if (memcmp(aset_hdr, ASET_MAGIC, 4) != 0) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    const u64 icon_off  = ReadU64LE(aset_hdr, 0x08);
    const u64 icon_size = ReadU64LE(aset_hdr, 0x10);
    if (icon_size == 0 || icon_size > MAX_JPEG_BYTES) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    // icon_off is relative to start of ASET header; we already read ASET_HEADER_SIZE.
    if (icon_off < ASET_HEADER_SIZE) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    const size_t skip_gap = static_cast<size_t>(icon_off) - ASET_HEADER_SIZE;
    if (skip_gap > 0 && !DrainBytes(f, skip_gap)) {
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 5. Read JPEG blob ─────────────────────────────────────────────────
    const size_t jpeg_size = static_cast<size_t>(icon_size);
    u8 *jpeg_buf = new u8[jpeg_size];
    if (fread(jpeg_buf, 1, jpeg_size, f) < jpeg_size) {
        delete[] jpeg_buf;
        fclose(f);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    fclose(f);

    // ── 6. Decode JPEG with SDL2_image ────────────────────────────────────
    SDL_RWops *rw = SDL_RWFromMem(jpeg_buf, static_cast<int>(jpeg_size));
    if (!rw) {
        delete[] jpeg_buf;
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    SDL_Surface *surf = IMG_Load_RW(rw, /*freesrc=*/1); // frees rw on return
    delete[] jpeg_buf;                                   // JPEG buf no longer needed
    if (!surf) {
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // Convert surface to RGBA8888.
    SDL_Surface *rgba_surf = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(surf);
    if (!rgba_surf) {
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    const s32 w = rgba_surf->w;
    const s32 h = rgba_surf->h;
    if (w <= 0 || h <= 0 ||
        static_cast<size_t>(w) * static_cast<size_t>(h) * 4 > MAX_PIXEL_BYTES) {
        SDL_FreeSurface(rgba_surf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // Copy pixels to a heap buffer (caller owns via delete[]).
    const size_t pixel_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    u8 *pixels = new u8[pixel_bytes];
    SDL_LockSurface(rgba_surf);
    memcpy(pixels, rgba_surf->pixels, pixel_bytes);
    SDL_UnlockSurface(rgba_surf);
    SDL_FreeSurface(rgba_surf);

    return NroIconResult{ pixels, w, h, true };
}

} // namespace ul::menu::qdesktop
