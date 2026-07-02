// qd_ModsLayout.cpp — Atmosphère LayeredFS mod browser UI element.
//
// B3.1 — launcher-side mod manager.
//
// Navigation state machine:
//   TitleList → D-pad up/down; A → enter SlotList for that title.
//   SlotList  → D-pad up/down; A → toggle slot enabled/disabled; B → TitleList.
//
// All SDL texture lifetime managed manually (lazy build on first render;
// freed in destructor and FreeDynamicTextures).  Pattern mirrors qd_CheatsLayout.

#include <ul/menu/qdesktop/qd_ModsLayout.hpp>
#include <ul/menu/qdesktop/qd_CheatTitleResolver.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>

// Forward-declared external (defined in main.cpp).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdModsLayout::QdModsLayout(const QdTheme &theme)
    : theme_(theme)
{
    UL_LOG_INFO("mods: QdModsLayout ctor");
}

QdModsLayout::~QdModsLayout() {
    UL_LOG_INFO("mods: QdModsLayout dtor");
    if (tex_no_mods_)     { pu::ui::render::DeleteTexture(tex_no_mods_);     tex_no_mods_     = nullptr; }
    if (tex_scanning_)    { pu::ui::render::DeleteTexture(tex_scanning_);    tex_scanning_    = nullptr; }
    if (tex_enabled_)     { pu::ui::render::DeleteTexture(tex_enabled_);     tex_enabled_     = nullptr; }
    if (tex_disabled_)    { pu::ui::render::DeleteTexture(tex_disabled_);    tex_disabled_    = nullptr; }
    if (tex_detail_hint_) { pu::ui::render::DeleteTexture(tex_detail_hint_); tex_detail_hint_ = nullptr; }
    if (detail_type_tex_) { pu::ui::render::DeleteTexture(detail_type_tex_); detail_type_tex_ = nullptr; }
    if (detail_status_tex_) {
        pu::ui::render::DeleteTexture(detail_status_tex_);
        detail_status_tex_ = nullptr;
    }
    FreeDynamicTextures();
}

// ── Static blit helper ────────────────────────────────────────────────────────

/*static*/ void QdModsLayout::BlitTex(SDL_Renderer *r, SDL_Texture *tex,
                                        const s32 x, const s32 y) {
    if (tex == nullptr || r == nullptr) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    const SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── SlotTypeLabel ─────────────────────────────────────────────────────────────

/*static*/ std::string QdModsLayout::SlotTypeLabel(const std::string &name,
                                                     const bool is_dir) {
    if (name == "romfs") {
        return "File replacements (romfs)";
    } else if (name == "exefs") {
        return "Executable patch (exefs)";
    } else if (name == "exefs_patches") {
        return "IPS/pchtxt patch set";
    }
    // Fallback for any other discovered directory.
    return name + (is_dir ? " (dir)" : " (file)");
}

// ── BuildTextures ─────────────────────────────────────────────────────────────

void QdModsLayout::BuildTextures(SDL_Renderer * /*r*/) {
    const auto small  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    tex_no_mods_    = pu::ui::render::RenderText(medium,
        std::string("No LayeredFS mods found.\n"
                    "Drop mod content into sdmc:/atmosphere/contents/<TID>/"),
        theme_.text_secondary);
    tex_scanning_   = pu::ui::render::RenderText(small,
        std::string("Scanning\xe2\x80\xa6"), theme_.text_secondary);
    tex_enabled_    = pu::ui::render::RenderText(small,
        std::string("\xe2\x97\x89 On"),  theme_.accent);         // ◉ On
    tex_disabled_   = pu::ui::render::RenderText(small,
        std::string("\xe2\x97\x8b Off"), theme_.text_secondary); // ○ Off
    tex_detail_hint_ = pu::ui::render::RenderText(small,
        std::string("Changes take effect on next launch."),
        theme_.text_secondary);

    textures_built_ = true;
    UL_LOG_INFO("mods: BuildTextures done");
}

// ── FreeDynamicTextures ───────────────────────────────────────────────────────

void QdModsLayout::FreeDynamicTextures() {
    for (auto *t : title_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    title_textures_.clear();

    for (auto *t : slot_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    slot_textures_.clear();
}

// ── BuildTitleTextures ────────────────────────────────────────────────────────

void QdModsLayout::BuildTitleTextures(SDL_Renderer * /*r*/) {
    const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    for (auto *t : title_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }
    title_textures_.resize(titles_.size(), nullptr);

    for (size_t i = 0; i < titles_.size(); ++i) {
        const ModSet &ms = titles_[i];
        const int n = static_cast<int>(ms.slots.size());
        char buf[256];
        snprintf(buf, sizeof(buf), "%s  (%d slot%s)",
                 ms.title_name.c_str(), n, n == 1 ? "" : "s");
        title_textures_[i] = pu::ui::render::RenderText(
            medium, std::string(buf), theme_.text_primary);
    }
}

// ── BuildSlotTextures ─────────────────────────────────────────────────────────

void QdModsLayout::BuildSlotTextures(SDL_Renderer * /*r*/) {
    const auto medium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    for (auto *t : slot_textures_) {
        if (t != nullptr) pu::ui::render::DeleteTexture(t);
    }

    if (active_title_idx_ < 0 ||
            static_cast<size_t>(active_title_idx_) >= titles_.size()) {
        slot_textures_.clear();
        return;
    }

    const ModSet &ms = titles_[static_cast<size_t>(active_title_idx_)];
    slot_textures_.resize(ms.slots.size(), nullptr);
    for (size_t i = 0; i < ms.slots.size(); ++i) {
        slot_textures_[i] = pu::ui::render::RenderText(
            medium, SlotTypeLabel(ms.slots[i].name, ms.slots[i].is_dir),
            theme_.text_primary);
    }
}

// ── Rescan ────────────────────────────────────────────────────────────────────

void QdModsLayout::Rescan() {
    FreeDynamicTextures();
    titles_  = QdModsManager::ScanInstalledMods();
    scanned_ = true;
    title_focus_      = 0;
    active_title_idx_ = -1;
    UL_LOG_INFO("mods: Rescan done, %zu mod sets", titles_.size());
}

// ── OpenForTitle ──────────────────────────────────────────────────────────────

void QdModsLayout::OpenForTitle(const u64 app_id) {
    if (!scanned_) {
        titles_  = QdModsManager::ScanInstalledMods();
        scanned_ = true;
    }

    if (app_id == 0) {
        mode_ = Mode::TitleList;
        return;
    }

    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx",
             static_cast<unsigned long long>(app_id));
    const std::string tid_str(hex);

    for (int i = 0; i < static_cast<int>(titles_.size()); ++i) {
        if (titles_[static_cast<size_t>(i)].tid == tid_str) {
            active_title_idx_ = i;
            slot_focus_ = 0;
            mode_ = Mode::SlotList;
            UL_LOG_INFO("mods: OpenForTitle app_id=0x%llx -> idx=%d '%s'",
                        static_cast<unsigned long long>(app_id), i,
                        titles_[static_cast<size_t>(i)].title_name.c_str());
            return;
        }
    }

    UL_LOG_INFO("mods: OpenForTitle app_id=0x%llx not found, TitleList",
                static_cast<unsigned long long>(app_id));
    mode_ = Mode::TitleList;
}

// ── EnterTitle ────────────────────────────────────────────────────────────────

void QdModsLayout::EnterTitle(const int idx, SDL_Renderer *r) {
    if (idx < 0 || static_cast<size_t>(idx) >= titles_.size()) return;

    active_title_idx_ = idx;
    slot_focus_       = 0;
    mode_             = Mode::SlotList;

    // Invalidate detail cache.
    detail_last_focus_   = -1;
    detail_last_enabled_ = false;

    BuildSlotTextures(r);
    UL_LOG_INFO("mods: EnterTitle idx=%d '%s', %zu slots",
                idx,
                titles_[static_cast<size_t>(idx)].title_name.c_str(),
                titles_[static_cast<size_t>(idx)].slots.size());
}

// ── ToggleFocusedSlot ─────────────────────────────────────────────────────────

void QdModsLayout::ToggleFocusedSlot() {
    if (active_title_idx_ < 0 ||
            static_cast<size_t>(active_title_idx_) >= titles_.size()) {
        return;
    }
    ModSet &ms = titles_[static_cast<size_t>(active_title_idx_)];

    if (slot_focus_ < 0 || static_cast<size_t>(slot_focus_) >= ms.slots.size()) {
        return;
    }

    const bool ok = QdModsManager::ToggleSlot(ms, static_cast<size_t>(slot_focus_));
    if (!ok) {
        UL_LOG_WARN("mods: ToggleSlot failed for slot %d in '%s'",
                    slot_focus_, ms.tid.c_str());
    }
    // Invalidate the detail pane cache so the new state repaints next frame.
    detail_last_focus_   = -1;
    detail_last_enabled_ = false;
}

// ── GetBottomHint ─────────────────────────────────────────────────────────────

std::string QdModsLayout::GetBottomHint() const {
    switch (mode_) {
        case Mode::TitleList:
            return "A  View mods \xc2\xb7  B  Close";
        case Mode::SlotList:
            return "A  Toggle \xc2\xb7  B  Back";
    }
    return "";
}

// ── OnBackRequested ───────────────────────────────────────────────────────────

bool QdModsLayout::OnBackRequested() {
    switch (mode_) {
        case Mode::SlotList:
            mode_       = Mode::TitleList;
            slot_focus_ = 0;
            for (auto *t : slot_textures_) {
                if (t != nullptr) pu::ui::render::DeleteTexture(t);
            }
            slot_textures_.clear();
            active_title_idx_ = -1;
            return true;
        case Mode::TitleList:
        default:
            return false;   // top level → window closes
    }
}

// ── GetDebugState ─────────────────────────────────────────────────────────────

std::string QdModsLayout::GetDebugState() const {
    switch (mode_) {
        case Mode::TitleList:
            return "mods:TitleList:focus=" + std::to_string(title_focus_);
        case Mode::SlotList:
            return "mods:SlotList:slot=" + std::to_string(slot_focus_)
                 + ":title=" + std::to_string(active_title_idx_);
        default:
            return "mods:?";
    }
}

// ── RenderTitleList ───────────────────────────────────────────────────────────

void QdModsLayout::RenderTitleList(SDL_Renderer *r, const s32 ox, const s32 oy) {
    if (!scanned_) {
        if (tex_scanning_) BlitTex(r, tex_scanning_, ox + kMargin, oy + kTopbarH + 16);
        return;
    }

    if (titles_.empty()) {
        if (tex_no_mods_) {
            int tw = 0, th = 0;
            SDL_QueryTexture(tex_no_mods_, nullptr, nullptr, &tw, &th);
            BlitTex(r, tex_no_mods_,
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

// ── RenderSlotList ────────────────────────────────────────────────────────────

void QdModsLayout::RenderSlotList(SDL_Renderer *r, const s32 ox, const s32 oy) {
    if (active_title_idx_ < 0 ||
            static_cast<size_t>(active_title_idx_) >= titles_.size()) {
        return;
    }
    const ModSet &ms = titles_[static_cast<size_t>(active_title_idx_)];

    if (ms.slots.empty()) {
        if (tex_no_mods_) {
            BlitTex(r, tex_no_mods_, ox + kMargin, oy + kTopbarH + 16);
        }
        return;
    }

    // Left pane: slot list.
    const s32 list_w = kDetailPaneX - kMargin - 8;
    s32 ly = oy + kTopbarH + kRowGap;

    for (int i = 0; i < static_cast<int>(ms.slots.size()); ++i) {
        const bool focused = (i == slot_focus_);
        const ModSlot &sl  = ms.slots[static_cast<size_t>(i)];

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

        // Enabled/disabled indicator (mirrors CheatsLayout).
        SDL_Texture *status_tex = sl.is_enabled ? tex_enabled_ : tex_disabled_;
        if (status_tex) {
            int sw = 0, sh = 0;
            SDL_QueryTexture(status_tex, nullptr, nullptr, &sw, &sh);
            BlitTex(r, status_tex, ox + kMargin + 6, ly + (kRowH - sh) / 2);
        }

        // Slot label.
        if (i < static_cast<int>(slot_textures_.size()) &&
                slot_textures_[static_cast<size_t>(i)] != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(slot_textures_[static_cast<size_t>(i)],
                             nullptr, nullptr, &tw, &th);
            const s32 name_x = ox + kMargin + 46;
            const s32 max_w  = (ox + kDetailPaneX) - name_x - 8;
            const s32 draw_w = (tw < max_w) ? tw : max_w;
            const u8 alpha   = sl.is_enabled ? 255u : 120u;
            SDL_SetTextureAlphaMod(slot_textures_[static_cast<size_t>(i)], alpha);
            const SDL_Rect src = { 0, 0, draw_w, th };
            const SDL_Rect dst = { name_x, ly + (kRowH - th) / 2, draw_w, th };
            SDL_RenderCopy(r, slot_textures_[static_cast<size_t>(i)], &src, &dst);
            SDL_SetTextureAlphaMod(slot_textures_[static_cast<size_t>(i)], 255);
        }

        ly += kRowH + kRowGap;
        if (ly + kRowH > oy + GetNaturalH() - kHintBarH) break;
    }

    // Right detail pane (mirrors CheatsLayout::RenderCheatList detail block).
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

        if (slot_focus_ >= 0 &&
                static_cast<size_t>(slot_focus_) < ms.slots.size()) {
            const ModSlot &sl = ms.slots[static_cast<size_t>(slot_focus_)];

            // Shadow-compare to avoid per-frame RenderText
            // (same W15-B optimisation from CheatsLayout).
            if (slot_focus_ != detail_last_focus_ ||
                sl.is_enabled != detail_last_enabled_) {
                const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

                if (detail_type_tex_) {
                    pu::ui::render::DeleteTexture(detail_type_tex_);
                    detail_type_tex_ = nullptr;
                }
                if (detail_status_tex_) {
                    pu::ui::render::DeleteTexture(detail_status_tex_);
                    detail_status_tex_ = nullptr;
                }

                detail_type_tex_ = pu::ui::render::RenderText(
                    small, SlotTypeLabel(sl.name, sl.is_dir), theme_.text_primary);

                const char *status = sl.is_enabled ? "Enabled" : "Disabled";
                detail_status_tex_ = pu::ui::render::RenderText(
                    small, std::string(status),
                    sl.is_enabled ? theme_.accent : theme_.text_secondary);

                detail_last_focus_   = slot_focus_;
                detail_last_enabled_ = sl.is_enabled;
            }

            if (detail_type_tex_) {
                BlitTex(r, detail_type_tex_, ox + kDetailPaneX + 12, oy + kTopbarH + 12);
            }
            if (detail_status_tex_) {
                BlitTex(r, detail_status_tex_, ox + kDetailPaneX + 12, oy + kTopbarH + 40);
            }
        }

        // "Changes take effect on next launch." hint at bottom of detail pane.
        if (tex_detail_hint_) {
            int hw = 0, hh = 0;
            SDL_QueryTexture(tex_detail_hint_, nullptr, nullptr, &hw, &hh);
            BlitTex(r, tex_detail_hint_,
                    ox + kDetailPaneX + 12,
                    oy + GetNaturalH() - kHintBarH - hh - 8);
        }
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdModsLayout::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                              const s32 ox, const s32 oy) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) return;

    // Lazy scan on first render.
    if (!scanned_) {
        titles_  = QdModsManager::ScanInstalledMods();
        scanned_ = true;
    }

    // Synchronous NACP title resolution (same pattern as QdCheatsLayout::OnRender).
    // One-shot per session; blocks ~500-2000 ms on first open; instant on subsequent
    // boots from the on-disk cache (sdmc:/ulaunch/cache/cheat_titles.tsv shared
    // with the Cheats UI — no duplicate cache file needed).
    if (!resolve_kicked_) {
        resolve_kicked_ = true;
        std::vector<std::uint64_t> tids;
        tids.reserve(titles_.size());
        for (const auto &ms : titles_) {
            std::uint64_t t = 0;
            if (std::sscanf(ms.tid.c_str(), "%016llx",
                            reinterpret_cast<unsigned long long *>(&t)) == 1) {
                tids.push_back(t);
            }
        }
        // Kick the resolver (fills the shared cheat_titles.tsv cache).
        QdCheatTitleResolver::StartResolve(tids, /*on_resolved=*/{});
        // Inline refresh — resolver runs synchronously; cache is populated now.
        for (auto &ms : titles_) {
            std::uint64_t t = 0;
            if (std::sscanf(ms.tid.c_str(), "%016llx",
                            reinterpret_cast<unsigned long long *>(&t)) == 1) {
                std::string nm = QdCheatTitleResolver::Lookup(t);
                if (!nm.empty()) {
                    ms.title_name = std::move(nm);
                }
            }
        }
        std::sort(titles_.begin(), titles_.end(),
                  [](const ModSet &a, const ModSet &b) {
                      return a.title_name < b.title_name;
                  });
        // Invalidate title textures — they will rebuild below with real names.
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
        BuildTitleTextures(r);
        // If OpenForTitle() placed us into SlotList before first render,
        // build slot textures now too.
        if (mode_ == Mode::SlotList && slot_textures_.empty()) {
            BuildSlotTextures(r);
        }
    }

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
        case Mode::SlotList:
            RenderSlotList(r, ox, oy);
            break;
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdModsLayout::OnInput(const u64 keys_down,
                            const u64 /*keys_up*/,
                            const u64 /*keys_held*/,
                            const pu::ui::TouchPoint /*touch_pos*/) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();

    if (mode_ == Mode::TitleList) {
        const int n = static_cast<int>(titles_.size());

        if (keys_down & HidNpadButton_Up) {
            if (title_focus_ > 0) --title_focus_;
        }
        if (keys_down & HidNpadButton_Down) {
            if (title_focus_ < n - 1) ++title_focus_;
        }
        if ((keys_down & HidNpadButton_A) && n > 0) {
            EnterTitle(title_focus_, r);
        }
        if (keys_down & HidNpadButton_B) {
            UL_LOG_INFO("mods: B in TitleList -> LoadMenu Main");
            if (g_MenuApplication) {
                g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
            }
        }
        return;
    }

    // Mode::SlotList.
    if (active_title_idx_ < 0 ||
            static_cast<size_t>(active_title_idx_) >= titles_.size()) {
        return;
    }
    const ModSet &ms = titles_[static_cast<size_t>(active_title_idx_)];
    const int n = static_cast<int>(ms.slots.size());

    if (keys_down & HidNpadButton_Up) {
        if (slot_focus_ > 0) --slot_focus_;
    }
    if (keys_down & HidNpadButton_Down) {
        if (slot_focus_ < n - 1) ++slot_focus_;
    }
    if (keys_down & HidNpadButton_A) {
        ToggleFocusedSlot();
    }
    if (keys_down & HidNpadButton_B) {
        UL_LOG_INFO("mods: B in SlotList -> TitleList");
        mode_       = Mode::TitleList;
        slot_focus_ = 0;
        for (auto *t : slot_textures_) {
            if (t != nullptr) pu::ui::render::DeleteTexture(t);
        }
        slot_textures_.clear();
        active_title_idx_ = -1;
    }
}

} // namespace ul::menu::qdesktop
