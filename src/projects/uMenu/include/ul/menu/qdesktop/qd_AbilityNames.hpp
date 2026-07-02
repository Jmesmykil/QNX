// qd_AbilityNames.hpp - full English ability-name lookup.
//
// PURPOSE: give the W13-SAVE-PARSER save editor real ability names
// (e.g. "Levitate") instead of "Ability #26" for every ability up to the
// current maximum.  Companion to qd_SpeciesNames.hpp; same shape, same rules.
//
// SOURCE: public ability list (Gen 3-9).  Ability names are public, factual
// reference data (a numbered list of names).  They are reproduced here as
// plain ASCII strings.  No third-party code was copied; this is a clean-room
// data table authored by hand.
//
// INDEXING: index == in-game ability ID, matching the PKHeX ability index
// space (verified anchors: 1=Stench, 22=Intimidate, 26=Levitate).  Index 0 is
// the "no ability" placeholder and maps to nullptr.
//
// ASCII NOTE: the on-device font has no accented glyphs, so every name is
// plain 7-bit ASCII.  Names that canonically use special characters are
// transliterated to ASCII:
//   - Curly apostrophes -> kept as ASCII '  ("Dragon's Maw", "Mind's Eye")
//   - Hyphens kept      -> "Soul-Heart", "Well-Baked Body"
//
// DUPLICATES: a handful of IDs legitimately share a display name because the
// game stores one ability name per form (e.g. ability 266/267 both "As One"
// for Calyrex's two riders; abilities 300-303 all "Embody Aspect" for
// Ogerpon's four masks).  These are reproduced verbatim, not deduplicated.
//
// Self-contained, header-only.  Only <cstdint> is required.
// Compiles clean with -std=c++20 -Wall -Wextra.

#pragma once

#include <cstdint>

namespace ul::menu::qdesktop {

    // Ability names indexed by in-game ability ID.
    //   index 0    = nullptr (invalid / "no ability")
    //   index 1    = "Stench"
    //   ...
    //   index 310  = "Poison Puppeteer" (current maximum, Gen 9)
    static constexpr const char *kAbilityNames[] = {
        nullptr,            // 0 = invalid
        // --- Generation 3 (Ruby/Sapphire/Emerald): abilities 1-76 ---
        "Stench",           "Drizzle",          "Speed Boost",      "Battle Armor",     "Sturdy",            // 1-5
        "Damp",             "Limber",           "Sand Veil",        "Static",           "Volt Absorb",       // 6-10
        "Water Absorb",     "Oblivious",        "Cloud Nine",       "Compound Eyes",    "Insomnia",          // 11-15
        "Color Change",     "Immunity",         "Flash Fire",       "Shield Dust",      "Own Tempo",         // 16-20
        "Suction Cups",     "Intimidate",       "Shadow Tag",       "Rough Skin",       "Wonder Guard",      // 21-25
        "Levitate",         "Effect Spore",     "Synchronize",      "Clear Body",       "Natural Cure",      // 26-30
        "Lightning Rod",    "Serene Grace",     "Swift Swim",       "Chlorophyll",      "Illuminate",        // 31-35
        "Trace",            "Huge Power",       "Poison Point",     "Inner Focus",      "Magma Armor",       // 36-40
        "Water Veil",       "Magnet Pull",      "Soundproof",       "Rain Dish",        "Sand Stream",       // 41-45
        "Pressure",         "Thick Fat",        "Early Bird",       "Flame Body",       "Run Away",          // 46-50
        "Keen Eye",         "Hyper Cutter",     "Pickup",           "Truant",           "Hustle",            // 51-55
        "Cute Charm",       "Plus",             "Minus",            "Forecast",         "Sticky Hold",       // 56-60
        "Shed Skin",        "Guts",             "Marvel Scale",     "Liquid Ooze",      "Overgrow",          // 61-65
        "Blaze",            "Torrent",          "Swarm",            "Rock Head",        "Drought",           // 66-70
        "Arena Trap",       "Vital Spirit",     "White Smoke",      "Pure Power",       "Shell Armor",       // 71-75
        "Air Lock",                                                                                          // 76
        // --- Generation 4 (Diamond/Pearl/Platinum): abilities 77-123 ---
        "Tangled Feet",     "Motor Drive",      "Rivalry",          "Steadfast",        "Snow Cloak",        // 77-81
        "Gluttony",         "Anger Point",      "Unburden",         "Heatproof",        "Simple",            // 82-86
        "Dry Skin",         "Download",         "Iron Fist",        "Poison Heal",      "Adaptability",      // 87-91
        "Skill Link",       "Hydration",        "Solar Power",      "Quick Feet",       "Normalize",         // 92-96
        "Sniper",           "Magic Guard",      "No Guard",         "Stall",            "Technician",        // 97-101
        "Leaf Guard",       "Klutz",            "Mold Breaker",     "Super Luck",       "Aftermath",         // 102-106
        "Anticipation",     "Forewarn",         "Unaware",          "Tinted Lens",      "Filter",            // 107-111
        "Slow Start",       "Scrappy",          "Storm Drain",      "Ice Body",         "Solid Rock",        // 112-116
        "Snow Warning",     "Honey Gather",     "Frisk",            "Reckless",         "Multitype",         // 117-121
        "Flower Gift",      "Bad Dreams",                                                                    // 122-123
        // --- Generation 5 (Black/White): abilities 124-164 ---
        "Pickpocket",       "Sheer Force",      "Contrary",         "Unnerve",          "Defiant",           // 124-128
        "Defeatist",        "Cursed Body",      "Healer",           "Friend Guard",     "Weak Armor",        // 129-133
        "Heavy Metal",      "Light Metal",      "Multiscale",       "Toxic Boost",      "Flare Boost",       // 134-138
        "Harvest",          "Telepathy",        "Moody",            "Overcoat",         "Poison Touch",      // 139-143
        "Regenerator",      "Big Pecks",        "Sand Rush",        "Wonder Skin",      "Analytic",          // 144-148
        "Illusion",         "Imposter",         "Infiltrator",      "Mummy",            "Moxie",             // 149-153
        "Justified",        "Rattled",          "Magic Bounce",     "Sap Sipper",       "Prankster",         // 154-158
        "Sand Force",       "Iron Barbs",       "Zen Mode",         "Victory Star",     "Turboblaze",        // 159-163
        "Teravolt",                                                                                          // 164
        // --- Generation 6 (X/Y): abilities 165-191 ---
        "Aroma Veil",       "Flower Veil",      "Cheek Pouch",      "Protean",          "Fur Coat",          // 165-169
        "Magician",         "Bulletproof",      "Competitive",      "Strong Jaw",       "Refrigerate",       // 170-174
        "Sweet Veil",       "Stance Change",    "Gale Wings",       "Mega Launcher",    "Grass Pelt",        // 175-179
        "Symbiosis",        "Tough Claws",      "Pixilate",         "Gooey",            "Aerilate",          // 180-184
        "Parental Bond",    "Dark Aura",        "Fairy Aura",       "Aura Break",       "Primordial Sea",    // 185-189
        "Desolate Land",    "Delta Stream",                                                                  // 190-191
        // --- Generation 7 (Sun/Moon): abilities 192-233 ---
        "Stamina",          "Wimp Out",         "Emergency Exit",   "Water Compaction", "Merciless",         // 192-196
        "Shields Down",     "Stakeout",         "Water Bubble",     "Steelworker",      "Berserk",           // 197-201
        "Slush Rush",       "Long Reach",       "Liquid Voice",     "Triage",           "Galvanize",         // 202-206
        "Surge Surfer",     "Schooling",        "Disguise",         "Battle Bond",      "Power Construct",   // 207-211
        "Corrosion",        "Comatose",         "Queenly Majesty",  "Innards Out",      "Dancer",            // 212-216
        "Battery",          "Fluffy",           "Dazzling",         "Soul-Heart",       "Tangling Hair",     // 217-221
        "Receiver",         "Power of Alchemy", "Beast Boost",      "RKS System",       "Electric Surge",    // 222-226
        "Psychic Surge",    "Misty Surge",      "Grassy Surge",     "Full Metal Body",  "Shadow Shield",     // 227-231
        "Prism Armor",      "Neuroforce",                                                                    // 232-233
        // --- Generation 8 (Sword/Shield): abilities 234-267 ---
        "Intrepid Sword",   "Dauntless Shield", "Libero",           "Ball Fetch",       "Cotton Down",       // 234-238
        "Propeller Tail",   "Mirror Armor",     "Gulp Missile",     "Stalwart",         "Steam Engine",      // 239-243
        "Punk Rock",        "Sand Spit",        "Ice Scales",       "Ripen",            "Ice Face",          // 244-248
        "Power Spot",       "Mimicry",          "Screen Cleaner",   "Steely Spirit",    "Perish Body",       // 249-253
        "Wandering Spirit", "Gorilla Tactics",  "Neutralizing Gas", "Pastel Veil",      "Hunger Switch",     // 254-258
        "Quick Draw",       "Unseen Fist",      "Curious Medicine", "Transistor",       "Dragon's Maw",      // 259-263
        "Chilling Neigh",   "Grim Neigh",       "As One",           "As One",                                // 264-267
        // --- Generation 9 (Scarlet/Violet): abilities 268-310 ---
        "Lingering Aroma",  "Seed Sower",       "Thermal Exchange", "Anger Shell",      "Purifying Salt",    // 268-272
        "Well-Baked Body",  "Wind Rider",       "Guard Dog",        "Rocky Payload",    "Wind Power",        // 273-277
        "Zero to Hero",     "Commander",        "Electromorphosis", "Protosynthesis",   "Quark Drive",       // 278-282
        "Good as Gold",     "Vessel of Ruin",   "Sword of Ruin",    "Tablets of Ruin",  "Beads of Ruin",     // 283-287
        "Orichalcum Pulse", "Hadron Engine",    "Opportunist",      "Cud Chew",         "Sharpness",         // 288-292
        "Supreme Overlord", "Costar",           "Toxic Debris",     "Armor Tail",       "Earth Eater",       // 293-297
        "Mycelium Might",   "Hospitality",      "Mind's Eye",       "Embody Aspect",    "Embody Aspect",     // 298-302
        "Embody Aspect",    "Embody Aspect",    "Toxic Chain",      "Supersweet Syrup", "Tera Shift",        // 303-307
        "Tera Shell",       "Teraform Zero",    "Poison Puppeteer",                                          // 308-310
    };

    // Total entries in the table (includes index-0 nullptr slot).
    // Highest valid ability ID = kAbilityNamesCount - 1.
    static constexpr int kAbilityNamesCount =
        static_cast<int>(sizeof(kAbilityNames) / sizeof(kAbilityNames[0]));

    // Return the English name for the given ability ID, or nullptr for 0 /
    // out-of-range so the caller can fall back to "Ability #NNN".
    // Bounds-checked against kAbilityNamesCount.
    inline const char *ability_name(const uint16_t id) {
        if (id > 0 && id < kAbilityNamesCount) {
            return kAbilityNames[id];
        }
        return nullptr;
    }

}
