// qd_VaultLayout.cpp — Finder-style NRO file browser element for uMenu C++ (v1.0.0).
// Stage 1 of docs/45_HBMenu_Replacement_Design.md: vault skeleton.
// Two-pane UI: sidebar (6 canonical roots) + main pane (dirs + NROs + files).
// NRO launch: smi::LaunchHomebrewLibraryApplet.
// Icon decode: ExtractNroIcon + QdIconCache (same path as QdDesktopIconsElement).

#include <ul/menu/qdesktop/qd_VaultLayout.hpp>
#include <ul/menu/qdesktop/qd_Transition.hpp>  // v3.6: RunLoadingSplash (NRO launch gap)
#include <ul/menu/qdesktop/qd_Audio.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>  // F6 (stabilize-5): RC-C — FadeOutToNonLibraryApplet
#include <ul/ul_Result.hpp>

// F6 (stabilize-5): RC-C — same extern pattern as qd_DesktopIcons.cpp / qd_Launchpad.cpp.
// Required so EnterFocused() can call FadeOutToNonLibraryApplet()+Finalize() AFTER
// smi::LaunchHomebrewLibraryApplet() (canonical order Launch → Fade → Finalize, see
// EnterFocused() rationale block).  Without these two follow-up calls uMenu
// re-asserts foreground and kills hbloader before the NRO can boot.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <dirent.h>
#include <sys/stat.h>   // stat() for Properties + size checks
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>    // Task D: std::sort
#include <switch/runtime/devices/fs_dev.h>  // fsdevGetDeviceFileSystem / fsFsDeleteDirectoryRecursively

namespace ul::menu::qdesktop {

// ── QdContentElement interface ────────────────────────────────────────────────
// v1.10.3.10: QdVaultLayout is a passive content renderer.  QdWindow owns all
// viewport arithmetic (scroll, scale, clip).  These two accessors replace the
// old SetContentSize / SetOwnerWindow pair; the natural canvas width is fixed at
// DEFAULT_WIN_W and the natural height grows with the number of grid rows so
// that QdWindow can compute the correct VSB travel distance.

s32 QdVaultLayout::GetNaturalW() const {
    return static_cast<s32>(DEFAULT_WIN_W);
}

s32 QdVaultLayout::GetNaturalH() const {
    const s32 cols = MainPaneCols(GetNaturalW());
    const s32 rows = (static_cast<s32>(entry_count_) + cols - 1) / cols;
    return VAULT_PATHBAR_H
         + rows * (VAULT_CELL_H + VAULT_CELL_GAP)
         + VAULT_CELL_GAP;
}

// ── Sidebar canonical roots ────────────────────────────────────────────────────
// Design doc §3.2: Desktop / Switch / Logs / Atmosphère / SD Root / Themes.
//
// W12 extension (creator request): explicit shortcuts to the important
// filesystem locations so the user can jump straight to save data, NSP
// installs, hekate payloads, etc. without typing paths.  Order matters —
// most-used at top.

const QdVaultLayout::SidebarRoot
QdVaultLayout::SIDEBAR_ROOTS[QdVaultLayout::SIDEBAR_ROOT_COUNT] = {
    { "Homebrew",      "sdmc:/switch/"                 }, ///< NRO browser (was "Switch (NROs)")
    { "Saves",         "sdmc:/atmosphere/contents/"    }, ///< per-title contents incl. save dirs
    { "NSP / NCA",     "sdmc:/nsp/"                    }, ///< common NSP staging dir (Tinfoil/Goldleaf convention)
    { "Payloads",      "sdmc:/bootloader/payloads/"    }, ///< hekate payload dropbox
    { "Atmosphère",    "sdmc:/atmosphere/"             }, ///< CFW root
    { "Themes",        "sdmc:/themes/"                 }, ///< NXThemes / Q OS theme installer drop
    { "Q OS",          "sdmc:/ulaunch/"                }, ///< our own config + cache + logs
    { "Logs",          "sdmc:/atmosphere/crash_reports/" }, ///< crash reports (most useful log path for users)
    { "Bootloader",    "sdmc:/bootloader/"             }, ///< hekate config + ini files
    { "Nintendo",      "sdmc:/Nintendo/"               }, ///< stock save data + private (read-only by convention)
    { "Switch (deep)", "sdmc:/switch/"                 }, ///< full switch dir browse (parity with W12-pre)
    { "SD Root",       "sdmc:/"                        }, ///< filesystem root
};

// ── Vault column computation ───────────────────────────────────────────────────

s32 QdVaultLayout::MainPaneCols(s32 content_w) {
    // Main pane width = element width minus sidebar minus one gap.
    const s32 pane_w = content_w - VAULT_SIDEBAR_W - VAULT_CELL_GAP;
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
    : theme_(theme), entry_count_(0), focus_idx_(0),
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
    // 2026-05-06: reset touch-drag scroll on navigation so the new directory
    // listing starts at the top.  Also clears any in-flight drag state so a
    // tap that triggers Navigate (folder open / sidebar) cannot leak its
    // movement counter into the post-navigation drag handler.
    drag_view_offset_y_   = 0;
    drag_in_progress_     = false;
    drag_passed_deadband_ = false;
}

// ── SetCategoryFilter ─────────────────────────────────────────────────────────

void QdVaultLayout::SetCategoryFilter(FolderIdx filter, FolderIdx filter2) {
    category_filter_  = filter;
    category_filter2_ = filter2;
    if (cwd_[0] != '\0') {
        // Re-scan so the filter takes effect immediately on whatever directory
        // is already open.  FreeEntryTextures is called first to reclaim any
        // GPU textures from the previous (unfiltered) scan.
        FreeEntryTextures();
        ScanCurrentDirectory();
    }
}

// ── ScanCurrentDirectory ──────────────────────────────────────────────────────

void QdVaultLayout::ScanCurrentDirectory() {
    entry_count_  = 0;
    focus_idx_    = 0;

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
            // B1.1: for NRO entries, prefer the NACP application name.
            //   ExtractNroNacp returns "" on any failure (no ASET, no NACP,
            //   empty name, malformed NRO) — keep the stem in that case.
            safe_copy(e.name, sizeof(e.name), de->d_name);
            if (is_nro && last_dot != nullptr) {
                const size_t strip = static_cast<size_t>(last_dot - de->d_name);
                if (strip < sizeof(e.name)) {
                    e.name[strip] = '\0';
                }
                // e.full_path already built above; use it as the NRO path.
                const std::string nacp_name = ExtractNroNacp(e.full_path);
                if (!nacp_name.empty()) {
                    snprintf(e.name, sizeof(e.name), "%s", nacp_name.c_str());
                }
            }
        }

        // Category filter: if a filter is active and this entry is an NRO,
        // look up its bucket and skip it when the bucket doesn't match any
        // of the configured filter buckets.
        // Folders and non-NRO files are always kept so the directory tree
        // remains navigable regardless of the active filter.
        if (category_filter_ != FolderIdx::None && e.kind == EntryKind::Nro) {
            const FolderIdx fi = QdFolderClassifier::Get().Lookup(e.full_path);
            const bool match1 = (fi == category_filter_);
            const bool match2 = (category_filter2_ != FolderIdx::None &&
                                  fi == category_filter2_);
            if (!match1 && !match2) {
                // Entry belongs to a different category — skip it.
                // Clear the entry slot so it doesn't leave stale data.
                e.icon_tex     = nullptr;
                e.icon_decoded = false;
                e.name[0]      = '\0';
                e.full_path[0] = '\0';
                e.kind         = EntryKind::OtherFile;
                continue;
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
    // v1.10.3.10: use GetNaturalW(); QdWindow clip rect handles viewport culling.
    const s32 cols       = MainPaneCols(GetNaturalW());
    const s32 col_stride = VAULT_CELL_W + VAULT_CELL_GAP;
    const s32 row_stride = VAULT_CELL_H + VAULT_CELL_GAP;

    const s32 col = static_cast<s32>(i) % cols;
    const s32 row = static_cast<s32>(i) / cols;

    // Main pane starts right of the sidebar + one gap.
    const s32 pane_left = origin_x + VAULT_SIDEBAR_W + VAULT_CELL_GAP;
    const s32 pane_top  = origin_y + VAULT_BODY_TOP + VAULT_PATHBAR_H + VAULT_CELL_GAP;

    out_x = pane_left + col * col_stride;
    // 2026-05-06: bake touch-drag scroll offset directly into the cell y so
    // render and hit-test observe identical positions.  drag_view_offset_y_
    // is positive when the content has been scrolled up by the user's finger.
    out_y = pane_top  + row * row_stride - drag_view_offset_y_;
    return true;
}

// ── MaxScrollOffsetY ──────────────────────────────────────────────────────────

s32 QdVaultLayout::MaxScrollOffsetY() const {
    // The natural canvas height already accounts for the row count.  Subtract
    // the pathbar's reserved strip — once the last row is within the pathbar+
    // body region the user has effectively "scrolled to the bottom."  Any
    // residual over-scroll is harmless because QdWindow's clip rect culls.
    const s32 cap = GetNaturalH() - VAULT_PATHBAR_H - VAULT_CELL_H;
    return (cap > 0) ? cap : 0;
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
        GetNaturalW() - VAULT_SIDEBAR_W,
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
                static_cast<u32>(GetNaturalW() - VAULT_SIDEBAR_W - 24));
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

    // Entry grid cells.  QdWindow's SDL clip rect handles viewport culling.
    for (size_t i = 0; i < entry_count_; ++i) {
        s32 cx, cy;
        if (!EntryRect(i, cx, cy, origin_x, origin_y)) {
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

// ── PromptText ────────────────────────────────────────────────────────────────
// Open the SwkbdConfig inline keyboard and return the entered string.
// Mirrors the pattern used in qd_CustomFolder.cpp and qd_Launchpad.cpp.

bool QdVaultLayout::PromptText(const char *header, const char *initial,
                                char *out_buf, size_t out_capacity) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        UL_LOG_WARN("vault: swkbdCreate failed");
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetType(&kbd, SwkbdType_All);
    swkbdConfigSetGuideText(&kbd, header);
    if (initial != nullptr && initial[0] != '\0') {
        swkbdConfigSetInitialText(&kbd, initial);
    }
    swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(out_capacity - 1u));
    const bool ok = R_SUCCEEDED(swkbdShow(&kbd, out_buf, out_capacity));
    swkbdClose(&kbd);
    return ok;
}

// ── ConfirmDelete ─────────────────────────────────────────────────────────────
// Show a two-choice swkbd prompt so the user must type "yes" to confirm.
// Case-insensitive; any other input is treated as cancel.

bool QdVaultLayout::ConfirmDelete(const char *path) {
    char answer[8] = {};
    char header[MAX_PATH + 32] = {};
    snprintf(header, sizeof(header), "Delete '%s'? (type yes)", path);
    if (!PromptText(header, "", answer, sizeof(answer))) {
        return false;
    }
    // Case-insensitive compare: "yes", "YES", "Yes" all confirm.
    char lower[8] = {};
    for (size_t i = 0; i < sizeof(answer) - 1u && answer[i]; ++i) {
        lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(answer[i])));
    }
    return (strncmp(lower, "yes", 3) == 0 && lower[3] == '\0');
}

// ── DoRename ──────────────────────────────────────────────────────────────────
// Rename the focused entry using swkbd; updates entries_ on success.

void QdVaultLayout::DoRename() {
    if (vault_ctx_target_entry_ >= entry_count_) return;
    const Entry &e = entries_[vault_ctx_target_entry_];

    char new_name[MAX_PATH] = {};
    if (!PromptText("New name", e.name, new_name, sizeof(new_name))) {
        return;  // user cancelled
    }
    if (new_name[0] == '\0' || strncmp(new_name, e.name, MAX_PATH) == 0) {
        return;  // empty or unchanged
    }

    // Build the destination path: same parent directory as the source.
    // Use 2× MAX_PATH to avoid -Wformat-truncation when cwd_ + name approach limit.
    char dest[MAX_PATH * 2] = {};
    snprintf(dest, sizeof(dest), "%s/%s", cwd_, new_name);

    if (rename(e.full_path, dest) != 0) {
        UL_LOG_WARN("vault: rename '%s' -> '%s' failed errno=%d",
                    e.full_path, dest, errno);
    } else {
        UL_LOG_INFO("vault: renamed '%s' -> '%s'", e.full_path, dest);
        QdAudio::Play(DesktopSfxEvent::VaultFileRename);
        ScanCurrentDirectory();
    }
}

// ── DoDelete ─────────────────────────────────────────────────────────────────
// Delete file or directory recursively.  Prompts the user for confirmation.

void QdVaultLayout::DoDelete() {
    if (vault_ctx_target_entry_ >= entry_count_) return;
    const Entry &e = entries_[vault_ctx_target_entry_];

    if (!ConfirmDelete(e.name)) {
        return;  // user cancelled
    }

    if (e.kind == EntryKind::Folder) {
        // Use fsFsDeleteDirectoryRecursively via the sdmc FsFileSystem*.
        FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
        if (sdmc == nullptr) {
            UL_LOG_WARN("vault: delete dir: fsdevGetDeviceFileSystem returned NULL");
            return;
        }
        // fsFsDeleteDirectoryRecursively expects a path relative to the FS root,
        // without the "sdmc:" prefix.
        const char *rel = e.full_path;
        if (strncmp(rel, "sdmc:", 5) == 0) rel += 5;
        const Result rc = fsFsDeleteDirectoryRecursively(sdmc, rel);
        if (R_FAILED(rc)) {
            UL_LOG_WARN("vault: fsFsDeleteDirectoryRecursively '%s' failed 0x%08X",
                        e.full_path, rc);
        } else {
            UL_LOG_INFO("vault: deleted directory '%s'", e.full_path);
            QdAudio::Play(DesktopSfxEvent::VaultFileDelete);
            ScanCurrentDirectory();
        }
    } else {
        if (remove(e.full_path) != 0) {
            UL_LOG_WARN("vault: remove '%s' failed errno=%d", e.full_path, errno);
        } else {
            UL_LOG_INFO("vault: deleted file '%s'", e.full_path);
            QdAudio::Play(DesktopSfxEvent::VaultFileDelete);
            ScanCurrentDirectory();
        }
    }
}

// ── DoCut ─────────────────────────────────────────────────────────────────────
// Mark the focused entry as the cut source in the clipboard.

void QdVaultLayout::DoCut() {
    if (vault_ctx_target_entry_ >= entry_count_) return;
    const Entry &e = entries_[vault_ctx_target_entry_];
    strncpy(clipboard_path_, e.full_path, MAX_PATH - 1u);
    clipboard_path_[MAX_PATH - 1u] = '\0';
    clipboard_is_cut_ = true;
    has_clipboard_    = true;
    UL_LOG_INFO("vault: cut '%s'", clipboard_path_);
}

// ── DoCopy ────────────────────────────────────────────────────────────────────
// Mark the focused entry as the copy source in the clipboard.

void QdVaultLayout::DoCopy() {
    if (vault_ctx_target_entry_ >= entry_count_) return;
    const Entry &e = entries_[vault_ctx_target_entry_];
    strncpy(clipboard_path_, e.full_path, MAX_PATH - 1u);
    clipboard_path_[MAX_PATH - 1u] = '\0';
    clipboard_is_cut_ = false;
    has_clipboard_    = true;
    UL_LOG_INFO("vault: copy '%s'", clipboard_path_);
}

// ── DoPaste ───────────────────────────────────────────────────────────────────
// Copy (or move) the clipboard item into cwd_.
// For files: fopen/fread/fwrite in 64KB chunks.
// For directories: decline with a log (recursive dir copy is out of scope for v2.0.1).

void QdVaultLayout::DoPaste() {
    if (!has_clipboard_) {
        UL_LOG_WARN("vault: paste: clipboard is empty");
        return;
    }

    // Derive the base name from clipboard_path_.
    const char *base = strrchr(clipboard_path_, '/');
    if (base == nullptr) {
        base = strrchr(clipboard_path_, ':');  // "sdmc:" edge case
    }
    base = (base != nullptr) ? base + 1 : clipboard_path_;
    if (base[0] == '\0') {
        UL_LOG_WARN("vault: paste: cannot derive basename from '%s'", clipboard_path_);
        return;
    }

    char dest[MAX_PATH * 2] = {};
    snprintf(dest, sizeof(dest), "%s/%s", cwd_, base);

    // Stat the clipboard source to check whether it's a file or directory.
    struct stat st = {};
    if (stat(clipboard_path_, &st) != 0) {
        UL_LOG_WARN("vault: paste: stat '%s' failed errno=%d", clipboard_path_, errno);
        has_clipboard_ = false;
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        // Recursive directory copy is out of scope for v2.0.1.
        // Move (rename) works atomically on the same filesystem.
        if (clipboard_is_cut_) {
            if (rename(clipboard_path_, dest) != 0) {
                UL_LOG_WARN("vault: paste move dir '%s' -> '%s' failed errno=%d",
                            clipboard_path_, dest, errno);
            } else {
                UL_LOG_INFO("vault: paste moved dir '%s' -> '%s'", clipboard_path_, dest);
                has_clipboard_ = false;
                ScanCurrentDirectory();
            }
        } else {
            UL_LOG_WARN("vault: paste: recursive dir copy not supported; use cut+paste to move");
        }
        return;
    }

    // File copy: read clipboard_path_ and write to dest in 64KB chunks.
    static constexpr size_t BUF_SIZE = 64u * 1024u;
    FILE *src_f = fopen(clipboard_path_, "rb");
    if (src_f == nullptr) {
        UL_LOG_WARN("vault: paste: fopen src '%s' failed errno=%d", clipboard_path_, errno);
        return;
    }
    FILE *dst_f = fopen(dest, "wb");
    if (dst_f == nullptr) {
        UL_LOG_WARN("vault: paste: fopen dst '%s' failed errno=%d", dest, errno);
        fclose(src_f);
        return;
    }

    // Heap-allocate the chunk buffer to avoid stack pressure.
    char *buf = static_cast<char *>(malloc(BUF_SIZE));
    if (buf == nullptr) {
        UL_LOG_WARN("vault: paste: malloc %zu failed", BUF_SIZE);
        fclose(src_f);
        fclose(dst_f);
        remove(dest);
        return;
    }

    bool copy_ok = true;
    size_t n;
    while ((n = fread(buf, 1u, BUF_SIZE, src_f)) > 0u) {
        if (fwrite(buf, 1u, n, dst_f) != n) {
            UL_LOG_WARN("vault: paste: fwrite failed errno=%d", errno);
            copy_ok = false;
            break;
        }
    }
    if (!feof(src_f)) {
        UL_LOG_WARN("vault: paste: fread error errno=%d", errno);
        copy_ok = false;
    }

    free(buf);
    fclose(src_f);
    fclose(dst_f);

    if (!copy_ok) {
        remove(dest);  // clean up partial write
        return;
    }

    UL_LOG_INFO("vault: paste copied '%s' -> '%s'", clipboard_path_, dest);
    QdAudio::Play(DesktopSfxEvent::VaultFileCopy);

    if (clipboard_is_cut_) {
        // Remove the source after a successful copy.
        if (remove(clipboard_path_) != 0) {
            UL_LOG_WARN("vault: paste: remove src '%s' after cut failed errno=%d",
                        clipboard_path_, errno);
        }
        has_clipboard_ = false;
    }

    ScanCurrentDirectory();
}

// ── DoNewFolder ───────────────────────────────────────────────────────────────
// Prompt for a folder name via swkbd, then mkdir it inside cwd_.

void QdVaultLayout::DoNewFolder() {
    char folder_name[MAX_PATH] = {};
    if (!PromptText("New folder name", "", folder_name, sizeof(folder_name))) {
        return;  // user cancelled
    }
    if (folder_name[0] == '\0') {
        return;  // empty name
    }

    // Buffer: cwd_ (≤MAX_PATH-1) + '/' + folder_name (≤MAX_PATH-1) + '\0'
    // = 2*(MAX_PATH-1) + 2 = MAX_PATH*2.  +2 gives 1 byte slack beyond the
    // worst-case concatenation so GCC's format-truncation analysis is satisfied.
    char new_dir[MAX_PATH * 2 + 2] = {};
    snprintf(new_dir, sizeof(new_dir), "%s/%s", cwd_, folder_name);

    if (mkdir(new_dir, 0755) != 0) {
        UL_LOG_WARN("vault: mkdir '%s' failed errno=%d", new_dir, errno);
    } else {
        UL_LOG_INFO("vault: created directory '%s'", new_dir);
        ScanCurrentDirectory();
    }
}

// ── RenderContextMenu ─────────────────────────────────────────────────────────
// Delegates to the unified QdContextMenu primitive.
// Called only while vault_ctx_menu_.IsOpen(); no-op otherwise (IsOpen guard
// in caller's OnRender block).

void QdVaultLayout::RenderContextMenu(SDL_Renderer *r) const {
    // QdContextMenu::Render is declared const; the object is mutable state but
    // we're only calling the const render path here.
    const_cast<QdContextMenu &>(vault_ctx_menu_).Render(r);
}

// ── DispatchContextMenuOption ─────────────────────────────────────────────────
// Called after vault_ctx_menu_ has closed (IsOpen() == false).
// Maps the confirmed GetSelectedIndex() result back to the per-opt index fields
// set by BuildVisibleContextMenuOptions() and fires the matching handler.
// Cancelled selections (GetSelectedIndex() == -1) are silently ignored.

void QdVaultLayout::DispatchContextMenuOption() {
    if (vault_ctx_target_entry_ >= entry_count_) {
        return;
    }

    const int sel = vault_ctx_menu_.GetSelectedIndex();
    if (sel == -1) {
        // User cancelled (B / ZL / touch-outside).
        UL_LOG_INFO("vault: ctx_menu cancelled");
        return;
    }

    // Derive the vault_ctx_target_entry_ alias used by the Do* helpers.
    // Set it before any helper call so the helpers find the right entry.
    const size_t saved_target = vault_ctx_target_entry_;

    UL_LOG_INFO("vault: ctx_menu dispatch sel=%d for entry %zu",
                sel, saved_target);

    // Dispatch by comparing sel against each per-opt index.  Only one
    // index will match; all others are -1 (not present in this open).
    if (sel == vault_ctx_opt_open_) {
        focus_idx_ = saved_target;
        EnterFocused();
    } else if (sel == vault_ctx_opt_launch_app_) {
        // Temporarily alias so DoLaunchAsApplication() finds the right entry.
        // DoLaunchAsApplication reads vault_ctx_target_entry_ — set above.
        DoLaunchAsApplication();
    } else if (sel == vault_ctx_opt_properties_) {
        const Entry &e = entries_[saved_target];
        struct stat st = {};
        if (stat(e.full_path, &st) == 0) {
            char time_buf[64] = {};
            struct tm *tm_info = localtime(&st.st_mtime);
            if (tm_info != nullptr) {
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            } else {
                snprintf(time_buf, sizeof(time_buf), "(unknown)");
            }
            UL_LOG_INFO("vault: properties '%s': size=%lld bytes, mtime=%s",
                        e.full_path, static_cast<long long>(st.st_size), time_buf);
        } else {
            UL_LOG_WARN("vault: properties stat() failed for '%s' errno=%d",
                        e.full_path, errno);
        }
    } else if (sel == vault_ctx_opt_rename_) {
        DoRename();
    } else if (sel == vault_ctx_opt_delete_) {
        DoDelete();
    } else if (sel == vault_ctx_opt_cut_) {
        DoCut();
    } else if (sel == vault_ctx_opt_copy_) {
        DoCopy();
    } else if (sel == vault_ctx_opt_paste_) {
        DoPaste();
    } else if (sel == vault_ctx_opt_new_folder_) {
        DoNewFolder();
    } else if (sel == vault_ctx_opt_save_editor_) {
        // W11-SAVE Part 2: open the Pokémon save editor window.
        // on_open_save_editor is wired by OpenVaultWindow() to call
        // QdDesktopIconsElement::OpenSaveEditorWindow().  Guard against a
        // nullptr callback (defensive; wiring is always set at open time).
        UL_LOG_INFO("vault: ctx_menu 'Edit Poke\x81mon save' for entry %zu", saved_target);
        if (on_open_save_editor) {
            on_open_save_editor();
        } else {
            UL_LOG_WARN("vault: on_open_save_editor callback not wired — no-op");
        }
    } else if (sel == vault_ctx_opt_cheats_) {
        // W12-CHEATS: open the cheat browser for the TID-shaped directory entry.
        // on_open_cheats is wired by OpenVaultWindow() to call
        // QdDesktopIconsElement::OpenCheatsWindow(app_id).
        UL_LOG_INFO("vault: ctx_menu 'View Cheats' app_id=0x%llx for entry %zu",
                    static_cast<unsigned long long>(vault_ctx_app_id_), saved_target);
        if (on_open_cheats) {
            on_open_cheats(vault_ctx_app_id_);
        } else {
            UL_LOG_WARN("vault: on_open_cheats callback not wired — no-op");
        }
    } else if (sel == vault_ctx_opt_mods_) {
        // B3.1-MODS: open the LayeredFS mod manager for the TID-shaped directory entry.
        // on_open_mods is wired by OpenVaultWindow() to call
        // QdDesktopIconsElement::OpenModsWindow(app_id).
        UL_LOG_INFO("vault: ctx_menu 'View Mods' app_id=0x%llx for entry %zu",
                    static_cast<unsigned long long>(vault_ctx_app_id_), saved_target);
        if (on_open_mods) {
            on_open_mods(vault_ctx_app_id_);
        } else {
            UL_LOG_WARN("vault: on_open_mods callback not wired — no-op");
        }
    } else if (sel == vault_ctx_opt_cancel_) {
        UL_LOG_INFO("vault: ctx_menu closed via Cancel option");
    } else {
        UL_LOG_WARN("vault: ctx_menu unknown sel=%d — ignoring", sel);
    }
}

// ── BuildVisibleContextMenuOptions ────────────────────────────────────────────
// Builds a std::vector<QdContextMenuItem> for the focused entry, assigns the
// per-opt index fields (vault_ctx_opt_*), then calls vault_ctx_menu_.Open().
// LaunchAsApp is the only EntryKind-conditional option: included only for .nro
// entries; omitted entirely (not shown as disabled) for everything else.
// Anchor is the centre of the main pane so the panel appears in a predictable
// screen location regardless of which entry was highlighted.

void QdVaultLayout::BuildVisibleContextMenuOptions() {
    // Reset all per-opt index fields before building the new list.
    vault_ctx_opt_open_        = -1;
    vault_ctx_opt_launch_app_  = -1;
    vault_ctx_opt_properties_  = -1;
    vault_ctx_opt_rename_      = -1;
    vault_ctx_opt_delete_      = -1;
    vault_ctx_opt_cut_         = -1;
    vault_ctx_opt_copy_        = -1;
    vault_ctx_opt_paste_       = -1;
    vault_ctx_opt_new_folder_  = -1;
    vault_ctx_opt_save_editor_ = -1;  // W11-SAVE Part 2
    vault_ctx_opt_cheats_      = -1;  // W12-CHEATS
    vault_ctx_opt_mods_        = -1;  // B3.1-MODS
    vault_ctx_opt_cancel_      = -1;
    vault_ctx_app_id_          = 0;

    const bool entry_is_nro =
        (vault_ctx_target_entry_ < entry_count_)
        && (entries_[vault_ctx_target_entry_].kind == EntryKind::Nro);

    std::vector<QdContextMenuItem> items;
    int idx = 0;

    // Helper: append a leaf item and record its index.
    auto push = [&](std::string label, std::string hint, int &opt_field) {
        QdContextMenuItem item;
        item.label    = std::move(label);
        item.key_hint = std::move(hint);
        opt_field = idx++;
        items.push_back(std::move(item));
    };

    push("Open",                   "[A]",  vault_ctx_opt_open_);
    if (entry_is_nro) {
        push("Launch as Application", "[R]", vault_ctx_opt_launch_app_);
    }
    push("Properties",             "[+]",  vault_ctx_opt_properties_);
    push("Rename",                 "[X]",  vault_ctx_opt_rename_);
    push("Delete",                 "[Y]",  vault_ctx_opt_delete_);
    push("Cut",                    "[L]",  vault_ctx_opt_cut_);
    push("Copy",                   "[R]",  vault_ctx_opt_copy_);
    push("Paste",                  "[ZR]", vault_ctx_opt_paste_);
    push("New Folder",             "[ZL]", vault_ctx_opt_new_folder_);
    // W12-SAVE-DISCO Part 4: filter "Edit Pokémon save" to:
    //   - Directories (user can navigate INTO a save directory and select it)
    //   - Files with extension .sav / .bin / .dat
    // Excluded from every other file type to reduce context-menu noise.
    {
        bool show_save_editor = false;
        if (vault_ctx_target_entry_ < entry_count_) {
            const Entry &ce = entries_[vault_ctx_target_entry_];
            if (ce.kind == EntryKind::Folder) {
                show_save_editor = true;
            } else {
                // Check extension on the full_path.
                const char *last_dot = nullptr;
                for (const char *p = ce.full_path; *p != '\0'; ++p) {
                    if (*p == '.') { last_dot = p; }
                }
                if (last_dot != nullptr && last_dot[1] != '\0') {
                    char ext[8] = {};
                    size_t ei = 0;
                    for (const char *p = last_dot + 1;
                         *p != '\0' && ei < 7; ++p, ++ei) {
                        ext[ei] = static_cast<char>(
                            tolower(static_cast<unsigned char>(*p)));
                    }
                    show_save_editor = (strcmp(ext, "sav") == 0 ||
                                        strcmp(ext, "bin") == 0 ||
                                        strcmp(ext, "dat") == 0);
                }
            }
        }
        if (show_save_editor) {
            push("Edit Pok\xc3\xa9mon save", "[Y]", vault_ctx_opt_save_editor_);
        }
    }

    // W12-CHEATS / B3.1-MODS: "View Cheats" and "View Mods" — shown when the
    // focused entry is a directory whose name looks like a 16-hex title-id
    // (i.e. browsing inside sdmc:/atmosphere/contents/ or any TID folder).
    // We derive the app_id from the directory name so OpenCheatsWindow /
    // OpenModsWindow can jump directly to the correct title's list.
    {
        vault_ctx_app_id_ = 0;
        if (vault_ctx_target_entry_ < entry_count_) {
            const Entry &ce = entries_[vault_ctx_target_entry_];
            if (ce.kind == EntryKind::Folder) {
                // Check if the name is exactly 16 hex characters (TID shape).
                const size_t nlen = strnlen(ce.name, sizeof(ce.name));
                if (nlen == 16) {
                    bool all_hex = true;
                    u64 parsed_tid = 0;
                    for (size_t ci = 0; ci < 16; ++ci) {
                        const char ch = ce.name[ci];
                        u64 nybble = 0;
                        if (ch >= '0' && ch <= '9')      { nybble = static_cast<u64>(ch - '0'); }
                        else if (ch >= 'a' && ch <= 'f') { nybble = static_cast<u64>(ch - 'a' + 10); }
                        else if (ch >= 'A' && ch <= 'F') { nybble = static_cast<u64>(ch - 'A' + 10); }
                        else { all_hex = false; break; }
                        parsed_tid = (parsed_tid << 4) | nybble;
                    }
                    if (all_hex && parsed_tid != 0) {
                        vault_ctx_app_id_ = parsed_tid;
                        push("View Cheats", "[ZL]", vault_ctx_opt_cheats_);
                        // B3.1-MODS: pair "View Mods" right below "View Cheats"
                        // so the two game-content tools stay together visually.
                        push("View Mods",   "[ZR]", vault_ctx_opt_mods_);
                    }
                }
            }
        }
    }

    push("Close Menu",             "[B]",  vault_ctx_opt_cancel_);

    // Anchor: centre of the main pane (right of sidebar, body area).
    const s32 main_left = VAULT_SIDEBAR_W;
    const s32 main_w    = GetNaturalW() - main_left;
    const s32 anchor_x  = main_left + main_w / 2;
    const s32 anchor_y  = VAULT_BODY_TOP + VAULT_BODY_H / 2;

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    vault_ctx_menu_.Open(r, items, anchor_x, anchor_y);

    UL_LOG_INFO("vault: ctx_menu opened for entry %zu '%s' (items=%d nro=%d)",
                vault_ctx_target_entry_,
                (vault_ctx_target_entry_ < entry_count_)
                    ? entries_[vault_ctx_target_entry_].name : "?",
                idx,
                static_cast<int>(entry_is_nro));
}

// ── DoLaunchAsApplication ─────────────────────────────────────────────────────
// 2026-05-06: hbmenu-parity "launch as application" path.
//
// Routes the focused .nro through smi::LaunchHomebrewApplication instead of
// smi::LaunchHomebrewLibraryApplet.  uSystem reads
// HomebrewApplicationTakeoverApplicationId from cfg::Config and uses that
// title id to take over the application slot — see
// uSystem/source/main.cpp:818-839 (case SystemMessage::LaunchHomebrewApplication
// pulls the app_id from g_Config.GetEntry(...)) and main.cpp:1143-1149
// (ActionType::LaunchHomebrewApplication consumes it).  uMenu does not need
// to forward the app_id; the SMI signature is path+argv only.
//
// Mirrors the existing fade/finalize sequence in EnterFocused() (the
// LibraryApplet path) — without it, uMenu re-asserts foreground status
// after the SMI call and kills hbloader before the NRO can boot.  Pattern
// parallels qd_DesktopIcons.cpp's LaunchIcon() and ui_MainMenuLayout.cpp:
// LaunchHomebrewApplication() at line 641.

void QdVaultLayout::DoLaunchAsApplication() {
    if (vault_ctx_target_entry_ >= entry_count_) {
        return;
    }
    const Entry &e = entries_[vault_ctx_target_entry_];
    if (e.kind != EntryKind::Nro || e.full_path[0] == '\0') {
        UL_LOG_WARN("vault: DoLaunchAsApplication ignored — entry not an .nro");
        return;
    }

    UL_LOG_INFO("vault: launch-as-application '%s'", e.full_path);
    // CRITICAL (W2-VAULT 2026-05-19): canonical SMI order is Launch → Fade →
    // Finalize.  Mirrors EnterFocused() and qd_DesktopIcons.cpp:5877-5889.
    // Earlier reversed order tore down the MenuApplication dispatcher state
    // before uSystem processed the launch message; see EnterFocused() above
    // for the full rationale.
    const auto rc = smi::LaunchHomebrewApplication(
        std::string(e.full_path), std::string(""));
    if (R_FAILED(rc)) {
        UL_LOG_WARN("vault: smi::LaunchHomebrewApplication rc=0x%08X", rc);
    }
    // v3.6: animated splash covers the NRO-body read gap.
    // Order: Launch → splash → FadeOut → Finalize (canonical — mirrors
    // W2-VAULT 2026-05-19 rationale above; fade-before-launch was the bug).
    qdesktop::RunLoadingSplash(2000);
    if (g_MenuApplication) {
        g_MenuApplication->FadeOutToNonLibraryApplet();
        g_MenuApplication->Finalize();
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
    // v1.10.3.10: use GetNaturalW(); QdWindow owns the bottom chrome hint bar.
    SDL_Rect backdrop { x, y + VAULT_BODY_TOP, GetNaturalW(), VAULT_BODY_H };
    SDL_RenderFillRect(r, &backdrop);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    RenderSidebar(r, x, y);
    RenderMainPane(r, x, y);

    // Task B: context menu floats above the file grid but below the viewer.
    // Delegates to the unified QdContextMenu primitive; no-op if not open.
    vault_ctx_menu_.Render(r);

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
    // Task B: context menu is the highest-priority overlay.
    // Delegate all input to vault_ctx_menu_ while it is open; dispatch and
    // return when it closes.  HandleInput returns true when the menu consumed
    // the input and may close itself (confirmed or cancelled).
    if (vault_ctx_menu_.IsOpen()) {
        const s32 tx = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.x);
        const s32 ty = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.y);
        // Vault has no software cursor; pass -1/-1 for cx/cy.
        vault_ctx_menu_.HandleInput(keys_down, keys_up, -1, -1, tx, ty);
        if (!vault_ctx_menu_.IsOpen()) {
            DispatchContextMenuOption();
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
                        // 2026-05-06: drag-block state must be in lockstep
                        // with the sidebar handler's early-return so the
                        // drag state machine doesn't carry a stale latch
                        // when sidebar consumes a touch mid/post-flick.
                        drag_in_progress_      = false;
                        drag_passed_deadband_  = false;
                        drag_was_touch_active_ = sb_touch_active_now;
                        return;
                    }
                }
            }
        }
        sb_was_touch_active_last_frame_ = sb_touch_active_now;
    }

    // ── Touch-drag scroll + tap dispatch (2026-05-06) ────────────────────────
    // State machine for the main-pane (entry grid) touch area:
    //
    //   touch-down (rising edge)  ─────►  record start_y / start_offset
    //                                     drag_in_progress_     = true
    //                                     drag_passed_deadband_ = false
    //   while held (drag_in_progress_):
    //       |delta| > DRAG_DEADBAND_PX  ─►  drag_passed_deadband_ = true,
    //                                       drag_view_offset_y_ tracks delta
    //   touch-up (falling edge):
    //       drag_passed_deadband_ == true   ─►  consume; no tap dispatch
    //       drag_passed_deadband_ == false  ─►  fire entry-tap at start pos
    //
    // The grid area is the rect right of the sidebar and below the pathbar.
    // Sidebar / hot-corner / context-menu hits are handled earlier in the
    // function and do not enter this state machine.  ZL long-press (context
    // menu) is button-driven and unaffected by touch movement.
    {
        const bool touch_active_now = !touch_pos.IsEmpty();
        const s32  tx               = static_cast<s32>(touch_pos.x);
        const s32  ty               = static_cast<s32>(touch_pos.y);

        // The drag-active rect is the entire main-pane area below the
        // pathbar — scrolling should start anywhere the user can see grid
        // cells, including the empty space between rows or below the last
        // row.  Sidebar (x < VAULT_SIDEBAR_W) is excluded; pathbar
        // (y < VAULT_BODY_TOP + VAULT_PATHBAR_H) is excluded.
        auto inside_grid_area = [&](s32 px, s32 py) -> bool {
            return px >= VAULT_SIDEBAR_W
                && px <  GetNaturalW()
                && py >= VAULT_BODY_TOP + VAULT_PATHBAR_H
                && py <  VAULT_BODY_TOP + VAULT_BODY_H;
        };

        // Rising edge: touch came down.  If inside the grid area, arm the
        // drag.  If outside (e.g. titlebar / scrollbar — those areas are
        // owned by QdWindow and we never see them anyway since QdWindow
        // routes them away from content), do nothing — fall through to the
        // legacy keyboard / D-pad handling so non-grid taps don't break.
        //
        // 2026-05-06: explicit sidebar-X early-bail.  inside_grid_area
        // already excludes px < VAULT_SIDEBAR_W, but that left the drag
        // state machine in a transient state on every sidebar touch which
        // appears to compete with the sidebar handler's edge-trigger latch
        // on rapid taps (creator report: "screen flick prevents touches
        // on the sidebar").  Skip the entire state mutation when the
        // touch lands in sidebar X — sidebar handler has already had its
        // turn earlier in OnInput and either consumed (early return) or
        // declined (Y missed visible items).  Either way the drag block
        // shouldn't touch its own state for a sidebar gesture.
        if (touch_active_now && !drag_was_touch_active_) {
            const bool in_sidebar_x = (tx >= 0 && tx < VAULT_SIDEBAR_W);
            if (in_sidebar_x) {
                // Leave drag state untouched; just sync the touch latch
                // so the falling-edge check below sees this frame.
                drag_was_touch_active_ = touch_active_now;
                return;
            }
            if (inside_grid_area(tx, ty)) {
                drag_in_progress_      = true;
                drag_passed_deadband_  = false;
                drag_start_touch_x_    = tx;
                drag_start_touch_y_    = ty;
                drag_start_offset_y_   = drag_view_offset_y_;
            } else {
                drag_in_progress_      = false;
                drag_passed_deadband_  = false;
            }
        }

        // Held: track movement against the start point.  Once the deadband
        // is exceeded the gesture is committed to a drag — update the scroll
        // offset every frame against the absolute delta from the start
        // position (not the per-frame delta) so a slow drift integrates
        // smoothly without jitter from held-frame (-1,-1) re-arrivals.
        if (touch_active_now && drag_in_progress_) {
            const s32 dy = ty - drag_start_touch_y_;
            const s32 dx = tx - drag_start_touch_x_;
            const s32 abs_dy = (dy < 0) ? -dy : dy;
            const s32 abs_dx = (dx < 0) ? -dx : dx;
            const s32 max_d  = (abs_dx > abs_dy) ? abs_dx : abs_dy;

            if (!drag_passed_deadband_ && max_d > DRAG_DEADBAND_PX) {
                drag_passed_deadband_ = true;
            }
            if (drag_passed_deadband_) {
                // Natural-scroll mapping: dragging down (positive dy) reveals
                // content above — scroll offset decreases (content moves
                // down on screen).  Standard mobile flick direction.
                s32 new_offset = drag_start_offset_y_ - dy;
                const s32 max_off = MaxScrollOffsetY();
                if (new_offset < 0)        new_offset = 0;
                if (new_offset > max_off)  new_offset = max_off;
                drag_view_offset_y_ = new_offset;
            }
        }

        // Falling edge: finger lifted.  If the gesture never crossed the
        // deadband, treat as a tap and dispatch the entry-tap hit-test at
        // the touch-down position (NOT the held-frame position — the
        // hardware can return (-1,-1) on the release frame).
        if (!touch_active_now && drag_was_touch_active_ && drag_in_progress_) {
            const bool was_drag = drag_passed_deadband_;
            const s32  start_x  = drag_start_touch_x_;
            const s32  start_y  = drag_start_touch_y_;
            drag_in_progress_     = false;
            drag_passed_deadband_ = false;

            if (!was_drag && entry_count_ > 0) {
                for (size_t i = 0; i < entry_count_; ++i) {
                    s32 cx = 0, cy = 0;
                    if (!EntryRect(i, cx, cy, 0, 0)) {
                        continue;
                    }
                    if (start_x >= cx && start_x < cx + VAULT_CELL_W &&
                        start_y >= cy && start_y < cy + VAULT_CELL_H) {
                        focus_idx_ = i;
                        UL_LOG_INFO("vault: touch tap on entry %zu '%s'",
                                    i, entries_[i].name);
                        drag_was_touch_active_ = touch_active_now;
                        EnterFocused();
                        return;
                    }
                }
            }
            // was_drag (or tap missed every cell) — gesture consumed; fall
            // through so the keyboard / D-pad handlers below still run for
            // any buttons that may have been pressed in the same frame.
        }

        drag_was_touch_active_ = touch_active_now;
    }

    if (entry_count_ == 0) {
        if (keys_down & HidNpadButton_B) {
            NavigateUp();
        }
        return;
    }

    const s32 cols = MainPaneCols(GetNaturalW());

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
    // Task B: ZL triggers the per-entry popup.
    // Only meaningful when the main pane is focused and an entry is selected.
    if ((keys_down & HidNpadButton_ZL) && !sidebar_focused_ && entry_count_ > 0) {
        vault_ctx_target_entry_ = focus_idx_;
        // BuildVisibleContextMenuOptions builds the items vector, assigns
        // per-opt indices, and calls vault_ctx_menu_.Open().
        BuildVisibleContextMenuOptions();
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
                // CRITICAL (W2-VAULT 2026-05-19): canonical launch order is
                //   1) smi::LaunchHomebrewLibraryApplet
                //   2) FadeOutToNonLibraryApplet
                //   3) Finalize
                // This matches qd_DesktopIcons.cpp:5877-5889 (cycle C1) and the
                // upstream MainMenuLayout::HandleHomebrewLaunch.  The previous
                // order (Fade + Finalize BEFORE Launch) tore down the
                // MenuApplication SMI dispatcher state before the launch
                // message was processed, so Horizon never recorded uMenu as
                // the library-applet parent.  Result: NRO launched, but on
                // exit Horizon returned to qlaunch (HOME) instead of uMenu —
                // user bug "Exiting almost every homebrew application doesn't
                // go back I have to press home."
                smi::LaunchHomebrewLibraryApplet(
                    std::string(e.full_path), std::string(""));
                // v3.6: animated splash covers the NRO-body read gap.
                // Order: Launch → splash → FadeOut → Finalize (canonical).
                qdesktop::RunLoadingSplash(2000);
                if (g_MenuApplication) {
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    g_MenuApplication->Finalize();
                }
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

// ── GetDebugState ─────────────────────────────────────────────────────────────

std::string QdVaultLayout::GetDebugState() const {
    if (sidebar_focused_) {
        return "vault:side";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "vault:%.40s:focus=%zu", cwd_, focus_idx_);
    return buf;
}

} // namespace ul::menu::qdesktop
