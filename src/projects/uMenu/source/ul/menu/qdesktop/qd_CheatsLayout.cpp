// qd_CheatsLayout.cpp — Atmosphère cheat browser UI element.
//
// W12-CHEATS (v3.4) / W14-CHEATS-INSTALLER (v3.5).
//
// Navigation state machine:
//   TitleList → D-pad up/down to move focus; A to enter a title's cheat list.
//               X → launch HTTPS installer (new in v3.5).
//   CheatList → D-pad up/down; A toggles enabled/disabled; Y views hex code;
//               B returns to TitleList.
//   CodePopup → B (or any) dismisses.
//   Installing → progress bar; Done → auto-return to TitleList with rescan;
//               B → abort + return (if not yet Done/Failed).
//
// All SDL texture lifetime is managed manually (lazy build on first render,
// free in FreeTextures / destructor).

#include <ul/menu/qdesktop/qd_CheatsLayout.hpp>
#include <ul/menu/qdesktop/qd_CheatTitleResolver.hpp>  // v3.6: async NACP resolver
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cctype>

// Forward-declared external (defined in main.cpp).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdCheatsLayout::QdCheatsLayout(const QdTheme &theme)
    : theme_(theme)
{
    UL_LOG_INFO("cheats: QdCheatsLayout ctor");
}

QdCheatsLayout::~QdCheatsLayout() {
    UL_LOG_INFO("cheats: QdCheatsLayout dtor");
    // Stop installer thread before freeing any state it might touch.
    if (installer_) installer_->Stop();
    if (tex_no_cheats_)    { pu::ui::render::DeleteTexture(tex_no_cheats_);    tex_no_cheats_    = nullptr; }
    if (tex_scanning_)     { pu::ui::render::DeleteTexture(tex_scanning_);     tex_scanning_     = nullptr; }
    if (tex_master_badge_) { pu::ui::render::DeleteTexture(tex_master_badge_); tex_master_badge_ = nullptr; }
    if (tex_enabled_)      { pu::ui::render::DeleteTexture(tex_enabled_);      tex_enabled_      = nullptr; }
    if (tex_disabled_)     { pu::ui::render::DeleteTexture(tex_disabled_);     tex_disabled_     = nullptr; }
    if (tex_detail_hint_)  { pu::ui::render::DeleteTexture(tex_detail_hint_);  tex_detail_hint_  = nullptr; }
    FreeDynamicTextures();
    FreePopupTextures();
    FreeInstallTextures();
}

// ── Static blit helper ────────────────────────────────────────────────────────

/*static*/ void QdCheatsLayout::BlitTex(SDL_Renderer *r, SDL_Texture *tex,
                                          const s32 x, const s32 y) {
    if (tex == nullptr || r == nullptr) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    const SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── BuildTextures ─────────────────────────────────────────────────────────────

void QdCheatsLayout::BuildTextures(SDL_Renderer * /*r*/) {
    const auto small  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    tex_no_cheats_    = pu::ui::render::RenderText(medium,
        std::string("No cheat files found. Drop GBATemp cheats into\n"
                    "sdmc:/atmosphere/contents/<TID>/cheats/<BID>.txt"),
        theme_.text_secondary);
    tex_scanning_     = pu::ui::render::RenderText(small,
        std::string("Scanning\xe2\x80\xa6"), theme_.text_secondary);
    tex_master_badge_ = pu::ui::render::RenderText(small,
        std::string("[master]"), theme_.accent);
    tex_enabled_      = pu::ui::render::RenderText(small,
        std::string("\xe2\x97\x89 On"),  theme_.accent);      // ◉ On
    tex_disabled_     = pu::ui::render::RenderText(small,
        std::string("\xe2\x97\x8b Off"), theme_.text_secondary); // ○ Off
    tex_detail_hint_  = pu::ui::render::RenderText(small,
        std::string("Enabled cheats take effect on next game launch."),
        theme_.text_secondary);

    textures_built_ = true;
    UL_LOG_INFO("cheats: BuildTextures done");
}

// ── FreeDynamicTextures ───────────────────────────────────────────────────────

void QdCheatsLayout::FreeDynamicTextures() {
    for (auto *t : title_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    title_textures_.clear();

    for (auto *t : cheat_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    cheat_textures_.clear();
}

// ── BuildTitleTextures ────────────────────────────────────────────────────────

void QdCheatsLayout::BuildTitleTextures(SDL_Renderer * /*r*/) {
    const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    const auto small  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

    for (auto *t : title_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    title_textures_.resize(titles_.size(), nullptr);

    for (size_t i = 0; i < titles_.size(); ++i) {
        // "Title Name  (N cheats)"
        char buf[256];
        snprintf(buf, sizeof(buf), "%s  (%d cheat%s)",
                 titles_[i].title_name.c_str(),
                 titles_[i].cheat_count,
                 titles_[i].cheat_count == 1 ? "" : "s");
        title_textures_[i] = pu::ui::render::RenderText(
            medium, std::string(buf), theme_.text_primary);
        (void)small;
    }
}

// ── BuildCheatTextures ────────────────────────────────────────────────────────

void QdCheatsLayout::BuildCheatTextures(SDL_Renderer * /*r*/) {
    const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    for (auto *t : cheat_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    cheat_textures_.resize(cheats_.size(), nullptr);

    for (size_t i = 0; i < cheats_.size(); ++i) {
        cheat_textures_[i] = pu::ui::render::RenderText(
            medium, cheats_[i].name, theme_.text_primary);
    }
}

// ── BuildPopupTextures ────────────────────────────────────────────────────────

void QdCheatsLayout::BuildPopupTextures(SDL_Renderer * /*r*/) {
    FreePopupTextures();
    if (cheat_focus_ < 0 || static_cast<size_t>(cheat_focus_) >= cheats_.size()) {
        return;
    }
    const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const CheatEntry &ce = cheats_[static_cast<size_t>(cheat_focus_)];

    // Header: name.
    popup_line_textures_.push_back(
        pu::ui::render::RenderText(small, "[" + ce.name + "]", theme_.accent));

    // Each hex-code line.
    for (const auto &l : ce.lines) {
        popup_line_textures_.push_back(
            pu::ui::render::RenderText(small, l, theme_.text_primary));
    }

    // Footer hint.
    popup_line_textures_.push_back(
        pu::ui::render::RenderText(small,
            std::string("B  Dismiss"), theme_.text_secondary));
}

// ── FreePopupTextures ─────────────────────────────────────────────────────────

void QdCheatsLayout::FreePopupTextures() {
    for (auto *t : popup_line_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    popup_line_textures_.clear();
}

// ── Rescan ────────────────────────────────────────────────────────────────────

void QdCheatsLayout::Rescan() {
    FreeDynamicTextures();
    titles_  = QdCheatsManager::ScanInstalledCheats();
    scanned_ = true;
    title_focus_ = 0;
    // Rebuild title textures if we already have a renderer available.
    // If not yet rendered, BuildTextures → BuildTitleTextures on first frame.
    UL_LOG_INFO("cheats: Rescan done, %zu titles", titles_.size());
}

// ── OpenForTitle ──────────────────────────────────────────────────────────────

void QdCheatsLayout::OpenForTitle(const u64 app_id) {
    if (!scanned_) {
        titles_  = QdCheatsManager::ScanInstalledCheats();
        scanned_ = true;
    }

    if (app_id == 0) {
        mode_ = Mode::TitleList;
        return;
    }

    // Convert app_id to lower-case hex string for comparison.
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx",
             static_cast<unsigned long long>(app_id));
    const std::string tid_str(hex);

    for (int i = 0; i < static_cast<int>(titles_.size()); ++i) {
        if (titles_[static_cast<size_t>(i)].tid == tid_str) {
            // Defer texture build to first render (renderer not available here).
            active_title_idx_ = i;
            cheats_  = QdCheatsManager::ParseCheatFile(
                titles_[static_cast<size_t>(i)].file_path);
            // W13-BUG-FIX: seed `enabled_` from the cheats' CURRENT file state
            // (every cheat without leading ';' = enabled), then OVERLAY any
            // explicit sidecar overrides if present.  Previous behaviour
            // defaulted `enabled_` to empty, so the UI thought all cheats were
            // disabled even when the .txt had them all enabled — meaning the
            // first toggle press was a no-op (set-to-already-set state).
            enabled_.clear();
            for (const auto &ce : cheats_) {
                if (ce.currently_enabled) enabled_.insert(ce.name);
            }
            const auto sidecar = QdCheatsManager::ReadEnabledSidecar(
                titles_[static_cast<size_t>(i)].tid,
                titles_[static_cast<size_t>(i)].bid);
            if (!sidecar.empty()) {
                // Sidecar override: trust it as the source of truth (user's
                // explicit toggle history).
                enabled_ = sidecar;
            }
            cheat_focus_ = 0;
            mode_ = Mode::CheatList;
            UL_LOG_INFO("cheats: OpenForTitle app_id=0x%llx -> idx=%d '%s'",
                        static_cast<unsigned long long>(app_id), i,
                        titles_[static_cast<size_t>(i)].title_name.c_str());
            return;
        }
    }

    // No match found — open TitleList so user can browse.
    UL_LOG_INFO("cheats: OpenForTitle app_id=0x%llx not found, falling back to TitleList",
                static_cast<unsigned long long>(app_id));
    mode_ = Mode::TitleList;
}

// ── EnterTitle ────────────────────────────────────────────────────────────────

void QdCheatsLayout::EnterTitle(const int idx, SDL_Renderer *r) {
    if (idx < 0 || static_cast<size_t>(idx) >= titles_.size()) return;

    active_title_idx_ = idx;
    cheats_  = QdCheatsManager::ParseCheatFile(
        titles_[static_cast<size_t>(idx)].file_path);
    // W13-BUG-FIX: seed enabled_ from file state, then overlay sidecar.
    // See OpenForTitle() above for full rationale.
    enabled_.clear();
    for (const auto &ce : cheats_) {
        if (ce.currently_enabled) enabled_.insert(ce.name);
    }
    {
        const auto sidecar = QdCheatsManager::ReadEnabledSidecar(
            titles_[static_cast<size_t>(idx)].tid,
            titles_[static_cast<size_t>(idx)].bid);
        if (!sidecar.empty()) enabled_ = sidecar;
    }
    cheat_focus_ = 0;
    mode_        = Mode::CheatList;
    BuildCheatTextures(r);
    UL_LOG_INFO("cheats: EnterTitle idx=%d '%s', %zu cheats",
                idx, titles_[static_cast<size_t>(idx)].title_name.c_str(),
                cheats_.size());
}

// ── ToggleFocusedCheat ────────────────────────────────────────────────────────

void QdCheatsLayout::ToggleFocusedCheat() {
    if (cheat_focus_ < 0 || static_cast<size_t>(cheat_focus_) >= cheats_.size()) return;

    const CheatEntry &ce = cheats_[static_cast<size_t>(cheat_focus_)];

    // Never disable Master Code.
    if (ce.is_master_code) {
        UL_LOG_INFO("cheats: toggle blocked — Master Code always enabled");
        return;
    }

    if (active_title_idx_ < 0 ||
            static_cast<size_t>(active_title_idx_) >= titles_.size()) {
        return;
    }

    const CheatFile &cf = titles_[static_cast<size_t>(active_title_idx_)];

    if (enabled_.count(ce.name) > 0) {
        enabled_.erase(ce.name);
        UL_LOG_INFO("cheats: disabled '%s'", ce.name.c_str());
    } else {
        enabled_.insert(ce.name);
        UL_LOG_INFO("cheats: enabled  '%s'", ce.name.c_str());
    }

    // W12-CHEATS wave 3: writeback to .txt so Atmosphère sees the change.
    // Build the full (name, enabled) state vector from the live cheats_ list.
    {
        std::vector<std::pair<std::string, bool>> states;
        states.reserve(cheats_.size());
        for (const auto &entry : cheats_) {
            // Master Code is always enabled — never add it as disabled.
            const bool en = entry.is_master_code || (enabled_.count(entry.name) > 0);
            states.emplace_back(entry.name, en);
        }
        const int rc = QdCheatsManager::WriteCheatEnabledState(
            cf.file_path, cf.tid, cf.bid, states);
        if (rc != 0) {
            UL_LOG_WARN("cheats: WriteCheatEnabledState failed for '%s' (rc=%d)",
                        cf.file_path.c_str(), rc);
        }
    }
}

// ── GetBottomHint ─────────────────────────────────────────────────────────────

std::string QdCheatsLayout::GetBottomHint() const {
    switch (mode_) {
        case Mode::TitleList:
            return "A  View \xc2\xb7  X  Install from server \xc2\xb7  B  Close";
        case Mode::CheatList:
            return "A  Toggle \xc2\xb7  Y  View code \xc2\xb7  B  Back";
        case Mode::CodePopup:
            return "B  Dismiss";
        case Mode::Installing:
            return "B  Cancel";
    }
    return "";
}

// ── OnBackRequested — hierarchical B (windowed; matches the in-OnInput B paths) ──
// QdWindow calls this when B is pressed, BEFORE closing the window.  Pop one level
// (popup / installing / cheat-list -> title list) and return true; return false at
// TitleList so the window closes.  The in-OnInput B handlers remain for the
// fullscreen path (where the chrome is absent).
bool QdCheatsLayout::OnBackRequested() {
    switch (mode_) {
        case Mode::CodePopup:                 // hex popup -> cheat list
            FreePopupTextures();
            mode_ = Mode::CheatList;
            return true;
        case Mode::Installing:                 // cancel install -> title list
            if (installer_) { installer_->Stop(); installer_.reset(); }
            FreeInstallTextures();
            mode_ = Mode::TitleList;
            return true;
        case Mode::CheatList:                   // cheat list -> title list
            mode_        = Mode::TitleList;
            cheat_focus_ = 0;
            cheats_.clear();
            for (auto *t : cheat_textures_) {
                if (t != nullptr) pu::ui::render::DeleteTexture(t);
            }
            cheat_textures_.clear();
            return true;
        case Mode::TitleList:
        default:
            return false;                       // top -> window closes
    }
}

// ── GetDebugState ─────────────────────────────────────────────────────────────

std::string QdCheatsLayout::GetDebugState() const {
    switch (mode_) {
        case Mode::TitleList:
            return "cheats:TitleList:focus=" + std::to_string(title_focus_);
        case Mode::CheatList: {
            std::string s = "cheats:CheatList:cheat=" + std::to_string(cheat_focus_);
            s += ":title=" + std::to_string(active_title_idx_);
            return s;
        }
        case Mode::CodePopup:
            return "cheats:CodePopup";
        case Mode::Installing:
            return "cheats:Installing";
        default:
            return "cheats:?";
    }
}

// ── RenderTitleList ───────────────────────────────────────────────────────────

void QdCheatsLayout::RenderTitleList(SDL_Renderer *r, const s32 ox, const s32 oy) {
    if (!scanned_) {
        // Still scanning — show placeholder.
        if (tex_scanning_) BlitTex(r, tex_scanning_, ox + kMargin, oy + kTopbarH + 16);
        return;
    }

    if (titles_.empty()) {
        if (tex_no_cheats_) {
            int tw = 0, th = 0;
            SDL_QueryTexture(tex_no_cheats_, nullptr, nullptr, &tw, &th);
            BlitTex(r, tex_no_cheats_,
                    ox + (GetNaturalW() - tw) / 2,
                    oy + (GetNaturalH() - th) / 2);
        }
        return;
    }

    s32 ly = oy + kTopbarH + kRowGap;
    const s32 list_w = GetNaturalW() - 2 * kMargin;

    for (int i = 0; i < static_cast<int>(titles_.size()); ++i) {
        const bool focused = (i == title_focus_);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        if (focused) {
            SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g,
                                   theme_.accent.b, 60);
        } else {
            SDL_SetRenderDrawColor(r, theme_.surface_glass.r,
                                   theme_.surface_glass.g,
                                   theme_.surface_glass.b, 140);
        }
        const SDL_Rect row_bg = { ox + kMargin, ly, list_w, kRowH };
        SDL_RenderFillRect(r, &row_bg);

        if (focused) {
            SDL_SetRenderDrawColor(r, theme_.focus_ring.r,
                                   theme_.focus_ring.g,
                                   theme_.focus_ring.b, 220);
            SDL_RenderDrawRect(r, &row_bg);
        }

        if (i < static_cast<int>(title_textures_.size()) &&
                title_textures_[static_cast<size_t>(i)] != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(title_textures_[static_cast<size_t>(i)],
                             nullptr, nullptr, &tw, &th);
            // UI fix: clamp width so a long (resolved real) game name can't
            // overrun the row. Clips at the field edge instead of overlapping.
            const s32 max_w  = list_w - 24;
            const s32 draw_w = (tw < max_w) ? tw : max_w;
            const SDL_Rect src = { 0, 0, draw_w, th };
            const SDL_Rect dst = { ox + kMargin + 12, ly + (kRowH - th) / 2,
                                   draw_w, th };
            SDL_RenderCopy(r, title_textures_[static_cast<size_t>(i)], &src, &dst);
        }

        ly += kRowH + kRowGap;
        if (ly + kRowH > oy + GetNaturalH() - kHintBarH) break;
    }
}

// ── RenderCheatList ───────────────────────────────────────────────────────────

void QdCheatsLayout::RenderCheatList(SDL_Renderer *r, const s32 ox, const s32 oy) {
    if (cheats_.empty()) {
        if (tex_no_cheats_) {
            BlitTex(r, tex_no_cheats_, ox + kMargin, oy + kTopbarH + 16);
        }
        return;
    }

    // Left pane: cheat list.
    const s32 list_w   = kDetailPaneX - kMargin - 8;
    s32 ly = oy + kTopbarH + kRowGap;

    for (int i = 0; i < static_cast<int>(cheats_.size()); ++i) {
        const bool focused  = (i == cheat_focus_);
        const CheatEntry &ce = cheats_[static_cast<size_t>(i)];
        const bool en       = ce.is_master_code ||
                               (enabled_.count(ce.name) > 0);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        if (focused) {
            SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g,
                                   theme_.accent.b, 60);
        } else {
            SDL_SetRenderDrawColor(r, theme_.surface_glass.r,
                                   theme_.surface_glass.g,
                                   theme_.surface_glass.b, 100);
        }
        const SDL_Rect row_bg = { ox + kMargin, ly, list_w, kRowH };
        SDL_RenderFillRect(r, &row_bg);

        if (focused) {
            SDL_SetRenderDrawColor(r, theme_.focus_ring.r,
                                   theme_.focus_ring.g,
                                   theme_.focus_ring.b, 220);
            SDL_RenderDrawRect(r, &row_bg);
        }

        // Enabled/disabled indicator.
        SDL_Texture *status_tex = en ? tex_enabled_ : tex_disabled_;
        if (status_tex) {
            int sw = 0, sh = 0;
            SDL_QueryTexture(status_tex, nullptr, nullptr, &sw, &sh);
            BlitTex(r, status_tex, ox + kMargin + 6, ly + (kRowH - sh) / 2);
        }

        // Cheat name.
        s32 name_x = ox + kMargin + 46;
        if (i < static_cast<int>(cheat_textures_.size()) &&
                cheat_textures_[static_cast<size_t>(i)] != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(cheat_textures_[static_cast<size_t>(i)],
                             nullptr, nullptr, &tw, &th);
            // UI fix: clamp width so a long cheat name can't overrun into the
            // right detail pane (it was rendering on top of the detail text).
            const s32 max_w  = (ox + kDetailPaneX) - name_x - 8;
            const s32 draw_w = (tw < max_w) ? tw : max_w;
            // Dim disabled (non-master) cheats.
            const u8 alpha = (!ce.is_master_code && !en) ? 120u : 255u;
            SDL_SetTextureAlphaMod(cheat_textures_[static_cast<size_t>(i)], alpha);
            const SDL_Rect src = { 0, 0, draw_w, th };
            const SDL_Rect dst = { name_x, ly + (kRowH - th) / 2, draw_w, th };
            SDL_RenderCopy(r, cheat_textures_[static_cast<size_t>(i)], &src, &dst);
            SDL_SetTextureAlphaMod(cheat_textures_[static_cast<size_t>(i)], 255);
        }

        // [master] badge.
        if (ce.is_master_code && tex_master_badge_) {
            int mw = 0, mh = 0;
            SDL_QueryTexture(tex_master_badge_, nullptr, nullptr, &mw, &mh);
            BlitTex(r, tex_master_badge_,
                    ox + kMargin + list_w - mw - 8,
                    ly + (kRowH - mh) / 2);
        }

        ly += kRowH + kRowGap;
        if (ly + kRowH > oy + GetNaturalH() - kHintBarH) break;
    }

    // Right detail pane: code-line count + sidecar status.
    {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, theme_.surface_glass.r,
                               theme_.surface_glass.g,
                               theme_.surface_glass.b, 80);
        const SDL_Rect detail_bg = {
            ox + kDetailPaneX,
            oy + kTopbarH,
            GetNaturalW() - kDetailPaneX - kMargin,
            GetNaturalH() - kTopbarH - kHintBarH
        };
        SDL_RenderFillRect(r, &detail_bg);

        if (cheat_focus_ >= 0 &&
                static_cast<size_t>(cheat_focus_) < cheats_.size()) {
            const CheatEntry &ce = cheats_[static_cast<size_t>(cheat_focus_)];
            const bool en2 = ce.is_master_code || (enabled_.count(ce.name) > 0);

            // W15-B FIX: shadow-compare focus + enabled.  Rebuild detail
            // textures only when EITHER changes — previously this block
            // RenderText'd + DeleteTexture'd 2 lines every frame
            // (~120 alloc+destroy/sec while CheatList is open).
            if (cheat_focus_ != detail_last_focus_ ||
                en2 != detail_last_enabled_) {
                const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

                if (detail_info_tex_) {
                    pu::ui::render::DeleteTexture(detail_info_tex_);
                    detail_info_tex_ = nullptr;
                }
                if (detail_status_tex_) {
                    pu::ui::render::DeleteTexture(detail_status_tex_);
                    detail_status_tex_ = nullptr;
                }

                char info[128];
                snprintf(info, sizeof(info), "%d instruction%s",
                         static_cast<int>(ce.lines.size()),
                         ce.lines.size() == 1 ? "" : "s");
                detail_info_tex_ = pu::ui::render::RenderText(
                    small, std::string(info), theme_.text_primary);

                const char *status = ce.is_master_code ? "Always enabled (Master Code)"
                                   : (en2 ? "Enabled" : "Disabled");
                detail_status_tex_ = pu::ui::render::RenderText(
                    small, std::string(status),
                    ce.is_master_code ? theme_.accent
                                      : (en2 ? theme_.accent : theme_.text_secondary));

                detail_last_focus_   = cheat_focus_;
                detail_last_enabled_ = en2;
            }

            if (detail_info_tex_) {
                BlitTex(r, detail_info_tex_, ox + kDetailPaneX + 12, oy + kTopbarH + 12);
            }
            if (detail_status_tex_) {
                BlitTex(r, detail_status_tex_, ox + kDetailPaneX + 12, oy + kTopbarH + 40);
            }
        }

        // "Takes effect on next launch" hint at bottom of detail pane.
        if (tex_detail_hint_) {
            int hw = 0, hh = 0;
            SDL_QueryTexture(tex_detail_hint_, nullptr, nullptr, &hw, &hh);
            BlitTex(r, tex_detail_hint_,
                    ox + kDetailPaneX + 12,
                    oy + GetNaturalH() - kHintBarH - hh - 8);
        }
    }
}

// ── RenderCodePopup ───────────────────────────────────────────────────────────

void QdCheatsLayout::RenderCodePopup(SDL_Renderer *r, const s32 ox, const s32 oy) {
    // Dim background.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    const SDL_Rect full = { ox, oy, GetNaturalW(), GetNaturalH() };
    SDL_RenderFillRect(r, &full);

    // Popup frame.
    const s32 pw = GetNaturalW() - 2 * kMargin;
    const s32 ph = GetNaturalH() - 80;
    const s32 px = ox + kMargin;
    const s32 py = oy + 40;

    SDL_SetRenderDrawColor(r, theme_.surface_glass.r,
                           theme_.surface_glass.g,
                           theme_.surface_glass.b, 230);
    const SDL_Rect popup_bg = { px, py, pw, ph };
    SDL_RenderFillRect(r, &popup_bg);

    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g,
                           theme_.accent.b, 180);
    SDL_RenderDrawRect(r, &popup_bg);

    // Lines.
    s32 ty = py + 12;
    const s32 line_gap = 22;
    for (auto *tex : popup_line_textures_) {
        if (tex == nullptr) continue;
        BlitTex(r, tex, px + 12, ty);
        ty += line_gap;
        if (ty + line_gap > py + ph - 12) break;
    }
}

// ── OnRender ─────────────────────────────────────────────────────────────────

void QdCheatsLayout::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                               const s32 ox, const s32 oy) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) return;

    // Lazy scan on first render.
    if (!scanned_) {
        titles_  = QdCheatsManager::ScanInstalledCheats();
        scanned_ = true;
    }

    // v3.6 absorb wave 1 re-enabled 2026-05-28 — SYNCHRONOUS resolver.
    //
    // The audio-thread crash that disabled the prior async version was
    // root-caused to std::thread spawn racing newlib's reent with SDL_mixer's
    // streaming-MP3 audio thread.  QdCheatTitleResolver was rewritten to
    // run on the calling (main UI) thread with namespace-static NS buffers
    // — no thread spawn, no malloc traffic.  Cost: first Cheats-window
    // open blocks ~500-2000 ms on NS IPC (one frame is "frozen").
    // Subsequent boots are instant — the cache lives at
    // sdmc:/ulaunch/cache/cheat_titles.tsv.
    if (!resolve_kicked_) {
        resolve_kicked_ = true;
        std::vector<std::uint64_t> tids;
        tids.reserve(titles_.size());
        for (const auto &cf : titles_) {
            std::uint64_t t = 0;
            if (std::sscanf(cf.tid.c_str(), "%016llx",
                            reinterpret_cast<unsigned long long *>(&t)) == 1) {
                tids.push_back(t);
            }
        }
        QdCheatTitleResolver::StartResolve(tids, /*on_resolved=*/{});
        // Inline refresh — synchronous resolver has already populated the
        // cache by the time we get here.
        for (auto &cf : titles_) {
            std::uint64_t t = 0;
            if (std::sscanf(cf.tid.c_str(), "%016llx",
                            reinterpret_cast<unsigned long long *>(&t)) == 1) {
                std::string nm = QdCheatTitleResolver::Lookup(t);
                if (!nm.empty()) {
                    cf.title_name = std::move(nm);
                }
            }
        }
        // Re-sort by display name so "Pokémon Sword" sorts as 'P' not 'T'.
        std::sort(titles_.begin(), titles_.end(),
                  [](const CheatFile &a, const CheatFile &b) {
                      return a.title_name < b.title_name;
                  });
        // Invalidate title textures so they rebuild from new names below.
        for (SDL_Texture *t : title_textures_) {
            if (t) SDL_DestroyTexture(t);
        }
        title_textures_.clear();
        labels_refreshed_ = true;
        resolve_done_.store(true, std::memory_order_release);
    }

    // Lazy texture build.
    if (!textures_built_) {
        BuildTextures(r);
        // Build title textures now that scan is done.
        BuildTitleTextures(r);
        // If we were placed into CheatList mode by OpenForTitle() before
        // first render, we need to build cheat textures now too.
        if (mode_ == Mode::CheatList && cheat_textures_.empty()) {
            BuildCheatTextures(r);
        }
    }

    // If title textures vector is stale (size != titles_), rebuild.
    if (title_textures_.size() != titles_.size()) {
        BuildTitleTextures(r);
    }

    // Background fill.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, theme_.desktop_bg.r, theme_.desktop_bg.g,
                           theme_.desktop_bg.b, 255);
    SDL_Rect bg = { ox, oy, GetNaturalW(), GetNaturalH() };
    SDL_RenderFillRect(r, &bg);

    // Top header bar.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g,
                           theme_.topbar_bg.b, 220);
    SDL_Rect header = { ox, oy, GetNaturalW(), kTopbarH };
    SDL_RenderFillRect(r, &header);

    // Header accent line.
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g,
                           theme_.accent.b, 160);
    SDL_RenderDrawLine(r, ox, oy + kTopbarH,
                       ox + GetNaturalW(), oy + kTopbarH);

    // Mode-specific body.
    switch (mode_) {
        case Mode::TitleList:
            RenderTitleList(r, ox, oy);
            break;
        case Mode::CheatList:
            RenderCheatList(r, ox, oy);
            break;
        case Mode::CodePopup:
            RenderCheatList(r, ox, oy);  // render list underneath the popup
            RenderCodePopup(r, ox, oy);
            break;
        case Mode::Installing:
            RenderInstalling(r, ox, oy);
            break;
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdCheatsLayout::OnInput(const u64 keys_down,
                              const u64 /*keys_up*/,
                              const u64 /*keys_held*/,
                              const pu::ui::TouchPoint /*touch_pos*/) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();

    if (mode_ == Mode::CodePopup) {
        // Any button dismisses the popup.
        if (keys_down != 0) {
            FreePopupTextures();
            mode_ = Mode::CheatList;
        }
        return;
    }

    // ── W14: Installer progress mode ─────────────────────────────────────
    if (mode_ == Mode::Installing) {
        // Poll progress snapshot each input tick (called ~60 Hz).
        if (installer_) {
            install_progress_ = installer_->GetProgress();
        }

        // B → cancel (stop thread, return to TitleList).
        if (keys_down & HidNpadButton_B) {
            UL_LOG_INFO("cheats: installer cancelled by user");
            if (installer_) {
                installer_->Stop();
                installer_.reset();
            }
            FreeInstallTextures();
            mode_ = Mode::TitleList;
            return;
        }

        // Auto-transition on Done or Failed.
        const auto phase = install_progress_.phase;
        if (phase == InstallerProgress::Phase::Done) {
            UL_LOG_INFO("cheats: installer Done — rescanning");
            if (installer_) { installer_->Stop(); installer_.reset(); }
            FreeInstallTextures();
            // Rescan so newly installed cheats appear immediately.
            Rescan();
            if (textures_built_) {
                BuildTitleTextures(r);
            }
            mode_ = Mode::TitleList;
        } else if (phase == InstallerProgress::Phase::Failed) {
            // Stay in Installing mode so the error message remains visible.
            // User must press B to dismiss.
        }
        return;
    }

    if (mode_ == Mode::TitleList) {
        const int n = static_cast<int>(titles_.size());

        if (keys_down & HidNpadButton_Up) {
            if (title_focus_ > 0) --title_focus_;
        }
        if (keys_down & HidNpadButton_Down) {
            if (title_focus_ < n - 1) ++title_focus_;
        }
        if (keys_down & HidNpadButton_A) {
            if (n > 0) {
                EnterTitle(title_focus_, r);
            }
        }
        // ── W14: X → launch HTTPS installer ──────────────────────────────
        if (keys_down & HidNpadButton_X) {
            UL_LOG_INFO("cheats: X in TitleList -> StartInstall");
            FreeInstallTextures();
            installer_ = std::make_unique<QdCheatsInstaller>();
            install_progress_ = InstallerProgress{};
            install_last_phase_ = InstallerProgress::Phase::Idle;
            install_last_pct_   = -1;
            installer_->StartInstall();
            mode_ = Mode::Installing;
            return;
        }
        if (keys_down & HidNpadButton_B) {
            UL_LOG_INFO("cheats: B in TitleList -> LoadMenu Main");
            if (g_MenuApplication) {
                g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
            }
        }
        return;
    }

    // Mode::CheatList.
    {
        const int n = static_cast<int>(cheats_.size());

        if (keys_down & HidNpadButton_Up) {
            if (cheat_focus_ > 0) --cheat_focus_;
        }
        if (keys_down & HidNpadButton_Down) {
            if (cheat_focus_ < n - 1) ++cheat_focus_;
        }
        if (keys_down & HidNpadButton_A) {
            ToggleFocusedCheat();
        }
        if (keys_down & HidNpadButton_Y) {
            // Open hex-code popup.
            if (n > 0) {
                BuildPopupTextures(r);
                mode_ = Mode::CodePopup;
                UL_LOG_INFO("cheats: Y -> CodePopup for cheat '%s'",
                            (cheat_focus_ >= 0 &&
                             static_cast<size_t>(cheat_focus_) < cheats_.size())
                                ? cheats_[static_cast<size_t>(cheat_focus_)].name.c_str()
                                : "?");
            }
        }
        if (keys_down & HidNpadButton_B) {
            UL_LOG_INFO("cheats: B in CheatList -> TitleList");
            mode_        = Mode::TitleList;
            cheat_focus_ = 0;
            cheats_.clear();
            for (auto *t : cheat_textures_) {
                if (t != nullptr) pu::ui::render::DeleteTexture(t);
            }
            cheat_textures_.clear();
        }
    }
}

// ── FreeInstallTextures ───────────────────────────────────────────────────────

void QdCheatsLayout::FreeInstallTextures() {
    if (tex_install_phase_)  {
        pu::ui::render::DeleteTexture(tex_install_phase_);
        tex_install_phase_  = nullptr;
    }
    if (tex_install_counts_) {
        pu::ui::render::DeleteTexture(tex_install_counts_);
        tex_install_counts_ = nullptr;
    }
    if (tex_install_error_)  {
        pu::ui::render::DeleteTexture(tex_install_error_);
        tex_install_error_  = nullptr;
    }
    install_last_phase_ = InstallerProgress::Phase::Idle;
    install_last_pct_   = -1;
}

// ── RenderInstalling ──────────────────────────────────────────────────────────

void QdCheatsLayout::RenderInstalling(SDL_Renderer *r,
                                       const s32 ox, const s32 oy) {
    // Poll progress snapshot each render frame so we see live updates.
    if (installer_) {
        install_progress_ = installer_->GetProgress();
    }
    const InstallerProgress &p = install_progress_;
    const bool phase_changed = (p.phase != install_last_phase_);
    const bool pct_changed   = (p.percent / 5 != install_last_pct_ / 5);

    // Rebuild textures when phase or (rounded) percent changes.
    if (phase_changed || pct_changed) {
        FreeInstallTextures();
        const auto small  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
        const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

        // Phase / step label.
        const pu::ui::Color phase_col =
            (p.phase == InstallerProgress::Phase::Failed) ? theme_.accent
                                                          : theme_.text_primary;
        const char *step_str = (p.phase == InstallerProgress::Phase::Failed)
                               ? p.error
                               : p.step;
        if (step_str[0] != '\0') {
            tex_install_phase_ = pu::ui::render::RenderText(
                medium, std::string(step_str), phase_col);
        }

        // Counts line (only meaningful after extraction).
        if (p.tids_done > 0 || p.files_written > 0) {
            char counts[128];
            snprintf(counts, sizeof(counts),
                     "%d game%s updated, %d file%s written",
                     p.tids_done,
                     p.tids_done == 1 ? "" : "s",
                     p.files_written,
                     p.files_written == 1 ? "" : "s");
            tex_install_counts_ = pu::ui::render::RenderText(
                small, std::string(counts), theme_.text_secondary);
        }

        install_last_phase_ = p.phase;
        install_last_pct_   = p.percent;
    }

    // ── Layout ────────────────────────────────────────────────────────────
    // Centre everything vertically in the body area (below topbar, above hint).
    const s32 body_top = oy + kTopbarH + 24;
    const s32 cx       = ox + GetNaturalW() / 2;

    // Phase / error label.
    if (tex_install_phase_) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex_install_phase_, nullptr, nullptr, &tw, &th);
        BlitTex(r, tex_install_phase_, cx - tw / 2, body_top);
    }

    // Progress bar (only while downloading).
    const bool show_bar = (p.phase == InstallerProgress::Phase::DownloadingBundle
                           || p.phase == InstallerProgress::Phase::Extracting);
    if (show_bar) {
        const s32 bar_w   = GetNaturalW() - 2 * kMargin;
        const s32 bar_h   = 14;
        const s32 bar_x   = ox + kMargin;
        const s32 bar_y   = body_top + 48;

        // Background track.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r,
                               theme_.surface_glass.r,
                               theme_.surface_glass.g,
                               theme_.surface_glass.b, 160);
        const SDL_Rect track = { bar_x, bar_y, bar_w, bar_h };
        SDL_RenderFillRect(r, &track);

        // Fill.
        if (p.percent > 0) {
            const s32 fill_w = bar_w * p.percent / 100;
            SDL_SetRenderDrawColor(r, theme_.accent.r,
                                   theme_.accent.g,
                                   theme_.accent.b, 220);
            const SDL_Rect fill = { bar_x, bar_y, fill_w, bar_h };
            SDL_RenderFillRect(r, &fill);
        }

        // Border.
        SDL_SetRenderDrawColor(r, theme_.focus_ring.r,
                               theme_.focus_ring.g,
                               theme_.focus_ring.b, 180);
        SDL_RenderDrawRect(r, &track);

        // Percent label to the right of bar.
        {
            char pct_label[16];
            snprintf(pct_label, sizeof(pct_label), "%d%%", p.percent);
            const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
            SDL_Texture *pct_tex = pu::ui::render::RenderText(
                small, std::string(pct_label), theme_.text_secondary);
            if (pct_tex) {
                int pw = 0, ph = 0;
                SDL_QueryTexture(pct_tex, nullptr, nullptr, &pw, &ph);
                BlitTex(r, pct_tex,
                        bar_x + bar_w - pw,
                        bar_y + bar_h + 4);
                pu::ui::render::DeleteTexture(pct_tex);
            }
        }
    }

    // Counts line.
    if (tex_install_counts_) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex_install_counts_, nullptr, nullptr, &tw, &th);
        const s32 counts_y = show_bar
                             ? body_top + 48 + 14 + 28
                             : body_top + 64;
        BlitTex(r, tex_install_counts_, cx - tw / 2, counts_y);
    }

    // Spinner dots animation for non-downloading phases (Enumerating,
    // FetchingRelease, Extracting).
    const bool spinning =
        (p.phase == InstallerProgress::Phase::EnumeratingInstalledGames
         || p.phase == InstallerProgress::Phase::FetchingReleaseInfo
         || p.phase == InstallerProgress::Phase::Extracting);
    if (spinning) {
        // Simple 3-dot tick animation using SDL_GetTicks.
        const Uint32 dots = (SDL_GetTicks() / 400) % 4;
        char anim[5] = "   ";
        for (Uint32 d = 0; d < dots; ++d) anim[d] = '.';
        anim[3] = '\0';
        const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
        SDL_Texture *spin_tex = pu::ui::render::RenderText(
            small, std::string(anim), theme_.accent);
        if (spin_tex) {
            int sw = 0, sh = 0;
            SDL_QueryTexture(spin_tex, nullptr, nullptr, &sw, &sh);
            BlitTex(r, spin_tex, cx - sw / 2, body_top + 36);
            pu::ui::render::DeleteTexture(spin_tex);
        }
    }

    // Done checkmark / error banner.
    if (p.phase == InstallerProgress::Phase::Done) {
        const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
        SDL_Texture *done_tex = pu::ui::render::RenderText(
            medium,
            std::string("\xe2\x9c\x93 Installed successfully!"),
            theme_.accent);
        if (done_tex) {
            int dw = 0, dh = 0;
            SDL_QueryTexture(done_tex, nullptr, nullptr, &dw, &dh);
            BlitTex(r, done_tex, cx - dw / 2, body_top + 36);
            pu::ui::render::DeleteTexture(done_tex);
        }
    }
}

} // namespace ul::menu::qdesktop
