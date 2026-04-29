// qd_NroAsset.cpp — NRO ASET icon extractor for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/nro_asset.rs.
//
// v1.8.20: kernel-direct I/O (Change 1).
//   File I/O replaced with fsFsOpenFile + fsFileRead (absolute-offset reads,
//   no DrainBytes, no fseek, no stdio buffering).  SDL_image retained for
//   JPEG decode (libjpeg-turbo is NOT in the link line).  DrainBytes removed.
//
// v1.8.20: static payload-icon table (Change 5).
//   ResolvePayloadIcon replaced with kPayloadIconTable + fsFsGetEntryType.
//   qd_HekateIni.hpp dependency removed (Hekate INI parse was 5× fopen chain
//   on every ScanPayloads call; static table is zero-alloc, zero-seek).
//
// Algorithm mirror (authoritative):
//   1. Get sdmc FsFileSystem* via fsdevGetDeviceFileSystem("sdmc").
//   2. Strip "sdmc:" prefix; call fsFsOpenFile with FsOpenMode_Read.
//   3. fsFileRead(f, 0, hdr, HDR_SIZE): read 0x80 bytes NRO header.
//   4. Verify NRO0 magic at hdr[0x10..0x14].
//   5. nro_size = *(u32 LE)(hdr + 0x18).
//   6. fsFileRead(f, nro_size, aset_hdr, ASET_HEADER_SIZE): absolute seek.
//   7. Verify ASET magic; extract icon_off, icon_size.
//   8. fsFileRead(f, nro_size + icon_off, jpeg_buf, icon_size): one read.
//   9. Decode JPEG with SDL_RWFromMem + IMG_Load_RW.
// Fallback (any failure): neutral gray #3A3A3A, 64×64 RGBA (v1.6.11+).

#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/ul_Result.hpp>
#include <switch/runtime/devices/fs_dev.h>  // fsdevGetDeviceFileSystem
#include <cstring>
#include <cstdio>
#include <cerrno>
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

// Strip "sdmc:" prefix from an sdmc path.
// fsFsOpenFile takes paths WITHOUT the "sdmc:" device prefix.
// Returns pointer into original string, past the prefix if present.
static inline const char *StripSdmcPrefix(const char *path) {
    if (path[0] == 's' && path[1] == 'd' && path[2] == 'm' &&
        path[3] == 'c' && path[4] == ':') {
        return path + 5;
    }
    return path;
}

// Probe whether a file exists on the sdmc filesystem using fsFsGetEntryType.
// Uses kernel IPC instead of fopen (avoids stdio buffering + errno path).
// Returns true if the entry exists (file or directory), false otherwise.
static bool FileExists(const char *sdmc_path) {
    FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (!sdmc) { return false; }
    FsDirEntryType etype;
    const Result rc = fsFsGetEntryType(sdmc, StripSdmcPrefix(sdmc_path), &etype);
    return R_SUCCEEDED(rc);
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
// 64×64 neutral-gray RGBA pixel buffer (#3A3A3A, fully opaque).
// Used when icon extraction fails for any app type.  Neutral gray is
// visually consistent and does NOT draw attention away from real icons.
// The random-HSL / DJB2-hash colouring that previously lived here was
// removed in v1.6.11 -- it produced jarring coloured squares when
// icons could not be decoded.
// Caller must free with delete[].
u8 *MakeFallbackIcon(const char *nro_path) {
    (void)nro_path; // path no longer hashed; parameter kept for ABI stability
    constexpr u8 kGray = 0x3A;
    constexpr size_t N = 64 * 64 * 4;
    u8 *buf = new u8[N];
    for (size_t i = 0; i < 64 * 64; ++i) {
        buf[i * 4 + 0] = kGray; // R
        buf[i * 4 + 1] = kGray; // G
        buf[i * 4 + 2] = kGray; // B
        buf[i * 4 + 3] = 0xFF;  // A
    }
    return buf;
}

// ── ExtractNroIcon ─────────────────────────────────────────────────────────
// Extract the JPEG icon from the ASET section of an NRO file.
// v1.8.20 (Change 1): uses fsFsOpenFile + fsFileRead (kernel IPC, absolute
// offsets) instead of fopen/fread/fseek/DrainBytes.  DrainBytes is eliminated
// entirely — fsFileRead takes an absolute `s64 off` parameter so there is
// no need to drain the NRO body to seek past it.
// SDL_image is retained for JPEG decode (libjpeg-turbo not in link line).
// Falls back to MakeFallbackIcon (neutral gray #3A3A3A) on any failure.
NroIconResult ExtractNroIcon(const char *nro_path) {
    // ── 1. Get sdmc FsFileSystem* and open file via kernel IPC ────────────
    FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (!sdmc) {
        UL_LOG_WARN("ExtractNroIcon: fsdevGetDeviceFileSystem(sdmc) returned null for '%s'", nro_path);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    FsFile fsf;
    const Result open_rc = fsFsOpenFile(sdmc, StripSdmcPrefix(nro_path),
                                        FsOpenMode_Read, &fsf);
    if (R_FAILED(open_rc)) {
        UL_LOG_WARN("ExtractNroIcon: fsFsOpenFile failed for '%s' rc=0x%X", nro_path, open_rc);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 2. Read NRO header (0x80 bytes) via absolute-offset read ─────────
    constexpr size_t HDR_SIZE = 0x80;
    u8 hdr[HDR_SIZE];
    u64 bytes_read = 0;
    Result rc = fsFileRead(&fsf, /*off=*/0, hdr, HDR_SIZE, 0, &bytes_read);
    if (R_FAILED(rc) || bytes_read < HDR_SIZE) {
        UL_LOG_WARN("ExtractNroIcon: NRO header read failed for '%s' rc=0x%X got=%llu need=%zu",
                    nro_path, rc, (unsigned long long)bytes_read, HDR_SIZE);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    // Validate NRO0 magic at file[0x10..0x14].
    if (memcmp(&hdr[0x10], NRO0_MAGIC, 4) != 0) {
        UL_LOG_WARN("ExtractNroIcon: NRO0 magic mismatch for '%s' got=0x%02X%02X%02X%02X",
                    nro_path, hdr[0x10], hdr[0x11], hdr[0x12], hdr[0x13]);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    const u32 nro_size = ReadU32LE(hdr, NRO_OFFSET_NRO_SIZE);
    if (nro_size < HDR_SIZE) {
        UL_LOG_WARN("ExtractNroIcon: nro_size too small for '%s' got=0x%X min=0x%zX",
                    nro_path, nro_size, HDR_SIZE);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 3. Validate NRO body against actual file size ─────────────────────
    // F3 (stabilize-5): no hard cap — large NROs (PPSSPP ~16-40 MB,
    // DolphinNX ~60 MB+) must not be rejected.  Validate nro_size ≤ file_size.
    s64 file_size = 0;
    rc = fsFileGetSize(&fsf, &file_size);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("ExtractNroIcon: fsFileGetSize failed for '%s' rc=0x%X", nro_path, rc);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    if (static_cast<s64>(nro_size) > file_size) {
        UL_LOG_WARN("ExtractNroIcon: nro_size exceeds file for '%s' nro_size=0x%X file_size=0x%llX",
                    nro_path, nro_size, (unsigned long long)file_size);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 4. Read ASET header (0x38 bytes) at absolute offset nro_size ─────
    // No DrainBytes needed — fsFileRead takes absolute `s64 off`.
    u8 aset_hdr[ASET_HEADER_SIZE];
    bytes_read = 0;
    rc = fsFileRead(&fsf, /*off=*/static_cast<s64>(nro_size),
                    aset_hdr, ASET_HEADER_SIZE, 0, &bytes_read);
    if (R_FAILED(rc) || bytes_read < ASET_HEADER_SIZE) {
        UL_LOG_WARN("ExtractNroIcon: ASET header read failed for '%s' rc=0x%X got=%llu need=%zu",
                    nro_path, rc, (unsigned long long)bytes_read, ASET_HEADER_SIZE);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    if (memcmp(aset_hdr, ASET_MAGIC, 4) != 0) {
        // v1.8.17 B65: expected LE bytes {'A','S','E','T'}.
        UL_LOG_WARN("ExtractNroIcon: ASET magic mismatch for '%s' got=0x%02X%02X%02X%02X",
                    nro_path, aset_hdr[0], aset_hdr[1], aset_hdr[2], aset_hdr[3]);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    const u64 icon_off  = ReadU64LE(aset_hdr, 0x08);
    const u64 icon_size = ReadU64LE(aset_hdr, 0x10);
    if (icon_size == 0 || icon_size > MAX_JPEG_BYTES) {
        UL_LOG_WARN("ExtractNroIcon: icon_size out of range for '%s' icon_size=0x%llX max=0x%zX",
                    nro_path, (unsigned long long)icon_size, MAX_JPEG_BYTES);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    if (icon_off < ASET_HEADER_SIZE) {
        UL_LOG_WARN("ExtractNroIcon: icon_off too small for '%s' icon_off=0x%llX min=0x%zX",
                    nro_path, (unsigned long long)icon_off, ASET_HEADER_SIZE);
        fsFileClose(&fsf);
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 5. Read JPEG blob at absolute offset (nro_size + icon_off) ───────
    // icon_off is relative to start of ASET header (which starts at nro_size).
    const s64 jpeg_abs_off = static_cast<s64>(nro_size) + static_cast<s64>(icon_off);
    const size_t jpeg_size = static_cast<size_t>(icon_size);
    u8 *jpeg_buf = new u8[jpeg_size];
    bytes_read = 0;
    rc = fsFileRead(&fsf, jpeg_abs_off, jpeg_buf, static_cast<u64>(jpeg_size), 0, &bytes_read);
    fsFileClose(&fsf);  // done with the file regardless of outcome
    if (R_FAILED(rc) || bytes_read < static_cast<u64>(jpeg_size)) {
        UL_LOG_WARN("ExtractNroIcon: JPEG blob read failed for '%s' rc=0x%X got=%llu need=%zu",
                    nro_path, rc, (unsigned long long)bytes_read, jpeg_size);
        delete[] jpeg_buf;
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // ── 6. Decode JPEG with SDL2_image (SDL_RWFromMem → IMG_Load_RW) ─────
    // libjpeg-turbo is NOT in the link line; SDL_image is the only JPEG decoder.
    SDL_RWops *rw = SDL_RWFromMem(jpeg_buf, static_cast<int>(jpeg_size));
    if (!rw) {
        UL_LOG_WARN("ExtractNroIcon: SDL_RWFromMem failed for '%s' sdl_err=%s", nro_path, SDL_GetError());
        delete[] jpeg_buf;
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }
    SDL_Surface *surf = IMG_Load_RW(rw, /*freesrc=*/1); // frees rw on return
    delete[] jpeg_buf;                                   // JPEG buf no longer needed
    if (!surf) {
        UL_LOG_WARN("ExtractNroIcon: IMG_Load_RW failed for '%s' img_err=%s", nro_path, IMG_GetError());
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    // Convert surface to ABGR8888 — on AArch64 LE this stores bytes as
    // [R, G, B, A] in memory, which is what cache_.Put / ScaleToBgra64 expect.
    // Using RGBA8888 here would give [A, B, G, R] (channels scrambled).
    // Mirrors the byte-order rule in LoadJpegIconToCache.
    SDL_Surface *rgba_surf = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if (!rgba_surf) {
        UL_LOG_WARN("ExtractNroIcon: SDL_ConvertSurfaceFormat failed for '%s' sdl_err=%s",
                    nro_path, SDL_GetError());
        u8 *fb = MakeFallbackIcon(nro_path);
        return NroIconResult{ fb, 64, 64, false };
    }

    const s32 w = rgba_surf->w;
    const s32 h = rgba_surf->h;
    if (w <= 0 || h <= 0 ||
        static_cast<size_t>(w) * static_cast<size_t>(h) * 4 > MAX_PIXEL_BYTES) {
        UL_LOG_WARN("ExtractNroIcon: pixel bounds check failed for '%s' w=%d h=%d", nro_path, w, h);
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

    UL_LOG_INFO("ExtractNroIcon: success for '%s' w=%d h=%d", nro_path, w, h);
    return NroIconResult{ pixels, w, h, true };
}

// ── ResolvePayloadIcon (Change 5, v1.8.20; extended v1.8.21) ─────────────
//
// v1.8.20: replaced the old complex priority-chain (Hekate INI parse +
// fopen existence checks) with a static constexpr lookup table and
// FileExists() (fsFsGetEntryType kernel IPC, no fd alloc, no stdio buffer).
//
// v1.8.21: added romfs_rel_path field to PayloadIconEntry.
//   Ten Q OS-branded 256×256 PNG icons are now bundled into the uMenu romfs
//   at romfs:/default/ui/Main/PayloadIcon/<name>.png.  Romfs assets are
//   compile-time invariants — no FileExists check is needed or possible via
//   fsFsGetEntryType (which operates on the sdmc device only).  romfs_rel_path
//   entries are returned unconditionally; the icon_path stored in NroEntry
//   carries the "romfs:/default/" prefix so PaintIconCell can detect and load
//   them via pu::ui::render::LoadImageFromFile (POSIX IMG_Load, works on romfs).
//
// Priority order per table entry (v1.8.21):
//   0. romfs_rel_path (if non-null) — always-present compile-time PNG bundle
//   1. primary_path   (if non-null) — sdmc: path checked via FileExists
//   2. sdmc:/bootloader/res/<stem>.bmp  (Hekate-exported icon convention)
//   3. sdmc:/bootloader/payloads/<stem>.jpg
//   4. sdmc:/bootloader/payloads/<stem>.bmp
//   5. sdmc:/bootloader/payloads/<stem>.png
//   6. sdmc:/switch/<stem>/icon.jpg
//   7. romfs:/default/ui/Main/PayloadIcon/icon_payload_generic.png (sentinel)
//
// Stem derivation: payload_name with .bin/.nro/.kip1/.kip stripped.
// Table lookup: case-insensitive exact match on stem.
// Returns "" only if the generic romfs sentinel is also missing (should never
// happen in a correctly built romfs).

// Romfs device + default theme prefix.  romfs is mounted at boot via
// romfsMountFromFsdev; this prefix is reachable via POSIX fopen / IMG_Load.
static constexpr const char kRomfsDefaultPrefix[] = "romfs:/default/";

namespace {

struct PayloadIconEntry {
    const char *stem;           // lowercase canonical stem for case-insensitive match
    const char *romfs_rel_path; // path relative to romfs:/default/ for bundled PNG,
                                // or nullptr if no themed bundle icon for this stem
    const char *primary_path;   // sdmc: path checked via FileExists, or nullptr
};

// Known payloads and system tools.
// Columns: stem | romfs_rel_path | primary_path (sdmc: or nullptr)
// romfs_rel_path uses the PayloadIcon/ subdirectory added in v1.8.21.
// primary_path keeps existing sdmc fallbacks so that creator-supplied BMPs
// on the SD card still take precedence over the themed bundle icon.
// Wait — the design intent is: romfs themed icon wins as the DEFAULT branded
// look; but if a creator has placed a Hekate-exported BMP on sdmc, that IS
// the creator's preferred icon. Use romfs as the fallback when sdmc is absent.
// So actual priority: sdmc primary_path first (if present on SD), then romfs.
// This preserves creator customization while guaranteeing a branded fallback.
static constexpr PayloadIconEntry kPayloadIconTable[] = {
    // ── Q OS themed bundle icons (v1.8.21) + existing sdmc primaries ─────────
    // hekate / hekate_ipl: themed icon; existing sdmc BMP takes priority if present
    { "hekate",              "ui/Main/PayloadIcon/icon_hekate.png",
                             "sdmc:/bootloader/res/hekate_ctcaer.bmp"     },
    { "hekate_ipl",          "ui/Main/PayloadIcon/icon_hekate.png",
                             "sdmc:/bootloader/res/hekate_ctcaer.bmp"     },
    { "hekate_ctcaer",       "ui/Main/PayloadIcon/icon_hekate.png",
                             "sdmc:/bootloader/res/hekate_ctcaer.bmp"     },
    { "nyx",                 "ui/Main/PayloadIcon/icon_hekate.png",
                             "sdmc:/bootloader/res/hekate_ctcaer.bmp"     },
    // reboot_to_hekate / reboot_to_payload: Q OS reboot tools
    { "reboot_to_hekate",    "ui/Main/PayloadIcon/icon_reboot_to_hekate.png",   nullptr },
    { "reboot_to_payload",   "ui/Main/PayloadIcon/icon_reboot_to_payload.png",  nullptr },
    // Lockpick_RCM
    { "lockpick_rcm",        "ui/Main/PayloadIcon/icon_lockpick_rcm.png",
                             "sdmc:/bootloader/res/lockpick_rcm.bmp"      },
    { "lockpick",            "ui/Main/PayloadIcon/icon_lockpick_rcm.png",
                             "sdmc:/bootloader/res/lockpick_rcm.bmp"      },
    // TegraExplorer
    { "tegraexplorer",       "ui/Main/PayloadIcon/icon_tegraexplorer.png",
                             "sdmc:/bootloader/res/tegraexplorer.bmp"     },
    // Quick-Reboot (various hyphen/underscore spellings)
    { "quick-reboot",        "ui/Main/PayloadIcon/icon_quick_reboot.png",       nullptr },
    { "quick_reboot",        "ui/Main/PayloadIcon/icon_quick_reboot.png",       nullptr },
    // biskeydump
    { "biskeydump",          "ui/Main/PayloadIcon/icon_biskeydump.png",         nullptr },
    // ChoiDujourNX (various spellings)
    { "choidujournx",        "ui/Main/PayloadIcon/icon_choi_dujour_nx.png",     nullptr },
    { "choi_dujour_nx",      "ui/Main/PayloadIcon/icon_choi_dujour_nx.png",     nullptr },
    { "choidujour",          "ui/Main/PayloadIcon/icon_choi_dujour_nx.png",     nullptr },
    // Daybreak
    { "daybreak",            "ui/Main/PayloadIcon/icon_daybreak.png",           nullptr },
    // ── Existing sdmc-only entries (no themed bundle icon yet) ───────────────
    // Atmosphère fusee / fusee-primary
    { "fusee",               nullptr, "sdmc:/bootloader/res/fusee.bmp"          },
    { "fusee-primary",       nullptr, "sdmc:/bootloader/res/fusee.bmp"          },
    { "fusee_primary",       nullptr, "sdmc:/bootloader/res/fusee.bmp"          },
    // ReiNX
    { "reinx",               nullptr, "sdmc:/bootloader/res/reinx.bmp"          },
    // SX OS loader
    { "sx",                  nullptr, "sdmc:/bootloader/res/sx.bmp"             },
    { "sx_loader",           nullptr, "sdmc:/bootloader/res/sx.bmp"             },
    // Memloader
    { "memloader",           nullptr, "sdmc:/bootloader/res/memloader.bmp"      },
    // DeepSea / Deepsea-Toolbox
    { "deepsea-toolbox",     nullptr, nullptr },
    { "deepsea_toolbox",     nullptr, nullptr },
    // Goldleaf
    { "goldleaf",            nullptr, nullptr },
    // Tinfoil
    { "tinfoil",             nullptr, nullptr },
    // DBI
    { "dbi",                 nullptr, nullptr },
    // Awoo Installer
    { "awoo",                nullptr, nullptr },
    // Atmosphere standalone  (not a payload but occasionally listed)
    { "atmosphere",          nullptr, nullptr },
};
static constexpr size_t kPayloadIconTableLen =
    sizeof(kPayloadIconTable) / sizeof(kPayloadIconTable[0]);

// Romfs generic fallback — returned when no stem match and no generic sdmc
// probe succeeds. Always present in a correctly packaged romfs.
static constexpr const char kGenericPayloadRomfsRel[] =
    "ui/Main/PayloadIcon/icon_payload_generic.png";

// Case-insensitive ASCII string comparison.  Returns true if a == b (ASCII).
static inline bool StemEq(const char *a, const char *b) {
    while (*a && *b) {
        if ((static_cast<unsigned char>(*a) | 0x20u) !=
            (static_cast<unsigned char>(*b) | 0x20u)) {
            return false;
        }
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

} // anonymous namespace (extension — StemEq + table live here)

std::string ResolvePayloadIcon(const char *payload_name) {
    if (!payload_name || payload_name[0] == '\0') {
        return std::string();
    }

    // ── 1. Derive stem by stripping known payload extensions ─────────────────
    std::string stem(payload_name);
    {
        static const char * const kExts[] = { ".bin", ".nro", ".kip1", ".kip", nullptr };
        for (int ei = 0; kExts[ei] != nullptr; ++ei) {
            const size_t ext_len = strlen(kExts[ei]);
            if (stem.size() > ext_len) {
                const size_t off = stem.size() - ext_len;
                bool match = true;
                for (size_t i = 0; i < ext_len && match; ++i) {
                    if ((stem[off + i] | 0x20) != (kExts[ei][i] | 0x20)) {
                        match = false;
                    }
                }
                if (match) { stem.erase(off); break; }
            }
        }
    }
    const char *s = stem.c_str();

    // ── 2. Table lookup ───────────────────────────────────────────────────────
    // Priority (v1.8.21):
    //   a. sdmc primary_path (FileExists) — creator-supplied art wins
    //   b. romfs_rel_path — themed bundle PNG (compile-time invariant,
    //                       no FileExists needed; loaded via POSIX IMG_Load)
    // If neither is set, fall through to generic sdmc probes.
    char buf[512];

    for (size_t ti = 0; ti < kPayloadIconTableLen; ++ti) {
        if (!StemEq(kPayloadIconTable[ti].stem, s)) {
            continue;
        }
        // Stem matched.
        // a. Probe sdmc primary_path first — honours creator-supplied art.
        if (kPayloadIconTable[ti].primary_path != nullptr) {
            if (FileExists(kPayloadIconTable[ti].primary_path)) {
                return std::string(kPayloadIconTable[ti].primary_path);
            }
        }
        // b. Remember the romfs bundle path; return it after sdmc missed.
        if (kPayloadIconTable[ti].romfs_rel_path != nullptr) {
            // Romfs files are compile-time invariants — no existence check.
            // Build the full romfs path and return it.
            return std::string(kRomfsDefaultPrefix) +
                   kPayloadIconTable[ti].romfs_rel_path;
        }
        // Stem matched but no romfs_rel_path and sdmc primary missed;
        // fall through to generic sdmc probes below.
        break;
    }

    // ── 3. Generic sdmc probes (apply to every unmatched or table-miss stem) ──
    // Priority a: sdmc:/bootloader/res/<stem>.bmp (Hekate export convention)
    snprintf(buf, sizeof(buf), "sdmc:/bootloader/res/%s.bmp", s);
    if (FileExists(buf)) { return std::string(buf); }

    // Priority b–d: sdmc:/bootloader/payloads/<stem>.{jpg,bmp,png}
    snprintf(buf, sizeof(buf), "sdmc:/bootloader/payloads/%s.jpg", s);
    if (FileExists(buf)) { return std::string(buf); }
    snprintf(buf, sizeof(buf), "sdmc:/bootloader/payloads/%s.bmp", s);
    if (FileExists(buf)) { return std::string(buf); }
    snprintf(buf, sizeof(buf), "sdmc:/bootloader/payloads/%s.png", s);
    if (FileExists(buf)) { return std::string(buf); }

    // Priority e: sdmc:/switch/<stem>/icon.jpg
    snprintf(buf, sizeof(buf), "sdmc:/switch/%s/icon.jpg", s);
    if (FileExists(buf)) { return std::string(buf); }

    // ── 4. Romfs generic sentinel — branded fallback for unknown payloads ─────
    // icon_payload_generic.png is always present in the built romfs.
    return std::string(kRomfsDefaultPrefix) + kGenericPayloadRomfsRel;
}

} // namespace ul::menu::qdesktop
