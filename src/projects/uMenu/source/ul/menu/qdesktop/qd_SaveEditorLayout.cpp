// qd_SaveEditorLayout.cpp — Pokemon save editor skeleton (PR-skeleton).
//
// This translation unit intentionally contains NO save-data I/O, NO
// SwishCrypto calls, and NO libnx fs calls — all of that comes in the
// follow-up PR after this integration model is reviewed.
//
// What lives here:
//   - QdSaveEditorLayout: SDL_Renderer drawing for TitlePicker + placeholder panels.
//   - QdSaveEditorHostLayout: thin IMenuLayout wrapper (required to survive
//     the MenuApplication::GetLayout<IMenuLayout> static_pointer_cast).
//
// Style: 4-space indent, namespace ul::menu::qdesktop, // doc comments.

#include <ul/menu/qdesktop/qd_SaveEditorLayout.hpp>
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>  // PANEL_HEADER_H, PANEL_HINT_BAR_H
#include <ul/menu/qdesktop/qd_SaveBackup.hpp>      // v3.6 re-enabled: static buffer
#include <ul/menu/qdesktop/qd_BDSPSaveParser.hpp>  // v3.6: BDSP party viewer (best-effort)
#include <minizip/unzip.h>                          // v3.6: read JKSV .zip backups
#include <cstdlib>                                  // malloc/free for zip extraction
#include <ul/menu/qdesktop/qd_SpeciesNames.hpp>     // v3.6: full National Dex names
#include <ul/menu/qdesktop/qd_MoveNames.hpp>        // v3.6.3: move-ID -> name
#include <ul/menu/qdesktop/qd_AbilityNames.hpp>     // v3.6.3: ability-ID -> name
#include <ul/menu/qdesktop/qd_ItemNames.hpp>        // v3.6.3: item-ID -> name
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_Common.hpp>                 // g_GlobalSettings
#include <ul/util/util_Telemetry.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <cstring>
#include <cstdio>
#include <dirent.h>     // W13-SAVE-PARSER: descend JKSV title dir → newest backup
#include <sys/stat.h>   // stat() to confirm the 'main' save file exists

extern ul::menu::ui::GlobalSettings    g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

// W13-SAVE-PARSER: species name lookup (minimal fallback for unnamed Pokémon).
// We ship a small inline table for species 1-151 to cover common cases;
// higher-index species display as "Species #NNN" until a full table is added.
namespace {
// First 151 species names (index 1=Bulbasaur … 151=Mew).
static const char *kSpeciesNames151[] = {
    nullptr,         // 0 = invalid
    "Bulbasaur",  "Ivysaur",    "Venusaur",   "Charmander", "Charmeleon",
    "Charizard",  "Squirtle",   "Wartortle",  "Blastoise",  "Caterpie",
    "Metapod",    "Butterfree", "Weedle",     "Kakuna",     "Beedrill",
    "Pidgey",     "Pidgeotto",  "Pidgeot",    "Rattata",    "Raticate",
    "Spearow",    "Fearow",     "Ekans",      "Arbok",      "Pikachu",
    "Raichu",     "Sandshrew",  "Sandslash",  "Nidoran-F",  "Nidorina",
    "Nidoqueen",  "Nidoran-M",  "Nidorino",   "Nidoking",   "Clefairy",
    "Clefable",   "Vulpix",     "Ninetales",  "Jigglypuff", "Wigglytuff",
    "Zubat",      "Golbat",     "Oddish",     "Gloom",      "Vileplume",
    "Paras",      "Parasect",   "Venonat",    "Venomoth",   "Diglett",
    "Dugtrio",    "Meowth",     "Persian",    "Psyduck",    "Golduck",
    "Mankey",     "Primeape",   "Growlithe",  "Arcanine",   "Poliwag",
    "Poliwhirl",  "Poliwrath",  "Abra",       "Kadabra",    "Alakazam",
    "Machop",     "Machoke",    "Machamp",    "Bellsprout", "Weepinbell",
    "Victreebel", "Tentacool",  "Tentacruel", "Geodude",    "Graveler",
    "Golem",      "Ponyta",     "Rapidash",   "Slowpoke",   "Slowbro",
    "Magnemite",  "Magneton",   "Farfetch'd", "Doduo",      "Dodrio",
    "Seel",       "Dewgong",    "Grimer",     "Muk",        "Shellder",
    "Cloyster",   "Gastly",     "Haunter",    "Gengar",     "Onix",
    "Drowzee",    "Hypno",      "Krabby",     "Kingler",    "Voltorb",
    "Electrode",  "Exeggcute",  "Exeggutor",  "Cubone",     "Marowak",
    "Hitmonlee",  "Hitmonchan", "Lickitung",  "Koffing",    "Weezing",
    "Rhyhorn",    "Rhydon",     "Chansey",    "Tangela",    "Kangaskhan",
    "Horsea",     "Seadra",     "Goldeen",    "Seaking",    "Staryu",
    "Starmie",    "Mr. Mime",   "Scyther",    "Jynx",       "Electabuzz",
    "Magmar",     "Pinsir",     "Tauros",     "Magikarp",   "Gyarados",
    "Lapras",     "Ditto",      "Eevee",      "Vaporeon",   "Jolteon",
    "Flareon",    "Porygon",    "Omanyte",    "Omastar",    "Kabuto",
    "Kabutops",   "Aerodactyl", "Snorlax",    "Articuno",   "Zapdos",
    "Moltres",    "Dratini",    "Dragonair",  "Dragonite",  "Mewtwo",
    "Mew",
};
static constexpr int kSpeciesNames151Count =
    static_cast<int>(sizeof(kSpeciesNames151) / sizeof(kSpeciesNames151[0]));

static const char* species_name(const uint16_t species) {
    if (species > 0 && species < kSpeciesNames151Count) {
        return kSpeciesNames151[species];
    }
    return nullptr;
}

// True only if s is non-empty and every byte is printable 7-bit ASCII.
// Used to reject CJK / garbage nicknames the on-device font can't render
// (they show as squares) so the display falls back to the English species
// name instead.
static bool is_clean_ascii(const char *s) {
    if (s == nullptr || s[0] == '\0') return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
         *p != '\0'; ++p) {
        if (*p < 0x20 || *p > 0x7E) return false;
    }
    return true;
}

// 25 Pokémon natures (index 0-24), plain ASCII.
static const char* nature_name(const uint8_t n) {
    static const char* const kNatures[25] = {
        "Hardy","Lonely","Brave","Adamant","Naughty",
        "Bold","Docile","Relaxed","Impish","Lax",
        "Timid","Hasty","Serious","Jolly","Naive",
        "Modest","Mild","Quiet","Bashful","Rash",
        "Calm","Gentle","Sassy","Careful","Quirky",
    };
    return (n < 25) ? kNatures[n] : "?";
}

static const char* gender_str(const uint8_t g) {
    switch (g) {
        case 0:  return "Male";
        case 1:  return "Female";
        default: return "-";
    }
}

static const char* swish_result_str(const ul::menu::qdesktop::SwishResult r) {
    using ul::menu::qdesktop::SwishResult;
    switch (r) {
        case SwishResult::Ok:            return "Ok";
        case SwishResult::InvalidMagic:  return "InvalidMagic";
        case SwishResult::BufferTooSmall: return "BufferTooSmall";
        case SwishResult::HashMismatch:  return "HashMismatch";
        default: return "Unknown";
    }
}

// W13-SAVE-PARSER fix helper: scan the immediate sub-folders of `dir` and
// return the path of the newest one that actually contains a readable loose
// "main" save.  The folder timestamp is fixed-width UTC (YYYYMMDD-HHMMSS),
// so string order == chronological order; an empty/partial newer folder is
// skipped so it can never hide a valid older backup.  Returns "" if none.
static std::string newest_main_in(const std::string &dir) {
    DIR *d = ::opendir(dir.c_str());
    if (d == nullptr) {
        return std::string();
    }
    std::string newest_folder;   // greatest stamp among folders holding a main.
    struct dirent *de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') continue;          // skip . .. dotfiles
        if (de->d_type != DT_DIR) continue;          // backups are folders
        // Skip folders that aren't a newer candidate than the best so far.
        if (!newest_folder.empty() &&
                std::strcmp(de->d_name, newest_folder.c_str()) <= 0) {
            continue;
        }
        std::string candidate = dir + de->d_name + "/main";
        struct stat st;
        if (::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            newest_folder = de->d_name;
        }
    }
    ::closedir(d);

    if (newest_folder.empty()) return std::string();
    return dir + newest_folder + "/main";
}

// W13-SAVE-PARSER fix: the autoscan stores the JKSV *title* directory
// (sdmc:/JKSV/<Game> [<tid>]/), NOT a path that contains the loose "main"
// save.  The actual save lives one timestamp level deeper:
//   <title dir>/<YYYYMMDD-HHMMSS>[_u<uid>]/main   (this app + JKSV default)
// so the previous "<title dir>/main" never existed and every view silently
// failed.  First look for that layout; if a backup tool nested an extra
// user-id level (<title>/<uid>/<timestamp>/main), descend once more into the
// newest sub-folder and retry.  Returns "" when no readable backup is found.
static std::string newest_backup_main(const std::string &title_dir_in) {
    std::string title_dir = title_dir_in;
    if (title_dir.empty()) return std::string();
    if (title_dir.back() != '/') title_dir += '/';

    // Canonical layout: timestamp folder directly under the title dir.
    std::string direct = newest_main_in(title_dir);
    if (!direct.empty()) {
        return direct;
    }

    // Fallback: a user-id level sits between the title and the timestamp.
    // Descend into the newest immediate sub-folder and look for a 'main' there.
    DIR *d = ::opendir(title_dir.c_str());
    if (d == nullptr) {
        return std::string();
    }
    std::string newest_sub;
    struct dirent *de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        if (de->d_type != DT_DIR) continue;
        if (newest_sub.empty() ||
                std::strcmp(de->d_name, newest_sub.c_str()) > 0) {
            newest_sub = de->d_name;
        }
    }
    ::closedir(d);
    if (newest_sub.empty()) return std::string();

    return newest_main_in(title_dir + newest_sub + "/");
}

// JKSV "ZIP" backup mode: each backup is a <title>/<YYYYMMDD-HHMMSS>[...].zip
// file (not a loose folder).  Find the newest .zip, extract its save file (the
// "main" entry, else the largest entry) into a temp file the parser can fopen,
// and return that temp path.  Returns "" if no usable zip backup is found.
// Mirrors the minizip usage already in qd_CheatsInstaller.
static std::string extract_newest_zip_save(const std::string &title_dir_in) {
    std::string title_dir = title_dir_in;
    if (title_dir.empty()) return std::string();
    if (title_dir.back() != '/') title_dir += '/';

    // Newest .zip by name (fixed-width UTC stamp => lexicographic == newest).
    DIR *d = ::opendir(title_dir.c_str());
    if (d == nullptr) return std::string();
    std::string newest_zip;
    struct dirent *de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        const size_t len = std::strlen(de->d_name);
        if (len < 5 || std::strcmp(de->d_name + len - 4, ".zip") != 0) continue;
        if (newest_zip.empty() ||
                std::strcmp(de->d_name, newest_zip.c_str()) > 0) {
            newest_zip = de->d_name;
        }
    }
    ::closedir(d);
    if (newest_zip.empty()) return std::string();

    const std::string zip_path = title_dir + newest_zip;
    unzFile zf = unzOpen(zip_path.c_str());
    if (zf == nullptr) {
        UL_LOG_WARN("save-editor: unzOpen('%s') failed", zip_path.c_str());
        return std::string();
    }

    // Pick the entry named "main" (the save file), else the largest entry.
    char  pick_name[512] = {0};
    uLong pick_size      = 0;
    int ret = unzGoToFirstFile(zf);
    while (ret == UNZ_OK) {
        unz_file_info fi;
        char name[512];
        if (unzGetCurrentFileInfo(zf, &fi, name, sizeof(name),
                                  nullptr, 0, nullptr, 0) == UNZ_OK) {
            const char *base = std::strrchr(name, '/');
            base = base ? base + 1 : name;
            if (std::strcmp(base, "main") == 0) {
                std::snprintf(pick_name, sizeof(pick_name), "%s", name);
                pick_size = fi.uncompressed_size;
                break;  // exact "main" wins immediately
            }
            if (fi.uncompressed_size > pick_size) {
                pick_size = fi.uncompressed_size;
                std::snprintf(pick_name, sizeof(pick_name), "%s", name);
            }
        }
        ret = unzGoToNextFile(zf);
    }

    // Sanity cap (Pokémon saves are < ~4 MB; 16 MB is a generous ceiling).
    if (pick_name[0] == '\0' || pick_size == 0 || pick_size > 16u * 1024 * 1024) {
        unzClose(zf);
        return std::string();
    }

    if (unzLocateFile(zf, pick_name, 0) != UNZ_OK ||
            unzOpenCurrentFile(zf) != UNZ_OK) {
        unzClose(zf);
        return std::string();
    }
    char *buf = static_cast<char *>(std::malloc(pick_size));
    if (buf == nullptr) {
        unzCloseCurrentFile(zf);
        unzClose(zf);
        return std::string();
    }
    const int n = unzReadCurrentFile(zf, buf, static_cast<unsigned>(pick_size));
    unzCloseCurrentFile(zf);
    unzClose(zf);
    if (n < 0 || static_cast<uLong>(n) != pick_size) {
        std::free(buf);
        return std::string();
    }

    // Write to a temp file the save parsers can fopen().
    ::mkdir("sdmc:/ulaunch/cache", 0777);
    ::mkdir("sdmc:/ulaunch/cache/save_tmp", 0777);
    static const char *kTmpPath = "sdmc:/ulaunch/cache/save_tmp/main";
    FILE *out = std::fopen(kTmpPath, "wb");
    if (out == nullptr) { std::free(buf); return std::string(); }
    const size_t w        = std::fwrite(buf, 1, pick_size, out);
    const bool   flush_ok = (std::fflush(out) == 0);
    const bool   close_ok = (std::fclose(out) == 0);
    std::free(buf);
    if (!flush_ok || !close_ok || w != pick_size) return std::string();

    UL_LOG_INFO("save-editor: extracted '%s' (%lu B) from %s -> %s",
                pick_name, static_cast<unsigned long>(pick_size),
                newest_zip.c_str(), kTmpPath);
    return std::string(kTmpPath);
}
} // anonymous namespace

// Forward-declared externals (defined in main.cpp).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Pixel layout constants (natural 1280×720 coordinate space) ───────────────

static constexpr s32 kMargin       = 40;                ///< Left/right margin.
static constexpr s32 kTopbarH      = PANEL_HEADER_H;   ///< Header bar height (36px, panel not OS).
static constexpr s32 kRowH         = 56;               ///< Height of each title-picker row.
static constexpr s32 kRowGap       = 8;                ///< Gap between rows.
static constexpr s32 kTabBarH      = 48;               ///< Height of the tab bar strip.
static constexpr s32 kTabW         = 180;              ///< Width of each tab button.
static constexpr s32 kHintBarH     = PANEL_HINT_BAR_H; ///< Bottom hint strip height.

// ── QdSaveEditorLayout static members ────────────────────────────────────────

constexpr const char *QdSaveEditorLayout::kGameNames[QdSaveEditorLayout::kGameCount];
constexpr const char *QdSaveEditorLayout::kTabLabels[QdSaveEditorLayout::kTabCount];

// ── Constructor / destructor ──────────────────────────────────────────────────

QdSaveEditorLayout::QdSaveEditorLayout(const QdTheme &theme)
    : theme_(theme)
{
    UL_LOG_INFO("save-editor: QdSaveEditorLayout ctor");
    // All SDL_Texture* arrays are zero-initialised by the in-class initialiser.
}

QdSaveEditorLayout::~QdSaveEditorLayout() {
    UL_LOG_INFO("save-editor: QdSaveEditorLayout dtor");
    FreeTextures();
}

// ── Texture lifecycle ─────────────────────────────────────────────────────────

// Populate a MonDetail from a decoded PK8 (SwSh) or PB8 (BDSP).  PK8 and PB8
// share field names, so one template serves both.  Only instantiated in this TU
// (call sites in OnInput / BoxModeInput below).
template <typename T>
QdSaveEditorLayout::MonDetail QdSaveEditorLayout::BuildDetail(const T &m) {
    MonDetail d;
    d.valid     = true;
    d.species   = m.species;
    d.level     = m.stat_level;
    d.nature    = m.nature;
    d.ability   = m.ability;
    d.gender    = m.gender;
    d.shiny     = m.is_shiny;
    d.held_item = m.held_item;
    d.pid       = m.pid;
    d.tid       = static_cast<uint16_t>(m.id32 & 0xFFFF);
    d.sid       = static_cast<uint16_t>(m.id32 >> 16);
    d.iv[0] = m.iv_hp;  d.iv[1] = m.iv_atk; d.iv[2] = m.iv_def;
    d.iv[3] = m.iv_spa; d.iv[4] = m.iv_spd; d.iv[5] = m.iv_spe;
    for (int i = 0; i < 4; ++i) d.moves[i] = m.moves[i];
    std::snprintf(d.name, sizeof(d.name), "%s", m.display_name);
    std::snprintf(d.ot,   sizeof(d.ot),   "%s", m.ot_display);
    return d;
}

void QdSaveEditorLayout::FreeTextures() {
    for (int i = 0; i < kGameCount; ++i) {
        if (title_textures_[i]) {
            pu::ui::render::DeleteTexture(title_textures_[i]);
            title_textures_[i] = nullptr;
        }
    }
    for (int i = 0; i < kTabCount; ++i) {
        if (tab_textures_[i]) {
            pu::ui::render::DeleteTexture(tab_textures_[i]);
            tab_textures_[i] = nullptr;
        }
    }
    if (placeholder_tex_) {
        pu::ui::render::DeleteTexture(placeholder_tex_);
        placeholder_tex_ = nullptr;
    }
    if (hint_tex_) {
        pu::ui::render::DeleteTexture(hint_tex_);
        hint_tex_ = nullptr;
    }
    // W12-SAVE-DISCO: release save-count suffix textures.
    for (int i = 0; i < kGameCount; ++i) {
        if (save_count_textures_[i]) {
            pu::ui::render::DeleteTexture(save_count_textures_[i]);
            save_count_textures_[i] = nullptr;
        }
    }
    if (rescan_tex_) {
        pu::ui::render::DeleteTexture(rescan_tex_);
        rescan_tex_ = nullptr;
    }
    // W12B-AUTOSCAN: release diagnostic line texture.
    if (diag_tex_) {
        pu::ui::render::DeleteTexture(diag_tex_);
        diag_tex_ = nullptr;
    }
    // W13-SAVE-PARSER: release party slot textures.
    FreePartyTextures();
    textures_built_ = false;
    scan_applied_   = false;
}

// ── W13-SAVE-PARSER: FreePartyTextures ───────────────────────────────────────

void QdSaveEditorLayout::FreePartyTextures() {
    for (int i = 0; i < 6; ++i) {
        if (party_name_tex_[i])  {
            pu::ui::render::DeleteTexture(party_name_tex_[i]);
            party_name_tex_[i] = nullptr;
        }
        if (party_level_tex_[i]) {
            pu::ui::render::DeleteTexture(party_level_tex_[i]);
            party_level_tex_[i] = nullptr;
        }
        if (party_item_tex_[i])  {
            pu::ui::render::DeleteTexture(party_item_tex_[i]);
            party_item_tex_[i] = nullptr;
        }
        if (party_shiny_tex_[i]) {
            pu::ui::render::DeleteTexture(party_shiny_tex_[i]);
            party_shiny_tex_[i] = nullptr;
        }
    }
    if (party_empty_tex_) {
        pu::ui::render::DeleteTexture(party_empty_tex_);
        party_empty_tex_ = nullptr;
    }
    if (parse_error_tex_) {
        pu::ui::render::DeleteTexture(parse_error_tex_);
        parse_error_tex_ = nullptr;
    }
    if (detail_defer_tex_) {
        pu::ui::render::DeleteTexture(detail_defer_tex_);
        detail_defer_tex_ = nullptr;
    }
}

// ── W13-SAVE-PARSER: BuildPartyTextures ──────────────────────────────────────

void QdSaveEditorLayout::BuildPartyTextures(SDL_Renderer * /*r*/) {
    const auto small_font  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const auto medium_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    FreePartyTextures();

    if (!save_loaded_) {
        // Build parse-error texture.
        char err_buf[64];
        snprintf(err_buf, sizeof(err_buf),
                 "Cannot read save: %s", swish_result_str(save_parse_result_));
        parse_error_tex_ = pu::ui::render::RenderText(
            medium_font, std::string(err_buf), theme_.button_close);
        return;
    }

    party_empty_tex_ = pu::ui::render::RenderText(
        small_font, std::string("— empty —"), theme_.text_secondary);

    // (detail_defer_tex_ retired in v3.6.3 — the detail view is real now; the
    // A-press opens RenderDetail instead of a "coming in vN" toast.)

    for (int i = 0; i < 6; ++i) {
        if (i >= current_save_.party_count) break;
        const PK8 &pk = current_save_.party[i];

        // Name: prefer a CLEAN ASCII nickname; otherwise the real species name.
        // Non-ASCII/CJK nicknames render as squares in our font, and
        // non-nicknamed mons leave display_name empty — both fall back to the
        // full National Dex name (1..1025), then the legacy Gen-1 table, then
        // "Species #NNN".
        const char *name_cstr =
            is_clean_ascii(pk.display_name) ? pk.display_name : nullptr;
        char species_buf[32];
        if (!name_cstr) {
            const char *sp = species_name_full(pk.species);
            if (!sp) sp = species_name(pk.species);
            if (sp) {
                name_cstr = sp;
            } else {
                snprintf(species_buf, sizeof(species_buf),
                         "Species #%u", static_cast<unsigned>(pk.species));
                name_cstr = species_buf;
            }
        }
        party_name_tex_[i] = pu::ui::render::RenderText(
            medium_font, std::string(name_cstr),
            pk.is_shiny ? theme_.accent : theme_.text_primary);

        // Level. Clamp to the valid 1..100 range — a value outside it means the
        // party-stat cache wasn't reliably decoded (e.g. BDSP's stat region),
        // so show "Lv.?" instead of an impossible number.
        char lv_buf[16];
        if (pk.stat_level >= 1 && pk.stat_level <= 100) {
            snprintf(lv_buf, sizeof(lv_buf), "Lv.%u",
                     static_cast<unsigned>(pk.stat_level));
        } else {
            snprintf(lv_buf, sizeof(lv_buf), "Lv.?");
        }
        party_level_tex_[i] = pu::ui::render::RenderText(
            small_font, std::string(lv_buf), theme_.text_secondary);

        // Held item — real name from the item table, falling back to the ID.
        char item_buf[40];
        if (pk.held_item > 0) {
            const char *iname = item_name(pk.held_item);
            if (iname) {
                snprintf(item_buf, sizeof(item_buf), "%s", iname);
            } else {
                snprintf(item_buf, sizeof(item_buf), "Item #%u",
                         static_cast<unsigned>(pk.held_item));
            }
        } else {
            snprintf(item_buf, sizeof(item_buf), "No item");
        }
        party_item_tex_[i] = pu::ui::render::RenderText(
            small_font, std::string(item_buf), theme_.text_secondary);

        // Shiny indicator.
        if (pk.is_shiny) {
            party_shiny_tex_[i] = pu::ui::render::RenderText(
                small_font, std::string("\xe2\x9c\xa8 Shiny"), theme_.accent);
        }
    }
}

// ── W13-SAVE-PARSER: TryLoadSave ─────────────────────────────────────────────

void QdSaveEditorLayout::TryLoadSave(SDL_Renderer *r) {
    save_loaded_        = false;
    save_parse_result_  = SwishResult::Ok;
    load_fail_reason_   = LoadFailReason::None;
    current_save_       = SwShSave();

    // Parsers available: Sword/Shield (index 1, full SwishCrypto parser) and
    // Brilliant Diamond / Shining Pearl (index 2, v3.6 best-effort BDSP/Sav8BS
    // decoder).  Every other Pokémon game has a JKSV backup but no decoder yet.
    if (title_focus_ != 1 && title_focus_ != 2) {
        UL_LOG_INFO("save-editor: TryLoadSave: game %d has no parser",
                    title_focus_);
        load_fail_reason_  = LoadFailReason::NoParser;
        save_parse_result_ = SwishResult::BufferTooSmall;
        BuildPartyTextures(r);
        return;
    }

    // Find the FOCUSED game's entry in the autoscan result.  NOTE: entry.save_dir
    // is the JKSV TITLE directory (sdmc:/JKSV/<Game> [<tid>]/), not a path that
    // contains the loose "main" — the actual save lives one timestamp level
    // deeper.  newest_backup_main() descends into the newest backup folder.
    const SaveScanResult &sr = autoscan_.GetResult();
    std::string title_dir;
    for (const auto &entry : sr.entries) {
        if (entry.game_index == title_focus_ && entry.save_count > 0) {
            title_dir = entry.save_dir;
            break;
        }
    }

    if (title_dir.empty()) {
        UL_LOG_INFO("save-editor: TryLoadSave: no SD backup directory for game %d",
                    title_focus_);
        load_fail_reason_  = LoadFailReason::NoSdBackup;
        save_parse_result_ = SwishResult::BufferTooSmall;
        BuildPartyTextures(r);
        return;
    }

    // Locate the save's "main": first the loose-folder layout
    // (<title>/<ts>/main), then JKSV ZIP-mode backups (<title>/<ts>.zip, which
    // we extract to a temp file).  Covers both JKSV backup configurations.
    std::string save_path = newest_backup_main(title_dir);
    if (save_path.empty()) {
        save_path = extract_newest_zip_save(title_dir);
    }
    if (save_path.empty()) {
        UL_LOG_WARN("save-editor: TryLoadSave: no readable save (loose or zip) "
                    "under '%s'", title_dir.c_str());
        load_fail_reason_  = LoadFailReason::FileMissing;
        save_parse_result_ = SwishResult::BufferTooSmall;
        BuildPartyTextures(r);
        return;
    }

    UL_LOG_INFO("save-editor: TryLoadSave: path='%s' (game %d)",
                save_path.c_str(), title_focus_);

    // Reset box-view state for the newly-loaded save.  Boxes are parsed lazily
    // (only when the user opens the box view) to keep load fast.  BDSP-only for
    // now — the box parser is host-verified against a real Sav8BS image.
    loaded_save_path_ = save_path;
    box_loaded_       = false;
    box_mode_         = false;
    box_expanded_     = -1;
    box_list_sel_     = 0;
    box_supported_    = (title_focus_ == 2);   // game 2 = Brilliant Diamond / Shining Pearl
    trainer_          = TrainerInfo{};         // repopulated below on a successful parse
    bag_items_.clear();
    bag_supported_    = false;
    bag_sel_          = 0;

    if (title_focus_ == 1) {
        // Sword/Shield — full SwishCrypto parser.
        SwShSave tmp;
        save_parse_result_ = QdSwShSaveParser::ParseFile(save_path, tmp);
        if (save_parse_result_ == SwishResult::Ok) {
            current_save_     = tmp;
            save_loaded_      = true;
            load_fail_reason_ = LoadFailReason::None;
            UL_LOG_INFO("save-editor: SwSh parse OK, party_count=%d", tmp.party_count);
        } else {
            load_fail_reason_ = LoadFailReason::ParseFailed;
            UL_LOG_INFO("save-editor: SwSh parse failed: %s",
                        swish_result_str(save_parse_result_));
        }
    } else {
        // Brilliant Diamond / Shining Pearl — v3.6 best-effort flat-save decoder.
        // PB8 mirrors PK8 field-for-field, so copy the displayed fields into the
        // PK8 party and the existing party-display path renders BDSP unchanged.
        BDSPSave bd;
        const BdspResult br = QdBDSPSaveParser::ParseFile(save_path, bd);
        if (br == BdspResult::Ok && bd.party_count > 0) {
            const int n = bd.party_count > 6 ? 6 : bd.party_count;
            current_save_.party_count = n;
            for (int i = 0; i < n; ++i) {
                const PB8 &s = bd.party[i];
                PK8       &d = current_save_.party[i];
                // PB8 mirrors PK8 field-for-field — copy the full record so the
                // detail view shows nature/ability/IVs/moves/OT, not just the
                // 5 list fields.
                d.species = s.species;   d.held_item = s.held_item; d.id32 = s.id32;
                d.exp     = s.exp;       d.ability   = s.ability;   d.pid  = s.pid;
                d.nature  = s.nature;    d.gender    = s.gender;    d.form = s.form;
                for (int m = 0; m < 4; ++m) d.moves[m] = s.moves[m];
                d.iv32   = s.iv32;
                d.iv_hp  = s.iv_hp;  d.iv_atk = s.iv_atk; d.iv_def = s.iv_def;
                d.iv_spe = s.iv_spe; d.iv_spa = s.iv_spa; d.iv_spd = s.iv_spd;
                d.stat_level   = s.stat_level;
                d.is_egg       = s.is_egg;
                d.is_nicknamed = s.is_nicknamed;
                d.is_shiny     = s.is_shiny;
                std::snprintf(d.display_name, sizeof(d.display_name), "%s", s.display_name);
                std::snprintf(d.ot_display,   sizeof(d.ot_display),   "%s", s.ot_display);
            }
            // Trainer card — host-verified MyStatus8b @ 0x79BB4.
            trainer_.valid  = true;
            std::snprintf(trainer_.ot, sizeof(trainer_.ot), "%s", bd.trainer_name);
            trainer_.tid    = bd.trainer_id;
            trainer_.sid    = bd.trainer_sid;
            trainer_.money  = bd.money;
            trainer_.gender = bd.gender;

            // Bag — host-verified MyItem8b @ 0x0563C.
            // M-2: check the return value; on failure leave bag_supported_ = false
            // so write-back cannot overwrite a valid save with an empty bag.
            {
                const BdspResult bag_rc =
                    QdBDSPSaveParser::ParseBagFromFile(save_path, bag_items_);
                if (bag_rc == BdspResult::Ok) {
                    bag_supported_ = true;
                    bag_sel_       = 0;
                } else {
                    bag_supported_ = false;
                    bag_items_.clear();
                    UL_LOG_WARN("save-editor: BDSP bag parse failed (rc=%d) — "
                                "bag view disabled to prevent write-back with empty data",
                                static_cast<int>(bag_rc));
                    if (g_MenuApplication) {
                        g_MenuApplication->ShowNotification(
                            "Bag data could not be read from this save file.", 4000);
                    }
                }
            }

            // DATA SNAPSHOT (creator-requested observability) — dump everything
            // parsed so log_uMenu.log shows the full picture of every load.
            UL_LOG_INFO("save-editor SNAPSHOT[BDSP] OT='%s' TID=%05u SID=%05u money=%u "
                        "gender=%s party=%d bag=%zu",
                        bd.trainer_name, static_cast<unsigned>(bd.trainer_id),
                        static_cast<unsigned>(bd.trainer_sid), static_cast<unsigned>(bd.money),
                        bd.gender == 0 ? "M" : "F", n, bag_items_.size());
            for (int i = 0; i < n; ++i) {
                const PB8 &s = bd.party[i];
                UL_LOG_INFO("  party[%d] sp=%u lv=%u shiny=%d held=%u nat=%u abil=%u "
                            "ivs=%u/%u/%u/%u/%u/%u",
                            i, static_cast<unsigned>(s.species), static_cast<unsigned>(s.stat_level),
                            static_cast<int>(s.is_shiny), static_cast<unsigned>(s.held_item),
                            static_cast<unsigned>(s.nature), static_cast<unsigned>(s.ability),
                            s.iv_hp, s.iv_atk, s.iv_def, s.iv_spa, s.iv_spd, s.iv_spe);
            }
            // Capped — each line is a synchronous SD write on the UI thread.
            {
                size_t shown = 0;
                for (const auto &it : bag_items_) {
                    if (shown++ >= 24) {
                        UL_LOG_INFO("  ... +%zu more bag items (log capped)",
                                    bag_items_.size() - 24);
                        break;
                    }
                    UL_LOG_INFO("  bag id=%u x%u", static_cast<unsigned>(it.id),
                                static_cast<unsigned>(it.count));
                }
            }

            save_loaded_      = true;
            load_fail_reason_ = LoadFailReason::None;
            UL_LOG_INFO("save-editor: BDSP parse OK, party=%d, OT='%s'", n, bd.trainer_name);
        } else {
            load_fail_reason_  = LoadFailReason::ParseFailed;
            save_parse_result_ = SwishResult::BufferTooSmall;
            UL_LOG_WARN("save-editor: BDSP parse failed/empty (br=%d count=%d)",
                        static_cast<int>(br), bd.party_count);
        }
    }

    BuildPartyTextures(r);
}

/// Build all SDL_Texture* objects from the theme palette.
/// Called lazily on first OnRender so the renderer is guaranteed live.
void QdSaveEditorLayout::BuildTextures(SDL_Renderer * /*r*/) {
    const auto small_font  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const auto medium_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    // Title picker row textures.
    for (int i = 0; i < kGameCount; ++i) {
        title_textures_[i] = pu::ui::render::RenderText(
            medium_font, std::string(kGameNames[i]), theme_.text_primary);
    }

    // Tab label textures.
    for (int i = 0; i < kTabCount; ++i) {
        tab_textures_[i] = pu::ui::render::RenderText(
            small_font, std::string(kTabLabels[i]), theme_.text_primary);
    }

    // "Coming soon" placeholder body text.
    placeholder_tex_ = pu::ui::render::RenderText(
        medium_font, std::string("Coming soon"), theme_.text_secondary);

    // Bottom hint bar.
    hint_tex_ = pu::ui::render::RenderText(
        small_font,
        std::string("A  Select    B  Back    L/R  Switch Tab"),
        theme_.text_secondary);

    // W12-SAVE-DISCO: "Rescanning…" status text built here.
    rescan_tex_ = pu::ui::render::RenderText(
        small_font,
        std::string("Scanning SD for saves\xe2\x80\xa6"),
        theme_.text_secondary);

    // (Removed: the old TitlePicker "no backups" toast texture.  It had no
    // reachable setter for toast_frames_, so it could never display — all
    // TitlePicker feedback now goes through ShowNotification in OnInput.)

    // Save-count textures start null; ApplyScanResult() fills them.

    textures_built_ = true;
    UL_LOG_INFO("save-editor: BuildTextures done");
}

// ── W12-SAVE-DISCO: ApplyScanResult ──────────────────────────────────────────

/// Pull save counts from autoscan into save_counts_[] and rebuild the per-game
/// suffix textures ("N saves found" / "(no saves)").  Called once from OnRender
/// after BuildTextures and after autoscan has run.

void QdSaveEditorLayout::ApplyScanResult(SDL_Renderer * /*r*/) {
    const SaveScanResult &sr = autoscan_.GetResult();
    if (!sr.scan_done) {
        return;
    }

    // Zero out save_counts_[] then populate from scan entries.
    for (int i = 0; i < kGameCount; ++i) {
        save_counts_[i] = 0;
    }
    for (const auto &entry : sr.entries) {
        if (entry.game_index >= 0 && entry.game_index < kGameCount) {
            // Sum across multiple directories for the same game.
            save_counts_[entry.game_index] += entry.save_count;
        }
    }

    // Rebuild per-game suffix textures.
    const auto small_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    for (int i = 0; i < kGameCount; ++i) {
        if (save_count_textures_[i]) {
            pu::ui::render::DeleteTexture(save_count_textures_[i]);
            save_count_textures_[i] = nullptr;
        }
        char buf[64];
        if (save_counts_[i] > 0) {
            snprintf(buf, sizeof(buf), "  (%d save%s found)",
                     save_counts_[i], save_counts_[i] == 1 ? "" : "s");
            save_count_textures_[i] = pu::ui::render::RenderText(
                small_font, std::string(buf), theme_.accent);
        } else {
            save_count_textures_[i] = pu::ui::render::RenderText(
                small_font, std::string("  (no saves)"), theme_.text_secondary);
        }
    }

    // W12B-AUTOSCAN: build diagnostic line texture from scan counters.
    {
        if (diag_tex_) {
            pu::ui::render::DeleteTexture(diag_tex_);
            diag_tex_ = nullptr;
        }
        char diag_buf[128];
        snprintf(diag_buf, sizeof(diag_buf),
                 "Scanned: %d paths   Skipped: %d (not found)",
                 sr.paths_probed, sr.paths_skipped);
        const auto small_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
        diag_tex_ = pu::ui::render::RenderText(
            small_font, std::string(diag_buf), theme_.text_secondary);
        UL_LOG_INFO("save-editor: diag line: %s", diag_buf);
    }

    scan_applied_ = true;
    UL_LOG_INFO("save-editor: scan result applied, %zu entries", sr.entries.size());
}

// ── RequestRescan ─────────────────────────────────────────────────────────────

void QdSaveEditorLayout::RequestRescan() {
    autoscan_.Rescan();
    scan_applied_ = false;
    // Free current suffix textures so they'll be rebuilt after rescan.
    for (int i = 0; i < kGameCount; ++i) {
        if (save_count_textures_[i]) {
            pu::ui::render::DeleteTexture(save_count_textures_[i]);
            save_count_textures_[i] = nullptr;
        }
        save_counts_[i] = -1;
    }
    // W12B-AUTOSCAN: release stale diagnostic texture; rebuilt in ApplyScanResult.
    if (diag_tex_) {
        pu::ui::render::DeleteTexture(diag_tex_);
        diag_tex_ = nullptr;
    }
    UL_LOG_INFO("save-editor: RequestRescan triggered");
}

// ── v3.6 absorb wave 1: JKSV-style backup hooks (re-enabled 2026-05-28) ───
//
// The qd_SaveBackup module now uses a namespace-static 16 KB copy buffer
// (BSS) — no per-file malloc/free traffic.  The audio-thread NULL-deref
// that disabled this in the v3.6-w1 build was traced to malloc churn from
// qd_CheatTitleResolver's std::thread + 24 KB heap NACP buffer, not from
// qd_SaveBackup itself.  Wiring restored.

namespace {

struct BackupTidEntry {
    const char *tid_lower;
    int         game_index;
    const char *display_title;
};

constexpr BackupTidEntry kBackupTidMap[] = {
    { "010003f003a34000", 0, "Pokemon Let's Go Pikachu" },
    { "0100187003a36000", 0, "Pokemon Let's Go Eevee"   },
    { "0100abf008968000", 1, "Pokemon Sword"            },
    { "01008db008c2c000", 1, "Pokemon Shield"           },
    { "0100862011c46000", 2, "Pokemon Brilliant Diamond" },
    { "010018e011d92000", 2, "Pokemon Shining Pearl"    },
    { "01001f5010dfa000", 3, "Pokemon Legends Arceus"   },
    { "0100a3d008c5c000", 4, "Pokemon Scarlet"          },
    { "01008f6008c5e000", 4, "Pokemon Violet"           },
};
constexpr std::size_t kBackupTidMapCount =
    sizeof(kBackupTidMap) / sizeof(kBackupTidMap[0]);

// Parse a 16-char lowercase hex string into u64.  Returns 0 on parse fail.
u64 ParseTidHex(const char *s) {
    u64 v = 0;
    if (std::sscanf(s, "%016llx",
                    reinterpret_cast<unsigned long long *>(&v)) != 1) {
        return 0;
    }
    return v;
}

// v3.6.1e: fs::ResultTargetNotFound = module 2 (FS), description 1002
// (verified against libvapours fs_results.hpp:45).  This is what
// fsdevMountSaveData returns when the save data simply does not exist on
// the NAND for that (application_id, uid) — i.e. the game isn't installed
// or has no save.  It is NOT a permission error.  libnx's fsdev path
// sometimes leaves stale bits in the upper byte of the returned value, so
// we mask to the meaningful low 22 bits before comparing.
constexpr u32 kFsResultTargetNotFound = 2u | (1002u << 9);  // = 0x7D402
inline bool IsTargetNotFound(Result rc) {
    return (static_cast<u32>(rc) & 0x3FFFFFu) == kFsResultTargetNotFound;
}

}  // namespace

void QdSaveEditorLayout::DoBackupFocusedGame() {
    // v3.6.1: enumerate ALL Switch users, not just uMenu's selected_user.
    // The save data for Pokémon games is per-user; the user who originally
    // played might differ from the user uMenu is logged in as.  Iterate
    // accountListAllUsers and try each (uid, tid) combo — any successful
    // mount gets backed up under JKSV/<game> [<tid>]/<UTC>_u<uid>/.  The
    // _u<uid> suffix (P0 fix) lets restore match a backup to its owning user.
    AccountUid uids[8] = {};
    s32 n_uids = 0;
    {
        Result rc = accountListAllUsers(uids, 8, &n_uids);
        if (R_FAILED(rc) || n_uids <= 0) {
            UL_LOG_WARN("save-editor: accountListAllUsers rc=0x%08X "
                        "count=%d — falling back to selected_user",
                        rc, n_uids);
            // Fallback to selected_user if listing fails.
            const AccountUid sel = g_GlobalSettings.system_status.selected_user;
            if (accountUidIsValid(&sel)) {
                uids[0] = sel;
                n_uids  = 1;
            } else {
                if (g_MenuApplication) {
                    g_MenuApplication->ShowNotification(
                        "No Switch users found on console.", 4000);
                }
                return;
            }
        }
    }
    UL_LOG_INFO("save-editor: backup pass — %d user(s) on console", n_uids);

    std::vector<std::string> ok_paths;
    Result                    last_fail_rc = 0;
    bool                      any_attempt  = false;
    for (std::size_t i = 0; i < kBackupTidMapCount; ++i) {
        const BackupTidEntry &e = kBackupTidMap[i];
        if (e.game_index != title_focus_) continue;
        const u64 tid = ParseTidHex(e.tid_lower);
        if (tid == 0) continue;
        const std::string game_dir =
            QdSaveBackup::MakeGameDir(std::string(e.display_title), tid);
        for (s32 u = 0; u < n_uids; ++u) {
            any_attempt = true;
            std::string out_path;
            const Result rc = QdSaveBackup::BackupSave(tid, uids[u], game_dir,
                                                         &out_path);
            if (R_SUCCEEDED(rc)) {
                UL_LOG_INFO("save-editor: backup ok %s (uid_hi=0x%016llX) -> %s",
                            e.display_title,
                            static_cast<unsigned long long>(uids[u].uid[0]),
                            out_path.c_str());
                ok_paths.push_back(std::string(e.display_title));
            } else {
                last_fail_rc = rc;
                UL_LOG_WARN("save-editor: backup miss %s (uid_hi=0x%016llX) "
                            "rc=0x%08X",
                            e.display_title,
                            static_cast<unsigned long long>(uids[u].uid[0]),
                            rc);
            }
        }
    }

    if (g_MenuApplication) {
        if (!ok_paths.empty()) {
            std::string msg = "Backed up: ";
            for (std::size_t i = 0; i < ok_paths.size(); ++i) {
                if (i) msg += ", ";
                msg += ok_paths[i];
            }
            g_MenuApplication->ShowNotification(msg, 5000);
        } else if (!any_attempt) {
            g_MenuApplication->ShowNotification(
                "Game not found in TID table for this focus.", 4000);
        } else if (IsTargetNotFound(last_fail_rc)) {
            // v3.6.1e: TargetNotFound = no live save on this NAND.  The
            // game isn't installed (or was never played on this console).
            // Backup has nothing to copy — this is correct, not a bug.
            g_MenuApplication->ShowNotification(
                "This game isn't installed on this console (no live save "
                "to back up). Your SD-card JKSV backups are unaffected.",
                5500);
        } else {
            // A genuinely unexpected error — surface the code for diagnosis.
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "Backup failed (rc=2002-%04u). Save may be in use.",
                          (static_cast<unsigned>(last_fail_rc) >> 9) & 0x1FFFu);
            g_MenuApplication->ShowNotification(buf, 5000);
        }
    }
}

void QdSaveEditorLayout::DoRestoreLatestFocusedGame() {
    // v3.6.1: same multi-user iteration as DoBackupFocusedGame so the
    // restore works regardless of which Switch user originally owned the
    // save.  First successful mount-and-restore per TID wins.
    AccountUid uids[8] = {};
    s32 n_uids = 0;
    {
        Result rc = accountListAllUsers(uids, 8, &n_uids);
        if (R_FAILED(rc) || n_uids <= 0) {
            const AccountUid sel = g_GlobalSettings.system_status.selected_user;
            if (accountUidIsValid(&sel)) {
                uids[0] = sel;
                n_uids  = 1;
            } else {
                if (g_MenuApplication) {
                    g_MenuApplication->ShowNotification(
                        "No Switch users found on console.", 4000);
                }
                return;
            }
        }
    }

    std::vector<std::string> ok_titles;
    bool   none_to_restore = true;
    Result last_fail_rc    = 0;

    for (std::size_t i = 0; i < kBackupTidMapCount; ++i) {
        const BackupTidEntry &e = kBackupTidMap[i];
        if (e.game_index != title_focus_) continue;
        const u64 tid = ParseTidHex(e.tid_lower);
        if (tid == 0) continue;
        const std::string game_dir =
            QdSaveBackup::MakeGameDir(std::string(e.display_title), tid);
        auto list = QdSaveBackup::ListBackups(game_dir);  // newest-first
        if (list.empty()) {
            continue;
        }
        none_to_restore = false;

        // P0 cross-user fix: restore each present user's OWN newest backup,
        // matched by the _u<uid> folder suffix.  NEVER write one user's backup
        // onto another user's save (the old code restored the globally-newest
        // backup onto whichever user mounted first).
        bool any_tagged = false;
        for (s32 u = 0; u < n_uids; ++u) {
            const BackupListEntry *match = nullptr;
            for (const auto &b : list) {
                if (b.has_owner
                        && b.owner_uid.uid[0] == uids[u].uid[0]
                        && b.owner_uid.uid[1] == uids[u].uid[1]) {
                    match = &b;  // list is newest-first → first hit is newest
                    break;
                }
            }
            if (!match) continue;
            any_tagged = true;
            const Result rc = QdSaveBackup::RestoreSave(
                tid, uids[u], game_dir, match->folder_name);
            if (R_SUCCEEDED(rc)) {
                UL_LOG_INFO("save-editor: restore ok %s from %s (owner uid_hi=0x%016llX)",
                            e.display_title, match->folder_name.c_str(),
                            static_cast<unsigned long long>(uids[u].uid[0]));
                ok_titles.push_back(std::string(e.display_title));
            } else {
                last_fail_rc = rc;
                UL_LOG_WARN("save-editor: restore miss %s (uid_hi=0x%016llX) rc=0x%08X",
                            e.display_title,
                            static_cast<unsigned long long>(uids[u].uid[0]), rc);
            }
        }

        // Legacy/JKSV backups carry no _u owner tag.  Only when NO present user
        // owned a tagged backup do we fall back to the newest untagged one — and
        // only onto the currently selected user (the active human's choice),
        // never guessed onto an arbitrary user.
        if (!any_tagged) {
            const BackupListEntry *legacy = nullptr;
            for (const auto &b : list) {
                if (!b.has_owner) { legacy = &b; break; }
            }
            const AccountUid sel = g_GlobalSettings.system_status.selected_user;
            if (legacy && accountUidIsValid(&sel)) {
                UL_LOG_INFO("save-editor: no owner-tagged backup; restoring LEGACY "
                            "%s from %s to selected_user",
                            e.display_title, legacy->folder_name.c_str());
                const Result rc = QdSaveBackup::RestoreSave(
                    tid, sel, game_dir, legacy->folder_name);
                if (R_SUCCEEDED(rc)) {
                    ok_titles.push_back(std::string(e.display_title));
                } else {
                    last_fail_rc = rc;
                    UL_LOG_WARN("save-editor: legacy restore miss %s rc=0x%08X",
                                e.display_title, rc);
                }
            }
        }
    }

    if (g_MenuApplication) {
        if (none_to_restore) {
            g_MenuApplication->ShowNotification(
                "No backups found for this game.", 4000);
        } else if (!ok_titles.empty()) {
            std::string msg = "Restored: ";
            for (std::size_t i = 0; i < ok_titles.size(); ++i) {
                if (i) msg += ", ";
                msg += ok_titles[i];
            }
            g_MenuApplication->ShowNotification(msg, 5000);
        } else if (IsTargetNotFound(last_fail_rc)) {
            // Restore needs the live save partition to exist (we copy INTO
            // it).  If the game isn't installed there's no target to write.
            g_MenuApplication->ShowNotification(
                "Install the game first — there's no save slot on this "
                "console to restore into.", 5500);
        } else {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "Restore failed (rc=2002-%04u).",
                          (static_cast<unsigned>(last_fail_rc) >> 9) & 0x1FFFu);
            g_MenuApplication->ShowNotification(buf, 5000);
        }
    }
}
// (no closing #endif — #if 0 block removed 2026-05-28)

// ── Static blit helper ────────────────────────────────────────────────────────

void QdSaveEditorLayout::BlitTex(SDL_Renderer *r, SDL_Texture *tex,
                                 const s32 x, const s32 y) {
    if (tex == nullptr || r == nullptr) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── W13-SAVE-PARSER: RenderParseError ────────────────────────────────────────

void QdSaveEditorLayout::RenderParseError(SDL_Renderer *r,
                                           const s32 ox, const s32 oy) const {
    if (parse_error_tex_) {
        int tw = 0, th = 0;
        SDL_QueryTexture(parse_error_tex_, nullptr, nullptr, &tw, &th);
        const s32 cx = ox + (GetNaturalW() - tw) / 2;
        const s32 cy = oy + GetNaturalH() / 2 - th / 2;
        BlitTex(r, parse_error_tex_, cx, cy);
    }
}

// ── W13-SAVE-PARSER: RenderPartyBox ──────────────────────────────────────────
//
// 2×3 grid of party slots.  Layout:
//   Row 0: slots 0, 3
//   Row 1: slots 1, 4
//   Row 2: slots 2, 5
// Each cell: kSlotW × kSlotH with a kSlotGap gap.
// Focus ring drawn around the selected slot.

static constexpr s32 kSlotW    = 560;  ///< Slot cell width.
static constexpr s32 kSlotH    = 136;  ///< Slot cell height.
static constexpr s32 kSlotGapX = 20;   ///< Horizontal gap between columns.
static constexpr s32 kSlotGapY = 12;   ///< Vertical gap between rows.

void QdSaveEditorLayout::RenderPartyBox(SDL_Renderer *r,
                                         const s32 ox, const s32 oy) const {
    // Grid origin: below the tab bar.
    // kTopbarH = PANEL_HEADER_H = 36 (file-scope kTopbarH already aliases this).
    static constexpr s32 kTabBarH = 48;  // same as outer kTabBarH; kept local for scope clarity
    const s32 grid_top = oy + kTopbarH + kTabBarH + 16;  // uses file-scope kTopbarH
    const s32 grid_left = ox + kMargin;

    // Detail-defer toast (A pressed on slot).
    if (detail_toast_frames_ > 0 && detail_defer_tex_) {
        int tw = 0, th = 0;
        SDL_QueryTexture(detail_defer_tex_, nullptr, nullptr, &tw, &th);
        const s32 tx = ox + (GetNaturalW() - tw) / 2;
        const s32 ty = oy + GetNaturalH() - kHintBarH - th - 8;
        const u8 alpha = (detail_toast_frames_ > 30)
            ? 220u
            : static_cast<u8>(220u * detail_toast_frames_ / 30);
        SDL_SetTextureAlphaMod(detail_defer_tex_, alpha);
        BlitTex(r, detail_defer_tex_, tx, ty);
        SDL_SetTextureAlphaMod(detail_defer_tex_, 255);
    }

    // Render party count subtitle.
    {
        char buf[32];
        if (save_loaded_) {
            snprintf(buf, sizeof(buf), "Party  (%d / 6)", current_save_.party_count);
        } else {
            snprintf(buf, sizeof(buf), "Party");
        }
        // Render inline — no cached texture, small cost.
        const auto small_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
        SDL_Texture *sub = pu::ui::render::RenderText(
            small_font, std::string(buf), theme_.text_secondary);
        if (sub) {
            BlitTex(r, sub, grid_left, grid_top - 22);
            pu::ui::render::DeleteTexture(sub);
        }
    }

    // Draw 2 columns × 3 rows.
    for (int i = 0; i < 6; ++i) {
        const int col = i < 3 ? 0 : 1;
        const int row = i < 3 ? i : i - 3;

        const s32 sx = grid_left + col * (kSlotW + kSlotGapX);
        const s32 sy = grid_top  + row * (kSlotH + kSlotGapY);

        const bool focused  = (i == party_focus_);
        const bool occupied = (i < current_save_.party_count);

        // Slot background.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        if (focused) {
            SDL_SetRenderDrawColor(r,
                theme_.accent.r, theme_.accent.g, theme_.accent.b, 50);
        } else {
            SDL_SetRenderDrawColor(r,
                theme_.surface_glass.r, theme_.surface_glass.g,
                theme_.surface_glass.b, occupied ? 180 : 80);
        }
        SDL_Rect slot_rect = { sx, sy, kSlotW, kSlotH };
        SDL_RenderFillRect(r, &slot_rect);

        // Focus ring.
        if (focused) {
            SDL_SetRenderDrawColor(r,
                theme_.focus_ring.r, theme_.focus_ring.g,
                theme_.focus_ring.b, 220);
            SDL_RenderDrawRect(r, &slot_rect);
        }

        if (!occupied) {
            // Empty slot placeholder.
            if (party_empty_tex_) {
                int tw = 0, th = 0;
                SDL_QueryTexture(party_empty_tex_, nullptr, nullptr, &tw, &th);
                BlitTex(r, party_empty_tex_,
                        sx + (kSlotW - tw) / 2,
                        sy + (kSlotH - th) / 2);
            }
            continue;
        }

        // Slot contents.
        const s32 pad_x = 16;
        const s32 pad_y = 14;

        // Name (top-left).
        if (party_name_tex_[i]) {
            BlitTex(r, party_name_tex_[i], sx + pad_x, sy + pad_y);
        }

        // Shiny indicator (top-right of name area).
        if (party_shiny_tex_[i]) {
            int sw = 0, sh = 0;
            SDL_QueryTexture(party_shiny_tex_[i], nullptr, nullptr, &sw, &sh);
            BlitTex(r, party_shiny_tex_[i],
                    sx + kSlotW - sw - pad_x,
                    sy + pad_y);
        }

        // Level (below name).
        if (party_level_tex_[i]) {
            int lw = 0, lh = 0;
            SDL_QueryTexture(party_level_tex_[i], nullptr, nullptr, &lw, &lh);
            BlitTex(r, party_level_tex_[i],
                    sx + pad_x,
                    sy + pad_y + 36);
        }

        // Held item (below level).
        if (party_item_tex_[i]) {
            BlitTex(r, party_item_tex_[i],
                    sx + pad_x,
                    sy + pad_y + 72);
        }
    }
}

// ── TitlePicker rendering ─────────────────────────────────────────────────────

void QdSaveEditorLayout::RenderTitlePicker(SDL_Renderer *r,
                                           const s32 ox, const s32 oy) const {
    const s32 list_x = ox + kMargin;
    s32       list_y = oy + kTopbarH + kRowGap;

    // W12-SAVE-DISCO: if scan not yet complete show "Scanning…" placeholder.
    if (!scan_applied_) {
        if (rescan_tex_) {
            int tw = 0, th = 0;
            SDL_QueryTexture(rescan_tex_, nullptr, nullptr, &tw, &th);
            BlitTex(r, rescan_tex_, list_x + 16, list_y + 8);
        }
    }

    for (int i = 0; i < kGameCount; ++i) {
        const bool focused = (i == title_focus_);
        // W12-SAVE-DISCO: dim rows with no saves.
        const bool has_saves = (save_counts_[i] > 0);

        // Row background.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        if (focused) {
            SDL_SetRenderDrawColor(r,
                theme_.accent.r, theme_.accent.g, theme_.accent.b, 60);
        } else if (has_saves || !scan_applied_) {
            SDL_SetRenderDrawColor(r,
                theme_.surface_glass.r, theme_.surface_glass.g,
                theme_.surface_glass.b, 180);
        } else {
            // No saves — grey out the row.
            SDL_SetRenderDrawColor(r,
                theme_.surface_glass.r, theme_.surface_glass.g,
                theme_.surface_glass.b, 80);
        }
        SDL_Rect row_bg = {
            list_x, list_y,
            GetNaturalW() - 2 * kMargin, kRowH
        };
        SDL_RenderFillRect(r, &row_bg);

        // Focus ring.
        if (focused) {
            SDL_SetRenderDrawColor(r,
                theme_.focus_ring.r, theme_.focus_ring.g,
                theme_.focus_ring.b, 220);
            SDL_RenderDrawRect(r, &row_bg);
        }

        // Game name.
        if (title_textures_[i]) {
            int tw = 0, th = 0;
            SDL_QueryTexture(title_textures_[i], nullptr, nullptr, &tw, &th);
            const s32 ty = list_y + (kRowH - th) / 2;
            // Dim the text for rows without saves.
            if (scan_applied_ && !has_saves) {
                SDL_SetTextureAlphaMod(title_textures_[i], 100);
            } else {
                SDL_SetTextureAlphaMod(title_textures_[i], 255);
            }
            BlitTex(r, title_textures_[i], list_x + 16, ty);

            // W12-SAVE-DISCO: render save-count suffix next to the game name.
            if (scan_applied_ && save_count_textures_[i]) {
                BlitTex(r, save_count_textures_[i], list_x + 16 + tw, ty);
            }
        }

        list_y += kRowH + kRowGap;
    }

    // W12B-AUTOSCAN: diagnostic line — "Scanned: N paths  Skipped: M (not found)".
    // Rendered below the game list, above the hint bar.
    if (scan_applied_ && diag_tex_) {
        int dw = 0, dh = 0;
        SDL_QueryTexture(diag_tex_, nullptr, nullptr, &dw, &dh);
        const s32 dx = ox + kMargin;
        // Place it kHintBarH + a small gap above the bottom of the canvas.
        const s32 dy = oy + GetNaturalH() - kHintBarH - dh - 6;
        SDL_SetTextureAlphaMod(diag_tex_, 160);
        BlitTex(r, diag_tex_, dx, dy);
        SDL_SetTextureAlphaMod(diag_tex_, 255);
    }
}

// ── Box view (collapsible PC boxes) ──────────────────────────────────────────

void QdSaveEditorLayout::LoadBoxesIfNeeded() {
    if (box_loaded_ || !box_supported_ || loaded_save_path_.empty()) {
        return;
    }
    const BdspResult res = QdBDSPSaveParser::ParseBoxesFromFile(
        loaded_save_path_, box_slots_, box_counts_);
    box_loaded_ = true;  // mark attempted even on failure (degrades to empty boxes)
    UL_LOG_INFO("save-editor: box load (BDSP) rc=%d occupied=%zu",
                static_cast<int>(res), box_slots_.size());
    // DATA SNAPSHOT — per-occupied-slot dump, CAPPED.  Each UL_LOG_INFO is a
    // synchronous fopen/fwrite/fclose to the SD on the UI thread; a full save can
    // have hundreds of boxed Pokémon, so cap the per-slot lines to avoid stalling
    // the UI.  The total count is always logged above.
    {
        size_t shown = 0;
        for (const auto &s : box_slots_) {
            if (shown++ >= 24) {
                UL_LOG_INFO("  ... +%zu more occupied slots (log capped)",
                            box_slots_.size() - 24);
                break;
            }
            UL_LOG_INFO("  box%d slot%d sp=%u lv=%u shiny=%d",
                        s.box + 1, s.slot + 1, static_cast<unsigned>(s.pk.species),
                        static_cast<unsigned>(s.pk.stat_level), static_cast<int>(s.pk.is_shiny));
        }
    }
}

void QdSaveEditorLayout::BoxModeInput(const u64 keys_down) {
    if (box_expanded_ < 0) {
        // Box list: navigate, open, or leave the box view.
        if ((keys_down & HidNpadButton_Down) && box_list_sel_ < kBoxCount - 1) {
            ++box_list_sel_;
        }
        if ((keys_down & HidNpadButton_Up) && box_list_sel_ > 0) {
            --box_list_sel_;
        }
        if (keys_down & HidNpadButton_A) {
            if (box_supported_ && box_counts_[box_list_sel_] > 0) {
                box_expanded_ = box_list_sel_;
                box_slot_sel_ = 0;
            }
        }
        if (keys_down & (HidNpadButton_Y | HidNpadButton_B)) {
            box_mode_ = false;  // collapse the whole view → back to party grid
        }
    } else {
        // Expanded box: navigate occupied slots, A opens detail, B/Y collapses.
        const int count = box_counts_[box_expanded_];
        if ((keys_down & HidNpadButton_Down) && box_slot_sel_ < count - 1) {
            ++box_slot_sel_;
        }
        if ((keys_down & HidNpadButton_Up) && box_slot_sel_ > 0) {
            --box_slot_sel_;
        }
        if (keys_down & HidNpadButton_A) {
            // Open detail for the box_slot_sel_-th occupied slot in this box.
            int occ = 0;
            for (const auto &s : box_slots_) {
                if (s.box != box_expanded_) continue;
                if (occ == box_slot_sel_) {
                    detail_      = BuildDetail(s.pk);
                    detail_open_ = true;
                    break;
                }
                ++occ;
            }
        }
        if (keys_down & (HidNpadButton_B | HidNpadButton_Y)) {
            box_expanded_ = -1;  // collapse back to the box list
        }
    }
}

void QdSaveEditorLayout::RenderBoxView(SDL_Renderer *r,
                                       const s32 ox, const s32 oy) const {
    const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    auto draw = [&](const std::string &s, s32 x, s32 y, const pu::ui::Color &c) {
        SDL_Texture *t = pu::ui::render::RenderText(small, s, c);
        if (!t) return;
        int w = 0, h = 0; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect d { x, y, w, h }; SDL_RenderCopy(r, t, nullptr, &d);
        pu::ui::render::DeleteTexture(t);
    };

    const s32 body_x = ox + kMargin;
    const s32 head_y = oy + kTopbarH + kTabBarH + 12;

    if (!box_supported_) {
        draw("PC Box view is available for Brilliant Diamond / Shining Pearl.",
             body_x, head_y, theme_.text_secondary);
        draw("(Sword/Shield box storage is coming in a later update.)",
             body_x, head_y + 26, theme_.text_secondary);
        return;
    }

    if (box_expanded_ < 0) {
        // ── Collapsed box list (one row per box; expand to see contents) ──
        draw("PC Boxes", body_x, head_y, theme_.accent);
        {
            char sub[40];
            std::snprintf(sub, sizeof(sub), "%zu stored", box_slots_.size());
            draw(sub, body_x + 200, head_y, theme_.text_secondary);
        }

        const s32 row_h    = 26;
        const s32 list_top = head_y + 30;
        const s32 avail_h  = GetNaturalH() - list_top - oy - kHintBarH - 8;
        const int visible  = avail_h > row_h ? avail_h / row_h : 1;
        int top = 0;
        if (box_list_sel_ >= visible) top = box_list_sel_ - visible + 1;

        for (int row = 0; row < visible; ++row) {
            const int b = top + row;
            if (b >= kBoxCount) break;
            const s32  ry  = list_top + row * row_h;
            const bool sel = (b == box_list_sel_);
            if (sel) {
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g,
                                       theme_.accent.b, 70);
                SDL_Rect hr { body_x, ry - 2, GetNaturalW() - 2 * kMargin, row_h - 2 };
                SDL_RenderFillRect(r, &hr);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            }
            char line[48];
            if (box_counts_[b] > 0) {
                std::snprintf(line, sizeof(line), "%s Box %d    (%d)",
                              sel ? "\xe2\x96\xb8" : "  ", b + 1, box_counts_[b]);
            } else {
                std::snprintf(line, sizeof(line), "   Box %d    - empty -", b + 1);
            }
            draw(line, body_x + 8, ry + 3,
                 (box_counts_[b] > 0)
                     ? (sel ? theme_.text_primary : theme_.text_secondary)
                     : theme_.titlebar_inactive);
        }
        return;
    }

    // ── Expanded single box: list its occupied slots with real names ──
    {
        char hdr[48];
        std::snprintf(hdr, sizeof(hdr), "Box %d    (%d)",
                      box_expanded_ + 1, box_counts_[box_expanded_]);
        draw(hdr, body_x, head_y, theme_.accent);
    }
    s32 ry  = head_y + 30;
    int occ = 0;  // running index of occupied slots in this box (for selection)
    for (const auto &s : box_slots_) {
        if (s.box != box_expanded_) continue;
        const PB8 &pk = s.pk;
        const char *nm =
            (pk.display_name[0] != '\0' && is_clean_ascii(pk.display_name))
                ? pk.display_name : nullptr;
        if (!nm) nm = species_name_full(pk.species);
        if (!nm) nm = species_name(pk.species);

        const bool sel = (occ == box_slot_sel_);
        if (sel) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 70);
            SDL_Rect hr { body_x, ry - 2, GetNaturalW() - 2 * kMargin, 24 };
            SDL_RenderFillRect(r, &hr);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }

        char lv[12];
        if (pk.stat_level >= 1 && pk.stat_level <= 100) {
            std::snprintf(lv, sizeof(lv), "Lv.%u", static_cast<unsigned>(pk.stat_level));
        } else {
            std::snprintf(lv, sizeof(lv), "Lv.?");
        }
        char line[96];
        std::snprintf(line, sizeof(line), "Slot %2d   %-16s  %s%s",
                      s.slot + 1, nm ? nm : "?", lv, pk.is_shiny ? "  *" : "");
        draw(line, body_x + 8, ry,
             pk.is_shiny ? theme_.accent
                         : (sel ? theme_.text_primary : theme_.text_secondary));
        ry += 24;
        ++occ;
    }
}

// ── Pokémon detail view ──────────────────────────────────────────────────────
//
// Full read-out of one Pokémon (party slot or box slot).  Every field below is
// REAL decoded data from the save — species/level/nature/gender/shiny/IVs are
// fully named; ability/held-item/move are shown as their numeric IDs (the large
// name tables for those are a separate data-only add, not a deferred feature).

void QdSaveEditorLayout::RenderDetail(SDL_Renderer *r,
                                      const s32 ox, const s32 oy) const {
    const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const auto med   = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    auto draw = [&](const std::string &s, s32 x, s32 y, const pu::ui::Color &c,
                    bool big = false) {
        SDL_Texture *t = pu::ui::render::RenderText(big ? med : small, s, c);
        if (!t) return;
        int w = 0, h = 0; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect d { x, y, w, h }; SDL_RenderCopy(r, t, nullptr, &d);
        pu::ui::render::DeleteTexture(t);
    };

    const MonDetail &d = detail_;
    const s32 bx = ox + kMargin;
    s32 y = oy + kTopbarH + kTabBarH + 12;

    const char *spname =
        (d.name[0] != '\0' && is_clean_ascii(d.name)) ? d.name
                                                      : species_name_full(d.species);
    if (!spname) spname = species_name(d.species);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s%s", spname ? spname : "?",
                  d.shiny ? "   * shiny" : "");
    draw(buf, bx, y, d.shiny ? theme_.accent : theme_.text_primary, true);
    y += 38;

    char lv[12];
    if (d.level >= 1 && d.level <= 100) {
        std::snprintf(lv, sizeof(lv), "Lv.%u", static_cast<unsigned>(d.level));
    } else {
        std::snprintf(lv, sizeof(lv), "Lv.?");
    }
    std::snprintf(buf, sizeof(buf), "%s    %s    Nature: %s",
                  lv, gender_str(d.gender), nature_name(d.nature));
    draw(buf, bx, y, theme_.text_primary); y += 28;

    {
        const char *abil = ability_name(d.ability);
        char abil_buf[24];
        if (!abil) { std::snprintf(abil_buf, sizeof(abil_buf), "#%u", static_cast<unsigned>(d.ability)); abil = abil_buf; }
        std::snprintf(buf, sizeof(buf), "Ability: %s", abil);
        draw(buf, bx, y, theme_.text_secondary); y += 26;
    }
    {
        const char *itm = d.held_item ? item_name(d.held_item) : "None";
        char itm_buf[40];
        if (!itm) { std::snprintf(itm_buf, sizeof(itm_buf), "Item #%u", static_cast<unsigned>(d.held_item)); itm = itm_buf; }
        std::snprintf(buf, sizeof(buf), "Held item: %s", itm);
        draw(buf, bx, y, theme_.text_secondary); y += 26;
    }

    std::snprintf(buf, sizeof(buf), "OT: %s      TID: %05u   SID: %05u",
                  d.ot[0] ? d.ot : "-",
                  static_cast<unsigned>(d.tid), static_cast<unsigned>(d.sid));
    draw(buf, bx, y, theme_.text_secondary); y += 26;

    std::snprintf(buf, sizeof(buf), "PID: %08X", static_cast<unsigned>(d.pid));
    draw(buf, bx, y, theme_.text_secondary); y += 32;

    draw("IVs   HP / Atk / Def / SpA / SpD / Spe", bx, y, theme_.accent); y += 26;
    std::snprintf(buf, sizeof(buf), "      %u / %u / %u / %u / %u / %u",
                  d.iv[0], d.iv[1], d.iv[2], d.iv[3], d.iv[4], d.iv[5]);
    draw(buf, bx, y, theme_.text_primary); y += 32;

    draw("Moves", bx, y, theme_.accent); y += 26;
    bool any_move = false;
    for (int i = 0; i < 4; ++i) {
        if (d.moves[i] == 0) continue;
        any_move = true;
        const char *mv = move_name(d.moves[i]);
        char mv_buf[24];
        if (!mv) { std::snprintf(mv_buf, sizeof(mv_buf), "Move #%u", static_cast<unsigned>(d.moves[i])); mv = mv_buf; }
        std::snprintf(buf, sizeof(buf), "   %s", mv);
        draw(buf, bx, y, theme_.text_primary); y += 22;
    }
    if (!any_move) {
        draw("   (none)", bx, y, theme_.text_secondary); y += 22;
    }

    draw("B / A:  Back", bx, oy + GetNaturalH() - kHintBarH + 4, theme_.text_secondary);
}

// ── Trainer tab ──────────────────────────────────────────────────────────────

void QdSaveEditorLayout::RenderTrainer(SDL_Renderer *r,
                                       const s32 ox, const s32 oy) const {
    const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const auto med   = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    auto draw = [&](const std::string &s, s32 x, s32 y, const pu::ui::Color &c,
                    bool big = false) {
        SDL_Texture *t = pu::ui::render::RenderText(big ? med : small, s, c);
        if (!t) return;
        int w = 0, h = 0; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect d { x, y, w, h }; SDL_RenderCopy(r, t, nullptr, &d);
        pu::ui::render::DeleteTexture(t);
    };

    const s32 bx = ox + kMargin;
    s32 y = oy + kTopbarH + kTabBarH + 16;

    if (!trainer_.valid) {
        draw("Trainer card", bx, y, theme_.accent, true); y += 36;
        draw("Available for Brilliant Diamond / Shining Pearl saves.",
             bx, y, theme_.text_secondary); y += 24;
        draw("(Sword/Shield trainer read is pending a save-key fix.)",
             bx, y, theme_.text_secondary);
        return;
    }

    draw(trainer_.ot[0] ? trainer_.ot : "Trainer", bx, y, theme_.text_primary, true);
    y += 42;
    draw(trainer_.gender == 0 ? "Male" : "Female", bx, y, theme_.text_secondary);
    y += 32;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Trainer ID:   %05u", static_cast<unsigned>(trainer_.tid));
    draw(buf, bx, y, theme_.text_primary);   y += 28;
    std::snprintf(buf, sizeof(buf), "Secret ID:    %05u", static_cast<unsigned>(trainer_.sid));
    draw(buf, bx, y, theme_.text_secondary); y += 28;
    std::snprintf(buf, sizeof(buf), "Money:        %u", static_cast<unsigned>(trainer_.money));
    draw(buf, bx, y, theme_.text_primary);   y += 28;
}

// ── Items / Bag tab ──────────────────────────────────────────────────────────

void QdSaveEditorLayout::RenderInventory(SDL_Renderer *r,
                                         const s32 ox, const s32 oy) const {
    const auto small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    const auto med   = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    auto draw = [&](const std::string &s, s32 x, s32 y, const pu::ui::Color &c,
                    bool big = false) {
        SDL_Texture *t = pu::ui::render::RenderText(big ? med : small, s, c);
        if (!t) return;
        int w = 0, h = 0; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect d { x, y, w, h }; SDL_RenderCopy(r, t, nullptr, &d);
        pu::ui::render::DeleteTexture(t);
    };

    const s32 bx = ox + kMargin;
    const s32 head_y = oy + kTopbarH + kTabBarH + 12;

    if (!bag_supported_) {
        draw("Bag", bx, head_y, theme_.accent, true);
        draw("Available for Brilliant Diamond / Shining Pearl saves.",
             bx, head_y + 36, theme_.text_secondary);
        draw("(Sword/Shield bag read is pending a save-key fix.)",
             bx, head_y + 60, theme_.text_secondary);
        return;
    }

    char hdr[40];
    std::snprintf(hdr, sizeof(hdr), "Bag  —  %zu item%s",
                  bag_items_.size(), bag_items_.size() == 1 ? "" : "s");
    draw(hdr, bx, head_y, theme_.accent);

    if (bag_items_.empty()) {
        draw("(empty)", bx, head_y + 30, theme_.text_secondary);
        return;
    }

    const s32 row_h    = 26;
    const s32 list_top = head_y + 30;
    const s32 avail_h  = GetNaturalH() - list_top - oy - kHintBarH - 8;
    const int visible  = avail_h > row_h ? avail_h / row_h : 1;
    int top = 0;
    if (bag_sel_ >= visible) top = bag_sel_ - visible + 1;

    for (int row = 0; row < visible; ++row) {
        const int idx = top + row;
        if (idx >= static_cast<int>(bag_items_.size())) break;
        const BagItemLite &it = bag_items_[static_cast<size_t>(idx)];
        const s32  ry  = list_top + row * row_h;
        const bool sel = (idx == bag_sel_);
        if (sel) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 70);
            SDL_Rect hr { bx, ry - 2, GetNaturalW() - 2 * kMargin, row_h - 2 };
            SDL_RenderFillRect(r, &hr);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
        const char *nm = item_name(it.id);
        char namebuf[24];
        if (!nm) { std::snprintf(namebuf, sizeof(namebuf), "Item #%u", static_cast<unsigned>(it.id)); nm = namebuf; }
        char line[64];
        std::snprintf(line, sizeof(line), "%-22s  x%u", nm, static_cast<unsigned>(it.count));
        draw(line, bx + 8, ry + 3, sel ? theme_.text_primary : theme_.text_secondary);
    }
}

// ── Tab-bar + placeholder rendering ──────────────────────────────────────────

void QdSaveEditorLayout::RenderPanel(SDL_Renderer *r,
                                     const s32 ox, const s32 oy) const {
    // Tab bar strip.
    const s32 tab_bar_y = oy + kTopbarH;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.surface_glass.r, theme_.surface_glass.g,
        theme_.surface_glass.b, 200);
    SDL_Rect tab_bar_bg = {
        ox, tab_bar_y, GetNaturalW(), kTabBarH
    };
    SDL_RenderFillRect(r, &tab_bar_bg);

    // Individual tabs.
    s32 tab_x = ox + kMargin;
    for (int i = 0; i < kTabCount; ++i) {
        const bool focused = (i == panel_focus_);
        SDL_Rect tab_rect = { tab_x, tab_bar_y + 4, kTabW, kTabBarH - 8 };

        if (focused) {
            SDL_SetRenderDrawColor(r,
                theme_.accent.r, theme_.accent.g, theme_.accent.b, 80);
            SDL_RenderFillRect(r, &tab_rect);
            SDL_SetRenderDrawColor(r,
                theme_.focus_ring.r, theme_.focus_ring.g,
                theme_.focus_ring.b, 200);
            SDL_RenderDrawRect(r, &tab_rect);
        }

        if (tab_textures_[i]) {
            int tw = 0, th = 0;
            SDL_QueryTexture(tab_textures_[i], nullptr, nullptr, &tw, &th);
            const s32 tx = tab_x + (kTabW - tw) / 2;
            const s32 ty = tab_bar_y + (kTabBarH - th) / 2;
            BlitTex(r, tab_textures_[i], tx, ty);
        }

        tab_x += kTabW + 8;
    }

    // Detail panel overrides the body when open.
    if (detail_open_) {
        RenderDetail(r, ox, oy);
        return;
    }

    // Body — dispatch to the appropriate sub-renderer.
    if (mode_ == Mode::PartyBox) {
        // W13-SAVE-PARSER: real party data (or parse-error panel).
        if (!save_loaded_ && parse_error_tex_) {
            RenderParseError(r, ox, oy);
        } else if (box_mode_) {
            RenderBoxView(r, ox, oy);
        } else {
            RenderPartyBox(r, ox, oy);
        }
    } else if (mode_ == Mode::Trainer) {
        RenderTrainer(r, ox, oy);
    } else {
        RenderInventory(r, ox, oy);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdSaveEditorLayout::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                  const s32 ox, const s32 oy) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) return;

    // Lazily build textures on first render (renderer guaranteed live here).
    if (!textures_built_) {
        BuildTextures(r);
    }

    // W12-SAVE-DISCO: apply scan results once the scan has completed.
    if (!scan_applied_) {
        const SaveScanResult &sr = autoscan_.GetResult();
        if (sr.scan_done) {
            ApplyScanResult(r);
        }
    }

    // W13-SAVE-PARSER: detail-defer toast timer (the only live toast).
    if (detail_toast_frames_ > 0) {
        --detail_toast_frames_;
    }

    // Background fill (desktop_bg at full opacity to darken beneath the panel).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,
        theme_.desktop_bg.r, theme_.desktop_bg.g,
        theme_.desktop_bg.b, 255);
    SDL_Rect bg = { ox, oy, GetNaturalW(), GetNaturalH() };
    SDL_RenderFillRect(r, &bg);

    // Top header bar.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.topbar_bg.r, theme_.topbar_bg.g,
        theme_.topbar_bg.b, 220);
    SDL_Rect header = { ox, oy, GetNaturalW(), kTopbarH };
    SDL_RenderFillRect(r, &header);

    // Accent separator below header.
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 160);
    SDL_RenderDrawLine(r, ox, oy + kTopbarH, ox + GetNaturalW(), oy + kTopbarH);

    // Mode-specific body.
    if (mode_ == Mode::TitlePicker) {
        RenderTitlePicker(r, ox, oy);
    } else {
        RenderPanel(r, ox, oy);
    }

    // Bottom hint bar.
    if (hint_tex_) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_tex_, nullptr, nullptr, &hw, &hh);
        const s32 hx = ox + (GetNaturalW() - hw) / 2;
        const s32 hy = oy + GetNaturalH() - kHintBarH + (kHintBarH - hh) / 2;
        BlitTex(r, hint_tex_, hx, hy);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdSaveEditorLayout::OnInput(const u64 keys_down,
                                 const u64 /*keys_up*/,
                                 const u64 /*keys_held*/,
                                 const pu::ui::TouchPoint /*touch_pos*/) {
    if (mode_ == Mode::TitlePicker) {
        // ── TitlePicker navigation ─────────────────────────────────────────

        if (keys_down & HidNpadButton_Up) {
            if (title_focus_ > 0) {
                --title_focus_;
            }
        }
        if (keys_down & HidNpadButton_Down) {
            if (title_focus_ < kGameCount - 1) {
                ++title_focus_;
            }
        }
        if (keys_down & HidNpadButton_A) {
            // v3.6.1e: A is the VIEW action — the Saves tile's primary
            // purpose is to display the party.  Honest behavior per case:
            //
            //   - Sword/Shield WITH a save file found on SD:
            //       enter PartyBox; the SwSh parser decodes + shows 6 party
            //       Pokémon.  This is the one fully-working view path.
            //   - Sword/Shield with NO SD save file:
            //       offer a live-NAND backup (DoBackupFocusedGame) — useful
            //       if the game is installed; honest "not installed" message
            //       otherwise.
            //   - Any OTHER Pokémon game (BDSP / Legends / Scarlet-Violet /
            //       Let's Go): there is no save parser for these yet, so
            //       viewing is impossible regardless of whether a backup
            //       file exists.  Two distinct honest messages: "no parser
            //       (your backup is safe)" when an SD backup IS present, vs.
            //       "no SD backup found" when it isn't.  Either way we never
            //       attempt a doomed save-data mount (which only returns
            //       TargetNotFound when the game isn't installed).  X still
            //       offers live-NAND backup for installed copies.
            const bool has_sd_backup =
                scan_applied_ && save_counts_[title_focus_] > 0;
            const char *gname =
                (title_focus_ >= 0 && title_focus_ < kGameCount)
                    ? kGameNames[title_focus_] : "this game";

            if ((title_focus_ == 1 || title_focus_ == 2) && has_sd_backup) {
                // Sword/Shield (full parser) or Brilliant Diamond/Shining Pearl
                // (v3.6 best-effort BDSP decoder) WITH an SD backup — try to view.
                UL_LOG_INFO("save-editor: A on game=%d w/ SD backup -> try PartyBox",
                            title_focus_);
                SDL_Renderer *r = pu::ui::render::GetMainRenderer();
                mode_        = Mode::PartyBox;
                panel_focus_ = 0;
                party_focus_ = 0;
                TryLoadSave(r);
                if (!save_loaded_) {
                    // UI fix: never strand the user in a blank PartyBox window.
                    // Revert to the list and say WHY — distinct per failure so a
                    // wrong-path / empty-folder miss isn't misreported as a bad
                    // save format.
                    mode_ = Mode::TitlePicker;
                    UL_LOG_WARN("save-editor: PartyBox load failed (reason=%d) "
                                "-> reverted to TitlePicker (no blank window)",
                                static_cast<int>(load_fail_reason_));
                    if (g_MenuApplication) {
                        std::string msg;
                        if (load_fail_reason_ == LoadFailReason::FileMissing) {
                            // Title folder present but no readable 'main' inside.
                            msg = "Found a backup folder for ";
                            msg += gname;
                            msg += " on the SD but no readable save file (main) "
                                   "inside it. Press X to back up the live save.";
                        } else {
                            // ParseFailed (read but parser rejected the format).
                            msg = "Couldn't read the ";
                            msg += gname;
                            msg += " save from the SD backup (empty or unsupported "
                                   "format). Press X to back up the live save.";
                        }
                        g_MenuApplication->ShowNotification(msg, 5500);
                    }
                }
            } else if (title_focus_ == 1) {
                // Sword/Shield with NO SD backup: a live-NAND backup is still
                // worthwhile if the game is installed; DoBackupFocusedGame emits
                // the honest "not installed" message when it isn't.
                UL_LOG_INFO("save-editor: A on SwSh, no SD backup -> backup");
                DoBackupFocusedGame();
            } else if (has_sd_backup) {
                // Other Pokémon game WITH an SD backup, but no parser exists for
                // it (BDSP / Legends / Scarlet-Violet / Let's Go).  Viewing is
                // impossible; reassure the user their backup is intact.
                UL_LOG_INFO("save-editor: A on game=%d w/ SD backup -> "
                            "no-parser message", title_focus_);
                if (g_MenuApplication) {
                    std::string msg = "Save viewing for ";
                    msg += gname;
                    msg += " isn't available yet (no parser). Your SD backup "
                           "is safe.";
                    g_MenuApplication->ShowNotification(msg, 5500);
                }
            } else {
                // Other Pokémon game with NO SD backup on the card at all.
                UL_LOG_INFO("save-editor: A on game=%d, no SD backup -> "
                            "no-backup message", title_focus_);
                if (g_MenuApplication) {
                    std::string msg = "No SD backup found for ";
                    msg += gname;
                    msg += ". Press X to back up its live save (if the game is "
                           "installed).";
                    g_MenuApplication->ShowNotification(msg, 5500);
                }
            }
        }
        // W12-SAVE-DISCO: Y triggers a rescan of the SD card.
        if (keys_down & HidNpadButton_Y) {
            UL_LOG_INFO("save-editor: Y pressed -> RequestRescan");
            RequestRescan();
        }
        // v3.6 re-enabled 2026-05-28: X = backup, ZL = restore most-recent.
        // The qd_SaveBackup module now uses a namespace-static 16 KB copy
        // buffer (BSS), eliminating the per-file malloc that the audio-thread
        // MP3 NULL-deref investigation flagged.  Safe to wire.
        if (keys_down & HidNpadButton_X) {
            UL_LOG_INFO("save-editor: X pressed -> DoBackupFocusedGame");
            DoBackupFocusedGame();
        }
        if (keys_down & HidNpadButton_ZL) {
            UL_LOG_INFO("save-editor: ZL pressed -> DoRestoreLatestFocusedGame");
            DoRestoreLatestFocusedGame();
        }
        if (keys_down & HidNpadButton_B) {
            // Dismiss the editor entirely — return to main desktop.
            UL_LOG_INFO("save-editor: B in TitlePicker -> LoadMenu Main");
            if (g_MenuApplication) {
                g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
            }
        }
    } else {
        // ── Panel tab navigation ───────────────────────────────────────────

        // Detail panel owns input while open — A or B dismiss it.
        if (detail_open_) {
            if (keys_down & (HidNpadButton_B | HidNpadButton_A)) {
                detail_open_ = false;
            }
            return;
        }

        // Box sub-view (within the Party/Box tab) owns input while active.
        if (mode_ == Mode::PartyBox && box_mode_) {
            BoxModeInput(keys_down);
            return;
        }
        // Y enters the collapsible box view from the party grid.
        if (mode_ == Mode::PartyBox && save_loaded_ && (keys_down & HidNpadButton_Y)) {
            box_mode_     = true;
            box_expanded_ = -1;
            box_list_sel_ = 0;
            LoadBoxesIfNeeded();
            return;
        }

        if (keys_down & (HidNpadButton_L | HidNpadButton_R)) {
            const int prev_focus = panel_focus_;
            if ((keys_down & HidNpadButton_L) && panel_focus_ > 0) {
                --panel_focus_;
            }
            if ((keys_down & HidNpadButton_R) && panel_focus_ < kTabCount - 1) {
                ++panel_focus_;
            }
            // BUGFIX (creator report 2026-06-13): L/R used to move the highlight
            // ONLY — mode_ stayed PartyBox, so A then opened party-detail instead
            // of the focused tab, and Items/Trainer were unreachable.  L/R now
            // OPENS the focused tab directly: sync mode_ and reset per-tab state.
            box_mode_     = false;
            box_expanded_ = -1;
            detail_open_  = false;
            switch (panel_focus_) {
                case 0:  mode_ = Mode::PartyBox;  break;
                case 1:  mode_ = Mode::Inventory; break;
                case 2:  mode_ = Mode::Trainer;   break;
                default: break;  // 3 = "Back" pseudo-tab: keep content; A returns to picker
            }
            UL_LOG_INFO("save-editor: TAB %d->%d  mode=%d (0=Pick 1=Party 2=Inv 3=Trn)  "
                        "save_loaded=%d box_supported=%d bag=%zu",
                        prev_focus, panel_focus_, static_cast<int>(mode_),
                        static_cast<int>(save_loaded_), static_cast<int>(box_supported_),
                        bag_items_.size());
        }

        // D-pad navigation within PartyBox (2×3 grid).
        if (mode_ == Mode::PartyBox && save_loaded_) {
            // Grid layout: col 0 = slots 0-2, col 1 = slots 3-5.
            // Left/Right switches column; Up/Down moves within column.
            if (keys_down & HidNpadButton_Up) {
                const int col = party_focus_ < 3 ? 0 : 1;
                const int row = party_focus_ < 3 ? party_focus_ : party_focus_ - 3;
                if (row > 0) {
                    party_focus_ = col == 0 ? row - 1 : row - 1 + 3;
                }
            }
            if (keys_down & HidNpadButton_Down) {
                const int col = party_focus_ < 3 ? 0 : 1;
                const int row = party_focus_ < 3 ? party_focus_ : party_focus_ - 3;
                const int max_row = 2;
                if (row < max_row) {
                    party_focus_ = col == 0 ? row + 1 : row + 1 + 3;
                }
            }
            if (keys_down & HidNpadButton_Left) {
                if (party_focus_ >= 3) {
                    party_focus_ -= 3;
                }
            }
            if (keys_down & HidNpadButton_Right) {
                if (party_focus_ < 3) {
                    party_focus_ += 3;
                    if (party_focus_ >= 6) party_focus_ = 5;
                }
            }
        }

        // Up/Down scroll the bag list in the Items tab.
        if (mode_ == Mode::Inventory && !bag_items_.empty()) {
            if ((keys_down & HidNpadButton_Down) &&
                bag_sel_ < static_cast<int>(bag_items_.size()) - 1) {
                ++bag_sel_;
            }
            if ((keys_down & HidNpadButton_Up) && bag_sel_ > 0) {
                --bag_sel_;
            }
        }

        // A on the "Back" pseudo-tab (index kTabCount-1) returns to TitlePicker.
        if (keys_down & HidNpadButton_A) {
            if (panel_focus_ == kTabCount - 1) {
                UL_LOG_INFO("save-editor: Back tab selected -> TitlePicker");
                mode_        = Mode::TitlePicker;
                panel_focus_ = 0;
            } else if (mode_ == Mode::PartyBox && save_loaded_) {
                // A on a party slot — open the full detail read-out.
                if (party_focus_ >= 0 && party_focus_ < current_save_.party_count) {
                    detail_      = BuildDetail(current_save_.party[party_focus_]);
                    detail_open_ = true;
                    UL_LOG_INFO("save-editor: A on party slot %d -> detail (species %u)",
                                party_focus_, static_cast<unsigned>(detail_.species));
                }
            } else {
                // Activate the selected tab.
                switch (panel_focus_) {
                    case 0: mode_ = Mode::PartyBox;   break;
                    case 1: mode_ = Mode::Inventory;  break;
                    case 2: mode_ = Mode::Trainer;    break;
                    default: break;
                }
                UL_LOG_INFO("save-editor: tab %d activated", panel_focus_);
            }
        }

        if (keys_down & HidNpadButton_B) {
            // B always goes back to TitlePicker from within a panel.
            UL_LOG_INFO("save-editor: B in panel -> TitlePicker");
            mode_          = Mode::TitlePicker;
            panel_focus_   = 0;
            party_focus_   = 0;
            save_loaded_   = false;
        }
    }
}

// ── GetBottomHint ─────────────────────────────────────────────────────────────

// W11-SAVE Part 5: context-appropriate bottom-bar hint string.
// Called from on_tick in OpenSaveEditorWindow so the QdWindow chrome always
// reflects the currently active navigation mode.
std::string QdSaveEditorLayout::GetBottomHint() const {
    // B is hierarchical-back: it pops one level and only CLOSES at TitlePicker.
    if (mode_ == Mode::TitlePicker) {
        return "A  Open game \xc2\xb7  Y  Rescan SD \xc2\xb7  B  Close";
    }
    if (detail_open_) {
        return "A / B  Back";
    }
    if (mode_ == Mode::PartyBox && box_mode_) {
        if (box_expanded_ >= 0) {
            return "\xe2\x86\x95  Slots \xc2\xb7  A  Detail \xc2\xb7  B / Y  Back";
        }
        return "\xe2\x86\x95  Box \xc2\xb7  A  Open \xc2\xb7  B / Y  Back";
    }
    if (mode_ == Mode::PartyBox && save_loaded_) {
        return "\xe2\x86\x95\xe2\x86\x94  Slot \xc2\xb7  A  Detail \xc2\xb7  Y  Boxes \xc2\xb7  L/R  Tab \xc2\xb7  B  Back";
    }
    if (mode_ == Mode::Inventory) {
        return "\xe2\x86\x95  Scroll \xc2\xb7  L/R  Tab \xc2\xb7  B  Back";
    }
    return "L/R  Tab \xc2\xb7  B  Back";
}

// ── OnBackRequested — hierarchical B (windowed; matches the in-OnInput B paths) ──
// QdWindow calls this when B is pressed, BEFORE closing the window.  Pop one nav
// level and return true (keep the window open); return false at TitlePicker so the
// window closes.  Mirrors the existing per-mode B handlers (which still serve the
// fullscreen path, where the chrome is absent).
bool QdSaveEditorLayout::OnBackRequested() {
    if (detail_open_) {                        // detail panel -> underlying view
        detail_open_ = false;
        return true;
    }
    if (mode_ == Mode::PartyBox && box_mode_) {
        if (box_expanded_ >= 0) {              // expanded box -> box list
            box_expanded_ = -1;
            return true;
        }
        box_mode_ = false;                     // box list -> party grid
        return true;
    }
    if (mode_ != Mode::TitlePicker) {          // any panel -> TitlePicker
        mode_        = Mode::TitlePicker;
        panel_focus_ = 0;
        return true;
    }
    return false;                              // TitlePicker (top) -> window closes
}

// ── GetDebugState ─────────────────────────────────────────────────────────────

std::string QdSaveEditorLayout::GetDebugState() const {
    const char *mode_name = nullptr;
    switch (mode_) {
        case Mode::TitlePicker: mode_name = "TitlePicker"; break;
        case Mode::PartyBox:    mode_name = "PartyBox";    break;
        case Mode::Inventory:   mode_name = "Inventory";   break;
        case Mode::Trainer:     mode_name = "Trainer";     break;
        default:                mode_name = "?";           break;
    }
    std::string s = std::string("save:") + mode_name
                  + ":game=" + std::to_string(title_focus_);
    if (box_mode_) s += ":box";
    return s;
}

// ── QdSaveEditorHostLayout ctor ───────────────────────────────────────────────

QdSaveEditorHostLayout::QdSaveEditorHostLayout(
        QdSaveEditorLayout::Ref element)
    : editor_element_(element)
{
    UL_LOG_INFO("save-editor: QdSaveEditorHostLayout ctor");
    this->SetBackgroundColor({ 0, 0, 0, 255 });
    this->Add(this->editor_element_);
}

// ── IMenuLayout obligations ───────────────────────────────────────────────────

void QdSaveEditorHostLayout::OnMenuInput(const u64 keys_down,
                                         const u64 keys_up,
                                         const u64 keys_held,
                                         const pu::ui::TouchPoint touch_pos) {
    // Element handles its own input through the Plutonium child-element
    // dispatch path (pu::ui::Layout::OnInput → child QdSaveEditorLayout::OnInput).
    // Nothing to forward manually here.
    (void)keys_down;
    (void)keys_up;
    (void)keys_held;
    (void)touch_pos;
}

bool QdSaveEditorHostLayout::OnHomeButtonPress() {
    UL_LOG_INFO("save-editor: OnHomeButtonPress -> MainMenu");
    g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
    return true;
}

void QdSaveEditorHostLayout::LoadSfx() {
    // No sfx in this PR.  Add here when sound cues are designed.
}

void QdSaveEditorHostLayout::DisposeSfx() {
    // Symmetric no-op — mirrors LoadSfx.
}

} // namespace ul::menu::qdesktop
