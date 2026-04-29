// qd_VaultLayout.cpp — Finder-style NRO file browser element for uMenu C++ (v1.0.0).
// Stage 1 of docs/45_HBMenu_Replacement_Design.md: vault skeleton.
// Two-pane UI: sidebar (6 canonical roots) + main pane (dirs + NROs + files).
// NRO launch: smi::LaunchHomebrewLibraryApplet.
// Icon decode: ExtractNroIcon + QdIconCache (same path as QdDesktopIconsElement).

#include <ul/menu/qdesktop/qd_VaultLayout.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>  // F6 (stabilize-5): RC-C — FadeOutToNonLibraryApplet
#include <ul/ul_Result.hpp>

// F6 (stabilize-5): RC-C — same extern pattern as qd_DesktopIcons.cpp / qd_Launchpad.cpp.
// Required so EnterFocused() can call FadeOutToNonLibraryApplet()+Finalize() before
// smi::LaunchHomebrewLibraryApplet(); without it uMenu re-asserts foreground and
// kills hbloader before the NRO can boot.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <dirent.h>
#include <sys/stat.h>   // Task B Properties: stat()
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>    // Task D: std::sort

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

// ── ClassifyByExtension ───────────────────────────────────────────────────────
// Task A: map file extension (lower-cased, after the dot) to an EntryKind.

/*static*/ QdVaultLayout::EntryKind
QdVaultLayout::ClassifyByExtension(const char *ext) {
    // .nca / .nsp / .xci — Nintendo archives
    if (strcmp(ext, "nca") == 0 || strcmp(ext, "nsp") == 0 ||
        strcmp(ext, "xci") == 0) {
        return EntryKind::NcaNspXci;
    }
    // Image files
    if (strcmp(ext, "png")  == 0 || strcmp(ext, "jpg")  == 0 ||
        strcmp(ext, "jpeg") == 0 || strcmp(ext, "bmp")  == 0 ||
        strcmp(ext, "gif")  == 0) {
        return EntryKind::ImageFile;
    }
    // Audio files
    if (strcmp(ext, "mp3")  == 0 || strcmp(ext, "wav")  == 0 ||
        strcmp(ext, "ogg")  == 0 || strcmp(ext, "flac") == 0 ||
        strcmp(ext, "aac")  == 0) {
        return EntryKind::AudioFile;
    }
    // Text / prose files
    if (strcmp(ext, "txt")  == 0 || strcmp(ext, "log")  == 0 ||
        strcmp(ext, "md")   == 0 || strcmp(ext, "nfo")  == 0) {
        return EntryKind::TextFile;
    }
    // Config / data files
    if (strcmp(ext, "json") == 0 || strcmp(ext, "toml") == 0 ||
        strcmp(ext, "ini")  == 0 || strcmp(ext, "cfg")  == 0 ||
        strcmp(ext, "xml")  == 0 || strcmp(ext, "yaml") == 0 ||
        strcmp(ext, "yml")  == 0) {
        return EntryKind::ConfigFile;
    }
    return EntryKind::OtherFile;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdVaultLayout::QdVaultLayout(const QdTheme &theme)
    : theme_(theme), entry_count_(0), focus_idx_(0), scroll_offset_(0),
      hint_bar_tex_(nullptr),
      text_viewer_(QdTextViewer::New(theme)),
      image_viewer_(QdImageViewer::New(theme)),
      viewer_active_(false)
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

    // Build the bottom hint bar once; freed in the destructor.
    // Text kept short so it fits at Small font size within 1920 px.
    const pu::ui::Color hint_col { 0x99u, 0x99u, 0xBBu, 0xFFu };
    hint_bar_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string("B / + Close   \xe2\x80\xa2   A Open   \xe2\x80\xa2   Y Sort   \xe2\x80\xa2   Up/Down Navigate"),
        hint_col);

    // Open the default root so the vault is usable right after construction.
    Navigate("sdmc:/switch/");
}

QdVaultLayout::~QdVaultLayout() {
    UL_LOG_INFO("vault: QdVaultLayout dtor");
    FreeEntryTextures();
    for (size_t i = 0; i < SIDEBAR_ROOT_COUNT; ++i) {
        if (sidebar_tex_[i] != nullptr) {
            pu::ui::render::DeleteTexture(sidebar_tex_[i]);
        }
    }
    if (hint_bar_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(hint_bar_tex_);
        hint_bar_tex_ = nullptr;
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
        // Skip '.' and '..' always; skip other dotfiles unless show_dotfiles_ is on.
        if (de->d_name[0] == '.') {
            // Always skip . and ..
            if (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0')) {
                continue;
            }
            // Other dotfiles: skip unless toggle is on.
            if (!show_dotfiles_) {
                continue;
            }
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
            // Build full_path first (needed for extension extraction).
            const size_t used = safe_copy(e.full_path, sizeof(e.full_path), cwd_);
            safe_append(e.full_path, sizeof(e.full_path), de->d_name, used);

            // Find the last dot in d_name to extract the extension.
            const size_t dlen = strnlen(de->d_name, 256);
            const char *last_dot = nullptr;
            for (size_t k = 0; k < dlen; ++k) {
                if (de->d_name[k] == '.') {
                    last_dot = &de->d_name[k];
                }
            }

            // Build a lower-cased extension (without the dot).
            char ext_lc[9] = {};
            bool is_nro = false;
            if (last_dot != nullptr && last_dot[1] != '\0') {
                size_t ei = 0;
                for (const char *p = last_dot + 1; *p != '\0' && ei < 8; ++p, ++ei) {
                    ext_lc[ei] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
                }
                is_nro = (ext_lc[0] == 'n' && ext_lc[1] == 'r' &&
                          ext_lc[2] == 'o' && ext_lc[3] == '\0');
            }

            if (is_nro) {
                e.kind = EntryKind::Nro;
            } else {
                e.kind = ClassifyByExtension(ext_lc);
            }

            // Copy display name; strip .nro suffix from NRO display names.
            safe_copy(e.name, sizeof(e.name), de->d_name);
            if (is_nro && last_dot != nullptr) {
                const size_t strip = static_cast<size_t>(last_dot - de->d_name);
                if (strip < sizeof(e.name)) {
                    e.name[strip] = '\0';
                }
            }
        }

        name_tex_[entry_count_] = nullptr;
        ++entry_count_;
    }
    closedir(d);

    // Task D: sort entries according to sort_mode_.
    // Folders always sort before files regardless of mode.
    if (entry_count_ > 1) {
        std::sort(entries_, entries_ + entry_count_,
            [this](const Entry &a, const Entry &b) -> bool {
                // Folders first.
                const bool a_dir = (a.kind == EntryKind::Folder);
                const bool b_dir = (b.kind == EntryKind::Folder);
                if (a_dir != b_dir) {
                    return a_dir > b_dir; // folders first
                }
                if (sort_mode_ == SortMode::ByKind) {
                    // Sort by kind ordinal, then name within the same kind.
                    const u8 ak = static_cast<u8>(a.kind);
                    const u8 bk = static_cast<u8>(b.kind);
                    if (ak != bk) {
                        return ak < bk;
                    }
                }
                // Alphabetical by display name (case-insensitive).
                return strncasecmp(a.name, b.name, sizeof(a.name)) < 0;
            });
    }

    UL_LOG_INFO("vault: scan '%s' → %zu entries sort=%d", cwd_, entry_count_,
                static_cast<int>(sort_mode_));
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
            pu::ui::render::DeleteTexture(name_tex_[i]);
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

    // (stabilize-6 / RC-C4): clip cells outside the visible scroll window so
    // scrolled-off rows do not return a false positive to touch hit-testing.
    // Mirrors the render-side clip at the OnRender main-pane culling check.
    const s32 visible_top    = origin_y + VAULT_BODY_TOP + VAULT_PATHBAR_H;
    const s32 visible_bottom = origin_y + VAULT_BODY_TOP + VAULT_BODY_H;
    if (out_y + VAULT_CELL_H <= visible_top || out_y >= visible_bottom) {
        return false;
    }
    return true;
}

// ── RenderSidebar ─────────────────────────────────────────────────────────────

void QdVaultLayout::RenderSidebar(SDL_Renderer *r,
                                   s32 origin_x, s32 origin_y) const {
    // (stabilize-6 / RC-C2): sidebar bg alpha reflects input focus.
    // Was 0xD0; new 0xE0 focused / 0x90 unfocused so the user gets a
    // visual cue that the sidebar is the active input target.
    const u8 sb_bg_alpha = sidebar_focused_ ? 0xE0u : 0x90u;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
                           theme_.surface_glass.r,
                           theme_.surface_glass.g,
                           theme_.surface_glass.b,
                           sb_bg_alpha);
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

        // (stabilize-6 / RC-C2): sidebar focus ring. Mirrors main-pane
        // ring (drawn at OnRender focus block) so the user sees the same
        // affordance whether they are in main-pane or sidebar mode.
        if (sidebar_focused_ && i == sidebar_idx_) {
            SDL_SetRenderDrawColor(r,
                                   theme_.focus_ring.r,
                                   theme_.focus_ring.g,
                                   theme_.focus_ring.b,
                                   0xFFu);
            SDL_Rect ring {
                origin_x + 1,
                item_y - 1,
                VAULT_SIDEBAR_W - 3,
                SIDEBAR_ITEM_H - 2
            };
            SDL_RenderDrawRect(r, &ring);
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
                pu::ui::render::DeleteTexture(path_tex);
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

        // Task A: draw procedural icon glyph for each EntryKind.
        // All glyphs use VAULT_ICON_SIZE×VAULT_ICON_SIZE space at (icon_x, icon_y).
        // Accent = cyan #7DD3FC outline against indigo-tinted background.
        const s32 IS = VAULT_ICON_SIZE;  // alias for brevity
        const u8 ar = theme_.accent.r, ag = theme_.accent.g, ab = theme_.accent.b;

        if (e.kind == EntryKind::Nro) {
            // ── NRO: decoded ASET icon or cyan-outline fallback ──────────────
            if (!e.icon_decoded) {
                DecodeNroIcon(e);
            }
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
                SDL_Rect icon_dst { icon_x, icon_y, IS, IS };
                SDL_RenderCopy(r, e.icon_tex, nullptr, &icon_dst);
            } else {
                // Cyan-outline rocket placeholder.
                SDL_SetRenderDrawColor(r, 0x10u, 0x10u, 0x28u, 0xFFu);
                SDL_Rect bg { icon_x, icon_y, IS, IS };
                SDL_RenderFillRect(r, &bg);
                SDL_SetRenderDrawColor(r, ar, ag, ab, 0xFFu);
                SDL_Rect outline { icon_x, icon_y, IS, IS };
                SDL_RenderDrawRect(r, &outline);
                // Rocket body: tall centre rect
                SDL_Rect body { icon_x + IS/2 - 6, icon_y + 8, 12, IS - 20 };
                SDL_RenderFillRect(r, &body);
                // Rocket nose cap
                SDL_Rect nose { icon_x + IS/2 - 4, icon_y + 4, 8, 8 };
                SDL_RenderFillRect(r, &nose);
                // Fins: two small rects at the bottom left/right of body
                SDL_Rect fin_l { icon_x + IS/2 - 12, icon_y + IS - 22, 6, 10 };
                SDL_Rect fin_r { icon_x + IS/2 + 6,  icon_y + IS - 22, 6, 10 };
                SDL_RenderFillRect(r, &fin_l);
                SDL_RenderFillRect(r, &fin_r);
            }

        } else if (e.kind == EntryKind::Folder) {
            // ── Folder: filled accent square with a tab nub on top ───────────
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, ar, ag, ab, 0xB0u);
            // Tab nub (upper-left, half-width, 10 px tall)
            SDL_Rect tab { icon_x + 4, icon_y, IS / 2, 10 };
            SDL_RenderFillRect(r, &tab);
            // Main folder body (starts 8 px below top to accommodate tab)
            SDL_Rect body { icon_x, icon_y + 8, IS, IS - 8 };
            SDL_RenderFillRect(r, &body);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        } else if (e.kind == EntryKind::NcaNspXci) {
            // ── NCA/NSP/XCI: cyan outline rect with bracket corner marks ─────
            SDL_SetRenderDrawColor(r, 0x0Au, 0x10u, 0x22u, 0xFFu);
            SDL_Rect bg { icon_x, icon_y, IS, IS };
            SDL_RenderFillRect(r, &bg);
            SDL_SetRenderDrawColor(r, ar, ag, ab, 0xFFu);
            SDL_RenderDrawRect(r, &bg);
            // Bracket marks: 4 L-shaped corner details (8 px each arm)
            // Top-left
            SDL_RenderDrawLine(r, icon_x+3,    icon_y+3,    icon_x+11,   icon_y+3);
            SDL_RenderDrawLine(r, icon_x+3,    icon_y+3,    icon_x+3,    icon_y+11);
            // Top-right
            SDL_RenderDrawLine(r, icon_x+IS-4, icon_y+3,    icon_x+IS-12,icon_y+3);
            SDL_RenderDrawLine(r, icon_x+IS-4, icon_y+3,    icon_x+IS-4, icon_y+11);
            // Bottom-left
            SDL_RenderDrawLine(r, icon_x+3,    icon_y+IS-4, icon_x+11,   icon_y+IS-4);
            SDL_RenderDrawLine(r, icon_x+3,    icon_y+IS-4, icon_x+3,    icon_y+IS-12);
            // Bottom-right
            SDL_RenderDrawLine(r, icon_x+IS-4, icon_y+IS-4, icon_x+IS-12,icon_y+IS-4);
            SDL_RenderDrawLine(r, icon_x+IS-4, icon_y+IS-4, icon_x+IS-4, icon_y+IS-12);
            // Centre label lines (simulated NSP badge)
            SDL_RenderDrawLine(r, icon_x+IS/2-10, icon_y+IS/2-3,  icon_x+IS/2+10, icon_y+IS/2-3);
            SDL_RenderDrawLine(r, icon_x+IS/2-8,  icon_y+IS/2+3,  icon_x+IS/2+8,  icon_y+IS/2+3);

        } else if (e.kind == EntryKind::TextFile) {
            // ── Text/Log/Md: outline page with horizontal line details ────────
            SDL_SetRenderDrawColor(r, 0x0Au, 0x0Eu, 0x1Au, 0xFFu);
            SDL_Rect bg { icon_x, icon_y, IS, IS };
            SDL_RenderFillRect(r, &bg);
            SDL_SetRenderDrawColor(r, ar, ag, ab, 0xFFu);
            // Page border with dog-eared top-right corner
            const s32 ear = 14;
            SDL_RenderDrawLine(r, icon_x+6,      icon_y+6,       icon_x+IS-6-ear, icon_y+6);
            SDL_RenderDrawLine(r, icon_x+IS-6-ear,icon_y+6,       icon_x+IS-6,     icon_y+6+ear);
            SDL_RenderDrawLine(r, icon_x+IS-6,   icon_y+6+ear,   icon_x+IS-6,     icon_y+IS-6);
            SDL_RenderDrawLine(r, icon_x+IS-6,   icon_y+IS-6,    icon_x+6,        icon_y+IS-6);
            SDL_RenderDrawLine(r, icon_x+6,      icon_y+IS-6,    icon_x+6,        icon_y+6);
            // Dog-ear fold
            SDL_RenderDrawLine(r, icon_x+IS-6-ear,icon_y+6,       icon_x+IS-6-ear, icon_y+6+ear);
            SDL_RenderDrawLine(r, icon_x+IS-6-ear,icon_y+6+ear,   icon_x+IS-6,     icon_y+6+ear);
            // Four text lines inside the page
            for (s32 li = 0; li < 4; ++li) {
                const s32 lx1 = icon_x + 12;
                const s32 lx2 = (li == 3) ? icon_x + IS - 20 : icon_x + IS - 12;
                const s32 ly  = icon_y + 24 + li * 9;
                SDL_RenderDrawLine(r, lx1, ly, lx2, ly);
            }

        } else if (e.kind == EntryKind::ImageFile) {
            // ── Image: outline frame with mountain + sun ──────────────────────
            SDL_SetRenderDrawColor(r, 0x08u, 0x0Cu, 0x18u, 0xFFu);
            SDL_Rect bg { icon_x, icon_y, IS, IS };
            SDL_RenderFillRect(r, &bg);
            SDL_SetRenderDrawColor(r, ar, ag, ab, 0xFFu);
            // Frame border
            SDL_RenderDrawRect(r, &bg);
            // Mountain: two diagonal lines from base to a peak
            const s32 mx  = icon_x + IS / 2;
            const s32 mby = icon_y + IS - 14;
            const s32 mty = icon_y + 20;
            SDL_RenderDrawLine(r, icon_x + 10, mby, mx,       mty);
            SDL_RenderDrawLine(r, mx,          mty, icon_x+IS-10, mby);
            SDL_RenderDrawLine(r, icon_x + 10, mby, icon_x+IS-10, mby);
            // Sun: small circle (drawn as 8 RenderDrawLine radii, r=7)
            const s32 scx = icon_x + IS - 18, scy = icon_y + 16;
            for (int ang = 0; ang < 8; ++ang) {
                // Approximate circle with 8 short lines from centre
                const float th = static_cast<float>(ang) * 3.14159f / 4.0f;
                const s32 x2 = scx + static_cast<s32>(6.0f * __builtin_cosf(th));
                const s32 y2 = scy + static_cast<s32>(6.0f * __builtin_sinf(th));
                SDL_RenderDrawLine(r, scx, scy, x2, y2);
            }

        } else if (e.kind == EntryKind::AudioFile) {
            // ── Audio: musical note (filled oval body + stem + flag) ─────────
            SDL_SetRenderDrawColor(r, 0x08u, 0x0Cu, 0x18u, 0xFFu);
            SDL_Rect bg { icon_x, icon_y, IS, IS };
            SDL_RenderFillRect(r, &bg);
            SDL_SetRenderDrawColor(r, ar, ag, ab, 0xFFu);
            // Note head (filled as a 10×7 oval via stacked horizontal lines)
            const s32 nhx = icon_x + IS/2 - 10, nhy = icon_y + IS - 22;
            for (s32 row = 0; row < 7; ++row) {
                const s32 half = (row < 3) ? (4 + row * 2) : (12 - (row - 3) * 2);
                SDL_RenderDrawLine(r, nhx + 5 - half, nhy + row,
                                      nhx + 5 + half, nhy + row);
            }
            // Stem (vertical line up from the right side of the note head)
            const s32 sx = nhx + 16;
            SDL_RenderDrawLine(r, sx, nhy, sx, nhy - 24);
            // Flag (two short curved lines descending from stem top)
            SDL_RenderDrawLine(r, sx,   nhy - 24, sx + 10, nhy - 18);
            SDL_RenderDrawLine(r, sx,   nhy - 20, sx + 8,  nhy - 14);

        } else if (e.kind == EntryKind::ConfigFile) {
            // ── Config: gear outline (hexagon with 6 notch bumps) ────────────
            SDL_SetRenderDrawColor(r, 0x0Cu, 0x0Au, 0x1Au, 0xFFu);
            SDL_Rect bg { icon_x, icon_y, IS, IS };
            SDL_RenderFillRect(r, &bg);
            SDL_SetRenderDrawColor(r, ar, ag, ab, 0xFFu);
            // Draw gear as a 12-point polygon approximation using SDL_RenderDrawLine.
            // Two radii: inner r=16, outer r=22; 6 teeth alternating.
            const s32 gcx = icon_x + IS / 2;
            const s32 gcy = icon_y + IS / 2;
            const float twopi = 6.2831853f;
            s32 prev_x = 0, prev_y = 0;
            for (int seg = 0; seg < 24; ++seg) {
                const float angle = (static_cast<float>(seg) / 24.0f) * twopi;
                const float r_val = ((seg % 4) < 2) ? 22.0f : 15.0f;
                const s32 nx = gcx + static_cast<s32>(r_val * __builtin_cosf(angle));
                const s32 ny = gcy + static_cast<s32>(r_val * __builtin_sinf(angle));
                if (seg > 0) {
                    SDL_RenderDrawLine(r, prev_x, prev_y, nx, ny);
                }
                prev_x = nx; prev_y = ny;
            }
            // Close the polygon
            {
                const float a0 = 0.0f;
                const s32 fx = gcx + static_cast<s32>(22.0f * __builtin_cosf(a0));
                const s32 fy = gcy + static_cast<s32>(22.0f * __builtin_sinf(a0));
                SDL_RenderDrawLine(r, prev_x, prev_y, fx, fy);
            }
            // Centre hole (small circle, r=6)
            for (int seg = 0; seg < 12; ++seg) {
                const float a = (static_cast<float>(seg) / 12.0f) * twopi;
                const float na = (static_cast<float>(seg + 1) / 12.0f) * twopi;
                const s32 x1 = gcx + static_cast<s32>(6.0f * __builtin_cosf(a));
                const s32 y1 = gcy + static_cast<s32>(6.0f * __builtin_sinf(a));
                const s32 x2 = gcx + static_cast<s32>(6.0f * __builtin_cosf(na));
                const s32 y2 = gcy + static_cast<s32>(6.0f * __builtin_sinf(na));
                SDL_RenderDrawLine(r, x1, y1, x2, y2);
            }

        } else {
            // ── OtherFile: muted grey square (unchanged baseline) ─────────────
            SDL_SetRenderDrawColor(r, 0x44u, 0x44u, 0x66u, 0xFFu);
            SDL_Rect fb { icon_x, icon_y, IS, IS };
            SDL_RenderFillRect(r, &fb);
            // Add a thin accent outline so the grey doesn't look entirely blank.
            SDL_SetRenderDrawColor(r, ar, ag, ab, 0x60u);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_RenderDrawRect(r, &fb);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
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

// ── RenderContextMenu ─────────────────────────────────────────────────────────
// Task B: Draw a centred popup panel listing the three context-menu options.
// Panel is 440×220 px, centred in the main-pane area (right of sidebar).
// The highlighted option gets a cyan fill row and a left-edge accent marker.

void QdVaultLayout::RenderContextMenu(SDL_Renderer *r) const {
    // Panel geometry (main-pane centre).
    static constexpr s32 PW = 440;
    static constexpr s32 PH = 220;
    const s32 main_left = VAULT_SIDEBAR_W;
    const s32 main_w    = 1920 - main_left;
    const s32 px = main_left + (main_w - PW) / 2;
    const s32 py = VAULT_BODY_TOP + (VAULT_BODY_H - PH) / 2;

    // Semi-transparent dark background.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x0Au, 0x0Au, 0x1Au, 0xEAu);
    SDL_Rect bg { px, py, PW, PH };
    SDL_RenderFillRect(r, &bg);

    // Cyan border.
    SDL_SetRenderDrawColor(r,
                           theme_.accent.r,
                           theme_.accent.g,
                           theme_.accent.b,
                           0xFFu);
    SDL_RenderDrawRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Title separator line below the top 42 px title area.
    const s32 title_h = 42;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
                           theme_.accent.r,
                           theme_.accent.g,
                           theme_.accent.b,
                           0x60u);
    SDL_RenderDrawLine(r, px + 8, py + title_h, px + PW - 8, py + title_h);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Option rows.
    static const char *const OPT_LABELS[CTX_MENU_OPT_COUNT] = {
        "  [A] Open",
        "  [+] Properties",
        "  [B] Close Menu",
    };
    const s32 row_h = (PH - title_h - 16) / CTX_MENU_OPT_COUNT;
    for (s32 i = 0; i < CTX_MENU_OPT_COUNT; ++i) {
        const s32 row_y = py + title_h + 8 + i * row_h;
        if (i == ctx_menu_opt_) {
            // Highlight row with a cyan tint fill.
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r,
                                   theme_.accent.r,
                                   theme_.accent.g,
                                   theme_.accent.b,
                                   0x38u);
            SDL_Rect hi { px + 4, row_y, PW - 8, row_h };
            SDL_RenderFillRect(r, &hi);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

            // Left-edge accent marker.
            SDL_SetRenderDrawColor(r,
                                   theme_.accent.r,
                                   theme_.accent.g,
                                   theme_.accent.b,
                                   0xFFu);
            SDL_RenderDrawLine(r, px + 4, row_y + 2, px + 4, row_y + row_h - 2);
        }
        // Render option label text.
        const pu::ui::Color label_col = (i == ctx_menu_opt_)
            ? pu::ui::Color { theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu }
            : pu::ui::Color { 0xCCu, 0xCCu, 0xDDu, 0xFFu };
        SDL_Texture *lbl_tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            OPT_LABELS[i],
            label_col);
        if (lbl_tex != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(lbl_tex, nullptr, nullptr, &tw, &th);
            SDL_Rect ldst { px + 14, row_y + (row_h - th) / 2, tw, th };
            SDL_RenderCopy(r, lbl_tex, nullptr, &ldst);
            pu::ui::render::DeleteTexture(lbl_tex);
        }
    }
}

// ── DispatchContextMenuOption ─────────────────────────────────────────────────
// Task B: Execute the highlighted option for ctx_menu_entry_ and close the menu.

void QdVaultLayout::DispatchContextMenuOption() {
    if (ctx_menu_entry_ >= entry_count_) {
        ctx_menu_active_ = false;
        return;
    }

    switch (ctx_menu_opt_) {
        case 0: {
            // Open — same as pressing A on the entry directly.
            ctx_menu_active_ = false;
            focus_idx_ = ctx_menu_entry_;
            EnterFocused();
            break;
        }
        case 1: {
            // Properties — stat() the file and log size + mtime.
            // Logged to uMenu telemetry rather than a sub-panel in v1.8.
            const Entry &e = entries_[ctx_menu_entry_];
            struct stat st = {};
            if (stat(e.full_path, &st) == 0) {
                char time_buf[64] = {};
                struct tm *tm_info = localtime(&st.st_mtime);
                if (tm_info != nullptr) {
                    strftime(time_buf, sizeof(time_buf),
                             "%Y-%m-%d %H:%M:%S", tm_info);
                } else {
                    snprintf(time_buf, sizeof(time_buf), "(unknown)");
                }
                UL_LOG_INFO("vault: properties '%s': size=%lld bytes, mtime=%s",
                            e.full_path,
                            static_cast<long long>(st.st_size),
                            time_buf);
            } else {
                UL_LOG_WARN("vault: properties stat() failed for '%s' errno=%d",
                            e.full_path, errno);
            }
            ctx_menu_active_ = false;
            break;
        }
        default:
            // Option 2 (Close Menu) or any unexpected value.
            ctx_menu_active_ = false;
            UL_LOG_INFO("vault: ctx_menu closed via option %d", ctx_menu_opt_);
            break;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdVaultLayout::OnRender(pu::ui::render::Renderer::Ref &drawer,
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

    // ── Bottom hint bar (v1.8.25) ──────────────────────────────────────────────
    // Single-row keybind legend at y=1040, horizontally centred.
    // Rendered below the file grid and above the dock area (1080-108=972 bottom
    // of VAULT_BODY, so 1040 sits in the gap between VAULT_BODY and the dock).
    static constexpr s32 VAULT_HINT_Y = 1040;
    if (hint_bar_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_bar_tex_, nullptr, nullptr, &hw, &hh);
        const SDL_Rect hdst { x + (1920 - hw) / 2, y + VAULT_HINT_Y, hw, hh };
        SDL_RenderCopy(r, hint_bar_tex_, nullptr, &hdst);
    }

    // Task B: context menu floats above the file grid but below the viewer.
    if (ctx_menu_active_) {
        RenderContextMenu(r);
    }

    // Draw the active viewer as a full-screen overlay on top of the vault UI.
    if (viewer_active_) {
        if (text_viewer_->IsOpen()) {
            text_viewer_->OnRender(drawer, x, y);
        } else if (image_viewer_->IsOpen()) {
            image_viewer_->OnRender(drawer, x, y);
        } else {
            // Both closed — should not happen, but clear the flag.
            viewer_active_ = false;
        }
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdVaultLayout::OnInput(const u64 keys_down,
                             const u64 keys_up,
                             const u64 keys_held,
                             const pu::ui::TouchPoint touch_pos) {
    // Task B: context menu is the highest-priority overlay.  When it is open,
    // Up/Down move the option cursor, A dispatches the option, B closes the menu.
    // No other input reaches the vault while the menu is up.
    if (ctx_menu_active_) {
        if (keys_down & HidNpadButton_Up) {
            if (ctx_menu_opt_ > 0) {
                --ctx_menu_opt_;
            }
        }
        if (keys_down & HidNpadButton_Down) {
            if (ctx_menu_opt_ < CTX_MENU_OPT_COUNT - 1) {
                ++ctx_menu_opt_;
            }
        }
        if (keys_down & HidNpadButton_A) {
            DispatchContextMenuOption();
        }
        if (keys_down & HidNpadButton_B) {
            ctx_menu_active_ = false;
            UL_LOG_INFO("vault: ctx_menu closed by B");
        }
        return;
    }

    // When a viewer is active, route all input to it until it closes.
    if (viewer_active_) {
        if (text_viewer_->IsOpen()) {
            text_viewer_->OnInput(keys_down, keys_up, keys_held, touch_pos);
            if (!text_viewer_->IsOpen()) {
                viewer_active_ = false;
            }
        } else if (image_viewer_->IsOpen()) {
            image_viewer_->OnInput(keys_down, keys_up, keys_held, touch_pos);
            if (!image_viewer_->IsOpen()) {
                viewer_active_ = false;
            }
        } else {
            viewer_active_ = false;
        }
        return;
    }

    (void)keys_up;

    // ── Plus: direct close — return to Desktop from any depth (v1.8.25) ────────
    // Mirrors the Launchpad pattern (qd_Launchpad.cpp:643) where B and Plus are
    // equivalent top-level close actions.  B in Vault means "navigate up one dir"
    // (reaching desktop when already at the root); Plus gives the user a direct
    // escape at any directory depth without having to back out step by step.
    if (keys_down & HidNpadButton_Plus) {
        UL_LOG_INFO("vault: Plus -> LoadMenu(Main)");
        if (g_MenuApplication) {
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        }
        return;
    }

    // ── Hot-corner close (v1.8.25) ────────────────────────────────────────────
    // Edge-triggered tap of the top-left LP_HOTCORNER_W×LP_HOTCORNER_H region
    // closes the Vault back to the Desktop.  Uses the same edge-trigger latch
    // pattern as qd_Launchpad.cpp:655-704 to prevent repeated firings while the
    // finger is held inside the corner.  The latch is vault_was_touch_active_
    // (local static) mirroring lp_was_touch_active_last_frame_ in Launchpad.
    // LP_HOTCORNER_W=96, LP_HOTCORNER_H=72 — imported from qd_Launchpad.hpp via
    // the already-included Plutonium headers; hardcoded here to avoid a cross-
    // header dependency.  Values must stay in sync with qd_Launchpad.hpp.
    {
        static constexpr s32 HC_W = 96;   // LP_HOTCORNER_W
        static constexpr s32 HC_H = 72;   // LP_HOTCORNER_H
        static bool vault_was_touch_active_ = false;
        const bool  touch_active_now   = !touch_pos.IsEmpty();
        const s32   tx                 = static_cast<s32>(touch_pos.x);
        const s32   ty                 = static_cast<s32>(touch_pos.y);
        const bool  touch_corner_now   = touch_active_now
                                         && tx >= 0 && tx < HC_W
                                         && ty >= 0 && ty < HC_H;
        const bool  touch_corner_edge  = touch_corner_now
                                         && !vault_was_touch_active_;
        vault_was_touch_active_ = touch_active_now;
        if (touch_corner_edge) {
            UL_LOG_INFO("vault: hot-corner tap edge tx=%d ty=%d -> LoadMenu(Main)", tx, ty);
            if (g_MenuApplication) {
                g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
            }
            return;
        }
    }

    // (stabilize-6 / RC-C3): D-pad auto-repeat — `keys_held` was previously
    // discarded, so a held direction never moved focus past the first frame.
    // Mirrors the auto-repeat idiom used in qd_DesktopIcons (per cycle-G3 nav).
    static constexpr u32 DPAD_REPEAT_DELAY_FRAMES    = 18u;
    static constexpr u32 DPAD_REPEAT_INTERVAL_FRAMES = 5u;
    auto dpad_should_repeat = [](u32 &held_frames, bool is_held) -> bool {
        if (!is_held) {
            held_frames = 0u;
            return false;
        }
        ++held_frames;
        if (held_frames <= DPAD_REPEAT_DELAY_FRAMES) {
            return false;
        }
        const u32 since_delay = held_frames - DPAD_REPEAT_DELAY_FRAMES;
        return (since_delay % DPAD_REPEAT_INTERVAL_FRAMES) == 0u;
    };
    const bool repeat_up    = dpad_should_repeat(dpad_held_frames_up_,
                                                  (keys_held & HidNpadButton_Up) != 0u);
    const bool repeat_down  = dpad_should_repeat(dpad_held_frames_down_,
                                                  (keys_held & HidNpadButton_Down) != 0u);
    const bool repeat_left  = dpad_should_repeat(dpad_held_frames_left_,
                                                  (keys_held & HidNpadButton_Left) != 0u);
    const bool repeat_right = dpad_should_repeat(dpad_held_frames_right_,
                                                  (keys_held & HidNpadButton_Right) != 0u);

    // ── Sidebar touch hit-test (stabilize-6 / RC-C2) ────────────────────────
    // Mirrors RenderSidebar() rects (SIDEBAR_ITEM_H=42, SIDEBAR_TOP_PAD=18).
    // Edge-triggered via sb_was_touch_active_last_frame_ so a single tap
    // fires exactly one Navigate(); a held finger does not re-fire.
    {
        static constexpr s32 SB_ITEM_H  = 42;   // mirror RenderSidebar()
        static constexpr s32 SB_TOP_PAD = 18;
        const bool sb_touch_active_now = !touch_pos.IsEmpty();
        if (sb_touch_active_now && !sb_was_touch_active_last_frame_) {
            const s32 tx = static_cast<s32>(touch_pos.x);
            const s32 ty = static_cast<s32>(touch_pos.y);
            if (tx >= 0 && tx < VAULT_SIDEBAR_W) {
                const s32 base_y = VAULT_BODY_TOP + SB_TOP_PAD;
                if (ty >= base_y &&
                    ty <  base_y + static_cast<s32>(SIDEBAR_ROOT_COUNT) * SB_ITEM_H) {
                    const s32 rel = ty - base_y;
                    const size_t hit = static_cast<size_t>(rel / SB_ITEM_H);
                    if (hit < SIDEBAR_ROOT_COUNT) {
                        UL_LOG_INFO("vault: sidebar tap idx=%zu path='%s'",
                                    hit, SIDEBAR_ROOTS[hit].path);
                        sidebar_idx_     = hit;
                        sidebar_focused_ = false;          // tap returns control to main
                        Navigate(SIDEBAR_ROOTS[hit].path);
                        sb_was_touch_active_last_frame_ = sb_touch_active_now;
                        return;
                    }
                }
            }
        }
        sb_was_touch_active_last_frame_ = sb_touch_active_now;
    }

    // F8 (stabilize-4): touch tap hit-test for Vault entry cells.
    // Previously (void)touch_pos meant touch taps on the Vault grid were
    // silently ignored — only D-pad + A worked to open entries.  Now we
    // iterate the visible entry grid, check whether the tap falls inside
    // a cell, move focus to that cell, and immediately call EnterFocused().
    //
    // F1-derivative (stabilize-6 / O-E F.5): use !touch_pos.IsEmpty()
    // sentinel instead of the broken (x != 0 || y != 0) check, matching
    // qd_DesktopIcons.cpp:1967, 2028 and qd_Launchpad.cpp:423, 473.
    if (!touch_pos.IsEmpty() && entry_count_ > 0) {
        for (size_t i = 0; i < entry_count_; ++i) {
            s32 cx, cy;
            if (!EntryRect(i, cx, cy, 0, 0)) {
                continue;
            }
            if (touch_pos.x >= cx && touch_pos.x < cx + VAULT_CELL_W &&
                touch_pos.y >= cy && touch_pos.y < cy + VAULT_CELL_H) {
                focus_idx_ = i;
                EnterFocused();
                return;
            }
        }
    }

    if (entry_count_ == 0) {
        if (keys_down & HidNpadButton_B) {
            NavigateUp();
        }
        return;
    }

    const s32 cols = MainPaneCols();

    // ── D-pad navigation ──────────────────────────────────────────────────────
    // (stabilize-6 / RC-C2): sidebar_focused_ routes UP/DOWN to sidebar_idx_,
    // RIGHT to main; main-mode LEFT at column 0 enters sidebar.
    if (sidebar_focused_) {
        if ((keys_down & HidNpadButton_Up) || repeat_up) {
            if (sidebar_idx_ > 0u) { --sidebar_idx_; }
            UL_LOG_INFO("vault: sidebar dpad up idx=%zu", sidebar_idx_);
        }
        if ((keys_down & HidNpadButton_Down) || repeat_down) {
            if (sidebar_idx_ + 1u < SIDEBAR_ROOT_COUNT) { ++sidebar_idx_; }
            UL_LOG_INFO("vault: sidebar dpad down idx=%zu", sidebar_idx_);
        }
        if ((keys_down & HidNpadButton_Left) || repeat_left) {
            // Sidebar already at left edge — silent no-op (matches main-pane
            // LEFT-at-col-0 semantics).
        }
        if ((keys_down & HidNpadButton_Right) || repeat_right) {
            sidebar_focused_ = false;
            UL_LOG_INFO("vault: sidebar dpad right -> main pane");
        }
    } else {
        if ((keys_down & HidNpadButton_Up) || repeat_up) {
            if (focus_idx_ >= static_cast<size_t>(cols)) {
                focus_idx_ -= static_cast<size_t>(cols);
            }
        }
        if ((keys_down & HidNpadButton_Down) || repeat_down) {
            if (focus_idx_ + static_cast<size_t>(cols) < entry_count_) {
                focus_idx_ += static_cast<size_t>(cols);
            }
        }
        if ((keys_down & HidNpadButton_Left) || repeat_left) {
            // (stabilize-6 / RC-C2): LEFT from column 0 jumps into the sidebar
            // instead of being a silent no-op.
            const s32 col = static_cast<s32>(focus_idx_) % cols;
            if (col == 0) {
                sidebar_focused_ = true;
                UL_LOG_INFO("vault: main dpad left at col0 -> sidebar idx=%zu",
                            sidebar_idx_);
            } else if (focus_idx_ > 0) {
                --focus_idx_;
            }
        }
        if ((keys_down & HidNpadButton_Right) || repeat_right) {
            if (focus_idx_ + 1 < entry_count_) {
                ++focus_idx_;
            }
        }
    }

    // ── Scroll tracking: keep focused row visible (main pane only) ──────────
    if (!sidebar_focused_) {
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
    // (stabilize-6 / RC-C2): A on sidebar = Navigate; A on main = EnterFocused.
    // A/B/ZR/ZL deliberately do NOT auto-repeat — held A would fire
    // EnterFocused() repeatedly (bad); held B would fire NavigateUp()
    // repeatedly (races directory scan).
    if ((keys_down & HidNpadButton_A) || (keys_down & HidNpadButton_ZR)) {
        if (sidebar_focused_) {
            UL_LOG_INFO("vault: sidebar A idx=%zu path='%s'",
                        sidebar_idx_, SIDEBAR_ROOTS[sidebar_idx_].path);
            Navigate(SIDEBAR_ROOTS[sidebar_idx_].path);
            sidebar_focused_ = false;
        } else {
            EnterFocused();
        }
    }

    // ── B: navigate up / release sidebar ────────────────────────────────────
    // (stabilize-6 / RC-C2): B on sidebar releases focus; B on main = NavigateUp.
    // Task B: B is now strictly for navigation; ZL has its own context-menu block.
    if (keys_down & HidNpadButton_B) {
        if (sidebar_focused_) {
            sidebar_focused_ = false;
            UL_LOG_INFO("vault: sidebar B -> main pane");
        } else {
            NavigateUp();
        }
    }

    // ── ZL: open context menu on focused entry ────────────────────────────────
    // Task B: ZL triggers the per-entry popup (Open / Properties / Close Menu).
    // Only meaningful when main pane is focused and an entry is selected.
    if ((keys_down & HidNpadButton_ZL) && !sidebar_focused_ && entry_count_ > 0) {
        ctx_menu_active_ = true;
        ctx_menu_entry_  = focus_idx_;
        ctx_menu_opt_    = 0;
        UL_LOG_INFO("vault: ZL ctx_menu opened for entry %zu '%s'",
                    ctx_menu_entry_, entries_[ctx_menu_entry_].name);
    }

    // ── Y: cycle sort mode ────────────────────────────────────────────────────
    // Task D: Y toggles between ByName and ByKind, then re-scans so the new
    // order takes effect immediately.
    if ((keys_down & HidNpadButton_Y) && !sidebar_focused_) {
        sort_mode_ = (sort_mode_ == SortMode::ByName) ? SortMode::ByKind
                                                      : SortMode::ByName;
        UL_LOG_INFO("vault: sort_mode_ -> %s",
                    sort_mode_ == SortMode::ByName ? "ByName" : "ByKind");
        ScanCurrentDirectory();
    }

    // ── Minus: toggle dotfile visibility ──────────────────────────────────────
    // Task D: Minus shows/hides dotfiles (e.g. .config, .nro-cache) and re-scans.
    if (keys_down & HidNpadButton_Minus) {
        show_dotfiles_ = !show_dotfiles_;
        UL_LOG_INFO("vault: show_dotfiles_ -> %s",
                    show_dotfiles_ ? "true" : "false");
        ScanCurrentDirectory();
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
    // Task C: at root, B-back returns to the main desktop instead of silently
    // no-opping.  Matches the qd_DesktopIcons HOME-press pattern.
    if (new_len < 7) {
        UL_LOG_INFO("vault: NavigateUp at root -> returning to MainMenu");
        if (g_MenuApplication) {
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        }
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
                // F6 (stabilize-5): RC-C — FadeOut+Finalize before NRO launch.
                // Without this, uMenu re-asserts foreground status after the
                // LaunchHomebrewLibraryApplet() call and kills hbloader before
                // the NRO can initialize.  Pattern mirrors qd_DesktopIcons.cpp
                // cycle-C1 fix at LaunchIcon() lines 2200-2203.
                if (g_MenuApplication) {
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    g_MenuApplication->Finalize();
                }
                smi::LaunchHomebrewLibraryApplet(
                    std::string(e.full_path), std::string(""));
            }
            break;

        // Task A/EnterFocused: all non-Folder, non-NRO kinds are routed here.
        // ScanCurrentDirectory() already classified the entry, so we dispatch
        // directly on e.kind rather than re-reading the extension.
        case EntryKind::ImageFile:
            if (image_viewer_->LoadFile(e.full_path)) {
                viewer_active_ = true;
                UL_LOG_INFO("vault: image viewer opened for '%s'", e.full_path);
            } else {
                UL_LOG_WARN("vault: image viewer failed to load '%s'", e.full_path);
            }
            break;

        case EntryKind::TextFile:
        case EntryKind::ConfigFile:
            if (text_viewer_->LoadFile(e.full_path)) {
                viewer_active_ = true;
                UL_LOG_INFO("vault: text viewer opened for '%s'", e.full_path);
            } else {
                UL_LOG_WARN("vault: text viewer failed to load '%s'", e.full_path);
            }
            break;

        case EntryKind::NcaNspXci:
            // Nintendo archives — no in-vault handler in v1.8; log and no-op.
            UL_LOG_INFO("vault: no handler for NCA/NSP/XCI '%s'", e.full_path);
            break;

        case EntryKind::AudioFile:
            // Audio playback — no in-vault audio player in v1.8; log and no-op.
            UL_LOG_INFO("vault: no audio player for '%s'", e.full_path);
            break;

        case EntryKind::OtherFile:
        default:
            // Attempt the text viewer as a fallback for unknown file types;
            // if it fails (binary/unsupported encoding) log silently.
            if (text_viewer_->LoadFile(e.full_path)) {
                viewer_active_ = true;
                UL_LOG_INFO("vault: text viewer (fallback) opened for '%s'", e.full_path);
            } else {
                UL_LOG_INFO("vault: no viewer for '%s'", e.full_path);
            }
            break;
    }
}

} // namespace ul::menu::qdesktop
