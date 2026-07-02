// overlay_FontCache.cpp — stbtt + pl:u glyph cache + DrawString.
//
// Implementation file is the SOLE site of STB_TRUETYPE_IMPLEMENTATION so
// stbtt's ~5000-line monolith only compiles once.  Header file in this
// namespace stays lightweight.

// stbtt's default allocators go through malloc; we let them route to the
// libnx heap that uSystem main bumped to 32MB.  Glyph cache for ASCII at
// 28pt is well under 1MB so this is comfortable.
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <ul/system/overlay/overlay_FontCache.hpp>
#include <ul/ul_Result.hpp>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ul::system::overlay {

namespace {

    // ── UTF-8 decoder ────────────────────────────────────────────────────
    // Returns next codepoint and advances p past the consumed bytes.
    // Returns 0 at end of string, U+FFFD on malformed input.
    u32 DecodeUtf8(const char *&p) {
        const u8 *up = reinterpret_cast<const u8*>(p);
        if (up[0] == 0) {
            return 0;
        }
        u32 cp;
        if (up[0] < 0x80) {
            cp = up[0];
            p += 1;
        } else if ((up[0] & 0xE0) == 0xC0) {
            cp = ((u32)(up[0] & 0x1F) << 6) | (u32)(up[1] & 0x3F);
            p += 2;
        } else if ((up[0] & 0xF0) == 0xE0) {
            cp = ((u32)(up[0] & 0x0F) << 12) | ((u32)(up[1] & 0x3F) << 6) | (u32)(up[2] & 0x3F);
            p += 3;
        } else if ((up[0] & 0xF8) == 0xF0) {
            cp = ((u32)(up[0] & 0x07) << 18) | ((u32)(up[1] & 0x3F) << 12)
               | ((u32)(up[2] & 0x3F) << 6)  | (u32)(up[3] & 0x3F);
            p += 4;
        } else {
            cp = 0xFFFD;
            p += 1;
        }
        return cp;
    }

    // ── CachedGlyph: pre-rasterized alpha bitmap + metrics ───────────────
    struct CachedGlyph {
        std::vector<u8> bitmap;  // gw * gh, single-channel 0..255
        s32 w       = 0;
        s32 h       = 0;
        s32 xoff    = 0;  // horizontal offset from cursor x
        s32 yoff    = 0;  // vertical offset from baseline (negative = above baseline)
        s32 advance = 0;  // horizontal advance to next glyph
    };

    // ── Impl: hidden state behind FontCache's void* impl_ ────────────────
    struct Impl {
        PlFontData       font_data    = {};
        stbtt_fontinfo   font         = {};
        bool             pl_initialized = false;
        // Key = (codepoint << 16) | pixel_height — supports up to 65535-px
        // glyphs (more than we'll ever need) and the full 16-bit codepoint
        // space common for Latin/CJK.  For codepoints above U+FFFF we'd
        // extend the key; not needed for chrome text.
        std::unordered_map<u64, CachedGlyph> cache;
    };

    // Get or rasterize a glyph.  Returns pointer into the cache map; valid
    // until the map is modified (never reordered/erased — append-only).
    CachedGlyph *GetCachedGlyph(Impl *impl, u32 codepoint, s32 pixel_height) {
        const u64 key = ((u64)codepoint << 16) | (u64)(u16)pixel_height;
        auto it = impl->cache.find(key);
        if (it != impl->cache.end()) {
            return &it->second;
        }

        CachedGlyph glyph;
        const float scale = stbtt_ScaleForPixelHeight(&impl->font, (float)pixel_height);

        int advance_w = 0;
        int lsb       = 0;
        stbtt_GetCodepointHMetrics(&impl->font, (int)codepoint, &advance_w, &lsb);
        glyph.advance = (s32)(advance_w * scale + 0.5f);

        unsigned char *bitmap = stbtt_GetCodepointBitmap(
            &impl->font, 0, scale, (int)codepoint,
            &glyph.w, &glyph.h, &glyph.xoff, &glyph.yoff);

        if (bitmap != nullptr && glyph.w > 0 && glyph.h > 0) {
            glyph.bitmap.assign(bitmap, bitmap + (size_t)glyph.w * (size_t)glyph.h);
            stbtt_FreeBitmap(bitmap, nullptr);
        } else if (bitmap != nullptr) {
            stbtt_FreeBitmap(bitmap, nullptr);
        }

        auto [iter, _inserted] = impl->cache.emplace(key, std::move(glyph));
        return &iter->second;
    }

}  // namespace

// ── FontCache ───────────────────────────────────────────────────────────

FontCache::FontCache() = default;
FontCache::~FontCache() { Finalize(); }

Result FontCache::Initialize() {
    if (ready_) {
        return ResultSuccess;
    }

    auto *impl = new Impl();
    impl_ = impl;

    // plInitialize: PlServiceType_User is the standard call for fonts.
    // System fallback isn't really needed but we try it if User somehow
    // fails (NPDM mismatch, fw quirk, etc.) so the overlay degrades
    // gracefully.
    Result rc = plInitialize(PlServiceType_User);
    UL_LOG_INFO("overlay/font: plInitialize(User) rc=0x%08X", rc);
    if (R_FAILED(rc)) {
        const Result fallback_rc = plInitialize(PlServiceType_System);
        UL_LOG_INFO("overlay/font: plInitialize(System) fallback rc=0x%08X", fallback_rc);
        if (R_FAILED(fallback_rc)) {
            delete impl;
            impl_ = nullptr;
            return fallback_rc;
        }
    }
    impl->pl_initialized = true;

    rc = plGetSharedFontByType(&impl->font_data, PlSharedFontType_Standard);
    UL_LOG_INFO("overlay/font: plGetSharedFontByType(Standard) rc=0x%08X size=%lu",
                rc, impl->font_data.size);
    if (R_FAILED(rc)) {
        Finalize();
        return rc;
    }

    const int offset = stbtt_GetFontOffsetForIndex(
        static_cast<const unsigned char*>(impl->font_data.address), 0);
    if (offset < 0) {
        UL_LOG_WARN("overlay/font: stbtt_GetFontOffsetForIndex=%d (font index 0 missing)", offset);
        Finalize();
        return MAKERESULT(Module_Libnx, 1);
    }

    if (!stbtt_InitFont(&impl->font,
                       static_cast<const unsigned char*>(impl->font_data.address),
                       offset)) {
        UL_LOG_WARN("overlay/font: stbtt_InitFont failed");
        Finalize();
        return MAKERESULT(Module_Libnx, 1);
    }

    UL_LOG_INFO("overlay/font: stbtt_InitFont ✓ ready");
    ready_ = true;
    return ResultSuccess;
}

void FontCache::Finalize() {
    if (impl_ == nullptr) {
        ready_ = false;
        return;
    }
    auto *impl = static_cast<Impl*>(impl_);
    if (impl->pl_initialized) {
        plExit();
    }
    delete impl;
    impl_  = nullptr;
    ready_ = false;
}

s32 FontCache::Ascent(s32 pixel_height) {
    if (!ready_ || pixel_height <= 0) return 0;
    auto *impl = static_cast<Impl*>(impl_);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&impl->font, &ascent, &descent, &line_gap);
    const float scale = stbtt_ScaleForPixelHeight(&impl->font, (float)pixel_height);
    return (s32)(ascent * scale + 0.5f);
}

s32 FontCache::MeasureString(s32 pixel_height, const char *utf8) {
    if (!ready_ || utf8 == nullptr || pixel_height <= 0) return 0;
    auto *impl = static_cast<Impl*>(impl_);

    s32 width = 0;
    const char *p = utf8;
    while (*p != '\0') {
        const u32 cp = DecodeUtf8(p);
        if (cp == 0 || cp == '\n') break;
        CachedGlyph *glyph = GetCachedGlyph(impl, cp, pixel_height);
        if (glyph != nullptr) {
            width += glyph->advance;
        }
    }
    return width;
}

s32 FontCache::DrawString(Renderer &renderer, s32 x, s32 y, s32 pixel_height,
                           const char *utf8, Color4444 color) {
    if (!ready_ || utf8 == nullptr || pixel_height <= 0) return x;
    auto *impl = static_cast<Impl*>(impl_);

    u8        *fb     = renderer.Fb();
    const u32  stride = renderer.StrideBytes();
    const s32  fb_w   = (s32)renderer.Width();
    const s32  fb_h   = (s32)renderer.Height();
    if (fb == nullptr) return x;

    // Split the color into 4-bit RGB (high 12 bits of u16) and 4-bit alpha
    // (low 4 bits).  We modulate alpha by the glyph's 8-bit coverage to
    // get anti-aliased edges, then pack back into RGBA_4444.
    const u32 color_rgb_12 = color.packed & 0xFFF0;
    const u32 color_alpha_4 = color.packed & 0x000F;

    s32 cursor = x;
    const char *p = utf8;
    while (*p != '\0') {
        const u32 cp = DecodeUtf8(p);
        if (cp == 0 || cp == '\n') break;

        CachedGlyph *glyph = GetCachedGlyph(impl, cp, pixel_height);
        if (glyph == nullptr) continue;

        if (!glyph->bitmap.empty()) {
            const s32 base_x = cursor + glyph->xoff;
            const s32 base_y = y      + glyph->yoff;

            for (s32 gy = 0; gy < glyph->h; ++gy) {
                const s32 fy = base_y + gy;
                if (fy < 0 || fy >= fb_h) continue;

                u16       *fb_row  = reinterpret_cast<u16*>(fb + (size_t)fy * stride);
                const u8  *src_row = &glyph->bitmap[(size_t)gy * (size_t)glyph->w];

                for (s32 gx = 0; gx < glyph->w; ++gx) {
                    const s32 fx = base_x + gx;
                    if (fx < 0 || fx >= fb_w) continue;

                    const u32 a8 = src_row[gx];
                    if (a8 == 0) continue;  // fully transparent — leave underlying pixel

                    // out_alpha (4 bits) = color_alpha (4 bits) × glyph_alpha (8 bits) / 255.
                    // For an opaque chrome color (alpha=15), a8=255 → 15 (opaque),
                    // a8=128 → 7 (half), a8=64 → 3 (quarter).  Gives soft AA edges.
                    const u32 out_alpha = (color_alpha_4 * a8) / 255;
                    if (out_alpha == 0) continue;
                    fb_row[fx] = (u16)(color_rgb_12 | (out_alpha & 0x000F));
                }
            }
        }

        cursor += glyph->advance;
    }
    return cursor;
}

// Process-wide singleton.  uSystem main initializes once after applet/sm/
// fs are ready; the render thread reads freely.
FontCache &SharedFontCache() {
    static FontCache instance;
    return instance;
}

}  // namespace ul::system::overlay
