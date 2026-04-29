// qd_TaskManager.cpp — Full-screen task manager modal for uMenu v1.9.
// See qd_TaskManager.hpp for lifecycle, wire-in spec, and memory budget.

#include <ul/menu/qdesktop/qd_TaskManager.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// Logging shim: match the pattern used across qdesktop (UL_LOG_INFO).
// If the macro isn't in scope from the precompiled header, define a no-op.
#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop {

// ── Layout constants ─────────────────────────────────────────────────────────
static constexpr int kScreenW       = 1920;
static constexpr int kScreenH       = 1080;
static constexpr int kBgAlpha       = 220;   // background overlay alpha
static constexpr int kTitleY        = 20;    // y of title texture
static constexpr int kColHdrY       = 65;    // y of column header texture
static constexpr int kFooterY       = 1010;  // y of footer texture
static constexpr int kActionMenuW   = 320;   // action popup width
static constexpr int kActionItemH   = 56;    // action popup row height
// Colour palette
static constexpr pu::ui::Color kColWhite   = { 255, 255, 255, 255 };
static constexpr pu::ui::Color kColGrey    = { 160, 160, 160, 255 };
static constexpr pu::ui::Color kColGreen   = {  80, 200, 100, 255 };
static constexpr pu::ui::Color kColYellow  = { 230, 200,  60, 255 };
static constexpr pu::ui::Color kColRed     = { 220,  70,  60, 255 };
static constexpr pu::ui::Color kColDim     = { 120, 120, 120, 255 };

// ── Protected-title deny-list ────────────────────────────────────────────────
// Terminating any of these via pmshell would crash or destabilise the system.
// Title IDs confirmed from applet.h comments and Atmosphère source.
static constexpr u64 kProtectedTitles[] = {
    0x0100000000001000ULL,  // qlaunch / uMenu slot (SystemAppletMenu)
    0x0100000000001010ULL,  // loginShare / uSystem slot
    0x010000000000100CULL,  // overlayDisp
    0x010000000000100DULL,  // photoViewer / hbloader slot
    0x0100000000001012ULL,  // starter (SystemApplication)
};

static bool IsProtectedTitle(u64 program_id) {
    for (u64 t : kProtectedTitles) {
        if (t == program_id) return true;
    }
    return false;
}

// ── Hardcoded display-name table ─────────────────────────────────────────────
struct NameEntry { u64 program_id; const char *name; };
static constexpr NameEntry kHardcodedNames[] = {
    { 0x0100000000001000ULL, "uMenu (this)" },
    { 0x0100000000001010ULL, "uSystem" },
    { 0x010000000000100CULL, "overlayDisp" },
    { 0x010000000000100DULL, "hbloader" },
    { 0x0100000000001012ULL, "starter" },
    { 0x0100000000001001ULL, "auth" },
    { 0x0100000000001002ULL, "cabinet" },
    { 0x0100000000001003ULL, "controller" },
    { 0x0100000000001004ULL, "dataErase" },
    { 0x0100000000001005ULL, "error" },
    { 0x0100000000001006ULL, "netConnect" },
    { 0x0100000000001007ULL, "playerSelect" },
    { 0x0100000000001008ULL, "swkbd" },
    { 0x0100000000001009ULL, "miiEdit" },
    { 0x010000000000100AULL, "webApplet" },
    { 0x010000000000100BULL, "shopN" },
    { 0x010000000000100EULL, "set" },
    { 0x010000000000100FULL, "offlineWeb" },
    { 0x0100000000001013ULL, "myPage" },
};

// ── Helper implementations ────────────────────────────────────────────────────

void QdTaskManager::MakeText(SDL_Renderer *r,
                              pu::ui::DefaultFontSize font_size,
                              const char *text,
                              pu::ui::Color color,
                              SDL_Texture **out_tex,
                              int *out_w, int *out_h)
{
    *out_tex = nullptr;
    *out_w   = 0;
    *out_h   = 0;
    if (text == nullptr || text[0] == '\0') return;
    SDL_Texture *tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(font_size),
        std::string(text),
        color);
    if (tex == nullptr) return;
    int w = 0, h = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    *out_tex = tex;
    *out_w   = w;
    *out_h   = h;
}

void QdTaskManager::FreeTexture(SDL_Texture **tex) {
    if (!tex || !*tex) return;
    pu::ui::render::DeleteTexture(*tex);
    *tex = nullptr;
}

void QdTaskManager::Blit(SDL_Renderer *r, SDL_Texture *tex,
                          int x, int y, int w, int h)
{
    if (!tex) return;
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── Constructor / destructor ─────────────────────────────────────────────────

QdTaskManager::QdTaskManager() {
    // Obtain own PID once at construction; valid for the lifetime of the process.
    svcGetProcessId(&own_pid_, CUR_PROCESS_HANDLE);
}

QdTaskManager::~QdTaskManager() {
    Close();
}

// ── Open ─────────────────────────────────────────────────────────────────────

void QdTaskManager::Open(SDL_Renderer *r) {
    // Re-entrant: if already open, tear down textures and re-enumerate.
    FreeAllTiles();
    FreeTexture(&tex_title_);
    FreeTexture(&tex_col_hdr_);
    FreeTexture(&tex_footer_);
    FreeTexture(&tex_empty_);
    for (int i = 0; i < 3; i++) FreeTexture(&tex_act_[i]);

    // Open service sessions (idempotent: set flag only on success).
    if (!pmdmnt_open_) {
        if (R_SUCCEEDED(pmdmntInitialize())) {
            pmdmnt_open_ = true;
        } else {
            UL_LOG_INFO("qdesktop/taskmgr: pmdmntInitialize failed");
        }
    }
    if (!pminfo_open_) {
        if (R_SUCCEEDED(pminfoInitialize())) {
            pminfo_open_ = true;
        } else {
            UL_LOG_INFO("qdesktop/taskmgr: pminfoInitialize failed");
        }
    }
    if (!pmshell_open_) {
        if (R_SUCCEEDED(pmshellInitialize())) {
            pmshell_open_ = true;
        } else {
            UL_LOG_INFO("qdesktop/taskmgr: pmshellInitialize failed");
        }
    }
    if (!ns_open_) {
        if (R_SUCCEEDED(nsInitialize())) {
            ns_open_ = true;
        } else {
            UL_LOG_INFO("qdesktop/taskmgr: nsInitialize failed");
        }
    }

    // Enumerate processes.
    count_ = EnumerateProcesses();
    selected_     = 0;
    scroll_offset_ = 0;
    action_menu_  = false;
    detail_panel_ = false;
    action_item_  = 0;
    refresh_frame_counter_ = 0;

    // Pre-render static textures.
    MakeText(r, pu::ui::DefaultFontSize::Large,
             "Task Manager",
             kColWhite, &tex_title_, &title_w_, &title_h_);

    MakeText(r, pu::ui::DefaultFontSize::Small,
             "Process                        PID              Memory         State",
             kColGrey, &tex_col_hdr_, &col_hdr_w_, &col_hdr_h_);

    MakeText(r, pu::ui::DefaultFontSize::Small,
             "A = Actions    B = Close    X = Refresh",
             kColGrey, &tex_footer_, &footer_w_, &footer_h_);

    if (count_ == 0) {
        MakeText(r, pu::ui::DefaultFontSize::Medium,
                 "No process information available",
                 kColGrey, &tex_empty_, &empty_w_, &empty_h_);
    }

    // Pre-render action menu item labels.
    const char *act_labels[3] = { "Switch To", "Close", "Details" };
    for (int i = 0; i < 3; i++) {
        MakeText(r, pu::ui::DefaultFontSize::Medium,
                 act_labels[i], kColWhite,
                 &tex_act_[i], &act_w_[i], &act_h_[i]);
    }

    // Pre-render each row tile.
    for (int i = 0; i < count_; i++) {
        RenderEntryTile(r, entries_[i], /*selected=*/i == selected_);
    }

    open_ = true;
    UL_LOG_INFO("qdesktop/taskmgr: Open() — %d processes", count_);
}

// ── Close ────────────────────────────────────────────────────────────────────

void QdTaskManager::Close() {
    if (!open_ && !pmdmnt_open_ && !pminfo_open_ && !pmshell_open_ && !ns_open_) return;

    FreeAllTiles();
    FreeTexture(&tex_title_);
    FreeTexture(&tex_col_hdr_);
    FreeTexture(&tex_footer_);
    FreeTexture(&tex_empty_);
    for (int i = 0; i < 3; i++) FreeTexture(&tex_act_[i]);

    if (pmdmnt_open_)  { pmdmntExit();  pmdmnt_open_  = false; }
    if (pminfo_open_)  { pminfoExit();  pminfo_open_  = false; }
    if (pmshell_open_) { pmshellExit(); pmshell_open_ = false; }
    if (ns_open_)      { nsExit();      ns_open_      = false; }

    count_  = 0;
    open_   = false;
    action_menu_  = false;
    detail_panel_ = false;
    refresh_frame_counter_ = 0;
}

// ── FreeAllTiles ─────────────────────────────────────────────────────────────

void QdTaskManager::FreeAllTiles() {
    for (int i = 0; i < kMaxEntries; i++) {
        FreeTexture(&entries_[i].tex_row);
        entries_[i].row_w = 0;
        entries_[i].row_h = 0;
    }
}

// ── ResolveName ─────────────────────────────────────────────────────────────

void QdTaskManager::ResolveName(u64 program_id, char out_name[64]) {
    // Check hardcoded table first to avoid slow NS calls for system titles.
    for (const auto &e : kHardcodedNames) {
        if (e.program_id == program_id) {
            strncpy(out_name, e.name, 63);
            out_name[63] = '\0';
            return;
        }
    }

    // Fall back to NS application control data.
    if (ns_open_ && program_id != 0) {
        NsApplicationControlData *ctrl = static_cast<NsApplicationControlData*>(
            malloc(sizeof(NsApplicationControlData)));
        if (ctrl) {
            u64 actual_size = 0;
            Result rc = nsGetApplicationControlData(
                NsApplicationControlSource_CacheOnly,
                program_id, ctrl,
                sizeof(NsApplicationControlData), &actual_size);
            if (R_FAILED(rc)) {
                // Try from storage if not in cache.
                rc = nsGetApplicationControlData(
                    NsApplicationControlSource_Storage,
                    program_id, ctrl,
                    sizeof(NsApplicationControlData), &actual_size);
            }
            if (R_SUCCEEDED(rc)) {
                NacpLanguageEntry *lang = nullptr;
                nacpGetLanguageEntry(&ctrl->nacp, &lang);
                if (lang && lang->name[0] != '\0') {
                    strncpy(out_name, lang->name, 63);
                    out_name[63] = '\0';
                    free(ctrl);
                    return;
                }
            }
            free(ctrl);
        }
    }

    // Final fallback: render the program ID in hex.
    snprintf(out_name, 64, "0x%016llX", (unsigned long long)program_id);
}

// ── EnumerateProcesses ───────────────────────────────────────────────────────

int QdTaskManager::EnumerateProcesses() {
    // Zero-out all entries first.
    for (int i = 0; i < kMaxEntries; i++) {
        entries_[i] = TmEntry{};
    }

    if (!pmdmnt_open_) {
        UL_LOG_INFO("qdesktop/taskmgr: pmdmnt not open, skipping enumeration");
        return 0;
    }

    u64 pids[kMaxEntries] = {};
    u32 ct = 0;
    Result rc = pmdmntGetJitDebugProcessIdList(&ct, pids, kMaxEntries);
    if (R_FAILED(rc)) {
        UL_LOG_INFO("qdesktop/taskmgr: pmdmntGetJitDebugProcessIdList failed 0x%X", rc);
        return 0;
    }
    if (ct > (u32)kMaxEntries) ct = (u32)kMaxEntries;

    // Get the foreground application PID for tagging.
    u64 app_pid = 0;
    pmdmntGetApplicationProcessId(&app_pid);  // Result ignored; app_pid stays 0 on fail.

    int valid = 0;
    for (u32 i = 0; i < ct; i++) {
        TmEntry &e = entries_[valid];
        e.pid = pids[i];

        // Resolve program ID.
        u64 prog_id = 0;
        rc = pmdmntGetProgramId(&prog_id, e.pid);
        if (R_SUCCEEDED(rc)) {
            e.program_id = prog_id;
        }

        e.is_self = (e.pid == own_pid_);
        e.is_app  = (e.pid == app_pid && app_pid != 0);
        e.protected_title = IsProtectedTitle(e.program_id);

        // Memory: use CUR_PROCESS_HANDLE for self (OQ-1 footgun avoidance).
        if (e.is_self) {
            svcGetInfo(&e.mem_used,  InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
            svcGetInfo(&e.mem_total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        } else {
            // svcOpenProcess is not available via libnx on retail firmware.
            // mem_used / mem_total stay 0 (rendered as "N/A"), proc_state stays -1.
        }

        ResolveName(e.program_id, e.name);
        valid++;
    }

    UL_LOG_INFO("qdesktop/taskmgr: enumerated %d processes", valid);
    return valid;
}

// ── RenderEntryTile ──────────────────────────────────────────────────────────
// Builds a single row texture: name at left, memory in centre, state badge at right.
// This is the only place that calls RenderText — and it only runs on Open() and refresh.

void QdTaskManager::RenderEntryTile(SDL_Renderer *r, TmEntry &entry, bool selected) {
    FreeTexture(&entry.tex_row);
    entry.row_w = 0;
    entry.row_h = 0;

    // Determine label colour from process state.
    pu::ui::Color name_color;
    if (entry.proc_state == ProcessState_Crashed || entry.proc_state == ProcessState_Exiting) {
        name_color = kColRed;
    } else if (entry.proc_state == ProcessState_DebugSuspended) {
        name_color = kColYellow;
    } else if (entry.proc_state < 0) {
        name_color = kColDim;  // unavailable
    } else {
        name_color = kColWhite;
    }

    // Build name string (annotate self / foreground app).
    char name_buf[96] = {};
    if (entry.is_self) {
        snprintf(name_buf, sizeof(name_buf), "%s [this]", entry.name);
    } else if (entry.is_app) {
        snprintf(name_buf, sizeof(name_buf), "%s [fg]", entry.name);
    } else {
        strncpy(name_buf, entry.name, sizeof(name_buf) - 1);
    }

    // Memory string.
    char mem_buf[64] = {};
    if (entry.mem_used == 0 && entry.mem_total == 0) {
        snprintf(mem_buf, sizeof(mem_buf), "N/A");
    } else {
        u64 used_mb  = entry.mem_used  / (1024 * 1024);
        u64 total_mb = entry.mem_total / (1024 * 1024);
        snprintf(mem_buf, sizeof(mem_buf), "%llu MB / %llu MB",
                 (unsigned long long)used_mb, (unsigned long long)total_mb);
    }

    // State badge string.
    const char *state_str = "?";
    pu::ui::Color state_color = kColGrey;
    switch (entry.proc_state) {
        case ProcessState_Running:        state_str = "Run";  state_color = kColGreen;  break;
        case ProcessState_RunningAttached: state_str = "Dbg"; state_color = kColYellow; break;
        case ProcessState_DebugSuspended: state_str = "Sus";  state_color = kColYellow; break;
        case ProcessState_Crashed:        state_str = "Crash";state_color = kColRed;    break;
        case ProcessState_Exiting:        state_str = "Exit"; state_color = kColRed;    break;
        case ProcessState_Exited:         state_str = "Done"; state_color = kColDim;    break;
        default:                          state_str = "N/A";  state_color = kColDim;    break;
    }

    // PID string.
    char pid_buf[32] = {};
    snprintf(pid_buf, sizeof(pid_buf), "PID %llu", (unsigned long long)entry.pid);

    // Render each component to a temporary texture, then composite onto an
    // off-screen surface and upload — one final SDL_Texture per row.
    // Approach: render to a temporary SDL_Texture as render target.
    const int tile_w = kScreenW - kPadX * 2;
    const int tile_h = kRowH;

    SDL_Texture *target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET,
                                            tile_w, tile_h);
    if (!target) return;
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);

    SDL_Texture *prev_target = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, target);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // Background (transparent, or slightly tinted for selected).
    if (selected) {
        SDL_SetRenderDrawColor(r, 60, 120, 200, 80);
    } else {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    }
    SDL_Rect fill = { 0, 0, tile_w, tile_h };
    SDL_RenderFillRect(r, &fill);

    // Name column (left-aligned at x=4, vertically centred).
    SDL_Texture *t_name = nullptr; int nw = 0, nh = 0;
    MakeText(r, pu::ui::DefaultFontSize::Medium, name_buf, name_color, &t_name, &nw, &nh);
    if (t_name) {
        Blit(r, t_name, 4, (tile_h - nh) / 2, nw, nh);
        FreeTexture(&t_name);
    }

    // Memory column (centre-right).
    SDL_Texture *t_mem = nullptr; int mw = 0, mh = 0;
    MakeText(r, pu::ui::DefaultFontSize::Small, mem_buf, kColGrey, &t_mem, &mw, &mh);
    if (t_mem) {
        Blit(r, t_mem, tile_w / 2, (tile_h - mh) / 2, mw, mh);
        FreeTexture(&t_mem);
    }

    // State badge (right-aligned).
    SDL_Texture *t_state = nullptr; int sw = 0, sh = 0;
    MakeText(r, pu::ui::DefaultFontSize::Small, state_str, state_color, &t_state, &sw, &sh);
    if (t_state) {
        Blit(r, t_state, tile_w - sw - 100, (tile_h - sh) / 2 - 8, sw, sh);
        FreeTexture(&t_state);
    }

    // PID (below state badge).
    SDL_Texture *t_pid = nullptr; int pw = 0, ph = 0;
    MakeText(r, pu::ui::DefaultFontSize::Small, pid_buf, kColDim, &t_pid, &pw, &ph);
    if (t_pid) {
        Blit(r, t_pid, tile_w - pw - 100, (tile_h - ph) / 2 + 8, pw, ph);
        FreeTexture(&t_pid);
    }

    // Horizontal separator at bottom of tile.
    SDL_SetRenderDrawColor(r, 60, 60, 60, 200);
    SDL_RenderDrawLine(r, 0, tile_h - 1, tile_w - 1, tile_h - 1);

    SDL_SetRenderTarget(r, prev_target);

    entry.tex_row = target;
    entry.row_w   = tile_w;
    entry.row_h   = tile_h;
}

// ── Render ───────────────────────────────────────────────────────────────────

void QdTaskManager::Render(SDL_Renderer *r) {
    if (!open_) return;

    // Auto-refresh.
    refresh_frame_counter_++;
    if (refresh_frame_counter_ >= kAutoRefreshFrames) {
        refresh_frame_counter_ = 0;
        FreeAllTiles();
        count_ = EnumerateProcesses();
        for (int i = 0; i < count_; i++) {
            RenderEntryTile(r, entries_[i], i == selected_);
        }
        // Clamp selected_ in case process list shrank.
        if (count_ == 0) { selected_ = 0; scroll_offset_ = 0; }
        else if (selected_ >= count_) { selected_ = count_ - 1; }
    }

    // ── Background dim overlay ────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 10, 10, 20, kBgAlpha);
    SDL_Rect bg = { 0, 0, kScreenW, kScreenH };
    SDL_RenderFillRect(r, &bg);

    // ── Title ─────────────────────────────────────────────────────────────────
    if (tex_title_) {
        int tx = (kScreenW - title_w_) / 2;
        Blit(r, tex_title_, tx, kTitleY, title_w_, title_h_);
    }

    // ── Column header ─────────────────────────────────────────────────────────
    if (tex_col_hdr_) {
        Blit(r, tex_col_hdr_, kPadX, kColHdrY, col_hdr_w_, col_hdr_h_);
    }

    // ── Row tiles ─────────────────────────────────────────────────────────────
    const int visible_rows = (kScreenH - kPadY - (kScreenH - kFooterY)) / kRowH;

    if (count_ == 0 && tex_empty_) {
        int ex = (kScreenW - empty_w_) / 2;
        int ey = kPadY + 80;
        Blit(r, tex_empty_, ex, ey, empty_w_, empty_h_);
    } else {
        int max_scroll = std::max(0, count_ - visible_rows);
        scroll_offset_ = std::min(scroll_offset_, max_scroll);

        for (int i = scroll_offset_; i < count_ && (i - scroll_offset_) < visible_rows; i++) {
            int row_y = kPadY + (i - scroll_offset_) * kRowH;
            TmEntry &e = entries_[i];

            // Selected-row highlight rect.
            if (i == selected_) {
                SDL_SetRenderDrawColor(r, 60, 120, 200, 80);
                SDL_Rect sel_rect = { kPadX, row_y, kScreenW - kPadX * 2, kRowH };
                SDL_RenderFillRect(r, &sel_rect);
            }

            if (e.tex_row) {
                Blit(r, e.tex_row, kPadX, row_y, e.row_w, e.row_h);
            }
        }

        // Scrollbar (thin right-edge rect if content overflows).
        if (count_ > visible_rows) {
            const int bar_h = (kScreenH - kPadY - 70) * visible_rows / count_;
            const int bar_y = kPadY + (kScreenH - kPadY - 70 - bar_h)
                              * scroll_offset_ / std::max(1, count_ - visible_rows);
            SDL_SetRenderDrawColor(r, 100, 100, 140, 200);
            SDL_Rect bar = { kScreenW - 10, bar_y, 6, bar_h };
            SDL_RenderFillRect(r, &bar);
        }
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    if (tex_footer_) {
        int fx = (kScreenW - footer_w_) / 2;
        Blit(r, tex_footer_, fx, kFooterY, footer_w_, footer_h_);
    }

    // ── Action menu popup (rendered last so it's on top) ──────────────────────
    if (action_menu_) {
        RenderActionMenu(r);
    }
}

// ── RenderActionMenu ─────────────────────────────────────────────────────────

void QdTaskManager::RenderActionMenu(SDL_Renderer *r) {
    if (selected_ >= count_) return;
    const TmEntry &e = entries_[selected_];

    // Position the popup to the right of the selected row, clamped to screen.
    int row_y = kPadY + (selected_ - scroll_offset_) * kRowH;
    int popup_x = kPadX + (kScreenW - kPadX * 2) / 2;
    int popup_y = row_y;
    const int popup_h = kActionItemH * 3 + 8;
    if (popup_y + popup_h > kScreenH - 60) popup_y = kScreenH - 60 - popup_h;

    // Background rect.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 20, 20, 40, 240);
    SDL_Rect popup_bg = { popup_x, popup_y, kActionMenuW, popup_h };
    SDL_RenderFillRect(r, &popup_bg);

    // Border.
    SDL_SetRenderDrawColor(r, 80, 80, 120, 255);
    SDL_RenderDrawRect(r, &popup_bg);

    // Items.
    for (int i = 0; i < 3; i++) {
        int iy = popup_y + 4 + i * kActionItemH;

        // Highlight for selected item.
        if (i == action_item_) {
            SDL_SetRenderDrawColor(r, 60, 60, 100, 200);
            SDL_Rect hi = { popup_x + 2, iy, kActionMenuW - 4, kActionItemH };
            SDL_RenderFillRect(r, &hi);
        }

        // Grey out "Close" for protected titles.
        bool greyed_out = (i == 1 && e.protected_title);

        if (tex_act_[i]) {
            if (greyed_out) {
                SDL_SetTextureColorMod(tex_act_[i], 80, 80, 80);
            } else {
                SDL_SetTextureColorMod(tex_act_[i], 255, 255, 255);
            }
            int lx = popup_x + 12;
            int ly = iy + (kActionItemH - act_h_[i]) / 2;
            Blit(r, tex_act_[i], lx, ly, act_w_[i], act_h_[i]);
        }
    }
}

// ── HandleInput ──────────────────────────────────────────────────────────────

bool QdTaskManager::HandleInput(u64 keys_down, u64 keys_held) {
    if (!open_) return false;

    (void)keys_held;  // reserved for future hold-to-scroll

    if (action_menu_) {
        if (keys_down & HidNpadButton_A) {
            ExecuteAction();
            return true;
        }
        if (keys_down & HidNpadButton_B) {
            action_menu_ = false;
            return true;
        }
        if (keys_down & HidNpadButton_Up) {
            action_item_ = (action_item_ + 2) % 3;  // wrap up
            return true;
        }
        if (keys_down & HidNpadButton_Down) {
            action_item_ = (action_item_ + 1) % 3;  // wrap down
            return true;
        }
        // Any other button collapses the action menu.
        if (keys_down) { action_menu_ = false; return true; }
        return true;
    }

    // Normal navigation.
    if (keys_down & HidNpadButton_Up) {
        if (selected_ > 0) {
            selected_--;
            if (selected_ < scroll_offset_) scroll_offset_ = selected_;
        }
        return true;
    }
    if (keys_down & HidNpadButton_Down) {
        if (selected_ < count_ - 1) {
            selected_++;
            const int visible_rows = (kScreenH - kPadY - (kScreenH - kFooterY)) / kRowH;
            if (selected_ >= scroll_offset_ + visible_rows) {
                scroll_offset_ = selected_ - visible_rows + 1;
            }
        }
        return true;
    }
    if (keys_down & HidNpadButton_A) {
        if (count_ > 0) {
            action_menu_ = true;
            action_item_ = 0;
        }
        return true;
    }
    if (keys_down & HidNpadButton_B) {
        Close();
        return true;
    }
    if (keys_down & HidNpadButton_X) {
        // Manual refresh.
        FreeAllTiles();
        count_ = EnumerateProcesses();
        if (count_ == 0) { selected_ = 0; scroll_offset_ = 0; }
        else if (selected_ >= count_) { selected_ = count_ - 1; }
        // Need the SDL_Renderer to re-render tiles; it is not passed to HandleInput.
        // The tile re-render must be done on the next Render() call.
        // Mark tiles as needing re-render by zeroing the renderer pointer side.
        // The Render() auto-refresh path will re-render on next frame since
        // all tiles are now nullptr (FreeAllTiles zeroed them).
        refresh_frame_counter_ = 0;
        return true;
    }
    // Any other button (Plus, Minus, etc.) also closes.
    if (keys_down) {
        Close();
        return true;
    }
    return true;  // consume all input while open
}

// ── ExecuteAction ─────────────────────────────────────────────────────────────

void QdTaskManager::ExecuteAction() {
    action_menu_ = false;

    if (selected_ >= count_) return;
    TmEntry &e = entries_[selected_];

    switch (action_item_) {
        case 0: {
            // Switch To: request foreground (target must cooperate per OQ-2).
            (void)appletRequestToGetForeground();
            UL_LOG_INFO("qdesktop/taskmgr: Switch To PID %llu",
                        (unsigned long long)e.pid);
            break;
        }
        case 1: {
            // Close: grey out for protected titles; terminate otherwise.
            if (e.protected_title) {
                UL_LOG_INFO("qdesktop/taskmgr: Close blocked — protected title 0x%016llX",
                            (unsigned long long)e.program_id);
                break;
            }
            if (!pmshell_open_) {
                UL_LOG_INFO("qdesktop/taskmgr: pmshell not open, cannot terminate");
                break;
            }
            (void)pmshellTerminateProcess(e.pid);
            UL_LOG_INFO("qdesktop/taskmgr: Terminate PID %llu",
                        (unsigned long long)e.pid);
            // Re-enumerate after termination so the row disappears.
            FreeAllTiles();
            count_ = EnumerateProcesses();
            if (count_ == 0) { selected_ = 0; scroll_offset_ = 0; }
            else if (selected_ >= count_) { selected_ = count_ - 1; }
            refresh_frame_counter_ = 0;
            break;
        }
        case 2: {
            // Details: log a one-liner for now; full sub-panel is v1.10 scope.
            UL_LOG_INFO("qdesktop/taskmgr: Details — PID=%llu prog=0x%016llX "
                        "mem=%llu/%llu state=%d",
                        (unsigned long long)e.pid,
                        (unsigned long long)e.program_id,
                        (unsigned long long)e.mem_used,
                        (unsigned long long)e.mem_total,
                        e.proc_state);
            detail_panel_ = true;
            break;
        }
    }
}

}  // namespace ul::menu::qdesktop
