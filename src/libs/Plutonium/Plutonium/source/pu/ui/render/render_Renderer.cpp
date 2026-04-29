#include <pu/ui/render/render_Renderer.hpp>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <tuple>

namespace pu::ui::render {

    namespace {

        // Global rendering vars
        sdl2::Renderer g_Renderer = nullptr;
        sdl2::Window g_Window = nullptr;
        sdl2::Surface g_WindowSurface = nullptr;

        // Global font object
        std::vector<std::pair<std::string, std::shared_ptr<ttf::Font>>> g_FontTable;

        inline bool ExistsFont(const std::string &font_name) {
            for(const auto &[name, font]: g_FontTable) {
                if(name == font_name) {
                    return true;
                }
            }

            return false;
        }

        // -----------------------------------------------------------------------
        // Task A — cross-frame LRU text-texture cache (v1.8.5 recalibration)
        //
        // History:
        //   v1.8 shipped a 512-entry LRU cache that evicted mid-frame:
        //   TextCacheInsert called SDL_DestroyTexture on the LRU victim while
        //   SDL's render command buffer still held a pointer to it from an
        //   SDL_RenderCopyEx earlier in the same frame.  This caused use-after-
        //   free blits → text disappearance (B44/B46/B47 root cause).
        //
        //   v1.8.1 (Fix C) reduced the cap to 64 entries and flushed the cache
        //   at every frame boundary to prevent dangling pointers within a frame.
        //   That solved mid-frame eviction but introduced cross-frame eviction:
        //   callers (folder sheets, monitor layout, file manager sidebar) hold
        //   SDL_Texture* returned by RenderText() as member fields across frames.
        //   With only 64 entries, normal navigation displaces those pointers
        //   from the cache before the next frame renders → blank text (B44/B46/
        //   B47).
        //
        //   v1.8.3 introduced the deferred-destroy queue: evicted textures are
        //   placed on g_text_cache_deferred_destroy and destroyed only at the
        //   START of the next frame (InitializeRender), after SDL_RenderPresent.
        //   This closed mid-frame eviction completely.
        //
        //   v1.8.4 introduced IsCacheOwnedTexture / g_text_cache_owned_pointers:
        //   DeleteTexture() in render_SDL2.cpp checks this set before calling
        //   SDL_DestroyTexture, preventing double-free of cache-owned textures.
        //
        // v1.8.5 — raise cap to 512 entries (closes B44/B46/B47):
        //   With deferred-destroy (v1.8.3) and cache-aware DeleteTexture (v1.8.4)
        //   both in place, the original v1.8.1 concern (mid-frame eviction) is
        //   fully closed.  The only remaining problem with the 64-entry cap is
        //   cross-frame eviction: the typical uMenu working set (folder open/
        //   close cycle, monitor screen, file manager sidebar) spans ~100–200
        //   distinct text strings across adjacent frames.  A 512-entry cap covers
        //   this working set with margin, eliminating eviction during normal
        //   navigation.
        //
        //   512 entries; mid-frame eviction is impossible because LRU eviction
        //   queues pointers to deferred-destroy and drains them at frame START,
        //   after SDL_RenderPresent.  Cross-frame caller pointer holds are safe
        //   at this size for typical uMenu working set.
        //
        // Memory budget at 512 entries:
        //   Typical text texture: label string rendered at UI font size, usually
        //   ≤400 px wide × ≤48 px tall at 4 bytes/pixel → ~75 KB worst case,
        //   ~10–20 KB typical.  512 × 20 KB ≈ 10 MB peak, well within the
        //   Switch's 4 GB LPDDR4 headroom and far below the prior 512-entry
        //   v1.8 cache which hit the same bound without the safety infrastructure.
        //   Audio-decode buffers (~1–2 MB each) are unaffected at this level.
        // -----------------------------------------------------------------------

        constexpr u32 TextCacheMaxEntries = 512;

        struct TextCacheKey {
            std::string font_name;
            std::string text;
            u8  r, g, b, a;
            u32 max_width;
            u32 max_height;

            bool operator==(const TextCacheKey &o) const {
                return font_name == o.font_name
                    && text      == o.text
                    && r         == o.r
                    && g         == o.g
                    && b         == o.b
                    && a         == o.a
                    && max_width == o.max_width
                    && max_height== o.max_height;
            }
        };

        struct TextCacheKeyHash {
            std::size_t operator()(const TextCacheKey &k) const noexcept {
                // FNV-1a mix over the fields.
                std::size_t h = 14695981039346656037ULL;
                auto mix = [&](const void *data, std::size_t len) {
                    const auto *p = reinterpret_cast<const unsigned char*>(data);
                    for(std::size_t i = 0; i < len; i++) {
                        h ^= p[i];
                        h *= 1099511628211ULL;
                    }
                };
                mix(k.font_name.data(), k.font_name.size());
                mix(k.text.data(), k.text.size());
                const u8 rgba[4] = { k.r, k.g, k.b, k.a };
                mix(rgba, 4);
                mix(&k.max_width,  sizeof(k.max_width));
                mix(&k.max_height, sizeof(k.max_height));
                return h;
            }
        };

        // Access-order list: front = most-recently-used.
        using TextCacheList = std::list<TextCacheKey>;

        struct TextCacheEntry {
            sdl2::Texture  tex;
            TextCacheList::iterator list_it;  // points into g_TextCacheOrder
        };

        std::unordered_map<TextCacheKey, TextCacheEntry, TextCacheKeyHash> g_TextCache;
        TextCacheList g_TextCacheOrder;

        // -----------------------------------------------------------------------
        // Cache-owned pointer set (v1.8.4 B42 fix)
        //
        // Every SDL_Texture* that is live inside g_TextCache is also tracked here.
        // DeleteTexture() (render_SDL2.cpp) checks this set before calling
        // SDL_DestroyTexture.  If the pointer is cache-owned, the caller must not
        // destroy it — the cache manages the lifetime (via the deferred-destroy
        // queue drained at the next InitializeRender() frame boundary).
        //
        // Insert/erase points:
        //   • TextCacheInsert: add the new texture on every successful insert.
        //   • TextCacheInsert eviction path: erase the victim BEFORE pushing to
        //     g_text_cache_deferred_destroy (so the deferred queue never holds a
        //     pointer that is also in the owned set).
        //   • TextCacheClear: cleared after all textures are destroyed (shutdown).
        // -----------------------------------------------------------------------
        std::unordered_set<SDL_Texture*> g_text_cache_owned_pointers;

        // -----------------------------------------------------------------------
        // Deferred-destroy queue (v1.8.3 cache-contract restoration)
        //
        // v1.7 cache contract: callers MAY hold the SDL_Texture* returned by
        // RenderText() across frames.  The cache owns the texture lifetime.
        // Evicted entries are moved here rather than destroyed immediately; the
        // queue is drained at the START of the next InitializeRender() call
        // (frame boundary), guaranteeing no mid-frame or cross-frame destruction
        // of a pointer a caller might still hold.
        //
        // Safety invariant: SDL_DestroyTexture is only ever called from
        // (a) the deferred queue drain at the head of InitializeRender(), and
        // (b) TextCacheClear() which is called only from Finalize() (shutdown).
        // Neither path fires between InitializeRender() and FinalizeRender() of
        // the same frame.
        // -----------------------------------------------------------------------
        std::vector<SDL_Texture*> g_text_cache_deferred_destroy;

        // Look up a cached texture; on hit, move to front (MRU).
        // Returns nullptr on miss.
        sdl2::Texture TextCacheLookup(const TextCacheKey &key) {
            auto it = g_TextCache.find(key);
            if(it == g_TextCache.end()) {
                return nullptr;
            }
            // Move to MRU position.
            g_TextCacheOrder.splice(g_TextCacheOrder.begin(), g_TextCacheOrder, it->second.list_it);
            return it->second.tex;
        }

        // -----------------------------------------------------------------------
        // Cache contract (v1.8.3):
        //   Callers MAY hold the SDL_Texture* returned by RenderText() across
        //   frames.  LRU eviction is deferred to the start of the next
        //   InitializeRender() call (frame boundary), guaranteeing no mid-frame
        //   texture destruction.  The cache survives across frames unless its
        //   capacity (TextCacheMaxEntries) is exceeded; evicted-but-not-yet-
        //   destroyed pointers may briefly dangle after eviction but are cleaned
        //   up at the safe frame boundary before any new rendering begins.
        //   Callers MUST NOT call SDL_DestroyTexture on a pointer returned by
        //   RenderText() — the cache owns the lifetime.
        // -----------------------------------------------------------------------

        // Insert a new entry. If the cache is full, evict the LRU (back of list).
        // Evicted textures are placed in g_text_cache_deferred_destroy rather than
        // destroyed immediately, preserving the cross-frame pointer validity
        // contract documented above.
        void TextCacheInsert(const TextCacheKey &key, sdl2::Texture tex) {
            // Evict LRU until under the cap — deferred, NOT immediate destroy.
            while(g_TextCache.size() >= TextCacheMaxEntries && !g_TextCacheOrder.empty()) {
                const auto &lru_key = g_TextCacheOrder.back();
                auto victim = g_TextCache.find(lru_key);
                if(victim != g_TextCache.end()) {
                    // Remove from owned-pointer set BEFORE the deferred queue so
                    // the two sets are never simultaneously inconsistent.
                    if(victim->second.tex != nullptr) {
                        g_text_cache_owned_pointers.erase(victim->second.tex);
                        // Push to deferred queue; drain happens at next frame boundary.
                        g_text_cache_deferred_destroy.push_back(victim->second.tex);
                    }
                    g_TextCache.erase(victim);
                }
                g_TextCacheOrder.pop_back();
            }
            // Insert at MRU position and register the pointer as cache-owned.
            g_TextCacheOrder.push_front(key);
            g_TextCache.emplace(key, TextCacheEntry{ tex, g_TextCacheOrder.begin() });
            if(tex != nullptr) {
                g_text_cache_owned_pointers.insert(tex);
            }
        }

        // Destroy every cached texture and clear the bookkeeping structures.
        // Called ONLY from Renderer::Finalize() before SDL_DestroyRenderer().
        // NOT called per-frame — the deferred-destroy queue handles frame-boundary
        // cleanup instead.  SDL_DestroyTexture requires the renderer to still be
        // alive; Finalize() guarantees that by calling this before the renderer
        // is destroyed.
        void TextCacheClear() {
            for(auto &[key, entry] : g_TextCache) {
                if(entry.tex != nullptr) {
                    SDL_DestroyTexture(entry.tex);
                    entry.tex = nullptr;
                }
            }
            g_TextCache.clear();
            g_TextCacheOrder.clear();
            // Clear owned-pointer set — all live cache textures were just destroyed.
            g_text_cache_owned_pointers.clear();
            // Also drain any deferred entries that haven't been flushed yet
            // (e.g. shutdown before the next frame boundary).
            // These were already removed from g_text_cache_owned_pointers at
            // eviction time, so no additional cleanup needed for the set here.
            for(SDL_Texture *t : g_text_cache_deferred_destroy) {
                if(t != nullptr) {
                    SDL_DestroyTexture(t);
                }
            }
            g_text_cache_deferred_destroy.clear();
        }

    }

    Result Renderer::Initialize() {
        if(!this->initialized) {
            if(this->init_opts.init_romfs) {
                PU_RC_TRY(romfsInit());
            }

            if(this->init_opts.NeedsPlService()) {
                PU_RC_TRY(plInitialize(static_cast<PlServiceType>(this->init_opts.pl_srv_type)));
            }

            padConfigureInput(this->init_opts.pad_player_count, this->init_opts.pad_style_tag);
            padInitializeWithMask(&this->input_pad, this->init_opts.pad_id_mask);

            if(SDL_Init(this->init_opts.sdl_flags) != 0) {
                return ResultSdlInitFailed;
            }
            
            g_Window = SDL_CreateWindow("Plutonium-SDL2", 0, 0, this->init_opts.width, this->init_opts.height, 0);
            if(g_Window == nullptr) {
                return ResultSdlCreateWindowFailed;
            }

            g_Renderer = SDL_CreateRenderer(g_Window, -1, this->init_opts.sdl_render_flags);
            if(g_Renderer == nullptr) {
                return ResultSdlCreateRendererFailed;
            }

            g_WindowSurface = SDL_GetWindowSurface(g_Window);
            SDL_SetRenderDrawBlendMode(g_Renderer, SDL_BLENDMODE_BLEND);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

            if(this->init_opts.init_img) {
                if(IMG_Init(this->init_opts.sdl_img_flags) != this->init_opts.sdl_img_flags) {
                    auto f = fopen("sdmc:/IMG_Init_Failed.txt", "w");
                    if(f != nullptr) {
                        fprintf(f, "IMG_Init failed with flags: %d %s\n", this->init_opts.sdl_img_flags, IMG_GetError());
                        fclose(f);
                    }

                    return ResultImgInitFailed;
                }
            }

            if(!this->init_opts.default_shared_fonts.empty() || !this->init_opts.default_font_paths.empty()) {
                if(TTF_Init() != 0) {
                    return ResultTtfInitFailed;
                }

                #define _CREATE_DEFAULT_FONT_FOR_SIZES(sizes) { \
                    for(const auto size: sizes) { \
                        auto default_font = std::make_shared<ttf::Font>(size); \
                        for(const auto &path: this->init_opts.default_font_paths) { \
                            default_font->LoadFromFile(path); \
                        } \
                        for(const auto type: this->init_opts.default_shared_fonts) { \
                            LoadSingleSharedFontInFont(default_font, type); \
                        } \
                        AddDefaultFont(default_font); \
                    } \
                }

                _CREATE_DEFAULT_FONT_FOR_SIZES(DefaultFontSizes);
                _CREATE_DEFAULT_FONT_FOR_SIZES(this->init_opts.extra_default_font_sizes);
            }

            this->initialized = true;
            this->base_a = TextureRenderOptions::NoAlpha;
            this->base_x = 0;
            this->base_y = 0;
        }

        return 0;
    }

    void Renderer::Finalize() {
        if(this->initialized) {
            // Shutdown-only cache flush.  TextCacheClear() is NOT called
            // per-frame (removed in v1.8.3; deferred-destroy queue drains in
            // InitializeRender() instead).  This is the only remaining call
            // site — it also drains the deferred queue and destroys any still-
            // cached textures before SDL_DestroyRenderer() is called below.
            // SDL_DestroyTexture requires the renderer to still be alive.
            TextCacheClear();

            // Close all the fonts before closing TTF
            g_FontTable.clear();

            if(this->init_opts.NeedsTtf()) {
                TTF_Quit();
            }

            if(this->init_opts.init_img) {
                IMG_Quit();
            }

            if(this->init_opts.NeedsPlService()) {
                plExit();
            }

            if(this->init_opts.init_romfs) {
                romfsExit();
            }

            SDL_DestroyRenderer(g_Renderer);
            SDL_FreeSurface(g_WindowSurface);
            SDL_DestroyWindow(g_Window);
            SDL_Quit();

            this->initialized = false;
        }
    }

    void Renderer::InitializeRender(const Color clr) {
        // v1.8.3 deferred-destroy drain: flush SDL_Textures that were evicted
        // from the LRU cache during the previous frame.  This is the ONLY place
        // SDL_DestroyTexture is called on cache-owned textures during normal
        // operation.  By draining here — at the very start of a new frame,
        // before any SDL_RenderCopyEx calls — we guarantee:
        //   (a) no caller still holds a live pointer to these textures
        //       (all RenderText() calls from the previous frame have completed),
        //   (b) the SDL renderer is still alive, and
        //   (c) cross-frame cache hits are preserved: textures that were NOT
        //       evicted survive into this frame unchanged.
        //
        // TextCacheClear() is NOT called here (per-frame full wipe removed in
        // v1.8.3).  It is called only from Finalize() on shutdown.
        for(SDL_Texture *tex : g_text_cache_deferred_destroy) {
            if(tex) {
                SDL_DestroyTexture(tex);
            }
        }
        g_text_cache_deferred_destroy.clear();

        SDL_SetRenderDrawColor(g_Renderer, clr.r, clr.g, clr.b, clr.a);
        SDL_RenderClear(g_Renderer);
    }

    void Renderer::FinalizeRender() {
        SDL_RenderPresent(g_Renderer);
    }

    void Renderer::RenderTexture(sdl2::Texture texture, const s32 x, const s32 y, const TextureRenderOptions opts) {
        if(texture == nullptr) {
            return;
        }

        s32 tex_w;
        s32 tex_h;
        if((opts.height == TextureRenderOptions::NoHeight) || (opts.height == TextureRenderOptions::NoWidth)) {
            SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);
        }
        
        SDL_Rect dst_rect = {
            .x = x + this->base_x,
            .y = y + this->base_y
        };
        SDL_Rect src_rect = {};

        if(opts.width != TextureRenderOptions::NoWidth) {
            dst_rect.w = opts.width;
            src_rect.w = opts.width;
        }
        else {
            dst_rect.w = tex_w;
            src_rect.w = tex_w;
        }
        if(opts.height != TextureRenderOptions::NoHeight) {
            dst_rect.h = opts.height;
            src_rect.h = opts.height;
        }
        else {
            dst_rect.h = tex_h;
            src_rect.h = tex_h;
        }

        SDL_Rect *src_rect_ptr = nullptr;
        if((opts.src_x != TextureRenderOptions::NoSourceX) || (opts.src_y != TextureRenderOptions::NoSourceY)) {
            src_rect.x = opts.src_x;
            src_rect.y = opts.src_y;
            src_rect_ptr = &src_rect;
        }

        float angle = 0;
        if(opts.rot_angle != TextureRenderOptions::NoRotation) {
            angle = opts.rot_angle;
        }

        const auto has_alpha_mod = opts.alpha_mod != TextureRenderOptions::NoAlpha;
        if(has_alpha_mod) {
            SetAlphaValue(texture, static_cast<u8>(opts.alpha_mod));
        }
        if(this->base_a >= 0) {
            SetAlphaValue(texture, static_cast<u8>(this->base_a));
        }

        SDL_RenderCopyEx(g_Renderer, texture, src_rect_ptr, &dst_rect, angle, nullptr, SDL_FLIP_NONE);

        if(has_alpha_mod || (this->base_a >= 0)) {
            // Aka unset alpha value, needed if the same texture is rendered several times with different alphas
            SetAlphaValue(texture, 0xFF);
        }
    }

    void Renderer::RenderRectangle(const Color clr, const s32 x, const s32 y, const s32 width, const s32 height) {
        const SDL_Rect rect = {
            .x = x + this->base_x,
            .y = y + this->base_y,
            .w = width,
            .h = height
        };
        SDL_SetRenderDrawColor(g_Renderer, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
        SDL_RenderDrawRect(g_Renderer, &rect);
    }

    void Renderer::RenderRectangleFill(const Color clr, const s32 x, const s32 y, const s32 width, const s32 height) {
        const SDL_Rect rect = {
            .x = x + this->base_x,
            .y = y + this->base_y,
            .w = width,
            .h = height
        };
        SDL_SetRenderDrawColor(g_Renderer, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
        SDL_RenderFillRect(g_Renderer, &rect);
    }
	
    void Renderer::RenderRoundedRectangle(const Color clr, const s32 x, const s32 y, const s32 width, const s32 height, const s32 radius) {
        auto proper_radius = radius;
        if((2 * proper_radius) > width) {
            proper_radius = width / 2;
        }
        if((2 * proper_radius) > height) {
            proper_radius = height / 2;
        }
        
        roundedRectangleRGBA(g_Renderer, x + this->base_x, y + this->base_y, x + this->base_x + width, y + this->base_y + height, proper_radius, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
        SDL_SetRenderDrawBlendMode(g_Renderer, SDL_BLENDMODE_BLEND);
    }

    void Renderer::RenderRoundedRectangleFill(const Color clr, const s32 x, const s32 y, const s32 width, const s32 height, const s32 radius) {
        auto proper_radius = radius;
        if((2 * proper_radius) > width) {
            proper_radius = width / 2;
        }
        if((2 * proper_radius) > height) {
            proper_radius = height / 2;
        }
        
        roundedBoxRGBA(g_Renderer, x + this->base_x, y + this->base_y, x + this->base_x + width, y + this->base_y + height, proper_radius, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
        SDL_SetRenderDrawBlendMode(g_Renderer, SDL_BLENDMODE_BLEND);
    }

    void Renderer::RenderCircle(const Color clr, const s32 x, const s32 y, const s32 radius) {
        circleRGBA(g_Renderer, x + this->base_x, y + this->base_y, radius - 1, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
        aacircleRGBA(g_Renderer, x + this->base_x, y + this->base_y, radius - 1, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
    }

    void Renderer::RenderCircleFill(const Color clr, const s32 x, const s32 y, const s32 radius) {
        filledCircleRGBA(g_Renderer, x + this->base_x, y + this->base_y, radius - 1, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
        aacircleRGBA(g_Renderer, x + this->base_x, y + this->base_y, radius - 1, clr.r, clr.g, clr.b, this->GetActualAlpha(clr.a));
    }

    void Renderer::RenderShadowSimple(const s32 x, const s32 y, const s32 width, const s32 height, const s32 base_alpha, const u8 main_alpha) {
        auto crop = false;
        auto shadow_width = width;
        auto shadow_x = x;
        auto shadow_y = y;
        for(auto cur_a = base_alpha; cur_a > 0; cur_a -= (180 / height)) {
            const Color shadow_clr = { 130, 130, 130, static_cast<u8>(cur_a * (main_alpha / 0xFF)) };
            this->RenderRectangleFill(shadow_clr, shadow_x + this->base_x, shadow_y + this->base_y, shadow_width, 1);
            if(crop) {
                shadow_width -= 2;
                shadow_x++;
            }
            crop = !crop;
            shadow_y++;
        }
    }

    sdl2::Renderer GetMainRenderer() {
        return g_Renderer;
    }

    sdl2::Window GetMainWindow() {
        return g_Window;
    }

    sdl2::Surface GetMainSurface() {
        return g_WindowSurface;
    }

    std::pair<u32, u32> GetDimensions() {
        s32 w = 0;
        s32 h = 0;
        SDL_GetWindowSize(g_Window, &w, &h);
        return { static_cast<u32>(w), static_cast<u32>(h) };
    }

    bool AddFont(const std::string &font_name, std::shared_ptr<ttf::Font> &font) {
        if(ExistsFont(font_name)) {
            return false;
        }

        g_FontTable.push_back(std::make_pair(font_name, std::move(font)));
        return true;
    }

    bool LoadSingleSharedFontInFont(std::shared_ptr<ttf::Font> &font, const PlSharedFontType type) {
        // Assume pl services are initialized, and return if anything unexpected happens
        PlFontData data = {};
        if(R_FAILED(plGetSharedFontByType(&data, type))) {
            return false;
        }
        if(!ttf::Font::IsValidFontFaceIndex(font->LoadFromMemory(data.address, data.size, ttf::Font::EmptyFontFaceDisposingFunction))) {
            return false;
        }

        return true;
    }

    bool LoadAllSharedFontsInFont(std::shared_ptr<ttf::Font> &font) {
        for(u32 i = 0; i < PlSharedFontType_Total; i++) {
            if(!LoadSingleSharedFontInFont(font, static_cast<PlSharedFontType>(i))) {
                return false;
            }
        }
        return true;
    }

    bool GetTextDimensions(const std::string &font_name, const std::string &text, s32 &out_width, s32 &out_height) {
        for(auto &[name, font]: g_FontTable) {
            if(name == font_name) {
                const auto [w, h] = font->GetTextDimensions(text);
                out_width = w;
                out_height = h;
                return true;
            }
        }
        return false;
    }

    s32 GetTextWidth(const std::string &font_name, const std::string &text) {
        s32 width = 0;
        s32 dummy;
        GetTextDimensions(font_name, text, width, dummy);
        return width;
    }

    s32 GetTextHeight(const std::string &font_name, const std::string &text) {
        s32 dummy;
        s32 height = 0;
        GetTextDimensions(font_name, text, dummy, height);
        return height;
    }

    bool IsCacheOwnedTexture(SDL_Texture *tex) {
        return tex != nullptr && g_text_cache_owned_pointers.count(tex) != 0;
    }

    sdl2::Texture RenderText(const std::string &font_name, const std::string &text, const Color clr, const u32 max_width, const u32 max_height) {
        // Task A — LRU cache look-up.
        // The cache stores the FINAL texture (after any truncation loop), keyed
        // by the full render parameters.  On a hit we return the cached pointer
        // directly — no TTF_RenderUTF8_Blended, no SDL_CreateTextureFromSurface.
        // Callers that call SDL_DestroyTexture on the returned pointer must NOT
        // do so for cached textures.  Plutonium's own callers (elm_TextBlock,
        // elm_Button, etc.) never destroy the result of RenderText() — they pass
        // it straight to RenderTexture() and let the cache own lifetime.  Any
        // external caller that tries to destroy a cached texture would corrupt
        // the cache; that pattern did not exist in the codebase at the time this
        // was written.
        const TextCacheKey cache_key{
            font_name, text,
            clr.r, clr.g, clr.b, clr.a,
            max_width, max_height
        };

        auto cached = TextCacheLookup(cache_key);
        if(cached != nullptr) {
            return cached;
        }

        // Cache miss — render the texture, then insert it.
        for(auto &[name, font]: g_FontTable) {
            if(name == font_name) {
                auto text_tex = font->RenderText(text, clr);

                if((max_width > 0) || (max_height > 0)) {
                    auto cur_text = text;
                    auto cur_width = GetTextureWidth(text_tex);
                    auto cur_height = GetTextureHeight(text_tex);
                    while(true) {
                        if(cur_text.empty()) {
                            break;
                        }
                        if((max_width > 0) && (cur_width <= (s32)max_width)) {
                            break;
                        }
                        if((max_height > 0) && (cur_height <= (s32)max_height)) {
                            break;
                        }

                        cur_text.pop_back();
                        DeleteTexture(text_tex);
                        text_tex = font->RenderText(cur_text + "...", clr);
                        cur_width = GetTextureWidth(text_tex);
                        cur_height = GetTextureHeight(text_tex);
                    }
                }

                TextCacheInsert(cache_key, text_tex);
                return text_tex;
            }
        }

        return nullptr;
    }

}
