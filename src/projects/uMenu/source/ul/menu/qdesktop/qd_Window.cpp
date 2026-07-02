// qd_Window.cpp — Generic window primitive implementation.
// See qd_Window.hpp for design notes.
//
// v1.10.3.10 SP3 centralized-scale changes:
//   - New() factory parameter changed to QdContentElement::Ref.
//   - SetContent() reads natural_w_/natural_h_ from content->GetNaturalW/H().
//     Re-caches each frame when content->IsNaturalSizeDirty() is true.
//   - OnRender content chain: SDL_RenderSetClipRect → SDL_RenderSetScale(scale_x, scale_y)
//     → content->OnRender(-scroll_x_, -scroll_y_) → reset scale to 1:1 → reset clip
//     → PaintScrollbars.  Replaces old two-path (scale_to_viewport_ + scroll-only).
//   - cur_scale_x_/cur_scale_y_ cached each frame; read by PollEvent for touch
//     coordinate translation: local = (screen - win_origin) / scale + scroll.
//   - SetContentLogicalSize() removed; scale_to_viewport_ removed.
//   - GetViewportSize / SetScrollOffset / PaintScrollbars updated to use natural_w_/h_.
//
// v1.10.3 changes:
//   - Traffic-light strip replaced with four 48×48 corner hit zones.
//     TL=close, TR=maximize, BL=minimize, BR=resize.
//     32×32 visible icon centered inside the 48×48 hit zone.
//   - Origin-delta drag model: records drag_origin_win_* + drag_origin_touch_*
//     at drag-start; drag_last_touch_* updated only on valid touch frames.
//     Position = origin_win + (last_touch - origin_touch). This survives the
//     Plutonium (-1,-1) held-frame issue that stalled the old model.
//   - Cursor/ZR hover: UpdateHoverForCursor sets hover_* booleans for
//     corner highlighting; TryActivateAtCursor fires buttons.
//   - ZR cursor-drag: BeginCursorDrag/UpdateCursorDrag/EndCursorDrag.
//   - Maximize / snap state setters.
//   - Content OnInput dispatch in PollEvent.
//   - Tick-based Refresh: content_->Refresh() every kTickRefreshHz frames.
#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <cmath>
#include <algorithm>

namespace ul::menu::qdesktop {

// QoL-T2 — jitter threshold for content-area finger drag-scroll.
// Movements smaller than this (in screen pixels) are treated as taps so that
// a stationary finger tap still reaches the content element.
static constexpr s32 kContentDragJitterPx = 8;

// WIN-5 SCISSOR helper: intersect two SDL_Rects.
// Returns the intersection in `out`; returns false (and leaves `out` unchanged)
// if the rects do not overlap.  When `constraint` is nullptr, `out` is set to
// `r` and true is returned (unconstrained pass-through).
static bool IntersectClip(const SDL_Rect& r, const SDL_Rect* constraint,
                           SDL_Rect& out) {
    if (!constraint) {
        out = r;
        return true;
    }
    const s32 x0 = std::max(r.x, constraint->x);
    const s32 y0 = std::max(r.y, constraint->y);
    const s32 x1 = std::min(r.x + r.w, constraint->x + constraint->w);
    const s32 y1 = std::min(r.y + r.h, constraint->y + constraint->h);
    if (x1 <= x0 || y1 <= y0) return false;
    out = { x0, y0, x1 - x0, y1 - y0 };
    return true;
}

// ── Factory ───────────────────────────────────────────────────────────────────

QdWindow::Ref QdWindow::New(const std::string& title,
                             QdContentElement::Ref elem,
                             s32 x, s32 y, s32 w, s32 h)
{
    auto win = std::shared_ptr<QdWindow>(new QdWindow());
    win->title_   = title;
    win->win_x_   = x;
    win->win_y_   = y;
    win->win_w_   = std::max(w, static_cast<s32>(WIN_MIN_W));
    win->win_h_   = std::max(h, static_cast<s32>(WIN_MIN_H));
    win->focused_ = true;
    win->state_   = WindowState::Normal;
    win->SetContent(std::move(elem));
    return win;
}

// ── SetContent ────────────────────────────────────────────────────────────────

void QdWindow::SetContent(QdContentElement::Ref content) {
    content_        = std::move(content);
    natural_w_      = content_ ? content_->GetNaturalW() : 0;
    natural_h_      = content_ ? content_->GetNaturalH() : 0;
    scroll_x_       = 0;
    scroll_y_       = 0;
    vp_cache_dirty_ = true;  // A2-OPT-3: new content may have different natural dims
    if (content_) content_->ClearNaturalSizeDirty();
    content_dirty_ = true;   // WIN-2: new content must be baked
}

// ── Destructor ────────────────────────────────────────────────────────────────

QdWindow::~QdWindow() {
    FreeTextures();
}

// ── Texture lifecycle ─────────────────────────────────────────────────────────

void QdWindow::FreeTextures() {
    frame_.FreeTextures();
    if (hint_tex_) {
        pu::ui::render::DeleteTexture(hint_tex_);
        hint_tex_   = nullptr;
        hint_tex_w_ = 0;
        hint_tex_h_ = 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (corner_tip_tex_[i]) {
            pu::ui::render::DeleteTexture(corner_tip_tex_[i]);
            corner_tip_tex_[i]   = nullptr;
            corner_tip_tex_w_[i] = 0;
            corner_tip_tex_h_[i] = 0;
        }
    }
    // WIN-2: free the content bake texture and mark dirty so it is rebuilt on
    // the next render.  FreeTextures() is called on geometry changes (snap,
    // maximize, restore, move-to) — viewport dimensions change, so the bake
    // texture must be recreated at the new size.
    if (content_bake_tex_) {
        SDL_DestroyTexture(content_bake_tex_);
        content_bake_tex_ = nullptr;
        bake_vw_          = 0;
        bake_vh_          = 0;
    }
    content_dirty_ = true;
}

// ── WIN-SCALE-FIX-2: Non-visible texture eviction ────────────────────────────
// Called by QdWindowManager on occluded/non-visible windows to release the
// large per-window baked textures (content bake + shadow + ring) while the
// window remains logically open.  Content is re-baked lazily when the window
// becomes visible again (content_dirty_ = true + vp_cache_dirty_ = true ensures
// the next OnRender recreates everything from scratch).
// Disc textures are tiny (30×30) and shared per-frame, so they are left intact.

void QdWindow::EvictBakeTextures() {
    if (content_bake_tex_) {
        SDL_DestroyTexture(content_bake_tex_);
        content_bake_tex_ = nullptr;
        bake_vw_          = 0;
        bake_vh_          = 0;
        content_dirty_    = true;
    }
    // Evict frame chrome caches (shadow + ring; disc textures are tiny and left).
    frame_.EvictLargeTextures();
    vp_cache_dirty_ = true;
}

bool QdWindow::HasBakeTextures() const {
    return (content_bake_tex_ != nullptr);
}

void QdWindow::SetHintText(const std::string& hint) {
    if (hint == hint_text_) {
        return; // nothing changed — avoid unnecessary texture re-creation
    }
    hint_text_ = hint;
    // Free the old texture (safe if nullptr — guarded above in FreeTextures).
    if (hint_tex_) {
        pu::ui::render::DeleteTexture(hint_tex_);
        hint_tex_   = nullptr;
        hint_tex_w_ = 0;
        hint_tex_h_ = 0;
    }
    if (!hint.empty()) {
        // Same hint-bar color used by QdVaultLayout / QdSettingsElement / etc.
        static constexpr pu::ui::Color kHintCol = { 0x99, 0x99, 0xBB, 0xFF };
        hint_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            hint,
            kHintCol);
        if (hint_tex_) {
            SDL_QueryTexture(hint_tex_, nullptr, nullptr, &hint_tex_w_, &hint_tex_h_);
        }
    }
}

// ── SDL drawing helpers ───────────────────────────────────────────────────────

void QdWindow::DrawCircle(SDL_Renderer* r, int cx, int cy, int rad,
                          pu::ui::Color col)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = static_cast<int>(sqrtf(static_cast<float>(rad * rad - dy * dy)));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void QdWindow::DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                               pu::ui::Color col, int req_rad)
{
    // Default rad=8 (radius/sm) keeps scrollbar call sites pixel-identical.
    // Clamped to w/2 and h/2 so narrow rects don't produce negative widths.
    const int rad = std::min({req_rad, w / 2, h / 2});

    // v2.0.3-A6: scrollbar fast-path — when the (clamped) corner radius is too
    // small to perceive (rad ≤ 4 means the visible curvature is ≤ 4 sub-pixel
    // pixels at scale 1.0, well below the 6.2" Switch screen's perceptible
    // limit at 1080p), skip the four DrawCircle calls and emit a single
    // SDL_RenderFillRect.  Saves ~36-104 SDL_RenderDrawLine/window/frame when
    // the VSB is visible (every frame for v2.0.2 enlarged folder windows).
    // Visual identity is unchanged at this size.
    if (rad <= 4) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
        SDL_Rect plain { x, y, w, h };
        SDL_RenderFillRect(r, &plain);
        return;
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);

    SDL_Rect center = { x + rad, y, w - 2 * rad, h };
    SDL_RenderFillRect(r, &center);
    SDL_Rect left   = { x,           y + rad, rad, h - 2 * rad };
    SDL_Rect right  = { x + w - rad, y + rad, rad, h - 2 * rad };
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);

    DrawCircle(r, x + rad,         y + rad,         rad, col);
    DrawCircle(r, x + w - rad - 1, y + rad,         rad, col);
    DrawCircle(r, x + rad,         y + h - rad - 1, rad, col);
    DrawCircle(r, x + w - rad - 1, y + h - rad - 1, rad, col);
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void QdWindow::OnRender(pu::ui::render::Renderer::Ref& drawer, s32 /*x*/, s32 /*y*/,
                        const SDL_Rect* visible_clip) {
    SDL_Renderer* r = pu::ui::render::GetMainRenderer();

    // Periodic tick — calls on_tick callback if set.
    // on_tick is wired by the caller (e.g. to QdSettingsHostLayout::Refresh())
    // because Refresh() is not part of the pu::ui::elm::Element interface.
    tick_counter_++;
    if (tick_counter_ >= kTickRefreshHz) {
        tick_counter_ = 0;
        if (on_tick && state_ == WindowState::Normal) {
            on_tick();
            // WIN-2: windows with a tick callback (Settings/About/Monitor) update
            // live data every kTickRefreshHz frames.  Mark the bake dirty so the
            // next OnRender re-renders content into the texture instead of using
            // the cached (now stale) bake.
            content_dirty_ = true;
        }
    }

    if (state_ == WindowState::Minimizing || state_ == WindowState::Restoring) {
        AdvanceAnimation();
    }
    if (state_ == WindowState::Minimized || state_ == WindowState::Closing) {
        return;
    }

    const int wx  = win_x_;
    const int wy  = win_y_;
    const int ww  = win_w_;
    const int wh  = win_h_;
    const int tbh = kTitlebarH;
    const u8  alpha = anim_alpha_;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // ── WIN-5 SCISSOR: apply the manager-supplied visible-region clip ─────────
    // Set once here, before chrome paint.  Each internal content-clip reset
    // (SDL_RenderSetClipRect(r, nullptr)) is replaced by a reset-to-visible_clip
    // so the outer boundary is maintained throughout the render.  Cleared to
    // nullptr at the very end, after PaintScrollbars.
    //
    // WIN-4b nullptr guard: when visible_clip is nullptr (manager decided the
    // scissor is not meaningful — e.g. diagonal cascade where bbox ≈ full window),
    // skip this outer clip-set entirely so no extra SDL_RenderSetClipRect call is
    // issued and the WIN-1 render batch is NOT flushed.  The internal content-clip
    // resets below use IntersectClip with nullptr → pass-through, so the content
    // paint path is identical to pre-scissor v3.7.29.  The final reset at the
    // bottom of this function always resets to nullptr regardless.
    if (visible_clip) {
        SDL_RenderSetClipRect(r, visible_clip);
    }

    // ── Chrome (v3.7 QdFrame: nine-patch SVG + code-drawn discs) ─────────────
    {
        const SDL_Rect fr { wx, wy, ww, wh };
        const BtnState cst  = hover_close_    ? BtnState::Hover : BtnState::Normal;
        const BtnState mist = hover_minimize_ ? BtnState::Hover : BtnState::Normal;
        const BtnState mxst = hover_maximize_ ? BtnState::Hover : BtnState::Normal;

        // Lazy-build disc button tooltips; pass the active one (if any) to
        // QdFrame::Paint so it overrides hint_tex_ in the status bar.
        SDL_Texture* tip_tex = nullptr;
        int          tip_w   = 0;
        int          tip_h   = 0;
        const int tip_idx = hover_close_ ? 0 : hover_maximize_ ? 1 : hover_minimize_ ? 2 : -1;
        if (tip_idx >= 0) {
            if (!corner_tip_tex_[tip_idx]) {
                static const char* const kTipText[3] = {
                    "Close window [B]",
                    "Maximize / Restore [+]",
                    "Minimize [-]",
                };
                const pu::ui::Color tip_col {
                    ::ul::menu::qdesktop::g_QdTheme.text_primary.r,
                    ::ul::menu::qdesktop::g_QdTheme.text_primary.g,
                    ::ul::menu::qdesktop::g_QdTheme.text_primary.b,
                    0xFFu,
                };
                corner_tip_tex_[tip_idx] = pu::ui::render::RenderText(
                    pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                    std::string(kTipText[tip_idx]),
                    tip_col);
                if (corner_tip_tex_[tip_idx]) {
                    SDL_QueryTexture(corner_tip_tex_[tip_idx], nullptr, nullptr,
                                     &corner_tip_tex_w_[tip_idx],
                                     &corner_tip_tex_h_[tip_idx]);
                }
            }
            tip_tex = corner_tip_tex_[tip_idx];
            tip_w   = corner_tip_tex_w_[tip_idx];
            tip_h   = corner_tip_tex_h_[tip_idx];
        }

        // Clamp to [0,9]: we ship 10 SVG masters; pack index can exceed that.
        const int raw_tidx  = static_cast<int>(::ul::menu::qdesktop::g_active_theme_pack_idx);
        const int theme_idx = (raw_tidx >= 0 && raw_tidx < 10) ? raw_tidx : 0;
        frame_.Paint(r, fr, focused_, maximized_, cst, mist, mxst, theme_idx, title_,
                     hint_tex_, hint_tex_w_, hint_tex_h_, tip_tex, tip_w, tip_h, alpha);
    }

    // ── Content element (SP3 centralized-scale model, v1.10.3.10) ───────────
    // QdWindow owns ALL scale and scroll arithmetic.  Content is passive: it
    // paints at natural (1:1) coordinates; QdWindow pre-applies SDL_RenderSetScale.
    //
    // Render contract (per qd_ContentElement.hpp):
    //   1. SDL_RenderSetClipRect set to { cx_pos+1, cy_pos, vw, vh }.
    //   2. SDL_RenderSetScale(r, scale_x, scale_y) applied.
    //   3. content_->OnRender(drawer, -scroll_x_, -scroll_y_) — natural coords.
    //   4. SDL_RenderSetScale reset to (1, 1); clip reset to nullptr.
    //   5. PaintScrollbars on top of reset clip.
    //
    // Native ref: SDL_RenderSetScale — SDL2/SDL_render.h:490;
    //             SDL_RenderSetClipRect — SDL2/SDL_render.h:462.
    // v1.10.3.10.2: Gate content paint to Normal state only.  During
    // Minimizing/Restoring animations the window shrinks toward the dock,
    // making vh approach 0 and scale_y → 0.  The pre-scale origin
    // (cy_pos / scale_y) blew up to 24,000+ pixels at end of animation,
    // overflowing SDL's snapshot capture path and crashing Atmosphère.
    // Animation transition is handled by titlebar paint + the snapshot
    // texture mechanism — content does not need to paint per-frame during
    // animation.
    if (content_ && state_ == WindowState::Normal)
    {
        // Re-cache natural dims if content signalled a size change.
        if (content_->IsNaturalSizeDirty()) {
            natural_w_ = content_->GetNaturalW();
            natural_h_ = content_->GetNaturalH();
            content_->ClearNaturalSizeDirty();
            vp_cache_dirty_ = true;  // A2-OPT-3: natural size changed → viewport recalc needed
            content_dirty_  = true;  // WIN-2: natural size changed → bake stale
        }

        // A2-OPT-3: compute viewport size once per frame (or when dirty).
        if (vp_cache_dirty_) {
            GetViewportSize(cached_vw_, cached_vh_);
            vp_cache_dirty_ = false;
        }
        const s32 vw = cached_vw_;
        const s32 vh = cached_vh_;

        // Compute scale: fit the natural canvas into the visible viewport.
        // If natural dims are 0 (content not yet measured), keep 1:1.
        // v1.10.3.10.3: UNIFORM scale — use the smaller of width/height ratios so
        // content keeps its natural aspect ratio.  Independent X/Y scaling
        // squished text vertically when natural_h > viewport_h (Settings 600,
        // About 896, Monitor 900 vs viewport 395 → Y-squish 34-56%).  Uniform
        // scale leaves margin in one axis but text reads at correct proportion.
        const float scale_x_indep = (natural_w_ > 0 && vw > 0)
            ? static_cast<float>(vw) / static_cast<float>(natural_w_)
            : 1.0f;
        const float scale_y_indep = (natural_h_ > 0 && vh > 0)
            ? static_cast<float>(vh) / static_cast<float>(natural_h_)
            : 1.0f;
        // v2.0.0: width-bound scale opt-in (per QdContentElement::PrefersWidthBoundScale).
        // Vault file grid uses this so cells stay at design size and excess
        // vertical content surfaces VSB-driven scrolling — natural uniform-scale
        // would shrink cells to ~41 % at default window because Vault's
        // natural_h grows with entry count (1168 px for 30 entries vs 480 vh).
        const bool width_bound = content_->PrefersWidthBoundScale();
        const float scale_uniform = width_bound
            ? scale_x_indep
            : ((scale_x_indep < scale_y_indep) ? scale_x_indep : scale_y_indep);
        const float scale_x = scale_uniform;
        const float scale_y = scale_uniform;

        // Cache for PollEvent touch coordinate translation.
        cur_scale_x_ = scale_x;
        cur_scale_y_ = scale_y;

        const int cx_pos = wx;
        const int cy_pos = wy + tbh;

        // v1.10.3.10.4 centering offsets — computed regardless of bake path so
        // PollEvent touch translation always has up-to-date cur_offset_x_/y_.
        const float visual_w = static_cast<float>(natural_w_) * scale_x;
        const float visual_h = static_cast<float>(natural_h_) * scale_y;
        const float h_margin = static_cast<float>(vw) - visual_w;
        const float v_margin = static_cast<float>(vh) - visual_h;
        cur_offset_x_ = (h_margin > 0.0f && scale_x > 0.0f)
                        ? (h_margin / (2.0f * scale_x)) : 0.0f;
        cur_offset_y_ = (v_margin > 0.0f && scale_y > 0.0f)
                        ? (v_margin / (2.0f * scale_y)) : 0.0f;

        // ── WIN-2: frozen-content render-to-texture bake ──────────────────────
        //
        // Conditions that bypass the bake and render content directly:
        //   • animating (state_ != Normal already exits above, but guard here too)
        //   • viewport degenerate (vw or vh ≤ 0)
        //   • on_tick is set on this window (Settings / About / Monitor update
        //     every tick — those windows mark content_dirty_ each tick so the
        //     bake still fires, but we skip the "use cached" fast-path and always
        //     re-bake to guarantee live data; see on_tick block at top of OnRender)
        //
        // Bake sequence when dirty:
        //   1. (Re)create content_bake_tex_ if missing or viewport size changed.
        //   2. Save current render target.
        //   3. Set render target to content_bake_tex_; clear to transparent.
        //   4. Render content at origin (0,0) — viewport-local coords inside bake.
        //   5. Restore render target to the saved value.
        //   6. Clear content_dirty_.
        //
        // Fast-path when clean:
        //   Blit content_bake_tex_ to the viewport rect.
        //
        // After either path: paint scrollbars on top of the viewport (unchanged).

        const bool animating   = (state_ != WindowState::Normal);
        const bool vp_degenerate = (vw <= 0 || vh <= 0);

        if (!animating && !vp_degenerate) {
            // Invalidate the bake when the viewport dimensions change.
            if (content_bake_tex_ && (bake_vw_ != vw || bake_vh_ != vh)) {
                SDL_DestroyTexture(content_bake_tex_);
                content_bake_tex_ = nullptr;
                bake_vw_          = 0;
                bake_vh_          = 0;
                content_dirty_    = true;
            }

            // Ensure the bake texture exists.
            if (!content_bake_tex_) {
                content_bake_tex_ = SDL_CreateTexture(r,
                                        SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET,
                                        vw, vh);
                if (content_bake_tex_) {
                    SDL_SetTextureBlendMode(content_bake_tex_, SDL_BLENDMODE_BLEND);
                    bake_vw_       = vw;
                    bake_vh_       = vh;
                    content_dirty_ = true;  // new texture: must bake immediately
                }
            }

            if (content_bake_tex_) {
                if (content_dirty_) {
                    // ── Bake: render content into the off-screen texture ──────────
                    SDL_Texture* prev_target = SDL_GetRenderTarget(r);
                    // WIN-SCALE-FIX-1: guard SDL_SetRenderTarget — on VRAM exhaustion
                    // SDL_SetRenderTarget returns -1 and leaves the renderer in the
                    // previous state.  Proceeding with a failed target redirect causes
                    // the bake draw calls to land on the wrong surface and can crash
                    // the NVN/GL driver.  When it fails, free the unusable texture
                    // and skip the bake body; content_bake_tex_ becomes nullptr and
                    // the if (!content_bake_tex_) block below does live-render fallback.
                    const bool target_ok = (SDL_SetRenderTarget(r, content_bake_tex_) == 0);
                    if (!target_ok) {
                        UL_LOG_WARN("qdesktop: content bake SetRenderTarget failed (%s) — "
                                    "freeing bake texture, will live-render this frame",
                                    SDL_GetError());
                        SDL_DestroyTexture(content_bake_tex_);
                        content_bake_tex_ = nullptr;
                        bake_vw_ = 0;
                        bake_vh_ = 0;
                    }

                    if (target_ok) {
                    // Clear the bake texture to fully transparent before painting.
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
                    SDL_RenderClear(r);

                    // Render content at viewport-local origin (0,0) inside the bake.
                    // The bake texture is exactly (vw x vh) so content lands at (0,0)
                    // with scale applied, matching what it would look like at cx_pos+1, cy_pos
                    // on screen.  Origin formula mirrors OnRender's main path but with
                    // cx_pos=−1, cy_pos=0 (so (cx_pos+1)/scale_x = 0).
                    const float bake_origin_x = 0.0f
                                             - static_cast<float>(scroll_x_)
                                             + cur_offset_x_;
                    const float bake_origin_y = 0.0f
                                             - static_cast<float>(scroll_y_)
                                             + cur_offset_y_;

                    SDL_RenderSetScale(r, scale_x, scale_y);
                    content_->OnRender(drawer,
                                       static_cast<s32>(std::lroundf(bake_origin_x)),
                                       static_cast<s32>(std::lroundf(bake_origin_y)));
                    SDL_RenderSetScale(r, 1.0f, 1.0f);

                    SDL_SetRenderTarget(r, prev_target);
                    content_dirty_ = false;
                    } // end if (target_ok)
                }

                // ── Blit the bake texture to the viewport rect on screen ──────────
                // WIN-5 SCISSOR: intersect the content viewport clip with the
                // manager-supplied visible_clip.  If the intersection is empty
                // (window is fully off-screen relative to the clip), skip the blit.
                // WIN-SCALE-FIX-1: re-check content_bake_tex_ — it may have been
                // freed by the SetRenderTarget failure guard above.
                if (content_bake_tex_) {
                    const SDL_Rect content_clip_rect { cx_pos + 1, cy_pos, vw, vh };
                    SDL_Rect active_clip;
                    if (IntersectClip(content_clip_rect, visible_clip, active_clip)) {
                        SDL_RenderSetClipRect(r, &active_clip);
                        const SDL_Rect dst { cx_pos + 1, cy_pos, vw, vh };
                        // Apply window animation alpha to the blit.
                        SDL_SetTextureAlphaMod(content_bake_tex_, alpha);
                        SDL_RenderCopy(r, content_bake_tex_, nullptr, &dst);
                    }
                    // Reset to outer visible_clip (not nullptr) so chrome painted
                    // after this block still respects the manager scissor.
                    SDL_RenderSetClipRect(r, visible_clip);
                }

            }

            // WIN-SCALE-FIX-1: content_bake_tex_ may have been freed above (SetRenderTarget
            // failure) — re-check and fall through to live render if so.
            if (!content_bake_tex_) {
                // Texture creation or redirect failed — fall back to direct render (always correct).
                // WIN-5 SCISSOR: intersect content viewport clip with visible_clip.
                const SDL_Rect content_clip_rect { cx_pos + 1, cy_pos, vw, vh };
                SDL_Rect active_clip;
                if (IntersectClip(content_clip_rect, visible_clip, active_clip)) {
                    SDL_RenderSetClipRect(r, &active_clip);
                    SDL_RenderSetScale(r, scale_x, scale_y);
                    const float origin_x = static_cast<float>(cx_pos + 1) / scale_x
                                         - static_cast<float>(scroll_x_) + cur_offset_x_;
                    const float origin_y = static_cast<float>(cy_pos) / scale_y
                                         - static_cast<float>(scroll_y_) + cur_offset_y_;
                    content_->OnRender(drawer,
                                       static_cast<s32>(std::lroundf(origin_x)),
                                       static_cast<s32>(std::lroundf(origin_y)));
                    SDL_RenderSetScale(r, 1.0f, 1.0f);
                }
                // Reset to outer visible_clip (not nullptr).
                SDL_RenderSetClipRect(r, visible_clip);
            }
        } else {
            // Degenerate viewport or animating: render content directly (unchanged path).
            // WIN-5 SCISSOR: intersect content viewport clip with visible_clip.
            const SDL_Rect content_clip_rect { cx_pos + 1, cy_pos, vw, vh };
            SDL_Rect active_clip;
            if (IntersectClip(content_clip_rect, visible_clip, active_clip)) {
                SDL_RenderSetClipRect(r, &active_clip);
                SDL_RenderSetScale(r, scale_x, scale_y);
                // Content paints at pre-scale coords. SDL_RenderSetScale multiplies
                // input coords by scale_x/y before writing to physical pixels. To make
                // content land at screen position (cx_pos+1, cy_pos), the pre-scale
                // origin must be ((cx_pos+1)/scale_x, cy_pos/scale_y). Then subtract
                // scroll (in natural coords) to apply scroll offset.
                // v1.10.3.10.1 fix: previous version passed (-scroll_x_, -scroll_y_)
                // which made content paint at world (0, 0) regardless of window
                // position — content visually bled across the entire screen instead
                // of being locked to the window frame.
                //
                // v1.10.3.10.4 centering: cur_offset_x_/y_ computed above.
                const float origin_x = static_cast<float>(cx_pos + 1) / scale_x
                                     - static_cast<float>(scroll_x_)
                                     + cur_offset_x_;
                const float origin_y = static_cast<float>(cy_pos) / scale_y
                                     - static_cast<float>(scroll_y_)
                                     + cur_offset_y_;
                // v2.0.2.2: round, don't truncate.  Truncating loses up to 1 pre-scale
                // unit, which becomes up to `scale` pixels downstream — for windowed
                // launchpads at scale 0.667 that's a ~0.67 px drift that visibly shifts
                // when the window is dragged.  Rounding produces nearest-pixel intent
                // and matches the content-element bridge's own rounding (qd_FolderLaunchpadElement.cpp).
                content_->OnRender(drawer,
                                   static_cast<s32>(std::lroundf(origin_x)),
                                   static_cast<s32>(std::lroundf(origin_y)));
                SDL_RenderSetScale(r, 1.0f, 1.0f);
            }
            // Reset to outer visible_clip (not nullptr).
            SDL_RenderSetClipRect(r, visible_clip);
        }

        // Paint scrollbars AFTER clip reset so they appear on top of content.
        // VSB visible when natural content taller than viewport; HSB likewise.
        // WIN-5 SCISSOR: PaintScrollbars inherits the visible_clip set above.
        PaintScrollbars(r, alpha);
    }

    // WIN-5 SCISSOR: final reset to nullptr after all rendering is done.
    // This clears the outer visible_clip so subsequent draw calls outside
    // this window (dock entries, context menus) are not clipped.
    SDL_RenderSetClipRect(r, nullptr);

}

// ── Animation ─────────────────────────────────────────────────────────────────

void QdWindow::AdvanceAnimation() {
    anim_frame_++;
    if (anim_frame_ > kAnimFrames) {
        anim_frame_ = kAnimFrames;
    }

    const float t = static_cast<float>(anim_frame_) / static_cast<float>(kAnimFrames);

    if (state_ == WindowState::Minimizing) {
        // Cubic ease-in (t³) — matches Nintendo HOME applet's window transition curve.
        // Per qos-native-logic-first.md §5 + native-logic audit 2026-04-29 finding F.
        const float t2 = t * t * t;

        win_x_ = static_cast<s32>(anim_orig_x_ + (anim_target_x_ - anim_orig_x_) * t2);
        win_y_ = static_cast<s32>(anim_orig_y_ + (anim_target_y_ - anim_orig_y_) * t2);
        win_w_ = static_cast<s32>(anim_orig_w_ + (static_cast<float>(SNAP_W) - anim_orig_w_) * t2);
        win_h_ = static_cast<s32>(anim_orig_h_ + (static_cast<float>(SNAP_H) - anim_orig_h_) * t2);
        anim_alpha_ = static_cast<u8>(255.0f * (1.0f - t));

        if (anim_frame_ >= kAnimFrames) {
            state_ = WindowState::Minimized;
            if (on_minimize_requested) {
                on_minimize_requested(this);
            }
        }
    } else if (state_ == WindowState::Restoring) {
        // Cubic ease-out (1-(1-t)³) — symmetric counterpart to Minimizing's t³.
        // Per qos-native-logic-first.md §5 + native-logic audit 2026-04-29 finding F.
        const float inv = 1.0f - t;
        const float t2  = 1.0f - inv * inv * inv;

        win_x_ = static_cast<s32>(anim_target_x_ + (anim_orig_x_ - anim_target_x_) * t2);
        win_y_ = static_cast<s32>(anim_target_y_ + (anim_orig_y_ - anim_target_y_) * t2);
        win_w_ = static_cast<s32>(static_cast<float>(SNAP_W) + (anim_orig_w_ - static_cast<float>(SNAP_W)) * t2);
        win_h_ = static_cast<s32>(static_cast<float>(SNAP_H) + (anim_orig_h_ - static_cast<float>(SNAP_H)) * t2);
        anim_alpha_ = static_cast<u8>(255.0f * t);

        if (anim_frame_ >= kAnimFrames) {
            win_x_      = anim_orig_x_;
            win_y_      = anim_orig_y_;
            win_w_      = anim_orig_w_;
            win_h_      = anim_orig_h_;
            state_      = WindowState::Normal;
            anim_alpha_ = 255;
            FreeTextures();
            // v1.10.3.2 Fix 4: force on_tick on the very next OnRender frame
            // so content (Settings, About) refreshes immediately after restore.
            // Without this, tick_counter_ may need up to kTickRefreshHz (~60)
            // frames to wrap, leaving stale or blank content visible.
            tick_counter_ = kTickRefreshHz;
        }
    }
}

// ── Animation setup ───────────────────────────────────────────────────────────

void QdWindow::BeginMinimizeAnimation() {
    anim_orig_x_ = win_x_;
    anim_orig_y_ = win_y_;
    anim_orig_w_ = win_w_;
    anim_orig_h_ = win_h_;
    anim_frame_  = 0;
    anim_alpha_  = 255;
    state_       = WindowState::Minimizing;
}

void QdWindow::SetMinimizeTarget(s32 target_x, s32 target_y) {
    anim_target_x_ = target_x;
    anim_target_y_ = target_y;
}

void QdWindow::BeginRestoreAnimation(s32 target_x, s32 target_y,
                                     s32 target_w, s32 target_h,
                                     s32 dock_x, s32 dock_y)
{
    win_x_       = dock_x;
    win_y_       = dock_y;
    win_w_       = static_cast<s32>(SNAP_W);
    win_h_       = static_cast<s32>(SNAP_H);

    anim_orig_x_ = target_x;
    anim_orig_y_ = target_y;
    anim_orig_w_ = target_w;
    anim_orig_h_ = target_h;
    anim_target_x_ = dock_x;
    anim_target_y_ = dock_y;
    anim_frame_  = 0;
    anim_alpha_  = 0;
    state_       = WindowState::Restoring;
}

// ── Input ─────────────────────────────────────────────────────────────────────

bool QdWindow::PollEvent(u64 keys_down, u64 keys_up, u64 keys_held,
                         pu::ui::TouchPoint touch_pos)
{
    if (state_ != WindowState::Normal) {
        return false;
    }

    const bool has_touch = (touch_pos.x >= 0 && touch_pos.y >= 0);
    const int  tx = static_cast<int>(touch_pos.x);
    const int  ty = static_cast<int>(touch_pos.y);

    // ── Scrollbar thumb drag (v1.10.3.5) — update position each frame ───────────
    if (vsb_drag_active_) {
        if (has_touch) {
            // Compute scroll_y from drag origin (origin-delta model).
            // A2-OPT-3: use viewport cache (computed in OnRender this frame).
            const s32 vh = cached_vh_;
            const s32 lh = (natural_h_ > 0) ? natural_h_ : vh;
            if (lh > vh) {
                const int track_h = vh;
                const int thumb_h = std::max(16, track_h * vh / lh);
                const int usable  = track_h - thumb_h;
                if (usable > 0) {
                    const s32 delta_y = ty - vsb_drag_origin_y_;
                    const s32 new_sy  = vsb_drag_origin_sy_
                                      + delta_y * (lh - vh) / usable;
                    SetScrollOffset(scroll_x_, new_sy);
                }
            }
        } else {
            vsb_drag_active_ = false;
        }
        return true;
    }
    if (hsb_drag_active_) {
        if (has_touch) {
            // A2-OPT-3: use viewport cache.
            const s32 vw = cached_vw_;
            const s32 lw = (natural_w_ > 0) ? natural_w_ : vw;
            if (lw > vw) {
                const int track_w = vw;
                const int thumb_w = std::max(16, track_w * vw / lw);
                const int usable  = track_w - thumb_w;
                if (usable > 0) {
                    const s32 delta_x = tx - hsb_drag_origin_x_;
                    const s32 new_sx  = hsb_drag_origin_sx_
                                      + delta_x * (lw - vw) / usable;
                    SetScrollOffset(new_sx, scroll_y_);
                }
            }
        } else {
            hsb_drag_active_ = false;
        }
        return true;
    }

    // ── Mouse wheel scroll (v1.10.3.5) ───────────────────────────────────────
    // Only scroll when cursor is over this window's content area.
    if (focused_) {
        HidMouseState mouse_state = {};
        hidGetMouseStates(&mouse_state, 1);
        const s32 wheel_y = static_cast<s32>(mouse_state.wheel_delta_y);
        const s32 wheel_x = static_cast<s32>(mouse_state.wheel_delta_x);
        if (wheel_y != 0) {
            // Positive wheel_delta_y = scroll up (content moves up = scroll offset increases).
            // Match macOS/Linux natural scroll convention (inverted from Windows default).
            SetScrollOffset(scroll_x_, scroll_y_ - wheel_y * 20);
            return true;
        }
        if (wheel_x != 0) {
            SetScrollOffset(scroll_x_ - wheel_x * 20, scroll_y_);
            return true;
        }
    }

    // ── Left analog stick pan/scroll (v1.10.3.8 SCOPE 2) ───────────────────────
    // Read the left stick and scroll the focused window's content.
    // Only fires when this window is focused (same guard as mouse wheel above).
    // Uses the libnx 4.x PadState API (same pattern as qd_Input.cpp:143-150).
    // Dead-zone 15% of ±32768 (~4915 units) prevents drift at rest.
    // Inverted Y: stick UP (+y from HID) → scroll content DOWN (scroll_y_ increases).
    // Pan speed: up to kMaxScrollPx px per frame at full deflection (linear).
    if (focused_) {
        static PadState s_stick_pad;
        static bool     s_stick_pad_init = false;
        if (!s_stick_pad_init) {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeAny(&s_stick_pad);
            s_stick_pad_init = true;
        }
        padUpdate(&s_stick_pad);
        HidAnalogStickState raw_l = padGetStickPos(&s_stick_pad, 0);

        constexpr s32 kDeadZone    = static_cast<s32>(32768 * 0.15f); // ~4915
        constexpr s32 kMaxStick    = 32767;
        constexpr s32 kMaxScrollPx = 12;
        const s32 sx = raw_l.x;
        const s32 sy = raw_l.y;
        const bool sx_live = (sx > kDeadZone || sx < -kDeadZone);
        const bool sy_live = (sy > kDeadZone || sy < -kDeadZone);
        if (sx_live || sy_live) {
            const s32 dx = sx_live ? (sx * kMaxScrollPx / kMaxStick) : 0;
            // Negate sy: stick UP (positive sy per HID) → scroll_y increases.
            const s32 dy = sy_live ? (-sy * kMaxScrollPx / kMaxStick) : 0;
            SetScrollOffset(scroll_x_ + dx, scroll_y_ + dy);
            // Do NOT return true here — titlebar drag and other touch events
            // still need to be processed in the same frame.
        }
    }

    // ── Origin-delta drag: update last-known touch each valid frame ───────────
    if (dragging_titlebar_ && has_touch) {
        drag_last_touch_x_ = tx;
        drag_last_touch_y_ = ty;
    }

    // ── Drag position update (runs on every frame while dragging) ─────────────
    if (dragging_titlebar_) {
        const s32 delta_x = drag_last_touch_x_ - drag_origin_touch_x_;
        const s32 delta_y = drag_last_touch_y_ - drag_origin_touch_y_;

        // QoL-T8: if the touch actually moved the window, this is a real
        // drag — invalidate the recorded tap so a follow-up tap can't
        // accidentally trigger maximize.
        if ((delta_x < -4 || delta_x > 4) || (delta_y < -4 || delta_y > 4)) {
            last_titlebar_tap_tick_ = 0;
        }

        s32 new_x = drag_origin_win_x_ + delta_x;
        s32 new_y = drag_origin_win_y_ + delta_y;

        if (new_y < kMinY) new_y = kMinY;
        if (new_y + kTitlebarH > kDragThresh) new_y = kDragThresh - kTitlebarH;

        win_x_ = new_x;
        win_y_ = new_y;

        // Drag-to-minimize: titlebar bottom dragged below threshold.
        if (GetTitlebarBottomY() >= kDragThresh) {
            dragging_titlebar_ = false;
            if (on_minimize_begin_) {
                on_minimize_begin_(this);
            }
            return true;
        }

        // Drag ends on touch-release.
        if (!has_touch) {
            dragging_titlebar_ = false;
        }
        return true;
    }

    // ── Touch resize drag — update / end ─────────────────────────────────────
    // BeginResizeDrag(tx,ty) is called from fresh-touch-down handling when the BR
    // corner button is tapped.  Subsequent frames arrive here while touch is held.
    // The ZR+cursor path in qd_WindowManager.cpp only covers mouse/cursor resize;
    // this block covers the touch path via the same UpdateResizeDrag/EndResizeDrag API.
    if (resize_drag_active_ && !cursor_drag_active_) {
        // cursor_drag_active_ guard: if ZR is also held, the WM drives the update;
        // don't double-update from both paths.
        if (has_touch) {
            UpdateResizeDrag(tx, ty);
            resize_no_touch_frames_ = 0;
        } else {
            // v2.9.11 — watchdog: end the resize on FIRST no-touch frame
            // (instant on normal touch-up).  Plus an explicit "force-end after
            // N consecutive no-touch frames" safety valve so a window that
            // never sees a touch-up (lost the focus race, event swallowed
            // elsewhere, etc.) cannot stay stuck in resize forever.
            // Without this, PollEvent's "consume while resizing" early-return
            // at line 847 swallows every subsequent input → "nothing opens
            // afterwards" HW bug (creator report 2026-05-18 evening).
            EndResizeDrag();
        }
        return true;
    }
    // v2.9.11 — paranoid watchdog: even when resize_drag_active_ is true but
    // we DON'T reach the branch above (e.g. cursor_drag_active_ is somehow
    // also true), tick the no-touch counter so we can recover from any stuck
    // state combination.  After ~10 frames with no touch, force-reset.
    if (resize_drag_active_ && !has_touch) {
        resize_no_touch_frames_++;
        if (resize_no_touch_frames_ > 10) {
            UL_LOG_WARN("qdesktop: window resize watchdog tripped — "
                        "no touch for >10 frames, force-ending drag");
            EndResizeDrag();
        }
    }

    // ── Fresh touch-down handling ─────────────────────────────────────────────
    if (has_touch) {
        // Hit-test chrome first (grip > discs > titlebar precedence in QdFrame).
        const SDL_Rect fr_touch { win_x_, win_y_, win_w_, win_h_ };
        const FrameRegion touch_rgn = frame_.HitTest({ tx, ty }, fr_touch);
        if (touch_rgn == FrameRegion::Close) {
            if (on_close_requested) on_close_requested(this);
            return true;
        }
        if (touch_rgn == FrameRegion::Maximize) {
            ToggleMaximize(0, static_cast<s32>(TOPBAR_H),
                           static_cast<s32>(SCREEN_W),
                           static_cast<s32>(SCREEN_H - TOPBAR_H - DOCK_H));
            return true;
        }
        if (touch_rgn == FrameRegion::Minimize) {
            if (on_minimize_begin_) on_minimize_begin_(this);
            return true;
        }
        if (touch_rgn == FrameRegion::ResizeBR) {
            BeginResizeDrag(tx, ty);
            return true;
        }

        // Titlebar drag zone: y in [win_y_, win_y_ + kTitlebarH).
        // Corner buttons have priority above; this catches the middle of the titlebar.
        if (tx >= win_x_ && tx < win_x_ + win_w_ &&
            ty >= win_y_ && ty < win_y_ + kTitlebarH)
        {
            // QoL-T8 — double-tap titlebar = toggle maximize.  Two quick
            // taps (< 300 ms apart) on the titlebar without dragging in
            // between fire ToggleMaximize.  Drag-then-tap is suppressed
            // because dragging_titlebar_ → movement zeros last_titlebar_tap_tick_.
            const u64 now_tick = armGetSystemTick();
            const bool have_recent_tap = (last_titlebar_tap_tick_ != 0);
            const u64 dt_ns = have_recent_tap
                ? armTicksToNs(now_tick - last_titlebar_tap_tick_)
                : 0;
            if (have_recent_tap && dt_ns < 300'000'000ULL /* 300 ms */) {
                ToggleMaximize(0, static_cast<s32>(TOPBAR_H),
                               static_cast<s32>(SCREEN_W),
                               static_cast<s32>(SCREEN_H - TOPBAR_H - DOCK_H));
                last_titlebar_tap_tick_ = 0;
                return true;
            }
            // First (or single) tap — record and start drag tracking.
            last_titlebar_tap_tick_ = now_tick;
            dragging_titlebar_   = true;
            drag_origin_win_x_   = win_x_;
            drag_origin_win_y_   = win_y_;
            drag_origin_touch_x_ = tx;
            drag_origin_touch_y_ = ty;
            drag_last_touch_x_   = tx;
            drag_last_touch_y_   = ty;
            return true;
        }

        // Scrollbar hit detection (v1.10.3.5) — must come before content forwarding.
        {
            // A2-OPT-3: use viewport cache.
            const s32 vw = cached_vw_;
            const s32 vh = cached_vh_;
            const int content_left = win_x_ + 1;
            const int content_top  = win_y_ + static_cast<s32>(kTitlebarH);

            // VSB zone: x in [content_left + vw, content_left + vw + kScrollbarW)
            const bool in_vsb_x = (tx >= content_left + vw &&
                                   tx <  content_left + vw + kScrollbarW);
            const bool in_vsb_y = (ty >= content_top && ty < content_top + vh);
            if (in_vsb_x && in_vsb_y) {
                vsb_drag_active_    = true;
                vsb_drag_origin_y_  = ty;
                vsb_drag_origin_sy_ = scroll_y_;
                return true;
            }

            // HSB zone: y in [content_top + vh, content_top + vh + kScrollbarW)
            const bool in_hsb_y = (ty >= content_top + vh &&
                                   ty <  content_top + vh + kScrollbarW);
            const bool in_hsb_x = (tx >= content_left && tx < content_left + vw);
            if (in_hsb_x && in_hsb_y) {
                hsb_drag_active_    = true;
                hsb_drag_origin_x_  = tx;
                hsb_drag_origin_sx_ = scroll_x_;
                return true;
            }
        }

        // Content area: forward to content element if within the content zone.
        // Exclude the status bar so touches on it are not forwarded to content.
        const int content_bottom = win_y_ + win_h_ - kStatusH;
        if (tx >= win_x_ && tx < win_x_ + win_w_ &&
            ty >= win_y_ && ty < content_bottom)
        {
            // ── QoL-T2: finger drag-scroll state machine ────────────────────────
            // Priority: scrollbar drag guards at top of PollEvent already returned
            // if vsb/hsb drag is active. Also skip if any chrome drag is in
            // progress (titlebar / resize). content_drag_active_ is the
            // "was touch active last frame in the content zone" flag.
            //
            // Touch-DOWN (rising edge): arm drag tracking.
            // Touch-HELD + engaged: consume as scroll, don't forward to content.
            // Touch-HELD + not yet engaged: forward as normal tap.
            // Touch-LIFT is handled in the !has_touch block below the outer if.
            //
            // Natural-scroll polarity: dragging finger DOWN reveals content that
            // was above → scroll_y_ decreases (same direction as wheel-up, which
            // fires SetScrollOffset(_, scroll_y_ - wheel_y * 20) at line ~810).
            //
            // Only enable drag when there is actually scrollable content.
            // A2-OPT-3: use viewport cache.
            const s32 vw_drag = cached_vw_;
            const s32 vh_drag = cached_vh_;
            const bool can_scroll_v = (natural_h_ > vh_drag);
            const bool can_scroll_h = (natural_w_ > vw_drag);

            if (!content_drag_active_ && (can_scroll_v || can_scroll_h)) {
                // Rising edge: arm the drag tracker.
                content_drag_active_    = true;
                content_drag_engaged_   = false;
                content_drag_origin_x_  = tx;
                content_drag_origin_y_  = ty;
                content_drag_origin_sx_ = scroll_x_;
                content_drag_origin_sy_ = scroll_y_;
                content_drag_last_y_    = ty;
                // Fall through to forward this first touch to content (tap arm).
            } else if (content_drag_active_ && !content_drag_engaged_) {
                // Held but not yet past jitter threshold: check movement.
                const s32 abs_dy = std::abs(ty - content_drag_origin_y_);
                const s32 abs_dx = std::abs(tx - content_drag_origin_x_);
                if (abs_dy > kContentDragJitterPx || abs_dx > kContentDragJitterPx) {
                    content_drag_engaged_ = true;
                }
                content_drag_last_y_ = ty;
                // Not yet engaged: fall through so content keeps seeing touch.
            }

            if (content_drag_active_ && content_drag_engaged_) {
                // Scroll gesture committed: apply delta from origin and consume.
                const s32 new_sx = can_scroll_h
                    ? content_drag_origin_sx_ - (tx - content_drag_origin_x_)
                    : scroll_x_;
                const s32 new_sy = can_scroll_v
                    ? content_drag_origin_sy_ - (ty - content_drag_origin_y_)
                    : scroll_y_;
                SetScrollOffset(new_sx, new_sy);
                content_drag_last_y_ = ty;
                return true;  // consumed by drag — DO NOT forward to content
            }
            // ── End QoL-T2 drag block ───────────────────────────────────────────

            if (content_) {
                // v1.10.3.10 (SP3): translate screen-absolute touch to content-local
                // natural coordinates, accounting for the centralized SDL_RenderSetScale
                // applied in OnRender and the current scroll offset.  Formula per
                // qd_ContentElement.hpp input contract:
                //   local.x = (screen_x - win_x_ - 1) / cur_scale_x_ + scroll_x_
                //   local.y = (screen_y - win_y_ - TITLEBAR_H) / cur_scale_y_ + scroll_y_
                // cur_scale_x_/y_ are cached each frame in OnRender (1.0f default before
                // first render); scroll_x_/y_ are the current pixel scroll offsets.
                //
                // v1.10.3.10.4 centering: subtract cur_offset_x_/y_ to mirror the
                // pre-scale offset that OnRender adds when uniform-scale leaves
                // empty viewport margin.  Without this, touch inputs land at the
                // wrong natural coordinate after the render origin shifts.
                // v2.0.4.6: use lroundf (round-to-nearest) instead of static_cast<s32>
                // (truncate-toward-zero) to mirror the OnRender origin computation at
                // qd_Window.cpp:462-463 (which already uses lroundf).  At sub-1.0
                // content scales (e.g., 0.589 for 1133×720 folder windows), one screen
                // pixel = ~1.7 natural units, so truncate-vs-round can diverge by up
                // to 1 natural unit between hit-test and paint — the user would see
                // a tap visibly on cell N highlight cell N±1.
                pu::ui::TouchPoint local_touch;
                local_touch.x = static_cast<s32>(std::lroundf(
                                    (static_cast<float>(tx) - static_cast<float>(win_x_) - 1.0f) / cur_scale_x_
                                    - cur_offset_x_))
                                 + scroll_x_;
                local_touch.y = static_cast<s32>(std::lroundf(
                                    (static_cast<float>(ty) - static_cast<float>(win_y_) - static_cast<float>(kTitlebarH)) / cur_scale_y_
                                    - cur_offset_y_))
                                 + scroll_y_;
                content_->OnInput(keys_down, keys_up, keys_held, local_touch);
            }
            return true;
        }

        // Bottom bar zone: touches here are consumed by the WM (BL/BR already handled
        // above); this catch-all ensures no event leaks to the content or caller.
        if (tx >= win_x_ && tx < win_x_ + win_w_ &&
            ty >= content_bottom && ty < win_y_ + win_h_)
        {
            return true;
        }

        // Touch outside this window entirely.
        return false;
    }

    // ── QoL-T2: finger drag-scroll lift (falling edge) ───────────────────────
    // When touch is released while a content drag was being tracked, clear all
    // drag state.  If the drag was engaged (scroll gesture), suppress the lift
    // from reaching content — the drag-end is not a tap.  If it was NOT engaged
    // (finger barely moved), the lift is a tap-release; normal content flow
    // already got the touch-down & held frames, so nothing extra is needed here.
    if (!has_touch && content_drag_active_) {
        content_drag_active_  = false;
        content_drag_engaged_ = false;
        // Drag-end: no forwarding to content (content never saw a "down" after
        // the jitter threshold was exceeded; tap-releases fall through naturally
        // because engaged stays false → content got all prior frames).
    }

    // ── No touch: forward controller input to content ─────────────────────────
    // Only dispatch controller keys when this window is focused and no touch
    // is in progress. This lets Settings/Vault respond to D-pad without the
    // WM needing to know about the content type.
    if (focused_ && content_) {
        // v1.10.3.5 D-pad scroll fallback: ZL held + D-pad scrolls the window.
        // ZL is the active-input-source modifier; held without a new source press
        // it acts as a scroll-mode key so content navigation doesn't conflict.
        const bool zl_held = (keys_held & HidNpadButton_ZL) != 0;
        if (zl_held) {
            constexpr s32 kScrollStep = 40;
            if (keys_down & HidNpadButton_Up)    { SetScrollOffset(scroll_x_, scroll_y_ - kScrollStep); return true; }
            if (keys_down & HidNpadButton_Down)  { SetScrollOffset(scroll_x_, scroll_y_ + kScrollStep); return true; }
            if (keys_down & HidNpadButton_Left)  { SetScrollOffset(scroll_x_ - kScrollStep, scroll_y_); return true; }
            if (keys_down & HidNpadButton_Right) { SetScrollOffset(scroll_x_ + kScrollStep, scroll_y_); return true; }
        }
        // v3.2.1 (BUG-WINBCLOSE): B / + close the window at the chrome level
        // BEFORE the nav-mask forwarding has a chance to hand them to the
        // hosted content element.  Historical bug: B was in nav_mask, so the
        // content received B, called its own Close() (which for layouts like
        // QdLaunchpadElement just hides the element with SetVisible(false)),
        // and the window itself stayed open.  Now B / + match the chrome
        // close-X corner button semantics — the only reasonable behaviour
        // inside a windowed layout.  Fullscreen layouts are unaffected because
        // they never get a QdWindow wrapping them.
        if ((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_Plus)) {
            // v3.7.7: B is hierarchical-back.  Give the hosted content a chance to
            // pop an internal sub-view first (e.g. album Image -> grid); only close
            // the window when it has nothing left to pop.  Plus always closes.
            if ((keys_down & HidNpadButton_B) && content_ && content_->OnBackRequested()) {
                return true;  // content consumed B (popped a level) — keep window open
            }
            if (on_close_requested) on_close_requested(this);
            return true;
        }

        // v3.1.3 (BUG-ZL): ZL added to nav_mask so it is forwarded to content
        // (e.g. QdFolderLaunchpadElement) BEFORE qd_DesktopIcons.cpp's ZL
        // handler gets a turn.  Without ZL here, the desktop ZL ladder fires
        // unconditionally for every window, opening the Close/Min/Max chrome
        // menu instead of the per-item launchpad context menu.
        //
        // B is REMOVED from nav_mask here — it is consumed above as the
        // window-close hotkey, not forwarded to content.
        const u64 nav_mask = HidNpadButton_Up | HidNpadButton_Down |
                             HidNpadButton_Left | HidNpadButton_Right |
                             HidNpadButton_A |
                             HidNpadButton_X | HidNpadButton_Y |
                             HidNpadButton_L | HidNpadButton_R |
                             HidNpadButton_ZL;
        if (keys_down & nav_mask) {
            pu::ui::TouchPoint no_touch;  // default ctor sets x=-1, y=-1
            content_->OnInput(keys_down, keys_up, keys_held, no_touch);
            return true;
        }
    }

    return false;
}

// ── Cursor / ZR interaction (v1.10.3) ─────────────────────────────────────────

void QdWindow::UpdateHoverForCursor(s32 cx, s32 cy) {
    hover_close_    = false;
    hover_maximize_ = false;
    hover_minimize_ = false;
    hover_resize_   = false;

    if (state_ != WindowState::Normal) return;

    const SDL_Rect fr { win_x_, win_y_, win_w_, win_h_ };
    switch (frame_.HitTest({ cx, cy }, fr)) {
        case FrameRegion::Close:    hover_close_    = true; break;
        case FrameRegion::Maximize: hover_maximize_ = true; break;
        case FrameRegion::Minimize: hover_minimize_ = true; break;
        case FrameRegion::ResizeBR: hover_resize_   = true; break;
        default: break;
    }
}

bool QdWindow::ContainsCursor(s32 cx, s32 cy) const {
    if (state_ != WindowState::Normal) return false;
    return cx >= win_x_ && cx < win_x_ + win_w_ &&
           cy >= win_y_ && cy < win_y_ + win_h_;
}

bool QdWindow::TryActivateAtCursor(s32 cx, s32 cy) {
    if (state_ != WindowState::Normal) return false;

    const SDL_Rect fr { win_x_, win_y_, win_w_, win_h_ };
    switch (frame_.HitTest({ cx, cy }, fr)) {
        case FrameRegion::Close:
            if (on_close_requested) on_close_requested(this);
            return true;
        case FrameRegion::Maximize:
            ToggleMaximize(0, static_cast<s32>(TOPBAR_H),
                           static_cast<s32>(SCREEN_W),
                           static_cast<s32>(SCREEN_H - TOPBAR_H - DOCK_H));
            return true;
        case FrameRegion::Minimize:
            if (on_minimize_begin_) on_minimize_begin_(this);
            return true;
        case FrameRegion::ResizeBR:
            BeginResizeDrag(cx, cy);
            return true;
        default:
            return false;
    }
}

void QdWindow::BeginCursorDrag(s32 cx, s32 cy) {
    cursor_drag_active_    = true;
    cursor_drag_origin_wx_ = win_x_;
    cursor_drag_origin_wy_ = win_y_;
    cursor_drag_origin_cx_ = cx;
    cursor_drag_origin_cy_ = cy;
}

void QdWindow::UpdateCursorDrag(s32 cx, s32 cy) {
    if (!cursor_drag_active_) return;

    s32 new_x = cursor_drag_origin_wx_ + (cx - cursor_drag_origin_cx_);
    s32 new_y = cursor_drag_origin_wy_ + (cy - cursor_drag_origin_cy_);

    if (new_y < kMinY) new_y = kMinY;
    if (new_y + kTitlebarH > kDragThresh) new_y = kDragThresh - kTitlebarH;

    win_x_ = new_x;
    win_y_ = new_y;
}

void QdWindow::EndCursorDrag() {
    cursor_drag_active_ = false;
}

// ── Resize drag (v1.10.3.1 BR corner) ─────────────────────────────────────────

static constexpr s32 MIN_WIN_W = 320;
static constexpr s32 MIN_WIN_H = 240;
static constexpr s32 MAX_WIN_W = 1600;
static constexpr s32 MAX_WIN_H = 920;

void QdWindow::BeginResizeDrag(s32 cx, s32 cy) {
    resize_drag_active_    = true;
    resize_drag_origin_wx_ = win_x_;
    resize_drag_origin_wy_ = win_y_;
    resize_drag_origin_ww_ = win_w_;
    resize_drag_origin_wh_ = win_h_;
    resize_drag_origin_cx_ = cx;
    resize_drag_origin_cy_ = cy;
}

void QdWindow::UpdateResizeDrag(s32 cx, s32 cy) {
    if (!resize_drag_active_) return;

    s32 new_w = resize_drag_origin_ww_ + (cx - resize_drag_origin_cx_);
    s32 new_h = resize_drag_origin_wh_ + (cy - resize_drag_origin_cy_);

    if (new_w < MIN_WIN_W) new_w = MIN_WIN_W;
    if (new_w > MAX_WIN_W) new_w = MAX_WIN_W;
    if (new_h < MIN_WIN_H) new_h = MIN_WIN_H;
    if (new_h > MAX_WIN_H) new_h = MAX_WIN_H;

    win_w_ = new_w;
    win_h_ = new_h;
    vp_cache_dirty_ = true;  // A2-OPT-3: size changed
    content_dirty_  = true;  // WIN-2: viewport size changed → bake stale
    // Position is unchanged (window grows rightward / downward from top-left origin).
}

void QdWindow::EndResizeDrag() {
    resize_drag_active_   = false;
    resize_no_touch_frames_ = 0;
    // Re-clamp scroll so the viewport doesn't show gray past content edge after resize.
    SetScrollOffset(scroll_x_, scroll_y_);
}

// v2.9.11 — explicit reset for ALL drag/resize state.  Called from
// WindowManager when this window loses focus, gets minimized, or is closed.
// Without this, a stuck resize_drag_active_ flag silently swallows every
// future input event via PollEvent's "consume while resizing" early-return,
// producing the "nothing opens afterwards" HW bug (creator report
// 2026-05-18 evening).
void QdWindow::ResetInteractionState() {
    resize_drag_active_     = false;
    resize_no_touch_frames_ = 0;
    cursor_drag_active_     = false;
    dragging_titlebar_      = false;
    // QoL-T2: also clear content finger-drag state on forced interaction reset.
    content_drag_active_    = false;
    content_drag_engaged_   = false;
}

// ── Snap / Maximize ───────────────────────────────────────────────────────────

void QdWindow::ApplySnap(SnapTarget target, s32 content_x, s32 content_y,
                          s32 content_w, s32 content_h)
{
    if (target == SnapTarget::None) return;

    // Save pre-snap geometry (only on the first snap after a free state).
    if (!snapped_) {
        pre_snap_x_ = win_x_;
        pre_snap_y_ = win_y_;
        pre_snap_w_ = win_w_;
        pre_snap_h_ = win_h_;
    }

    const s32 half_w    = content_w / 2;
    const s32 half_h    = content_h / 2;
    const s32 cx        = content_x + half_w;  // center x
    const s32 cy        = content_y + half_h;  // center y

    switch (target) {
        case SnapTarget::HalfLeft:
            win_x_ = content_x;
            win_y_ = content_y;
            win_w_ = half_w;
            win_h_ = content_h;
            break;
        case SnapTarget::HalfRight:
            win_x_ = cx;
            win_y_ = content_y;
            win_w_ = half_w;
            win_h_ = content_h;
            break;
        case SnapTarget::QuadrantTopLeft:
            win_x_ = content_x;
            win_y_ = content_y;
            win_w_ = half_w;
            win_h_ = half_h;
            break;
        case SnapTarget::QuadrantTopRight:
            win_x_ = cx;
            win_y_ = content_y;
            win_w_ = half_w;
            win_h_ = half_h;
            break;
        case SnapTarget::QuadrantBottomLeft:
            win_x_ = content_x;
            win_y_ = cy;
            win_w_ = half_w;
            win_h_ = half_h;
            break;
        case SnapTarget::QuadrantBottomRight:
            win_x_ = cx;
            win_y_ = cy;
            win_w_ = half_w;
            win_h_ = half_h;
            break;
        case SnapTarget::None:
            break;
    }

    // Enforce minimum window size after snap geometry.
    if (win_w_ < static_cast<s32>(WIN_MIN_W)) win_w_ = static_cast<s32>(WIN_MIN_W);
    if (win_h_ < static_cast<s32>(WIN_MIN_H)) win_h_ = static_cast<s32>(WIN_MIN_H);

    snapped_        = true;
    vp_cache_dirty_ = true;  // A2-OPT-3: geometry changed by snap
    FreeTextures();  // invalidate title texture — width may have changed
}

void QdWindow::RestoreFromSnap() {
    if (!snapped_) return;
    win_x_          = pre_snap_x_;
    win_y_          = pre_snap_y_;
    win_w_          = pre_snap_w_;
    win_h_          = pre_snap_h_;
    snapped_        = false;
    vp_cache_dirty_ = true;  // A2-OPT-3: geometry restored
    FreeTextures();
}

void QdWindow::ToggleMaximize(s32 content_x, s32 content_y,
                               s32 content_w, s32 content_h)
{
    if (!maximized_) {
        // Save current geometry and go full content-area.
        pre_max_x_ = win_x_;
        pre_max_y_ = win_y_;
        pre_max_w_ = win_w_;
        pre_max_h_ = win_h_;
        win_x_     = content_x;
        win_y_     = content_y;
        win_w_     = content_w;
        win_h_     = content_h;
        maximized_ = true;
    } else {
        // Restore.
        win_x_     = pre_max_x_;
        win_y_     = pre_max_y_;
        win_w_     = pre_max_w_;
        win_h_     = pre_max_h_;
        maximized_ = false;
    }
    vp_cache_dirty_ = true;  // A2-OPT-3: geometry changed by maximize/restore
    FreeTextures();
}

// Z2.7 — Move to absolute screen coords.  Like a manual drag-completion but
// without going through pre_snap / pre_max bookkeeping.  Clears any snap /
// maximize state since explicit movement supersedes both (matches existing
// behavior when the user drags a snapped window in the input ladder).
void QdWindow::MoveTo(s32 win_x, s32 win_y) {
    win_x_          = win_x;
    win_y_          = win_y;
    snapped_        = false;
    maximized_      = false;
    vp_cache_dirty_ = true;  // A2-OPT-3: position changed (scrollbar strips follow win_x_/y_)
    FreeTextures();   // FBO needs to be rebuilt at the new geometry
}

// ── Scroll viewport API (v1.10.3.5) ──────────────────────────────────────────

void QdWindow::GetViewportSize(s32& vw, s32& vh) const {
    // SP3 centralized-scale model (v1.10.3.10): SetContentLogicalSize removed.
    // Viewport: 1 px border each side; titlebar (40) + status bar (34) + 1 px floor.
    // base_vh for a 480-tall window: 480 - 40 - 34 - 1 = 405 px.
    const s32 base_vw = win_w_ - 2;
    const s32 base_vh = win_h_ - kTitlebarH - kStatusH - 1;

    // Scrollbar visibility: determined by natural content dimensions vs base viewport.
    // natural_w_/h_ are read from GetNaturalW/H() at SetContent() time and re-cached
    // when IsNaturalSizeDirty() is true.  If no content yet, treat as equal to viewport.
    const s32 lw = (natural_w_ > 0) ? natural_w_ : base_vw;
    const s32 lh = (natural_h_ > 0) ? natural_h_ : base_vh;

    // If content taller than viewport, VSB appears → reduce vw.
    // If content wider than viewport,  HSB appears → reduce vh.
    // Resolve interdependency: check without scrollbars first, then with.
    bool need_vsb = (lh > base_vh);
    bool need_hsb = (lw > base_vw);
    // Second pass: if VSB narrows vw, does that now require HSB?
    if (need_vsb && !need_hsb) need_hsb = (lw > base_vw - kScrollbarW);
    // And if HSB shortens vh, does that now require VSB?
    if (need_hsb && !need_vsb) need_vsb = (lh > base_vh - kScrollbarW);

    vw = base_vw - (need_vsb ? kScrollbarW : 0);
    vh = base_vh - (need_hsb ? kScrollbarW : 0);
}

void QdWindow::SetScrollOffset(s32 x, s32 y) {
    // A2-OPT-3: use cached viewport if fresh; recompute if dirty (e.g. called
    // from EndResizeDrag before next OnRender).
    if (vp_cache_dirty_) {
        GetViewportSize(cached_vw_, cached_vh_);
        vp_cache_dirty_ = false;
    }
    const s32 vw = cached_vw_;
    const s32 vh = cached_vh_;

    const s32 lw = (natural_w_ > 0) ? natural_w_ : vw;
    const s32 lh = (natural_h_ > 0) ? natural_h_ : vh;

    const s32 max_x = (lw > vw) ? (lw - vw) : 0;
    const s32 max_y = (lh > vh) ? (lh - vh) : 0;

    if (x < 0) x = 0;
    if (x > max_x) x = max_x;
    if (y < 0) y = 0;
    if (y > max_y) y = max_y;

    const bool changed = (x != scroll_x_ || y != scroll_y_);
    scroll_x_ = x;
    scroll_y_ = y;

    if (changed) {
        content_dirty_ = true;  // WIN-2: scroll offset changed → bake stale
        if (on_scroll_update) {
            on_scroll_update(scroll_x_, scroll_y_);
        }
    }
}

// ── Scrollbar paint helpers ───────────────────────────────────────────────────
// Colors per Q OS brand palette (feedback_filament_brand_palette.md):
//   Thumb: cyan #00E5FF at 80% alpha — consistent with focus ring cyan
//   Track: navy #0C0C24 at 30% alpha — subtle fill, matches window background

// v2.6.0 — scrollbar thumb/track resolve from g_QdTheme so they flip per theme.
// Macros expand to pu::ui::Color literals so existing alpha_col(kFoo) wrappers
// still see .r/.g/.b/.a fields. Alpha values preserved from v2.1.0.
#define kScrollThumb pu::ui::Color{ ::ul::menu::qdesktop::g_QdTheme.accent.r,     ::ul::menu::qdesktop::g_QdTheme.accent.g,     ::ul::menu::qdesktop::g_QdTheme.accent.b,     0xCCu }
#define kScrollTrack pu::ui::Color{ ::ul::menu::qdesktop::g_QdTheme.desktop_bg.r, ::ul::menu::qdesktop::g_QdTheme.desktop_bg.g, ::ul::menu::qdesktop::g_QdTheme.desktop_bg.b, 0x4Cu }

void QdWindow::PaintScrollbars(SDL_Renderer* r, u8 alpha) {
    // Already outside the content clip (caller reset to nullptr).
    // A2-OPT-3: cache is warm (OnRender called GetViewportSize this frame).
    const s32 vw = cached_vw_;
    const s32 vh = cached_vh_;

    const s32 base_vw = win_w_ - 2;
    const s32 base_vh = win_h_ - kTitlebarH - kStatusH - 1;

    // SP3 centralized-scale model (v1.10.3.10): scrollbar visibility is determined
    // entirely by natural_w_/h_ vs viewport.  scale_to_viewport_ removed — all content
    // uses SDL_RenderSetScale and scrollbars show whenever natural size exceeds viewport.
    const s32 lw = (natural_w_ > 0) ? natural_w_ : base_vw;
    const s32 lh = (natural_h_ > 0) ? natural_h_ : base_vh;

    const bool need_vsb = (lh > vh + (lw > vw ? kScrollbarW : 0)) || (lh > base_vh);
    const bool need_hsb = (lw > vw + (lh > vh ? kScrollbarW : 0)) || (lw > base_vw);

    const int content_left = win_x_ + 1;
    const int content_top  = win_y_ + static_cast<s32>(kTitlebarH);

    // Alpha-modulate to match window fade during minimize/restore animation.
    auto alpha_col = [alpha](pu::ui::Color c) -> pu::ui::Color {
        c.a = static_cast<u8>(static_cast<int>(c.a) * alpha / 255);
        return c;
    };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // ── Vertical scrollbar ────────────────────────────────────────────────────
    if (need_vsb) {
        const int track_x = content_left + vw;   // immediately right of content viewport
        const int track_y = content_top;
        const int track_h = vh;

        // Track
        pu::ui::Color tc = alpha_col(kScrollTrack);
        SDL_SetRenderDrawColor(r, tc.r, tc.g, tc.b, tc.a);
        SDL_Rect track_rect = { track_x, track_y, kScrollbarW, track_h };
        SDL_RenderFillRect(r, &track_rect);

        // Thumb
        const s32 max_sy  = lh - vh;
        const int thumb_h = std::max(16, track_h * vh / lh);
        const int thumb_y = (max_sy > 0)
            ? track_y + (track_h - thumb_h) * scroll_y_ / max_sy
            : track_y;
        pu::ui::Color thc = alpha_col(kScrollThumb);
        DrawRoundedRect(r, track_x, thumb_y, kScrollbarW, thumb_h, thc);
    }

    // ── Horizontal scrollbar ──────────────────────────────────────────────────
    if (need_hsb) {
        const int track_x = content_left;
        const int track_y = content_top + vh;    // immediately below content viewport
        const int track_w = vw;

        // Track
        pu::ui::Color tc = alpha_col(kScrollTrack);
        SDL_SetRenderDrawColor(r, tc.r, tc.g, tc.b, tc.a);
        SDL_Rect track_rect = { track_x, track_y, track_w, kScrollbarW };
        SDL_RenderFillRect(r, &track_rect);

        // Thumb
        const s32 max_sx  = lw - vw;
        const int thumb_w = std::max(16, track_w * vw / lw);
        const int thumb_x = (max_sx > 0)
            ? track_x + (track_w - thumb_w) * scroll_x_ / max_sx
            : track_x;
        pu::ui::Color thc = alpha_col(kScrollThumb);
        DrawRoundedRect(r, thumb_x, track_y, thumb_w, kScrollbarW, thc);
    }
}

} // namespace ul::menu::qdesktop
