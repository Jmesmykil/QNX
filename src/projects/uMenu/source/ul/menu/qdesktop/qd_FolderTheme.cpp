// qd_FolderTheme.cpp — Folder tile theme-pack registry implementation.
//
// 10 packs total:
//   0 "Glass"     — procedural: translucent rounded-glyph on cyan ring
//   1 "Neon"      — procedural: neon glyph with glowing border
//   2 "Minimal"   — procedural: flat solid icon on dark background
//   3 "Retro"     — procedural: pixel-grid icon, chunky 8x8 blocks
//   4 "Cards"     — romfs PNG: romfs/folder-themes/cards/<cat>.png
//   5 "Pastel"    — romfs PNG: romfs/folder-themes/pastel/<cat>.png
//   6 "Dark"      — romfs PNG: romfs/folder-themes/dark/<cat>.png
//   7 "Gradient"  — romfs PNG: romfs/folder-themes/gradient/<cat>.png
//   8 "Blueprint" — romfs PNG: romfs/folder-themes/blueprint/<cat>.png
//   9 "Pixel"     — romfs PNG: romfs/folder-themes/pixel/<cat>.png
//
// PNG packs fall back to pack 0 (Glass procedural) if the PNG file is absent.
//
// Persistence path: sdmc:/ulaunch/qos-folder-theme.toml
//   Format: one line "pack=N" (N is 0..9).

#include <ul/menu/qdesktop/qd_FolderTheme.hpp>
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/ul_Include.hpp>
#include <ul/ul_Result.hpp>  // UL_LOG_INFO / UL_LOG_WARN
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <sys/stat.h>        // mkdir(2)
#include <switch.h>          // fsdevCommitDevice
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

namespace ul::menu::qdesktop {

namespace {

// ── Persistence paths ─────────────────────────────────────────────────────────

static constexpr const char *THEME_PATH     = "sdmc:/ulaunch/qos-folder-theme.toml";
static constexpr const char *THEME_TMP_PATH = "sdmc:/ulaunch/qos-folder-theme.toml.tmp";
// v3.1.1: wallpaper persistence decoupled from folder-theme so the two ctx
// menu actions ("Change Wallpaper" and "Choose Folder Theme") can mutate
// independent concerns.
static constexpr const char *WALL_PATH      = "sdmc:/ulaunch/qos-wallpaper.toml";
static constexpr const char *WALL_TMP_PATH  = "sdmc:/ulaunch/qos-wallpaper.toml.tmp";

// ── Pack descriptor table ─────────────────────────────────────────────────────

struct PackDesc {
    const char *name;          ///< display name shown in settings
    const char *romfs_subdir;  ///< nullptr for procedural packs
};

static constexpr PackDesc kPacks[kFolderThemePackCount] = {
    { "Q OS",      nullptr       },  // canonical default — was "Glass" in v2.4.0
    { "Neon",      nullptr       },
    { "Minimal",   nullptr       },
    { "Retro",     nullptr       },
    { "Cards",     "cards"       },
    { "Pastel",    "pastel"      },
    { "Dark",      "dark"        },
    { "Gradient",  "gradient"    },
    { "Blueprint", "blueprint"   },
    { "Pixel",     "pixel"       },
};

// Category → file stem (used for romfs PNG lookup).
static constexpr const char *kCatStems[static_cast<size_t>(FolderThemeCat::Count)] = {
    "nxgames",   // NxGames
    "homebrew",  // Homebrew
    "system",    // System
    "payloads",  // Payloads
    "builtin",   // Builtin
    "custom",    // Custom
};

// Category → glyph letter (used for procedural packs).
static constexpr char kCatGlyphs[static_cast<size_t>(FolderThemeCat::Count)] = {
    'G', 'H', 'S', 'P', 'B', 'F',
};

// Category → RGB tint for procedural packs.
struct CatColor { u8 r, g, b; };
static constexpr CatColor kCatColors[static_cast<size_t>(FolderThemeCat::Count)] = {
    { 0x60, 0xA5, 0xFA }, // NxGames  — blue
    { 0x4A, 0xDE, 0x80 }, // Homebrew — green
    { 0xC0, 0x84, 0xFC }, // System   — purple
    { 0xE0, 0x78, 0x40 }, // Payloads — orange
    { 0xA7, 0x8B, 0xFA }, // Builtin  — lavender
    { 0x7D, 0xD3, 0xFC }, // Custom   — cyan
};

// ── Procedural draw helpers ───────────────────────────────────────────────────

static void FillRect(SDL_Renderer *r, s32 x, s32 y, s32 w, s32 h,
                     u8 rr, u8 gg, u8 bb, u8 aa) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
    const SDL_Rect rect { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

static void OutlineRect(SDL_Renderer *r, s32 x, s32 y, s32 w, s32 h,
                        u8 rr, u8 gg, u8 bb, u8 aa) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
    const SDL_Rect rect { x, y, w, h };
    SDL_RenderDrawRect(r, &rect);
}

// Draw a single ASCII character as a 5x7 pixel font (upscaled) using filled
// rectangles. Only letters and digits used as folder glyphs are encoded.
// Each glyph is a 5-wide x 7-tall bit pattern (top row = bit 34, LSB = bit 0).
static constexpr u64 GlyphBits(char c) {
    // 5x7 pixel bitmaps for G H S P B F E C (folder glyphs).
    // Bit layout: row-major, bit 34 = top-left pixel, bit 0 = bottom-right.
    switch (c) {
        case 'G': return 0b01110100001000011110100011110ull;
        case 'H': return 0b10001100011111110001100011000ull;  // 0x11111 should be full
        case 'S': return 0b01111100001111100001000011110ull;
        case 'P': return 0b11110100011111010000100001000ull;
        case 'B': return 0b11110100011111010001100011110ull;
        case 'F': return 0b11111100001111010000100001000ull;
        case 'E': return 0b11111100001111010000100011111ull;
        case 'C': return 0b01110100001000010000100010110ull; // close enough
        default:  return 0b00000000000000000000000000000ull;
    }
}

static void DrawGlyph(SDL_Renderer *r, char c, s32 cx, s32 cy, s32 cell_sz,
                      u8 rr, u8 gg, u8 bb) {
    // cell_sz pixels per bit-cell; glyph is 5 cols x 7 rows.
    const s32 gw = 5 * cell_sz;
    const s32 gh = 7 * cell_sz;
    const s32 x0 = cx - gw / 2;
    const s32 y0 = cy - gh / 2;
    const u64 bits = GlyphBits(c);
    for (s32 row = 0; row < 7; ++row) {
        for (s32 col = 0; col < 5; ++col) {
            const u32 bit_idx = (u32)(6 - row) * 5u + (u32)(4 - col);
            if ((bits >> bit_idx) & 1u) {
                FillRect(r,
                         x0 + col * cell_sz,
                         y0 + row * cell_sz,
                         cell_sz, cell_sz,
                         rr, gg, bb, 0xFF);
            }
        }
    }
}

// ── Pack 0: Glass ─────────────────────────────────────────────────────────────
// Translucent dark tile + cyan ring + white glyph.
static void DrawPack0Glass(SDL_Renderer *r, FolderThemeCat cat,
                           s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    // Dark translucent background.
    FillRect(r, x, y, w, h, 0x10, 0x10, 0x28, 0xD0);
    // Cyan/category ring (2px inset on each side for the ring, 2px wide).
    OutlineRect(r, x + 2, y + 2, w - 4, h - 4, col.r, col.g, col.b, 0xD0);
    OutlineRect(r, x + 3, y + 3, w - 6, h - 6, col.r, col.g, col.b, 0x60);
    // Glyph centred; cell size = h/14 clamped [2, 8].
    const s32 cell = std::max(2, std::min(8, h / 14));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, 0xE0, 0xE0, 0xF0);
}

// ── Pack 1: Neon ─────────────────────────────────────────────────────────────
// Black tile + bright neon border + colored glyph.
static void DrawPack1Neon(SDL_Renderer *r, FolderThemeCat cat,
                          s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    FillRect(r, x, y, w, h, 0x00, 0x00, 0x00, 0xFF);
    // Neon glow: 4 concentric outlines at decreasing alpha.
    for (s32 inset = 0; inset < 4; ++inset) {
        OutlineRect(r, x + inset, y + inset, w - inset * 2, h - inset * 2,
                    col.r, col.g, col.b, (u8)(0xFF - inset * 50));
    }
    const s32 cell = std::max(2, std::min(8, h / 12));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, col.r, col.g, col.b);
}

// ── Pack 2: Minimal ──────────────────────────────────────────────────────────
// Flat solid category-colored tile, white glyph.
static void DrawPack2Minimal(SDL_Renderer *r, FolderThemeCat cat,
                             s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    FillRect(r, x, y, w, h, col.r, col.g, col.b, 0xFF);
    const s32 cell = std::max(2, std::min(8, h / 12));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, 0xFF, 0xFF, 0xFF);
}

// ── Pack 3: Retro ────────────────────────────────────────────────────────────
// Dark tile, big 8x8 pixel-art glyph (2x cell size), subtle grid background.
static void DrawPack3Retro(SDL_Renderer *r, FolderThemeCat cat,
                           s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    // Dark tile.
    FillRect(r, x, y, w, h, 0x08, 0x08, 0x10, 0xFF);

    // Subtle 8px grid lines.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x20, 0x20, 0x40, 0x50);
    for (s32 gx = x; gx < x + w; gx += 8) {
        SDL_RenderDrawLine(r, gx, y, gx, y + h - 1);
    }
    for (s32 gy = y; gy < y + h; gy += 8) {
        SDL_RenderDrawLine(r, x, gy, x + w - 1, gy);
    }

    // Large chunky glyph.
    const s32 cell = std::max(3, std::min(10, h / 10));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, col.r, col.g, col.b);
}

// ── PNG pack draw ─────────────────────────────────────────────────────────────
// Tries to load and blit the category PNG. Returns true on success.
// v2.4.0: PNGs are an optional override on top of the procedural pack.
// If no PNG ships, the procedural fallback for that pack (4-9) handles render.
//
// v3.2.1 (W4-LEAKS P0 #1):
// Each render frame called IMG_LoadTexture (romfs open + PNG decode + GPU
// upload) followed by SDL_DestroyTexture for every folder tile. On a typical
// dock with 6 folders that is ~360 GPU alloc/destroy cycles per second at
// 60 fps.  Net byte count was zero but NVN's allocator does not compact, so
// the GPU heap fragmented continuously — the primary contributor to the
// "massive leaks everywhere" symptom on HW.
//
// Fix: lazy texture cache keyed by (pack_idx, cat).  Bounded by design at
// kFolderThemePackCount × FolderThemeCat::Count = 60 textures, ~3.8 MB at
// 128×128 RGBA, with ~6 packs actually populated (4–9).  Textures live for
// the process lifetime — the SDL_Renderer outlives every theme switch so
// they remain valid.  A sentinel value `kPngPackMissSentinel` records "we
// tried, the file was absent" so we don't re-attempt the load every frame.

static constexpr size_t kPngPackTotalSlots =
    kFolderThemePackCount * static_cast<size_t>(FolderThemeCat::Count);

// Distinct from nullptr so we can tell "not yet attempted" (nullptr) apart
// from "we attempted and the file was missing or decode failed".
static SDL_Texture *const kPngPackMissSentinel = reinterpret_cast<SDL_Texture *>(uintptr_t{1});

// Cache slot: nullptr = not loaded yet, kPngPackMissSentinel = miss recorded,
// any other value = valid texture.
static SDL_Texture *g_png_pack_cache[kPngPackTotalSlots] = {};
// W6-LEDGER: per-slot ledger handles (0 = not tracked).
static uint64_t     g_png_pack_ledger[kPngPackTotalSlots] = {};

static inline size_t PngPackCacheIndex(size_t pack_idx, FolderThemeCat cat) {
    return pack_idx * static_cast<size_t>(FolderThemeCat::Count)
         + static_cast<size_t>(cat);
}

static bool DrawPngPack(SDL_Renderer *r, size_t pack_idx, const char *subdir,
                        FolderThemeCat cat,
                        s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    if (ci >= static_cast<size_t>(FolderThemeCat::Count)) return false;
    if (pack_idx >= kFolderThemePackCount) return false;

    const size_t slot = PngPackCacheIndex(pack_idx, cat);
    SDL_Texture *tex = g_png_pack_cache[slot];

    if (tex == kPngPackMissSentinel) {
        // We already tried this (pack, cat) and the file is absent — let the
        // caller fall through to the procedural pack.
        return false;
    }

    if (tex == nullptr) {
        char path[256];
        snprintf(path, sizeof(path), "romfs:/folder-themes/%s/%s.png",
                 subdir, kCatStems[ci]);
        tex = IMG_LoadTexture(r, path);
        if (tex == nullptr) {
            g_png_pack_cache[slot] = kPngPackMissSentinel;
            return false;
        }
        g_png_pack_cache[slot] = tex;
        // W6-LEDGER: track newly loaded PNG-pack texture.
        {
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            const size_t tex_bytes = (tw > 0 && th > 0)
                ? static_cast<size_t>(tw) * static_cast<size_t>(th) * 4u : 0u;
            char ledger_tag[32];
            snprintf(ledger_tag, sizeof(ledger_tag), "pngpack:%zu/%zu",
                     pack_idx, ci);
            g_png_pack_ledger[slot] = UL_LEDGER_TRACK(
                ::ul::menu::qdesktop::QdResKind::Texture, ledger_tag, tex_bytes);
        }
    }

    const SDL_Rect dst { x, y, w, h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
    return true;
}

// ── Pack 4: Cards ────────────────────────────────────────────────────────────
// Stack of 3 layered cards in category color + accent border + centered glyph.
static void DrawPack4Cards(SDL_Renderer *r, FolderThemeCat cat,
                           s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    // 3 layered cards, each smaller and offset down-right.
    for (s32 i = 2; i >= 0; --i) {
        const s32 inset = i * 4;
        const u8 alpha = (i == 0) ? 0xFF : (u8)(0xFF - i * 0x40);
        FillRect(r, x + inset, y + inset, w - inset * 2, h - inset * 2,
                 (u8)(col.r * 2 / 3), (u8)(col.g * 2 / 3), (u8)(col.b * 2 / 3), alpha);
        OutlineRect(r, x + inset, y + inset, w - inset * 2, h - inset * 2,
                    col.r, col.g, col.b, alpha);
    }

    const s32 cell = std::max(2, std::min(8, h / 14));
    DrawGlyph(r, glyph, x + w / 2 - 2, y + h / 2 - 2, cell, 0xFF, 0xFF, 0xFF);
}

// ── Pack 5: Pastel ───────────────────────────────────────────────────────────
// Soft pastel tile + concentric circles + soft glyph.
static void DrawPack5Pastel(SDL_Renderer *r, FolderThemeCat cat,
                            s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    // Soft pastel background — pastel-shifted category color.
    const u8 pr = (u8)((col.r + 0xFF) / 2);
    const u8 pg = (u8)((col.g + 0xFF) / 2);
    const u8 pb = (u8)((col.b + 0xFF) / 2);
    FillRect(r, x, y, w, h, pr, pg, pb, 0xE0);

    // Concentric soft outlines (decreasing alpha).
    for (s32 i = 0; i < 4; ++i) {
        const s32 inset = i * 3;
        OutlineRect(r, x + inset, y + inset, w - inset * 2, h - inset * 2,
                    0xFF, 0xFF, 0xFF, (u8)(0x90 - i * 0x20));
    }

    const s32 cell = std::max(2, std::min(8, h / 12));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, col.r / 2, col.g / 2, col.b / 2);
}

// ── Pack 6: Dark ─────────────────────────────────────────────────────────────
// Pure black tile + 1-px white border + glyph in category color (muted).
static void DrawPack6Dark(SDL_Renderer *r, FolderThemeCat cat,
                          s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    FillRect(r, x, y, w, h, 0x00, 0x00, 0x00, 0xFF);
    OutlineRect(r, x, y, w, h, 0x40, 0x40, 0x44, 0xFF);
    const s32 cell = std::max(2, std::min(8, h / 14));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell,
              (u8)(col.r * 3 / 4), (u8)(col.g * 3 / 4), (u8)(col.b * 3 / 4));
}

// ── Pack 7: Gradient ─────────────────────────────────────────────────────────
// Vertical gradient from category color (top) to dark (bottom) + light glyph.
static void DrawPack7Gradient(SDL_Renderer *r, FolderThemeCat cat,
                              s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    // Vertical gradient — 1-pixel rows interpolating top→bottom.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (s32 dy = 0; dy < h; ++dy) {
        const s32 t = 256 - (dy * 256 / (h > 0 ? h : 1));
        const u8 rr = (u8)((u32)col.r * t / 256);
        const u8 gg = (u8)((u32)col.g * t / 256);
        const u8 bb = (u8)((u32)col.b * t / 256);
        SDL_SetRenderDrawColor(r, rr, gg, bb, 0xFF);
        const SDL_Rect row { x, y + dy, w, 1 };
        SDL_RenderFillRect(r, &row);
    }
    OutlineRect(r, x, y, w, h, col.r, col.g, col.b, 0xFF);
    const s32 cell = std::max(2, std::min(8, h / 12));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, 0xFF, 0xFF, 0xFF);
}

// ── Pack 8: Blueprint ────────────────────────────────────────────────────────
// Deep blueprint blue + 8-px white grid + technical glyph.
static void DrawPack8Blueprint(SDL_Renderer *r, FolderThemeCat cat,
                               s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const char glyph = kCatGlyphs[ci];

    FillRect(r, x, y, w, h, 0x05, 0x18, 0x32, 0xFF);

    // 8-px grid (faint cyan).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x7A, 0xE0, 0xFF, 0x50);
    for (s32 gx = x; gx < x + w; gx += 8) {
        SDL_RenderDrawLine(r, gx, y, gx, y + h - 1);
    }
    for (s32 gy = y; gy < y + h; gy += 8) {
        SDL_RenderDrawLine(r, x, gy, x + w - 1, gy);
    }
    // 1-px outer border.
    OutlineRect(r, x, y, w, h, 0xA8, 0xE8, 0xFF, 0xFF);

    const s32 cell = std::max(2, std::min(8, h / 12));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, 0xE0, 0xF0, 0xFF);
}

// ── Pack 9: Pixel ────────────────────────────────────────────────────────────
// 8-bit pixel art aesthetic: bold 4-px checkerboard background + chunky glyph.
static void DrawPack9Pixel(SDL_Renderer *r, FolderThemeCat cat,
                           s32 x, s32 y, s32 w, s32 h) {
    const size_t ci = static_cast<size_t>(cat);
    const CatColor &col = kCatColors[ci];
    const char glyph = kCatGlyphs[ci];

    // Dark base.
    FillRect(r, x, y, w, h, 0x00, 0x00, 0x18, 0xFF);

    // 4-px checkerboard in category color.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (s32 dy = 0; dy < h; dy += 4) {
        for (s32 dx = 0; dx < w; dx += 4) {
            const bool on = (((dx / 4) + (dy / 4)) & 1) == 0;
            if (on) {
                SDL_SetRenderDrawColor(r,
                    (u8)(col.r * 2 / 3),
                    (u8)(col.g * 2 / 3),
                    (u8)(col.b * 2 / 3),
                    0xFF);
                const SDL_Rect rect { x + dx, y + dy, 4, 4 };
                SDL_RenderFillRect(r, &rect);
            }
        }
    }
    // Chunky 2-px outer border in NES yellow.
    OutlineRect(r, x,     y,     w,     h,     0xFF, 0xCC, 0x00, 0xFF);
    OutlineRect(r, x + 1, y + 1, w - 2, h - 2, 0xFF, 0xCC, 0x00, 0xFF);

    // Big chunky glyph in NES green.
    const s32 cell = std::max(3, std::min(10, h / 10));
    DrawGlyph(r, glyph, x + w / 2, y + h / 2, cell, 0x00, 0xE8, 0x00);
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

const char *FolderThemePackName(size_t pack_idx) {
    if (pack_idx >= kFolderThemePackCount) pack_idx = 0;
    return kPacks[pack_idx].name;
}

void DrawFolderThemeIcon(SDL_Renderer *r, size_t pack_idx, FolderThemeCat cat,
                         s32 x, s32 y, s32 w, s32 h) {
    if (pack_idx >= kFolderThemePackCount) pack_idx = 0;
    if (w <= 0 || h <= 0) return;

    const PackDesc &desc = kPacks[pack_idx];

    // PNG packs (4–9): if a romfs PNG ships for this pack/category, prefer it.
    // Otherwise fall through to the matching procedural draw — v2.4.0 wires
    // packs 4-9 as procedural-by-default so the in-binary build always
    // renders distinct visuals for all 10 packs.
    if (desc.romfs_subdir != nullptr) {
        if (DrawPngPack(r, pack_idx, desc.romfs_subdir, cat, x, y, w, h)) {
            return;
        }
        // Fall through to procedural pack matching this index.
    }

    // Procedural packs (0–9 — all 10 implemented as of v2.4.0).
    switch (pack_idx) {
        case 0: DrawPack0Glass    (r, cat, x, y, w, h); break;
        case 1: DrawPack1Neon     (r, cat, x, y, w, h); break;
        case 2: DrawPack2Minimal  (r, cat, x, y, w, h); break;
        case 3: DrawPack3Retro    (r, cat, x, y, w, h); break;
        case 4: DrawPack4Cards    (r, cat, x, y, w, h); break;
        case 5: DrawPack5Pastel   (r, cat, x, y, w, h); break;
        case 6: DrawPack6Dark     (r, cat, x, y, w, h); break;
        case 7: DrawPack7Gradient (r, cat, x, y, w, h); break;
        case 8: DrawPack8Blueprint(r, cat, x, y, w, h); break;
        case 9: DrawPack9Pixel    (r, cat, x, y, w, h); break;
        default: DrawPack0Glass   (r, cat, x, y, w, h); break;
    }
}

size_t LoadFolderThemePack() {
    FILE *f = fopen(THEME_PATH, "r");
    if (f == nullptr) {
        UL_LOG_INFO("qdesktop: LoadFolderThemePack: '%s' absent (errno=%d) — defaulting pack=0",
                    THEME_PATH, errno);
        return 0;
    }

    size_t pack = 0;
    char line[64] = {};
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "pack=", 5) == 0) {
            char *end = nullptr;
            const long v = strtol(line + 5, &end, 10);
            if (end != line + 5 && v >= 0 && (size_t)v < kFolderThemePackCount) {
                pack = (size_t)v;
            }
            break;
        }
    }
    fclose(f);
    UL_LOG_INFO("qdesktop: LoadFolderThemePack: pack=%zu loaded from %s", pack, THEME_PATH);
    return pack;
}

void SaveFolderThemePack(size_t pack_idx) {
    if (pack_idx >= kFolderThemePackCount) pack_idx = 0;

    // v2.5.0 — match the SaveFavorites pattern exactly. The previous version
    // did fopen+fprintf+fclose+rename and lost the write across RestartMenu
    // because libnx buffers SD writes until fsdevCommitDevice is called.
    // Ensure parent dir, write tmp, atomic rename, then COMMIT.
    mkdir("sdmc:/ulaunch", 0777);  // idempotent

    FILE *f = fopen(THEME_TMP_PATH, "w");
    if (f == nullptr) {
        UL_LOG_WARN("qdesktop: SaveFolderThemePack: fopen(tmp) failed errno=%d (path=%s)",
                    errno, THEME_TMP_PATH);
        return;
    }
    fprintf(f, "pack=%zu\n", pack_idx);
    fclose(f);

    // v3.7 FIX: Switch's FsService RenameFile returns errno=17 (EEXIST) when the
    // destination already exists — POSIX overwrite semantics do NOT hold here, so
    // the rename (and thus EVERY theme save after the first) silently failed and
    // the pack reverted on restart.  Remove the canonical file first (ENOENT =
    // first-write, expected) — exactly the SaveFavorites pattern this claims to
    // follow.
    if (remove(THEME_PATH) != 0 && errno != ENOENT) {
        UL_LOG_WARN("qdesktop: SaveFolderThemePack: remove(canonical) failed errno=%d", errno);
    }
    if (rename(THEME_TMP_PATH, THEME_PATH) != 0) {
        UL_LOG_WARN("qdesktop: SaveFolderThemePack: rename failed errno=%d", errno);
        return;
    }

    // Critical: flush the SD-card backing store. Without this, libnx keeps the
    // write in its in-memory FAT cache and the rename only takes effect
    // in-process. When the menu process terminates (FadeOutToNonLibraryApplet
    // + RestartMenu), the buffered write is lost and the next boot loads
    // pack=0 (Q OS default). Same fix as qd_DesktopIcons::SaveFavorites.
    const Result commit_rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(commit_rc)) {
        UL_LOG_WARN("qdesktop: SaveFolderThemePack: fsdevCommitDevice rc=0x%x", commit_rc);
    }
    UL_LOG_INFO("qdesktop: SaveFolderThemePack: pack=%zu written + committed to %s",
                pack_idx, THEME_PATH);
}

// v3.1.1 — Wallpaper-pack persistence (decoupled from folder-theme).
//
// Same Load/Save shape as the folder-theme version above.  Distinct file so
// the wallpaper choice and the palette+folder-theme choice can drift apart
// from each other.  When the file is absent (e.g. on first boot after the
// v3.1.1 update), Load returns the existing folder-theme pack so the user's
// prior unified selection is preserved.

size_t LoadActiveWallpaperPack() {
    FILE *f = fopen(WALL_PATH, "r");
    if (f == nullptr) {
        const size_t fallback = LoadFolderThemePack();
        UL_LOG_INFO("qdesktop: LoadActiveWallpaperPack: '%s' absent — falling back to folder-theme pack=%zu",
                    WALL_PATH, fallback);
        return fallback;
    }
    size_t pack = 0;
    char line[64] = {};
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "pack=", 5) == 0) {
            char *end = nullptr;
            const long v = strtol(line + 5, &end, 10);
            if (end != line + 5 && v >= 0 && (size_t)v < kFolderThemePackCount) {
                pack = (size_t)v;
            }
            break;
        }
    }
    fclose(f);
    UL_LOG_INFO("qdesktop: LoadActiveWallpaperPack: pack=%zu loaded from %s", pack, WALL_PATH);
    return pack;
}

void SaveActiveWallpaperPack(size_t pack_idx) {
    if (pack_idx >= kFolderThemePackCount) pack_idx = 0;
    mkdir("sdmc:/ulaunch", 0777);
    FILE *f = fopen(WALL_TMP_PATH, "w");
    if (f == nullptr) {
        UL_LOG_WARN("qdesktop: SaveActiveWallpaperPack: fopen(tmp) failed errno=%d (path=%s)",
                    errno, WALL_TMP_PATH);
        return;
    }
    fprintf(f, "pack=%zu\n", pack_idx);
    fclose(f);
    // v3.7 FIX: see SaveFolderThemePack — Switch rename() can't overwrite an
    // existing file (errno=17 EEXIST), which silently broke every wallpaper save
    // after the first.  Remove the canonical file first (ENOENT = first-write).
    if (remove(WALL_PATH) != 0 && errno != ENOENT) {
        UL_LOG_WARN("qdesktop: SaveActiveWallpaperPack: remove(canonical) failed errno=%d", errno);
    }
    if (rename(WALL_TMP_PATH, WALL_PATH) != 0) {
        UL_LOG_WARN("qdesktop: SaveActiveWallpaperPack: rename failed errno=%d", errno);
        return;
    }
    const Result commit_rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(commit_rc)) {
        UL_LOG_WARN("qdesktop: SaveActiveWallpaperPack: fsdevCommitDevice rc=0x%x", commit_rc);
    }
    UL_LOG_INFO("qdesktop: SaveActiveWallpaperPack: pack=%zu written + committed to %s",
                pack_idx, WALL_PATH);
}

// W5-TRANSITIONS #2: PNG pack cache invalidation.
// Destroys real texture slots and resets them to nullptr so the next
// DrawFolderThemeIcon call re-loads from the newly active pack.
// Slots holding kPngPackMissSentinel (failed load from the OLD pack) are
// also cleared so a new pack that DOES have the PNG will pick it up.
void InvalidatePngPackCache() {
    for (size_t i = 0; i < kPngPackTotalSlots; ++i) {
        if (g_png_pack_cache[i] != nullptr &&
            g_png_pack_cache[i] != kPngPackMissSentinel) {
            // W6-LEDGER: untrack before destroying.
            UL_LEDGER_UNTRACK(g_png_pack_ledger[i]);
            SDL_DestroyTexture(g_png_pack_cache[i]);
        }
        g_png_pack_cache[i] = nullptr;
        g_png_pack_ledger[i] = 0;
    }
}

} // namespace ul::menu::qdesktop
