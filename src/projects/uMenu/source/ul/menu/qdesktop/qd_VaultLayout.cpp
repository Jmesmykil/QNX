// qd_VaultLayout.cpp — Finder-style NRO file browser element for uMenu C++ (v1.0.0).
// Stage 1 of docs/45_HBMenu_Replacement_Design.md: vault skeleton.
// Two-pane UI: sidebar (6 canonical roots) + main pane (dirs + NROs + files).
// NRO launch: smi::LaunchHomebrewLibraryApplet.
// Icon decode: ExtractNroIcon + QdIconCache (same path as QdDesktopIconsElement).

#include <ul/menu/qdesktop/qd_VaultLayout.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>

namespace ul::menu::qdesktop {

// ── Sidebar canonical roots ────────────────────────────────────────────────────
// Design doc §3.2: Desktop / Switch / Logs / Atmosphère / SD Root / Themes.

const QdVaultLayout::SidebarRoot
QdVaultLayout::SIDEBAR_ROOTS[QdVaultLayout::SIDEBAR_ROOT_COUNT] = {
    { "Switch (NROs)", "sdmc:/switch/"              },
    { "Atmosphère",    "sdmc:/atmosphere/"          },
    { "Q OS",          "sdmc:/qos-shell/"           },
    { "Logs",          "sdmc:/qos-shell/logs/"      },
    { "Themes",        "sdmc:/atmosphere/contents/" },
    { "SD Root",       "sdmc:/"                     },
};

// ── Vault column computation ───────────────────────────────────────────────────

s32 QdVaultLayout::MainPaneCols() {
    // Main pane width = screen width minus sidebar minus one gap.
    const s32 pane_w = 1920 - VAULT_SIDEBAR_W - VAULT_CELL_GAP;
    const s32 col_stride = VAULT_CELL_W + VAULT_CELL_GAP;
    const s32 cols = pane_w / col_stride;
    return (cols < 1) ? 1 : cols;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdVaultLayout::QdVaultLayout(const QdTheme &theme)
    : theme_(theme), entry_count_(0), focus_idx_(0), scroll_offset_(0)
{
    UL_LOG_INFO("vault: QdVaultLayout ctor");
    cwd_[0] = '\0';

    // Null-init all per-entry and sidebar texture slots.
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        entries_[i].icon_tex     = nullptr;
        entries_[i].icon_decoded = false;
        entries_[i].name[0]      = '\0';
        entries_[i].full_path[0] = '\0';
        entries_[i].kind         = EntryKind::OtherFile;
        name_tex_[i]             = nullptr;
    }
    for (size_t i = 0; i < SIDEBAR_ROOT_COUNT; ++i) {
        sidebar_tex_[i] = nullptr;
    }

    // Open the default root so the vault is usable right after construction.
    Navigate("sdmc:/switch/");
}

QdVaultLayout::~QdVaultLayout() {
    UL_LOG_INFO("vault: QdVaultLayout dtor");
    FreeEntryTextures();
    for (size_t i = 0; i < SIDEBAR_ROOT_COUNT; ++i) {
        if (sidebar_tex_[i] != nullptr) {
            SDL_DestroyTexture(sidebar_tex_[i]);
            sidebar_tex_[i] = nullptr;
        }
    }
}

// ── Navigate ─────────────────────────────────────────────────────────────────

void QdVaultLayout::Navigate(const char *path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    FreeEntryTextures();
    snprintf(cwd_, sizeof(cwd_), "%s", path);
    // Ensure trailing slash.
    const size_t len = strnlen(cwd_, sizeof(cwd_) - 1);
    if (len > 0 && cwd_[len - 1] != '/') {
        if (len + 1 < sizeof(cwd_)) {
            cwd_[len]     = '/';
            cwd_[len + 1] = '\0';
        }
    }
    ScanCurrentDirectory();
}

// ── ScanCurrentDirectory ──────────────────────────────────────────────────────

void QdVaultLayout::ScanCurrentDirectory() {
    entry_count_  = 0;
    focus_idx_    = 0;
    scroll_offset_ = 0;

    if (cwd_[0] == '\0') {
        return;
    }

    DIR *d = opendir(cwd_);
    if (d == nullptr) {
        UL_LOG_WARN("vault: opendir failed for '%s'", cwd_);
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != nullptr && entry_count_ < MAX_ENTRIES) {
        // Skip hidden and navigation entries.
        if (de->d_name[0] == '.') {
            continue;
        }

        Entry &e = entries_[entry_count_];
        e.icon_tex     = nullptr;
        e.icon_decoded = false;

        // Helpers: bounded string copy/append via memcpy to avoid
        // -Werror=format-truncation and -Werror=stringop-truncation.
        // Truncation of display names (name[64]) is intentional.
        auto safe_copy = [](char *dst, size_t dst_cap, const char *src) -> size_t {
            const size_t src_len = strnlen(src, dst_cap);
            const size_t copy_n  = (src_len < dst_cap - 1) ? src_len : (dst_cap - 1);
            __builtin_memcpy(dst, src, copy_n);
            dst[copy_n] = '\0';
            return copy_n;
        };
        auto safe_append = [](char *dst, size_t dst_cap, const char *src, size_t already_used) {
            if (already_used + 1 >= dst_cap) { return; }
            const size_t remaining = dst_cap - already_used - 1;
            const size_t src_len   = strnlen(src, remaining);
            const size_t copy_n    = (src_len < remaining) ? src_len : remaining;
            __builtin_memcpy(dst + already_used, src, copy_n);
            dst[already_used + copy_n] = '\0';
        };

        // Determine kind.
        if (de->d_type == DT_DIR) {
            e.kind = EntryKind::Folder;
            safe_copy(e.name, sizeof(e.name), de->d_name);
            size_t used = safe_copy(e.full_path, sizeof(e.full_path), cwd_);
            safe_append(e.full_path, sizeof(e.full_path), de->d_name, used);
            // Append trailing '/'.
            used = strnlen(e.full_path, sizeof(e.full_path));
            if (used + 1 < sizeof(e.full_path)) {
                e.full_path[used]     = '/';
                e.full_path[used + 1] = '\0';
            }
        } else {
            // Check for .nro suffix (case-insensitive) using full d_name length.
            const size_t dlen = strnlen(de->d_name, 256);
            bool is_nro = false;
            if (dlen > 4) {
                const char *ext = de->d_name + dlen - 4;
                is_nro = (ext[0] == '.' &&
                          (ext[1] == 'n' || ext[1] == 'N') &&
                          (ext[2] == 'r' || ext[2] == 'R') &&
                          (ext[3] == 'o' || ext[3] == 'O'));
            }
            e.kind = is_nro ? EntryKind::Nro : EntryKind::OtherFile;

            // Copy display name; truncation to 64 bytes is intentional.
            safe_copy(e.name, sizeof(e.name), de->d_name);
            if (is_nro && dlen > 4) {
                const size_t strip = dlen - 4;
                if (strip < sizeof(e.name)) {
                    e.name[strip] = '\0';
                }
            }
            // Build full_path.
            const size_t used = safe_copy(e.full_path, sizeof(e.full_path), cwd_);
            safe_append(e.full_path, sizeof(e.full_path), de->d_name, used);
        }

        name_tex_[entry_count_] = nullptr;
        ++entry_count_;
    }
    closedir(d);
    UL_LOG_INFO("vault: scan '%s' → %zu entries", cwd_, entry_count_);
}

// ── FreeEntryTextures ─────────────────────────────────────────────────────────

void QdVaultLayout::FreeEntryTextures() {
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        if (entries_[i].icon_tex != nullptr) {
            SDL_DestroyTexture(entries_[i].icon_tex);
            entries_[i].icon_tex     = nullptr;
            entries_[i].icon_decoded = false;
        }
        if (name_tex_[i] != nullptr) {
            SDL_DestroyTexture(name_tex_[i]);
            name_tex_[i] = nullptr;
        }
    }
}

// ── DecodeNroIcon ─────────────────────────────────────────────────────────────

bool QdVaultLayout::DecodeNroIcon(Entry &e) {
    e.icon_decoded = true;

    if (e.full_path[0] == '\0') {
        return false;
    }

    // Check in-memory + on-disk cache first.
    const u8 *cached = cache_.Get(e.full_path);
    if (cached != nullptr) {
        return true; // Already cached — icon_tex will be built during render.
    }

    // Not cached — extract from NRO ASET section.
    NroIconResult res = ExtractNroIcon(e.full_path);
    if (res.valid && res.pixels != nullptr && res.width > 0 && res.height > 0) {
        cache_.Put(e.full_path, res.pixels, res.width, res.height);
        FreeNroIcon(res);
        return true;
    }

    // Extraction failed — generate DJB2-derived fallback (F-05: free always).
    UL_LOG_WARN("vault: DecodeNroIcon: ExtractNroIcon failed for %s"
                " valid=%d width=%d height=%d — using fallback colour block",
                e.full_path,
                static_cast<int>(res.valid),
                res.width,
                res.height);
    FreeNroIcon(res);
    u8 *fallback = MakeFallbackIcon(e.full_path);
    if (fallback != nullptr) {
        cache_.Put(e.full_path,
                   fallback,
                   static_cast<s32>(CACHE_ICON_W),
                   static_cast<s32>(CACHE_ICON_H));
        delete[] fallback;
    }
    return false;
}

// ── EntryRect ─────────────────────────────────────────────────────────────────

bool QdVaultLayout::EntryRect(size_t i,
                               s32 &out_x, s32 &out_y,
                               s32 origin_x, s32 origin_y) const {
    if (i >= entry_count_) {
        return false;
    }
    const s32 cols      = MainPaneCols();
    const s32 col_stride = VAULT_CELL_W + VAULT_CELL_GAP;
    const s32 row_stride = VAULT_CELL_H + VAULT_CELL_GAP;

    const s32 col = static_cast<s32>(i) % cols;
    const s32 row = static_cast<s32>(i) / cols;

    // Main pane starts right of the sidebar + one gap.
    const s32 pane_left = origin_x + VAULT_SIDEBAR_W + VAULT_CELL_GAP;
    const s32 pane_top  = origin_y + VAULT_BODY_TOP + VAULT_PATHBAR_H + VAULT_CELL_GAP;

    out_x = pane_left + col * col_stride;
    out_y = pane_top  + row * row_stride - scroll_offset_;
    return true;
}

// ── RenderSidebar ─────────────────────────────────────────────────────────────

void QdVaultLayout::RenderSidebar(SDL_Renderer *r,
                                   s32 origin_x, s32 origin_y) const {
    // Background panel.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
                           theme_.surface_glass.r,
                           theme_.surface_glass.g,
                           theme_.surface_glass.b,
                           0xD0u);
    SDL_Rect sidebar_bg {
        origin_x,
        origin_y + VAULT_BODY_TOP,
        VAULT_SIDEBAR_W,
        VAULT_BODY_H
    };
    SDL_RenderFillRect(r, &sidebar_bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Right border separator (1 px).
    SDL_SetRenderDrawColor(r,
                           theme_.focus_ring.r,
                           theme_.focus_ring.g,
                           theme_.focus_ring.b,
                           0x40u);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect sep {
        origin_x + VAULT_SIDEBAR_W - 1,
        origin_y + VAULT_BODY_TOP,
        1,
        VAULT_BODY_H
    };
    SDL_RenderFillRect(r, &sep);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Sidebar root labels.
    static constexpr s32 SIDEBAR_ITEM_H  = 42;
    static constexpr s32 SIDEBAR_LABEL_X = 18;
    static constexpr s32 SIDEBAR_TOP_PAD = 18;

    for (size_t i = 0; i < SIDEBAR_ROOT_COUNT; ++i) {
        const s32 item_y = origin_y + VAULT_BODY_TOP + SIDEBAR_TOP_PAD
                           + static_cast<s32>(i) * SIDEBAR_ITEM_H;

        // Highlight the entry that matches cwd_.
        const bool active = (strncmp(cwd_, SIDEBAR_ROOTS[i].path, MAX_PATH) == 0);
        if (active) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r,
                                   theme_.accent.r,
                                   theme_.accent.g,
                                   theme_.accent.b,
                                   0x30u);
            SDL_Rect hi {
                origin_x,
                item_y,
                VAULT_SIDEBAR_W - 1,
                SIDEBAR_ITEM_H - 4
            };
            SDL_RenderFillRect(r, &hi);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }

        // Lazy-build and cache the sidebar label texture.
        // sidebar_tex_ is declared const on this method; cast is safe because
        // the underlying array is mutable — const on the method means we don't
        // mutate logical state, but texture caching is a rendering detail.
        SDL_Texture **stex = const_cast<SDL_Texture **>(&sidebar_tex_[i]);
        if (*stex == nullptr) {
            const pu::ui::Color lbl_clr = active
                ? theme_.accent
                : theme_.text_secondary;
            *stex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(SIDEBAR_ROOTS[i].label),
                lbl_clr);
        }
        if (*stex != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(*stex, nullptr, nullptr, &tw, &th);
            SDL_Rect dst {
                origin_x + SIDEBAR_LABEL_X,
                item_y + (SIDEBAR_ITEM_H - th) / 2,
                tw, th
            };
            SDL_RenderCopy(r, *stex, nullptr, &dst);
        }
    }
}

// ── RenderMainPane ────────────────────────────────────────────────────────────

void QdVaultLayout::RenderMainPane(SDL_Renderer *r,
                                   s32 origin_x, s32 origin_y) {
    // Path bar background.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
                           theme_.topbar_bg.r,
                           theme_.topbar_bg.g,
                           theme_.topbar_bg.b,
                           0xE0u);
    const s32 pathbar_left = origin_x + VAULT_SIDEBAR_W;
    SDL_Rect pathbar {
        pathbar_left,
        origin_y + VAULT_BODY_TOP,
        1920 - VAULT_SIDEBAR_W,
        VAULT_PATHBAR_H
    };
    SDL_RenderFillRect(r, &pathbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Path text (lazy — rebuild when cwd_ changes is not cached; we rebuild
    // unconditionally at small cost because the path bar changes infrequently).
    {
        static char last_cwd[MAX_PATH] = "";
        static SDL_Texture *path_tex   = nullptr;
        if (strncmp(last_cwd, cwd_, MAX_PATH) != 0) {
            if (path_tex != nullptr) {
                SDL_DestroyTexture(path_tex);
                path_tex = nullptr;
            }
            snprintf(last_cwd, sizeof(last_cwd), "%s", cwd_);
            const pu::ui::Color pc { 0xE0u, 0xE0u, 0xF0u, 0xFFu };
            path_tex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(cwd_), pc,
                static_cast<u32>(1920 - VAULT_SIDEBAR_W - 24));
        }
        if (path_tex != nullptr) {
            int pw = 0, ph = 0;
            SDL_QueryTexture(path_tex, nullptr, nullptr, &pw, &ph);
            SDL_Rect dst {
                pathbar_left + 12,
                origin_y + VAULT_BODY_TOP + (VAULT_PATHBAR_H - ph) / 2,
                pw, ph
            };
            SDL_RenderCopy(r, path_tex, nullptr, &dst);
        }
    }

    // Entry grid cells.
    const s32 visible_top    = origin_y + VAULT_BODY_TOP + VAULT_PATHBAR_H;
    const s32 visible_bottom = origin_y + VAULT_BODY_TOP + VAULT_BODY_H;

    for (size_t i = 0; i < entry_count_; ++i) {
        s32 cx, cy;
        if (!EntryRect(i, cx, cy, origin_x, origin_y)) {
            continue;
        }
        // Cull cells fully above or below the visible area.
        if (cy + VAULT_CELL_H < visible_top || cy > visible_bottom) {
            continue;
        }

        Entry &e = entries_[i];
        const bool is_focused = (i == focus_idx_);

        // ── Cell background ──────────────────────────────────────────────
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const u8 bg_alpha = is_focused ? 0x70u : 0x40u;
        SDL_SetRenderDrawColor(r,
                               theme_.surface_glass.r,
                               theme_.surface_glass.g,
                               theme_.surface_glass.b,
                               bg_alpha);
        SDL_Rect cell_bg { cx, cy, VAULT_CELL_W, VAULT_CELL_H };
        SDL_RenderFillRect(r, &cell_bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // ── Icon area ────────────────────────────────────────────────────
        const s32 icon_x = cx + (VAULT_CELL_W - VAULT_ICON_SIZE) / 2;
        const s32 icon_y = cy + 6;

        if (e.kind == EntryKind::Nro) {
            // Lazy decode on first render.
            if (!e.icon_decoded) {
                DecodeNroIcon(e);
            }
            // Build SDL_Texture from cache on first paint.
            if (e.icon_tex == nullptr) {
                const u8 *bgra = cache_.Get(e.full_path);
                if (bgra != nullptr) {
                    e.icon_tex = SDL_CreateTexture(r,
                                                   SDL_PIXELFORMAT_ABGR8888,
                                                   SDL_TEXTUREACCESS_STREAMING,
                                                   static_cast<int>(CACHE_ICON_W),
                                                   static_cast<int>(CACHE_ICON_H));
                    if (e.icon_tex != nullptr) {
                        SDL_UpdateTexture(e.icon_tex, nullptr, bgra,
                                          static_cast<int>(CACHE_ICON_W) * 4);
                    }
                }
            }
            if (e.icon_tex != nullptr) {
                SDL_Rect icon_dst { icon_x, icon_y, VAULT_ICON_SIZE, VAULT_ICON_SIZE };
                SDL_RenderCopy(r, e.icon_tex, nullptr, &icon_dst);
            } else {
                // Fallback colour block for NRO with no icon yet.
                SDL_SetRenderDrawColor(r, 0x30u, 0x30u, 0x60u, 0xFFu);
                SDL_Rect fb { icon_x, icon_y, VAULT_ICON_SIZE, VAULT_ICON_SIZE };
                SDL_RenderFillRect(r, &fb);
            }
        } else if (e.kind == EntryKind::Folder) {
            // Folder: solid accent-tinted square.
            SDL_SetRenderDrawColor(r,
                                   theme_.accent.r,
                                   theme_.accent.g,
                                   theme_.accent.b,
                                   0xB0u);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_Rect fb { icon_x, icon_y, VAULT_ICON_SIZE, VAULT_ICON_SIZE };
            SDL_RenderFillRect(r, &fb);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        } else {
            // Other file: muted grey square.
            SDL_SetRenderDrawColor(r, 0x44u, 0x44u, 0x66u, 0xFFu);
            SDL_Rect fb { icon_x, icon_y, VAULT_ICON_SIZE, VAULT_ICON_SIZE };
            SDL_RenderFillRect(r, &fb);
        }

        // ── Name label ────────────────────────────────────────────────────
        if (name_tex_[i] == nullptr && e.name[0] != '\0') {
            char display[20];
            const size_t nlen = strnlen(e.name, sizeof(e.name));
            if (nlen > 14) {
                memcpy(display, e.name, 11);
                display[11] = '.'; display[12] = '.'; display[13] = '.';
                display[14] = '\0';
            } else {
                memcpy(display, e.name, nlen);
                display[nlen] = '\0';
            }
            const pu::ui::Color nc { 0xE0u, 0xE0u, 0xF0u, 0xFFu };
            name_tex_[i] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(display), nc,
                static_cast<u32>(VAULT_CELL_W - 4));
        }
        if (name_tex_[i] != nullptr) {
            int nw = 0, nh = 0;
            SDL_QueryTexture(name_tex_[i], nullptr, nullptr, &nw, &nh);
            SDL_Rect ndst {
                cx + (VAULT_CELL_W - nw) / 2,
                cy + VAULT_ICON_SIZE + 8,
                nw, nh
            };
            SDL_RenderCopy(r, name_tex_[i], nullptr, &ndst);
        }

        // ── Focus ring ────────────────────────────────────────────────────
        if (is_focused) {
            SDL_SetRenderDrawColor(r,
                                   theme_.focus_ring.r,
                                   theme_.focus_ring.g,
                                   theme_.focus_ring.b,
                                   0xFFu);
            SDL_Rect ring { cx - 1, cy - 1, VAULT_CELL_W + 2, VAULT_CELL_H + 2 };
            SDL_RenderDrawRect(r, &ring);
        }
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdVaultLayout::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                              const s32 x, const s32 y) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    {
        static bool logged_once = false;
        if (!logged_once) {
            UL_LOG_INFO("vault: OnRender first call renderer=%p entries=%zu",
                        static_cast<void *>(r), entry_count_);
            logged_once = true;
        }
    }
    if (r == nullptr) {
        return;
    }

    // Full vault backdrop.
    SDL_SetRenderDrawColor(r,
                           theme_.desktop_bg.r,
                           theme_.desktop_bg.g,
                           theme_.desktop_bg.b,
                           0xF0u);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect backdrop { x, y + VAULT_BODY_TOP, 1920, VAULT_BODY_H };
    SDL_RenderFillRect(r, &backdrop);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    RenderSidebar(r, x, y);
    RenderMainPane(r, x, y);
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdVaultLayout::OnInput(const u64 keys_down,
                             const u64 keys_up,
                             const u64 keys_held,
                             const pu::ui::TouchPoint touch_pos) {
    (void)keys_up;
    (void)keys_held;
    (void)touch_pos;

    if (entry_count_ == 0) {
        if (keys_down & HidNpadButton_B) {
            NavigateUp();
        }
        return;
    }

    const s32 cols = MainPaneCols();

    // ── D-pad navigation ──────────────────────────────────────────────────────
    if (keys_down & HidNpadButton_Up) {
        if (focus_idx_ >= static_cast<size_t>(cols)) {
            focus_idx_ -= static_cast<size_t>(cols);
        }
    }
    if (keys_down & HidNpadButton_Down) {
        if (focus_idx_ + static_cast<size_t>(cols) < entry_count_) {
            focus_idx_ += static_cast<size_t>(cols);
        }
    }
    if (keys_down & HidNpadButton_Left) {
        if (focus_idx_ > 0) {
            --focus_idx_;
        }
    }
    if (keys_down & HidNpadButton_Right) {
        if (focus_idx_ + 1 < entry_count_) {
            ++focus_idx_;
        }
    }

    // ── Scroll tracking: keep focused row visible ─────────────────────────────
    {
        const s32 row_stride = VAULT_CELL_H + VAULT_CELL_GAP;
        const s32 focused_row = static_cast<s32>(focus_idx_) / cols;
        const s32 focused_top = focused_row * row_stride;
        const s32 focused_bot = focused_top + VAULT_CELL_H;

        const s32 visible_h = VAULT_BODY_H - VAULT_PATHBAR_H - VAULT_CELL_GAP;
        if (focused_top < scroll_offset_) {
            scroll_offset_ = focused_top;
        } else if (focused_bot > scroll_offset_ + visible_h) {
            scroll_offset_ = focused_bot - visible_h;
        }
    }

    // ── A / ZR: enter / launch ───────────────────────────────────────────────
    // ZR mirrors A so the shoulder trigger can be used as the primary action.
    if ((keys_down & HidNpadButton_A) || (keys_down & HidNpadButton_ZR)) {
        EnterFocused();
    }

    // ── B / ZL: navigate up ──────────────────────────────────────────────────
    // ZL mirrors B so the left shoulder trigger navigates back (go up a level).
    if ((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_ZL)) {
        NavigateUp();
    }
}

// ── NavigateUp ───────────────────────────────────────────────────────────────

void QdVaultLayout::NavigateUp() {
    // cwd_ always has a trailing slash; strip it to find the parent.
    // e.g. "sdmc:/switch/tools/" → "sdmc:/switch/" → parent is "sdmc:/"
    const size_t len = strnlen(cwd_, sizeof(cwd_));
    if (len == 0) {
        return;
    }

    // Find the second-to-last slash (the one before the trailing slash).
    // If none, we're already at the root "sdmc:/".
    char parent[MAX_PATH];
    snprintf(parent, sizeof(parent), "%s", cwd_);

    // Strip the trailing slash.
    size_t end = len;
    if (end > 0 && parent[end - 1] == '/') {
        parent[--end] = '\0';
    }
    // Now find the last '/'.
    const char *last_slash = nullptr;
    for (size_t i = 0; i < end; ++i) {
        if (parent[i] == '/') {
            last_slash = &parent[i];
        }
    }
    if (last_slash == nullptr) {
        // No slash left — stay put.
        return;
    }
    // Truncate at the slash (keep the slash as the trailing separator).
    const size_t new_len = static_cast<size_t>(last_slash - parent) + 1;
    parent[new_len] = '\0';

    // Don't navigate above "sdmc:/" (7 chars).
    if (new_len < 7) {
        return;
    }

    Navigate(parent);
}

// ── EnterFocused ─────────────────────────────────────────────────────────────

void QdVaultLayout::EnterFocused() {
    if (focus_idx_ >= entry_count_) {
        return;
    }
    const Entry &e = entries_[focus_idx_];

    switch (e.kind) {
        case EntryKind::Folder:
            Navigate(e.full_path);
            break;

        case EntryKind::Nro:
            if (e.full_path[0] != '\0') {
                UL_LOG_INFO("vault: launch NRO '%s'", e.full_path);
                smi::LaunchHomebrewLibraryApplet(
                    std::string(e.full_path), std::string(""));
            }
            break;

        case EntryKind::OtherFile:
            // Stage 3 will add inline text/image viewers.
            // For now, show a brief visual indication (focus ring is sufficient).
            UL_LOG_INFO("vault: selected non-NRO file '%s' (no viewer yet)", e.full_path);
            break;
    }
}

} // namespace ul::menu::qdesktop
