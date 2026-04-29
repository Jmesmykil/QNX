#pragma once
#include <ul/ul_Include.hpp>
#include <string>
#include <unordered_map>
#include <array>

namespace ul::menu::qdesktop {

enum class FolderIdx : u8 {
    None            = 0,
    NxGames         = 1,
    ThirdPartyGames = 2,
    Emulators       = 3,
    Tools           = 4,
    System          = 5,
    Payloads        = 6,
    QOS             = 7,
    Homebrew        = 8,
};

constexpr size_t kFolderCount = 8;

struct FolderSpec {
    FolderIdx   idx;
    const char *display_name;
    char        glyph;
    u8          r, g, b;
};

inline constexpr FolderSpec kFolderSpecs[kFolderCount] = {
    { FolderIdx::NxGames,         "NX Games",  'G', 0x60, 0xA5, 0xFA },
    { FolderIdx::ThirdPartyGames, "3rd Party", 'G', 0x48, 0xC9, 0x74 },
    { FolderIdx::Emulators,       "Emulators", 'E', 0x4A, 0xDE, 0x80 },
    { FolderIdx::Tools,           "Tools",     'T', 0xFB, 0xBF, 0x24 },
    { FolderIdx::System,          "System",    'S', 0xC0, 0x84, 0xFC },
    { FolderIdx::Payloads,        "Payloads",  'P', 0xE0, 0x78, 0x40 },
    { FolderIdx::QOS,             "Q OS",      'Q', 0xA7, 0x8B, 0xFA },
    { FolderIdx::Homebrew,        "Homebrew",  'H', 0x80, 0x80, 0x80 },
};

enum class LaunchpadTabKind : u8 { Favorites = 0, Folder = 1 };

struct LaunchpadTab {
    LaunchpadTabKind kind;
    FolderIdx        folder;
    const char      *display_name;
    u8               r, g, b;
};

class QdFolderClassifier {
public:
    static QdFolderClassifier &Get();

    void Reset();

    FolderIdx ClassifyNro(const std::string &stable_id,
                          const std::string &nro_path,
                          const std::string &nacp_name,
                          const std::string &nacp_author,
                          bool               is_qos_nro);

    FolderIdx ClassifyApplication(const std::string &stable_id,
                                  u64                app_id,
                                  bool               is_nintendo_publisher);

    FolderIdx ClassifyPayload(const std::string &stable_id,
                              const std::string &payload_name);

    FolderIdx ClassifyBuiltin(const std::string &stable_id,
                              const std::string &builtin_name);

    FolderIdx Lookup(const std::string &stable_id) const;

    size_t BucketCount(FolderIdx idx) const;

    void SetUserOverride(const std::string &stable_id, FolderIdx idx);
    void RemoveUserOverride(const std::string &stable_id);

    // Direct table write for use by legacy shims (qd_AutoFolders.cpp).
    // Does not touch overrides_ and does not persist to disk.
    void RegisterDirect(const std::string &stable_id, FolderIdx idx);

private:
    QdFolderClassifier() { LoadOverrides(); }

    void      LoadOverrides();
    void      PersistOverrides();
    FolderIdx AutoClassifyNro(const std::string &nro_path,
                               const std::string &nacp_name,
                               const std::string &nacp_author,
                               bool               is_qos_nro);

    std::unordered_map<std::string, FolderIdx> table_;
    std::unordered_map<std::string, FolderIdx> overrides_;
    std::array<size_t, kFolderCount>           bucket_counts_{};
};

} // namespace ul::menu::qdesktop
