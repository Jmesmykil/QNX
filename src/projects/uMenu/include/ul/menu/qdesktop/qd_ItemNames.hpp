// qd_ItemNames.hpp -- full English item-name lookup for the save editor.
//
// PURPOSE: give the W13-SAVE-PARSER save editor real item names
// (e.g. "Master Ball") instead of "Item #1" for every held / bag item ID
// up to the current item-ID maximum.  This mirrors qd_SpeciesNames.hpp: an
// indexed, header-only table with a bounds-checked inline lookup.
//
// SOURCE: the public, factual item list (a numbered list of names) shared by
// the modern main-series games.  Item names are public reference data and are
// reproduced here as plain ASCII strings.  No third-party code was copied;
// this is a clean-room data table authored by hand.
//
// INDEXING: index == in-game item ID.  This is the unified item-ID space the
// games have used since Gen 3 (verified against the anchors below).  Item IDs
// are NOT the Gen-1/2 bag numbering -- in this scheme Potion is ID 17, not 50.
//   index 0    = nullptr (invalid / "None")
//   index 1    = "Master Ball"   (anchor)
//   index 4    = "Poke Ball"     (anchor)
//   index 17   = "Potion"
//   ...
//   index 2684 = "Canari Bread"  (current maximum)
//
// UNUSED SLOTS: some IDs in the range are never-released / internal-only
// placeholder slots.  Those map to nullptr so the caller falls back to
// "Item #NNN".  IDs that exist but have no published name are kept as the
// literal "???" so the index stays aligned with the in-game ID.
//
// ASCII NOTE: the on-device font has no CJK or accented glyphs, so every
// name is plain 7-bit ASCII.  Names that canonically use special characters
// are transliterated to ASCII:
//   - Gender symbols  -> "Nidoran-F" / "Nidoran-M" (e.g. "Nidoran-M Candy")
//   - Apostrophes     -> kept as ASCII '  ("King's Rock", "Oak's Letter")
//   - Accents dropped -> "Poke Ball", "Pokemon Box Link", "Epice Noire"
//   - Periods kept    -> "Mr. Mime Card", "Guard Spec.", "X Sp. Atk"
//   - Hyphens kept    -> "Twice-Spiced Radish"
//
// Self-contained, header-only.  Only <cstdint> is required.
// Compiles clean with -std=c++20 -Wall -Wextra.

#pragma once

#include <cstdint>

namespace ul::menu::qdesktop {

    // Item names indexed by in-game item ID.
    //   index 0       = nullptr (invalid / "None")
    //   index 1       = "Master Ball"
    //   ...
    //   index 2684    = "Canari Bread" (current item-ID maximum)
    static constexpr const char *kItemNamesFull[] = {
        nullptr,            // 0 = invalid ("None")
        "Master Ball",    "Ultra Ball",     "Great Ball",     "Poke Ball",      "Safari Ball",  // 1-5
        "Net Ball",       "Dive Ball",      "Nest Ball",      "Repeat Ball",    "Timer Ball",  // 6-10
        "Luxury Ball",    "Premier Ball",   "Dusk Ball",      "Heal Ball",      "Quick Ball",  // 11-15
        "Cherish Ball",   "Potion",         "Antidote",       "Burn Heal",      "Ice Heal",  // 16-20
        "Awakening",      "Paralyze Heal",  "Full Restore",   "Max Potion",     "Hyper Potion",  // 21-25
        "Super Potion",   "Full Heal",      "Revive",         "Max Revive",     "Fresh Water",  // 26-30
        "Soda Pop",       "Lemonade",       "Moomoo Milk",    "Energy Powder",  "Energy Root",  // 31-35
        "Heal Powder",    "Revival Herb",   "Ether",          "Max Ether",      "Elixir",  // 36-40
        "Max Elixir",     "Lava Cookie",    "Berry Juice",    "Sacred Ash",     "HP Up",  // 41-45
        "Protein",        "Iron",           "Carbos",         "Calcium",        "Rare Candy",  // 46-50
        "PP Up",          "Zinc",           "PP Max",         "Old Gateau",     "Guard Spec.",  // 51-55
        "Dire Hit",       "X Attack",       "X Defense",      "X Speed",        "X Accuracy",  // 56-60
        "X Sp. Atk",      "X Sp. Def",      "Poke Doll",      "Fluffy Tail",    "Blue Flute",  // 61-65
        "Yellow Flute",   "Red Flute",      "Black Flute",    "White Flute",    "Shoal Salt",  // 66-70
        "Shoal Shell",    "Red Shard",      "Blue Shard",     "Yellow Shard",   "Green Shard",  // 71-75
        "Super Repel",    "Max Repel",      "Escape Rope",    "Repel",          "Sun Stone",  // 76-80
        "Moon Stone",     "Fire Stone",     "Thunder Stone",  "Water Stone",    "Leaf Stone",  // 81-85
        "Tiny Mushroom",  "Big Mushroom",   "Pearl",          "Big Pearl",      "Stardust",  // 86-90
        "Star Piece",     "Nugget",         "Heart Scale",    "Honey",          "Growth Mulch",  // 91-95
        "Damp Mulch",     "Stable Mulch",   "Gooey Mulch",    "Root Fossil",    "Claw Fossil",  // 96-100
        "Helix Fossil",   "Dome Fossil",    "Old Amber",      "Armor Fossil",   "Skull Fossil",  // 101-105
        "Rare Bone",      "Shiny Stone",    "Dusk Stone",     "Dawn Stone",     "Oval Stone",  // 106-110
        "Odd Keystone",   "Griseous Orb",   "Tea",            "???",            "Autograph",  // 111-115
        "Douse Drive",    "Shock Drive",    "Burn Drive",     "Chill Drive",    "???",  // 116-120
        "Pokemon Box Link", "Medicine Pocket", "TM Case",        "Candy Jar",      "Power-Up Pocket",  // 121-125
        "Clothing Trunk", "Catching Pocket", "Battle Pocket",  "???",            "???",  // 126-130
        "???",            "???",            "???",            "Sweet Heart",    "Adamant Orb",  // 131-135
        "Lustrous Orb",   "Greet Mail",     "Favored Mail",   "RSVP Mail",      "Thanks Mail",  // 136-140
        "Inquiry Mail",   "Like Mail",      "Reply Mail",     "Bridge Mail S",  "Bridge Mail D",  // 141-145
        "Bridge Mail T",  "Bridge Mail V",  "Bridge Mail M",  "Cheri Berry",    "Chesto Berry",  // 146-150
        "Pecha Berry",    "Rawst Berry",    "Aspear Berry",   "Leppa Berry",    "Oran Berry",  // 151-155
        "Persim Berry",   "Lum Berry",      "Sitrus Berry",   "Figy Berry",     "Wiki Berry",  // 156-160
        "Mago Berry",     "Aguav Berry",    "Iapapa Berry",   "Razz Berry",     "Bluk Berry",  // 161-165
        "Nanab Berry",    "Wepear Berry",   "Pinap Berry",    "Pomeg Berry",    "Kelpsy Berry",  // 166-170
        "Qualot Berry",   "Hondew Berry",   "Grepa Berry",    "Tamato Berry",   "Cornn Berry",  // 171-175
        "Magost Berry",   "Rabuta Berry",   "Nomel Berry",    "Spelon Berry",   "Pamtre Berry",  // 176-180
        "Watmel Berry",   "Durin Berry",    "Belue Berry",    "Occa Berry",     "Passho Berry",  // 181-185
        "Wacan Berry",    "Rindo Berry",    "Yache Berry",    "Chople Berry",   "Kebia Berry",  // 186-190
        "Shuca Berry",    "Coba Berry",     "Payapa Berry",   "Tanga Berry",    "Charti Berry",  // 191-195
        "Kasib Berry",    "Haban Berry",    "Colbur Berry",   "Babiri Berry",   "Chilan Berry",  // 196-200
        "Liechi Berry",   "Ganlon Berry",   "Salac Berry",    "Petaya Berry",   "Apicot Berry",  // 201-205
        "Lansat Berry",   "Starf Berry",    "Enigma Berry",   "Micle Berry",    "Custap Berry",  // 206-210
        "Jaboca Berry",   "Rowap Berry",    "Bright Powder",  "White Herb",     "Macho Brace",  // 211-215
        "Exp. Share",     "Quick Claw",     "Soothe Bell",    "Mental Herb",    "Choice Band",  // 216-220
        "King's Rock",    "Silver Powder",  "Amulet Coin",    "Cleanse Tag",    "Soul Dew",  // 221-225
        "Deep Sea Tooth", "Deep Sea Scale", "Smoke Ball",     "Everstone",      "Focus Band",  // 226-230
        "Lucky Egg",      "Scope Lens",     "Metal Coat",     "Leftovers",      "Dragon Scale",  // 231-235
        "Light Ball",     "Soft Sand",      "Hard Stone",     "Miracle Seed",   "Black Glasses",  // 236-240
        "Black Belt",     "Magnet",         "Mystic Water",   "Sharp Beak",     "Poison Barb",  // 241-245
        "Never-Melt Ice", "Spell Tag",      "Twisted Spoon",  "Charcoal",       "Dragon Fang",  // 246-250
        "Silk Scarf",     "Upgrade",        "Shell Bell",     "Sea Incense",    "Lax Incense",  // 251-255
        "Lucky Punch",    "Metal Powder",   "Thick Club",     "Leek",           "Red Scarf",  // 256-260
        "Blue Scarf",     "Pink Scarf",     "Green Scarf",    "Yellow Scarf",   "Wide Lens",  // 261-265
        "Muscle Band",    "Wise Glasses",   "Expert Belt",    "Light Clay",     "Life Orb",  // 266-270
        "Power Herb",     "Toxic Orb",      "Flame Orb",      "Quick Powder",   "Focus Sash",  // 271-275
        "Zoom Lens",      "Metronome",      "Iron Ball",      "Lagging Tail",   "Destiny Knot",  // 276-280
        "Black Sludge",   "Icy Rock",       "Smooth Rock",    "Heat Rock",      "Damp Rock",  // 281-285
        "Grip Claw",      "Choice Scarf",   "Sticky Barb",    "Power Bracer",   "Power Belt",  // 286-290
        "Power Lens",     "Power Band",     "Power Anklet",   "Power Weight",   "Shed Shell",  // 291-295
        "Big Root",       "Choice Specs",   "Flame Plate",    "Splash Plate",   "Zap Plate",  // 296-300
        "Meadow Plate",   "Icicle Plate",   "Fist Plate",     "Toxic Plate",    "Earth Plate",  // 301-305
        "Sky Plate",      "Mind Plate",     "Insect Plate",   "Stone Plate",    "Spooky Plate",  // 306-310
        "Draco Plate",    "Dread Plate",    "Iron Plate",     "Odd Incense",    "Rock Incense",  // 311-315
        "Full Incense",   "Wave Incense",   "Rose Incense",   "Luck Incense",   "Pure Incense",  // 316-320
        "Protector",      "Electirizer",    "Magmarizer",     "Dubious Disc",   "Reaper Cloth",  // 321-325
        "Razor Claw",     "Razor Fang",     "TM01",           "TM02",           "TM03",  // 326-330
        "TM04",           "TM05",           "TM06",           "TM07",           "TM08",  // 331-335
        "TM09",           "TM10",           "TM11",           "TM12",           "TM13",  // 336-340
        "TM14",           "TM15",           "TM16",           "TM17",           "TM18",  // 341-345
        "TM19",           "TM20",           "TM21",           "TM22",           "TM23",  // 346-350
        "TM24",           "TM25",           "TM26",           "TM27",           "TM28",  // 351-355
        "TM29",           "TM30",           "TM31",           "TM32",           "TM33",  // 356-360
        "TM34",           "TM35",           "TM36",           "TM37",           "TM38",  // 361-365
        "TM39",           "TM40",           "TM41",           "TM42",           "TM43",  // 366-370
        "TM44",           "TM45",           "TM46",           "TM47",           "TM48",  // 371-375
        "TM49",           "TM50",           "TM51",           "TM52",           "TM53",  // 376-380
        "TM54",           "TM55",           "TM56",           "TM57",           "TM58",  // 381-385
        "TM59",           "TM60",           "TM61",           "TM62",           "TM63",  // 386-390
        "TM64",           "TM65",           "TM66",           "TM67",           "TM68",  // 391-395
        "TM69",           "TM70",           "TM71",           "TM72",           "TM73",  // 396-400
        "TM74",           "TM75",           "TM76",           "TM77",           "TM78",  // 401-405
        "TM79",           "TM80",           "TM81",           "TM82",           "TM83",  // 406-410
        "TM84",           "TM85",           "TM86",           "TM87",           "TM88",  // 411-415
        "TM89",           "TM90",           "TM91",           "TM92",           "HM01",  // 416-420
        "HM02",           "HM03",           "HM04",           "HM05",           "HM06",  // 421-425
        "???",            "???",            "Explorer Kit",   "Loot Sack",      "Rule Book",  // 426-430
        "Poke Radar",     "Point Card",     "Guidebook",      "Sticker Case",   "Fashion Case",  // 431-435
        "Sticker Bag",    "Pal Pad",        "Works Key",      "Old Charm",      "Galactic Key",  // 436-440
        "Red Chain",      "Town Map",       "Vs. Seeker",     "Coin Case",      "Old Rod",  // 441-445
        "Good Rod",       "Super Rod",      "Sprayduck",      "Poffin Case",    "Bike",  // 446-450
        "Suite Key",      "Oak's Letter",   "Lunar Feather",  "Member Card",    "Azure Flute",  // 451-455
        "S.S. Ticket",    "Contest Pass",   "Magma Stone",    "Parcel",         "Coupon 1",  // 456-460
        "Coupon 2",       "Coupon 3",       "Storage Key",    "Secret Medicine", "Vs. Recorder",  // 461-465
        "Gracidea",       "Secret Key",     "Apricorn Box",   "Unown Report",   "Berry Pots",  // 466-470
        "Dowsing Machine", "Blue Card",      "Slowpoke Tail",  "Clear Bell",     "Card Key",  // 471-475
        "Basement Key",   "Squirt Bottle",  "Red Scale",      "Lost Item",      "Pass",  // 476-480
        "Machine Part",   "Silver Feather", "Rainbow Feather", "Mystery Egg",    "Red Apricorn",  // 481-485
        "Blue Apricorn",  "Yellow Apricorn", "Green Apricorn", "Pink Apricorn",  "White Apricorn",  // 486-490
        "Black Apricorn", "Fast Ball",      "Level Ball",     "Lure Ball",      "Heavy Ball",  // 491-495
        "Love Ball",      "Friend Ball",    "Moon Ball",      "Sport Ball",     "Park Ball",  // 496-500
        "Photo Album",    "GB Sounds",      "Tidal Bell",     "Rage Candy Bar", "Data Card 01",  // 501-505
        "Data Card 02",   "Data Card 03",   "Data Card 04",   "Data Card 05",   "Data Card 06",  // 506-510
        "Data Card 07",   "Data Card 08",   "Data Card 09",   "Data Card 10",   "Data Card 11",  // 511-515
        "Data Card 12",   "Data Card 13",   "Data Card 14",   "Data Card 15",   "Data Card 16",  // 516-520
        "Data Card 17",   "Data Card 18",   "Data Card 19",   "Data Card 20",   "Data Card 21",  // 521-525
        "Data Card 22",   "Data Card 23",   "Data Card 24",   "Data Card 25",   "Data Card 26",  // 526-530
        "Data Card 27",   "Jade Orb",       "Lock Capsule",   "Red Orb",        "Blue Orb",  // 531-535
        "Enigma Stone",   "Prism Scale",    "Eviolite",       "Float Stone",    "Rocky Helmet",  // 536-540
        "Air Balloon",    "Red Card",       "Ring Target",    "Binding Band",   "Absorb Bulb",  // 541-545
        "Cell Battery",   "Eject Button",   "Fire Gem",       "Water Gem",      "Electric Gem",  // 546-550
        "Grass Gem",      "Ice Gem",        "Fighting Gem",   "Poison Gem",     "Ground Gem",  // 551-555
        "Flying Gem",     "Psychic Gem",    "Bug Gem",        "Rock Gem",       "Ghost Gem",  // 556-560
        "Dragon Gem",     "Dark Gem",       "Steel Gem",      "Normal Gem",     "Health Feather",  // 561-565
        "Muscle Feather", "Resist Feather", "Genius Feather", "Clever Feather", "Swift Feather",  // 566-570
        "Pretty Feather", "Cover Fossil",   "Plume Fossil",   "Liberty Pass",   "Pass Orb",  // 571-575
        "Dream Ball",     "Poke Toy",       "Prop Case",      "Dragon Skull",   "Balm Mushroom",  // 576-580
        "Big Nugget",     "Pearl String",   "Comet Shard",    "Relic Copper",   "Relic Silver",  // 581-585
        "Relic Gold",     "Relic Vase",     "Relic Band",     "Relic Statue",   "Relic Crown",  // 586-590
        "Casteliacone",   "Dire Hit 2",     "X Speed 2",      "X Sp. Atk 2",    "X Sp. Def 2",  // 591-595
        "X Defense 2",    "X Attack 2",     "X Accuracy 2",   "X Speed 3",      "X Sp. Atk 3",  // 596-600
        "X Sp. Def 3",    "X Defense 3",    "X Attack 3",     "X Accuracy 3",   "X Speed 6",  // 601-605
        "X Sp. Atk 6",    "X Sp. Def 6",    "X Defense 6",    "X Attack 6",     "X Accuracy 6",  // 606-610
        "Ability Urge",   "Item Drop",      "Item Urge",      "Reset Urge",     "Dire Hit 3",  // 611-615
        "Light Stone",    "Dark Stone",     "TM93",           "TM94",           "TM95",  // 616-620
        "Xtransceiver",   "???",            "Gram 1",         "Gram 2",         "Gram 3",  // 621-625
        "Xtransceiver",   "Medal Box",      "DNA Splicers",   "DNA Splicers",   "Permit",  // 626-630
        "Oval Charm",     "Shiny Charm",    "Plasma Card",    "Grubby Hanky",   "Colress Machine",  // 631-635
        "Dropped Item",   "Dropped Item",   "Reveal Glass",   "Weakness Policy", "Assault Vest",  // 636-640
        "Holo Caster",    "Prof's Letter",  "Roller Skates",  "Pixie Plate",    "Ability Capsule",  // 641-645
        "Whipped Dream",  "Sachet",         "Luminous Moss",  "Snowball",       "Safety Goggles",  // 646-650
        "Poke Flute",     "Rich Mulch",     "Surprise Mulch", "Boost Mulch",    "Amaze Mulch",  // 651-655
        "Gengarite",      "Gardevoirite",   "Ampharosite",    "Venusaurite",    "Charizardite X",  // 656-660
        "Blastoisinite",  "Mewtwonite X",   "Mewtwonite Y",   "Blazikenite",    "Medichamite",  // 661-665
        "Houndoominite",  "Aggronite",      "Banettite",      "Tyranitarite",   "Scizorite",  // 666-670
        "Pinsirite",      "Aerodactylite",  "Lucarionite",    "Abomasite",      "Kangaskhanite",  // 671-675
        "Gyaradosite",    "Absolite",       "Charizardite Y", "Alakazite",      "Heracronite",  // 676-680
        "Mawilite",       "Manectite",      "Garchompite",    "Latiasite",      "Latiosite",  // 681-685
        "Roseli Berry",   "Kee Berry",      "Maranga Berry",  "Sprinklotad",    "TM96",  // 686-690
        "TM97",           "TM98",           "TM99",           "TM100",          "Power Plant Pass",  // 691-695
        "Mega Ring",      "Intriguing Stone", "Common Stone",   "Discount Coupon", "Elevator Key",  // 696-700
        "TMV Pass",       "Honor of Kalos", "Adventure Guide", "Strange Souvenir", "Lens Case",  // 701-705
        "Makeup Bag",     "Travel Trunk",   "Lumiose Galette", "Shalour Sable",  "Jaw Fossil",  // 706-710
        "Sail Fossil",    "Looker Ticket",  "Bike",           "Holo Caster",    "Fairy Gem",  // 711-715
        "Mega Charm",     "Mega Glove",     "Mach Bike",      "Acro Bike",      "Wailmer Pail",  // 716-720
        "Devon Parts",    "Soot Sack",      "Basement Key",   "Pokeblock Kit",  "Letter",  // 721-725
        "Eon Ticket",     "Scanner",        "Go-Goggles",     "Meteorite",      "Key to Room 1",  // 726-730
        "Key to Room 2",  "Key to Room 4",  "Key to Room 6",  "Storage Key",    "Devon Scope",  // 731-735
        "S.S. Ticket",    "HM07",           "Devon Scuba Gear", "Contest Costume", "Contest Costume",  // 736-740
        "Magma Suit",     "Aqua Suit",      "Pair of Tickets", "Mega Bracelet",  "Mega Pendant",  // 741-745
        "Mega Glasses",   "Mega Anchor",    "Mega Stickpin",  "Mega Tiara",     "Mega Anklet",  // 746-750
        "Meteorite",      "Swampertite",    "Sceptilite",     "Sablenite",      "Altarianite",  // 751-755
        "Galladite",      "Audinite",       "Metagrossite",   "Sharpedonite",   "Slowbronite",  // 756-760
        "Steelixite",     "Pidgeotite",     "Glalitite",      "Diancite",       "Prison Bottle",  // 761-765
        "Mega Cuff",      "Cameruptite",    "Lopunnite",      "Salamencite",    "Beedrillite",  // 766-770
        "Meteorite",      "Meteorite",      "Key Stone",      "Meteorite Shard", "Eon Flute",  // 771-775
        "Normalium Z",    "Firium Z",       "Waterium Z",     "Electrium Z",    "Grassium Z",  // 776-780
        "Icium Z",        "Fightinium Z",   "Poisonium Z",    "Groundium Z",    "Flyinium Z",  // 781-785
        "Psychium Z",     "Buginium Z",     "Rockium Z",      "Ghostium Z",     "Dragonium Z",  // 786-790
        "Darkinium Z",    "Steelium Z",     "Fairium Z",      "Pikanium Z",     "Bottle Cap",  // 791-795
        "Gold Bottle Cap", "Z-Ring",         "Decidium Z",     "Incinium Z",     "Primarium Z",  // 796-800
        "Tapunium Z",     "Marshadium Z",   "Aloraichium Z",  "Snorlium Z",     "Eevium Z",  // 801-805
        "Mewnium Z",      "Normalium Z",    "Firium Z",       "Waterium Z",     "Electrium Z",  // 806-810
        "Grassium Z",     "Icium Z",        "Fightinium Z",   "Poisonium Z",    "Groundium Z",  // 811-815
        "Flyinium Z",     "Psychium Z",     "Buginium Z",     "Rockium Z",      "Ghostium Z",  // 816-820
        "Dragonium Z",    "Darkinium Z",    "Steelium Z",     "Fairium Z",      "Pikanium Z",  // 821-825
        "Decidium Z",     "Incinium Z",     "Primarium Z",    "Tapunium Z",     "Marshadium Z",  // 826-830
        "Aloraichium Z",  "Snorlium Z",     "Eevium Z",       "Mewnium Z",      "Pikashunium Z",  // 831-835
        "Pikashunium Z",  "???",            "???",            "???",            "???",  // 836-840
        "Forage Bag",     "Fishing Rod",    "Professor's Mask", "Festival Ticket", "Sparkling Stone",  // 841-845
        "Adrenaline Orb", "Zygarde Cube",   "???",            "Ice Stone",      "Ride Pager",  // 846-850
        "Beast Ball",     "Big Malasada",   "Red Nectar",     "Yellow Nectar",  "Pink Nectar",  // 851-855
        "Purple Nectar",  "Sun Flute",      "Moon Flute",     "???",            "Enigmatic Card",  // 856-860
        "Silver Razz Berry", "Golden Razz Berry", "Silver Nanab Berry", "Golden Nanab Berry", "Silver Pinap Berry",  // 861-865
        "Golden Pinap Berry", "???",            "???",            "???",            "???",  // 866-870
        "???",            "Secret Key",     "S.S. Ticket",    "Silph Scope",    "Parcel",  // 871-875
        "Card Key",       "Gold Teeth",     "Lift Key",       "Terrain Extender", "Protective Pads",  // 876-880
        "Electric Seed",  "Psychic Seed",   "Misty Seed",     "Grassy Seed",    "Stretchy Spring",  // 881-885
        "Chalky Stone",   "Marble",         "Lone Earring",   "Beach Glass",    "Gold Leaf",  // 886-890
        "Silver Leaf",    "Polished Mud Ball", "Tropical Shell", "Leaf Letter",    "Leaf Letter",  // 891-895
        "Small Bouquet",  "???",            "???",            "???",            "Lure",  // 896-900
        "Super Lure",     "Max Lure",       "Pewter Crunchies", "Fighting Memory", "Flying Memory",  // 901-905
        "Poison Memory",  "Ground Memory",  "Rock Memory",    "Bug Memory",     "Ghost Memory",  // 906-910
        "Steel Memory",   "Fire Memory",    "Water Memory",   "Grass Memory",   "Electric Memory",  // 911-915
        "Psychic Memory", "Ice Memory",     "Dragon Memory",  "Dark Memory",    "Fairy Memory",  // 916-920
        "Solganium Z",    "Lunalium Z",     "Ultranecrozium Z", "Mimikium Z",     "Lycanium Z",  // 921-925
        "Kommonium Z",    "Solganium Z",    "Lunalium Z",     "Ultranecrozium Z", "Mimikium Z",  // 926-930
        "Lycanium Z",     "Kommonium Z",    "Z-Power Ring",   "Pink Petal",     "Orange Petal",  // 931-935
        "Blue Petal",     "Red Petal",      "Green Petal",    "Yellow Petal",   "Purple Petal",  // 936-940
        "Rainbow Flower", "Surge Badge",    "N-Solarizer",    "N-Lunarizer",    "N-Solarizer",  // 941-945
        "N-Lunarizer",    "Ilima's Normalium Z", "Left Poke Ball", "Roto Hatch",     "Roto Bargain",  // 946-950
        "Roto Prize Money", "Roto Exp. Points", "Roto Friendship", "Roto Encounter", "Roto Stealth",  // 951-955
        "Roto HP Restore", "Roto PP Restore", "Roto Boost",     "Roto Catch",     "Health Candy",  // 956-960
        "Mighty Candy",   "Tough Candy",    "Smart Candy",    "Courage Candy",  "Quick Candy",  // 961-965
        "Health Candy L", "Mighty Candy L", "Tough Candy L",  "Smart Candy L",  "Courage Candy L",  // 966-970
        "Quick Candy L",  "Health Candy XL", "Mighty Candy XL", "Tough Candy XL", "Smart Candy XL",  // 971-975
        "Courage Candy XL", "Quick Candy XL", "Bulbasaur Candy", "Charmander Candy", "Squirtle Candy",  // 976-980
        "Caterpie Candy", "Weedle Candy",   "Pidgey Candy",   "Rattata Candy",  "Spearow Candy",  // 981-985
        "Ekans Candy",    "Pikachu Candy",  "Sandshrew Candy", "Nidoran-F Candy", "Nidoran-M Candy",  // 986-990
        "Clefairy Candy", "Vulpix Candy",   "Jigglypuff Candy", "Zubat Candy",    "Oddish Candy",  // 991-995
        "Paras Candy",    "Venonat Candy",  "Diglett Candy",  "Meowth Candy",   "Psyduck Candy",  // 996-1000
        "Mankey Candy",   "Growlithe Candy", "Poliwag Candy",  "Abra Candy",     "Machop Candy",  // 1001-1005
        "Bellsprout Candy", "Tentacool Candy", "Geodude Candy",  "Ponyta Candy",   "Slowpoke Candy",  // 1006-1010
        "Magnemite Candy", "Farfetch'd Candy", "Doduo Candy",    "Seel Candy",     "Grimer Candy",  // 1011-1015
        "Shellder Candy", "Gastly Candy",   "Onix Candy",     "Drowzee Candy",  "Krabby Candy",  // 1016-1020
        "Voltorb Candy",  "Exeggcute Candy", "Cubone Candy",   "Hitmonlee Candy", "Hitmonchan Candy",  // 1021-1025
        "Lickitung Candy", "Koffing Candy",  "Rhyhorn Candy",  "Chansey Candy",  "Tangela Candy",  // 1026-1030
        "Kangaskhan Candy", "Horsea Candy",   "Goldeen Candy",  "Staryu Candy",   "Mr. Mime Candy",  // 1031-1035
        "Scyther Candy",  "Jynx Candy",     "Electabuzz Candy", "Pinsir Candy",   "Tauros Candy",  // 1036-1040
        "Magikarp Candy", "Lapras Candy",   "Ditto Candy",    "Eevee Candy",    "Porygon Candy",  // 1041-1045
        "Omanyte Candy",  "Kabuto Candy",   "Aerodactyl Candy", "Snorlax Candy",  "Articuno Candy",  // 1046-1050
        "Zapdos Candy",   "Moltres Candy",  "Dratini Candy",  "Mewtwo Candy",   "Mew Candy",  // 1051-1055
        "Meltan Candy",   "Magmar Candy",   "???",            "???",            "???",  // 1056-1060
        "???",            "???",            "???",            "???",            "???",  // 1061-1065
        "???",            "???",            "???",            "???",            "???",  // 1066-1070
        "???",            "???",            "???",            "Endorsement",    "Pokemon Box Link",  // 1071-1075
        "Wishing Star",   "Dynamax Band",   "???",            "???",            "Fishing Rod",  // 1076-1080
        "Rotom Bike",     "???",            "???",            "Sausages",       "Bob's Food Tin",  // 1081-1085
        "Bach's Food Tin", "Tin of Beans",   "Bread",          "Pasta",          "Mixed Mushrooms",  // 1086-1090
        "Smoke-Poke Tail", "Large Leek",     "Fancy Apple",    "Brittle Bones",  "Pack of Potatoes",  // 1091-1095
        "Pungent Root",   "Salad Mix",      "Fried Food",     "Boiled Egg",     "Camping Gear",  // 1096-1100
        "???",            "???",            "Rusted Sword",   "Rusted Shield",  "Fossilized Bird",  // 1101-1105
        "Fossilized Fish", "Fossilized Drake", "Fossilized Dino", "Strawberry Sweet", "Love Sweet",  // 1106-1110
        "Berry Sweet",    "Clover Sweet",   "Flower Sweet",   "Star Sweet",     "Ribbon Sweet",  // 1111-1115
        "Sweet Apple",    "Tart Apple",     "Throat Spray",   "Eject Pack",     "Heavy-Duty Boots",  // 1116-1120
        "Blunder Policy", "Room Service",   "Utility Umbrella", "Exp. Candy XS",  "Exp. Candy S",  // 1121-1125
        "Exp. Candy M",   "Exp. Candy L",   "Exp. Candy XL",  "Dynamax Candy",  "TR00",  // 1126-1130
        "TR01",           "TR02",           "TR03",           "TR04",           "TR05",  // 1131-1135
        "TR06",           "TR07",           "TR08",           "TR09",           "TR10",  // 1136-1140
        "TR11",           "TR12",           "TR13",           "TR14",           "TR15",  // 1141-1145
        "TR16",           "TR17",           "TR18",           "TR19",           "TR20",  // 1146-1150
        "TR21",           "TR22",           "TR23",           "TR24",           "TR25",  // 1151-1155
        "TR26",           "TR27",           "TR28",           "TR29",           "TR30",  // 1156-1160
        "TR31",           "TR32",           "TR33",           "TR34",           "TR35",  // 1161-1165
        "TR36",           "TR37",           "TR38",           "TR39",           "TR40",  // 1166-1170
        "TR41",           "TR42",           "TR43",           "TR44",           "TR45",  // 1171-1175
        "TR46",           "TR47",           "TR48",           "TR49",           "TR50",  // 1176-1180
        "TR51",           "TR52",           "TR53",           "TR54",           "TR55",  // 1181-1185
        "TR56",           "TR57",           "TR58",           "TR59",           "TR60",  // 1186-1190
        "TR61",           "TR62",           "TR63",           "TR64",           "TR65",  // 1191-1195
        "TR66",           "TR67",           "TR68",           "TR69",           "TR70",  // 1196-1200
        "TR71",           "TR72",           "TR73",           "TR74",           "TR75",  // 1201-1205
        "TR76",           "TR77",           "TR78",           "TR79",           "TR80",  // 1206-1210
        "TR81",           "TR82",           "TR83",           "TR84",           "TR85",  // 1211-1215
        "TR86",           "TR87",           "TR88",           "TR89",           "TR90",  // 1216-1220
        "TR91",           "TR92",           "TR93",           "TR94",           "TR95",  // 1221-1225
        "TR96",           "TR97",           "TR98",           "TR99",           "TM00",  // 1226-1230
        "Lonely Mint",    "Adamant Mint",   "Naughty Mint",   "Brave Mint",     "Bold Mint",  // 1231-1235
        "Impish Mint",    "Lax Mint",       "Relaxed Mint",   "Modest Mint",    "Mild Mint",  // 1236-1240
        "Rash Mint",      "Quiet Mint",     "Calm Mint",      "Gentle Mint",    "Careful Mint",  // 1241-1245
        "Sassy Mint",     "Timid Mint",     "Hasty Mint",     "Jolly Mint",     "Naive Mint",  // 1246-1250
        "Serious Mint",   "Wishing Piece",  "Cracked Pot",    "Chipped Pot",    "Hi-tech Earbuds",  // 1251-1255
        "Fruit Bunch",    "Moomoo Cheese",  "Spice Mix",      "Fresh Cream",    "Packaged Curry",  // 1256-1260
        "Coconut Milk",   "Instant Noodles", "Precooked Burger", "Gigantamix",     "Wishing Chip",  // 1261-1265
        "Rotom Bike",     "Catching Charm", "???",            "Old Letter",     "Band Autograph",  // 1266-1270
        "Sonia's Book",   "???",            "???",            "???",            "???",  // 1271-1275
        "???",            "???",            "Rotom Catalog",  nullptr,          nullptr,  // 1276-1280
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1281-1285
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1286-1290
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1291-1295
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1296-1300
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1301-1305
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1306-1310
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1311-1315
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1316-1320
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1321-1325
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1326-1330
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1331-1335
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1336-1340
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1341-1345
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1346-1350
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1351-1355
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1356-1360
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1361-1365
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1366-1370
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1371-1375
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1376-1380
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1381-1385
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1386-1390
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1391-1395
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1396-1400
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1401-1405
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1406-1410
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1411-1415
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1416-1420
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1421-1425
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1426-1430
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1431-1435
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1436-1440
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1441-1445
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1446-1450
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1451-1455
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1456-1460
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1461-1465
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1466-1470
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1471-1475
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1476-1480
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1481-1485
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1486-1490
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1491-1495
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1496-1500
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1501-1505
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1506-1510
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1511-1515
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1516-1520
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1521-1525
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1526-1530
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1531-1535
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1536-1540
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1541-1545
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1546-1550
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1551-1555
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1556-1560
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1561-1565
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1566-1570
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1571-1575
        nullptr,          nullptr,          nullptr,          "Max Honey",      "Max Mushrooms",  // 1576-1580
        "Galarica Twig",  "Galarica Cuff",  "Style Card",     "Armor Pass",     "Rotom Bike",  // 1581-1585
        "Rotom Bike",     "Exp. Charm",     "Armorite Ore",   "Mark Charm",     "Reins of Unity",  // 1586-1590
        "Reins of Unity", "Galarica Wreath", "Legendary Clue 1", "Legendary Clue 2", "Legendary Clue 3",  // 1591-1595
        "Legendary Clue?", "Crown Pass",     "Wooden Crown",   "Radiant Petal",  "White Mane Hair",  // 1596-1600
        "Black Mane Hair", "Iceroot Carrot", "Shaderoot Carrot", "Dynite Ore",     "Carrot Seeds",  // 1601-1605
        "Ability Patch",  "Reins of Unity", "Time Balm",      "Space Balm",     "Mysterious Balm",  // 1606-1610
        "Linking Cord",   "Hometown Muffin", "Apricorn",       "Jubilife Muffin", "Aux Powerguard",  // 1611-1615
        "Dire Hit",       "Choice Dumpling", "Twice-Spiced Radish", "Swap Snack",     "Caster Fern",  // 1616-1620
        "Seed of Mastery", "Poke Ball",      "???",            "Eternal Ice",    "Uxie's Claw",  // 1621-1625
        "Azelf's Fang",   "Mesprit's Plume", "Tumblestone",    "Celestica Flute", "Remedy",  // 1626-1630
        "Fine Remedy",    "Dazzling Honey", "Hearty Grains",  "Plump Beans",    "Springy Mushroom",  // 1631-1635
        "Crunchy Salt",   "Wood",           "King's Leaf",    "Marsh Balm",     "Poke Ball",  // 1636-1640
        "Great Ball",     "Ultra Ball",     "Feather Ball",   "Pokeshi Doll",   "???",  // 1641-1645
        "Smoke Bomb",     "Scatter Bang",   "Sticky Glob",    "Star Piece",     "Mushroom Cake",  // 1646-1650
        "Bugwort",        "Honey Cake",     "Grain Cake",     "Bean Cake",      "Salt Cake",  // 1651-1655
        "Potion",         "Super Potion",   "Hyper Potion",   "Max Potion",     "Full Restore",  // 1656-1660
        "Remedy",         "Fine Remedy",    "Superb Remedy",  "Old Gateau",     "Jubilife Muffin",  // 1661-1665
        "Full Heal",      "Revive",         "Max Revive",     "Max Ether",      "Max Elixir",  // 1666-1670
        "Stealth Spray",  "???",            "Aux Power",      "Aux Guard",      "Dire Hit",  // 1671-1675
        "Aux Evasion",    "Aux Powerguard", "Forest Balm",    "Iron Chunk",     "???",  // 1676-1680
        "Black Tumblestone", "Sky Tumblestone", "???",            "Ball of Mud",    "???",  // 1681-1685
        "Pop Pod",        "Sootfoot Root",  "Spoiled Apricorn", "Snowball",       "Sticky Glob",  // 1686-1690
        "Black Augurite", "Peat Block",     "Stealth Spray",  "Medicinal Leek", "Vivichoke",  // 1691-1695
        "Pep-Up Plant",   "???",            "???",            "Tempting Charm B", "Tempting Charm P",  // 1696-1700
        "Swordcap",       "Iron Barktongue", "Doppel Bonnets", "Direshroom",     "Sand Radish",  // 1701-1705
        "Tempting Charm T", "Tempting Charm Y", "Candy Truffle",  "Cake-Lure Base", "Poke Ball",  // 1706-1710
        "Great Ball",     "Ultra Ball",     "Feather Ball",   "???",            "???",  // 1711-1715
        "Scatter Bang",   "Smoke Bomb",     "???",            "???",            "Pokeshi Doll",  // 1716-1720
        "Volcano Balm",   "Mountain Balm",  "Snow Balm",      "Honey Cake",     "Grain Cake",  // 1721-1725
        "Bean Cake",      "Mushroom Cake",  "Salt Cake",      "Swap Snack",     "Choice Dumpling",  // 1726-1730
        "Twice-Spiced Radish", "Survival Charm R", "Survival Charm B", "Survival Charm P", "Survival Charm T",  // 1731-1735
        "Survival Charm Y", "Torn Journal",   "Warding Charm R", "Warding Charm B", "Warding Charm P",  // 1736-1740
        "Warding Charm T", "Warding Charm Y", "Wall Fragment",  "Basculegion Food", "Old Journal",  // 1741-1745
        "Wing Ball",      "Jet Ball",       "Heavy Ball",     "Leaden Ball",    "Gigaton Ball",  // 1746-1750
        "Wing Ball",      "Jet Ball",       "Heavy Ball",     "Hopo Berry",     "Superb Remedy",  // 1751-1755
        "Aux Power",      "Aux Guard",      "Aux Evasion",    "Grit Dust",      "Grit Gravel",  // 1756-1760
        "Grit Pebble",    "Grit Rock",      "Secret Medicine", "Tempting Charm R", "Lost Satchel",  // 1761-1765
        "Lost Satchel",   "Lost Satchel",   "Lost Satchel",   "Lost Satchel",   "???",  // 1766-1770
        "Origin Ball",    "???",            "???",            "???",            "???",  // 1771-1775
        "Origin Ore",     "Adamant Crystal", "Lustrous Globe", "Griseous Core",  "Blank Plate",  // 1776-1780
        "???",            "Crafting Kit",   "Leaden Ball",    "Gigaton Ball",   "Strange Ball",  // 1781-1785
        "Pokedex",        "Old Verse 1",    "Old Verse 2",    "Old Verse 3",    "Old Verse 4",  // 1786-1790
        "???",            "Old Verse 5",    "Old Verse 6",    "Old Verse 7",    "Old Verse 8",  // 1791-1795
        "Old Verse 9",    "Old Verse 10",   "Old Verse 11",   "Old Verse 12",   "Old Verse 13",  // 1796-1800
        "Old Verse 14",   "Old Verse 15",   "Old Verse 16",   "Old Verse 17",   "Old Verse 18",  // 1801-1805
        "Old Verse 19",   "Old Verse 20",   "Mysterious Shard S", "Mysterious Shard L", "Digger Drill",  // 1806-1810
        "Kanto Slate",    "Johto Slate",    "Soul Slate",     "Rainbow Slate",  "Squall Slate",  // 1811-1815
        "Oceanic Slate",  "Tectonic Slate", "Stratospheric Slate", "Genome Slate",   "Discovery Slate",  // 1816-1820
        "Distortion Slate", "DS Sounds",      nullptr,          nullptr,          nullptr,  // 1821-1825
        nullptr,          nullptr,          "Legend Plate",   "Rotom Phone",    "Sandwich",  // 1826-1830
        "Koraidon's Poke Ball", "Miraidon's Poke Ball", "Tera Orb",       "Scarlet Book",   "Violet Book",  // 1831-1835
        "Kofu's Wallet",  nullptr,          nullptr,          nullptr,          nullptr,  // 1836-1840
        nullptr,          "Tiny Bamboo Shoot", "Big Bamboo Shoot", nullptr,          nullptr,  // 1841-1845
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1846-1850
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1851-1855
        nullptr,          "Scroll of Darkness", "Scroll of Waters", nullptr,          nullptr,  // 1856-1860
        "Malicious Armor", "Normal Tera Shard", "Fire Tera Shard", "Water Tera Shard", "Electric Tera Shard",  // 1861-1865
        "Grass Tera Shard", "Ice Tera Shard", "Fighting Tera Shard", "Poison Tera Shard", "Ground Tera Shard",  // 1866-1870
        "Flying Tera Shard", "Psychic Tera Shard", "Bug Tera Shard", "Rock Tera Shard", "Ghost Tera Shard",  // 1871-1875
        "Dragon Tera Shard", "Dark Tera Shard", "Steel Tera Shard", "Fairy Tera Shard", "Booster Energy",  // 1876-1880
        "Ability Shield", "Clear Amulet",   "Mirror Herb",    "Punching Glove", "Covert Cloak",  // 1881-1885
        "Loaded Dice",    nullptr,          "Baguette",       "Mayonnaise",     "Ketchup",  // 1886-1890
        "Mustard",        "Butter",         "Peanut Butter",  "Chili Sauce",    "Salt",  // 1891-1895
        "Pepper",         "Yogurt",         "Whipped Cream",  "Cream Cheese",   "Jam",  // 1896-1900
        "Marmalade",      "Olive Oil",      "Vinegar",        "Sweet Herba Mystica", "Salty Herba Mystica",  // 1901-1905
        "Sour Herba Mystica", "Bitter Herba Mystica", "Spicy Herba Mystica", "Lettuce",        "Tomato",  // 1906-1910
        "Cherry Tomatoes", "Cucumber",       "Pickle",         "Onion",          "Red Onion",  // 1911-1915
        "Green Bell Pepper", "Red Bell Pepper", "Yellow Bell Pepper", "Avocado",        "Bacon",  // 1916-1920
        "Ham",            "Prosciutto",     "Chorizo",        "Herbed Sausage", "Hamburger",  // 1921-1925
        "Klawf Stick",    "Smoked Fillet",  "Fried Fillet",   "Egg",            "Potato Tortilla",  // 1926-1930
        "Tofu",           "Rice",           "Noodles",        "Potato Salad",   "Cheese",  // 1931-1935
        "Banana",         "Strawberry",     "Apple",          "Kiwi",           "Pineapple",  // 1936-1940
        "Jalapeno",       "Horseradish",    "Curry Powder",   "Wasabi",         "Watercress",  // 1941-1945
        "Basil",          nullptr,          nullptr,          nullptr,          nullptr,  // 1946-1950
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 1951-1955
        "Venonat Fang",   "Diglett Dirt",   "Meowth Fur",     "Psyduck Down",   "Mankey Fur",  // 1956-1960
        "Growlithe Fur",  "Slowpoke Claw",  "Magnemite Screw", "Grimer Toxin",   "Shellder Pearl",  // 1961-1965
        "Gastly Gas",     "Drowzee Fur",    "Voltorb Sparks", "Scyther Claw",   "Tauros Hair",  // 1966-1970
        "Magikarp Scales", "Ditto Goo",      "Eevee Fur",      "Dratini Scales", "Pichu Fur",  // 1971-1975
        "Igglybuff Fluff", "Mareep Wool",    "Hoppip Leaf",    "Sunkern Leaf",   "Murkrow Bauble",  // 1976-1980
        "Misdreavus Tears", "Girafarig Fur",  "Pineco Husk",    "Dunsparce Scales", "Qwilfish Spines",  // 1981-1985
        "Heracross Claw", "Sneasel Claw",   "Teddiursa Claw", "Delibird Parcel", "Houndour Fang",  // 1986-1990
        "Phanpy Nail",    "Stantler Hair",  "Larvitar Claw",  "Wingull Feather", "Ralts Dust",  // 1991-1995
        "Surskit Syrup",  "Shroomish Spores", "Slakoth Fur",    "Makuhita Sweat", "Azurill Fur",  // 1996-2000
        "Sableye Gem",    "Meditite Sweat", "Gulpin Mucus",   "Numel Lava",     "Torkoal Coal",  // 2001-2005
        "Spoink Pearl",   "Cacnea Needle",  "Swablu Fluff",   "Zangoose Claw",  "Seviper Fang",  // 2006-2010
        "Barboach Slime", "Shuppet Scrap",  "Tropius Leaf",   "Snorunt Fur",    "Luvdisc Scales",  // 2011-2015
        "Bagon Scales",   "Starly Feather", "Kricketot Shell", "Shinx Fang",     "Combee Honey",  // 2016-2020
        "Pachirisu Fur",  "Buizel Fur",     "Shellos Mucus",  "Drifloon Gas",   "Stunky Fur",  // 2021-2025
        "Bronzor Fragment", "Bonsly Tears",   "Happiny Dust",   "Spiritomb Fragment", "Gible Scales",  // 2026-2030
        "Riolu Fur",      "Hippopotas Sand", "Croagunk Poison", "Finneon Scales", "Snover Berries",  // 2031-2035
        "Rotom Sparks",   "Petilil Leaf",   "Basculin Fang",  "Sandile Claw",   "Zorua Fur",  // 2036-2040
        "Gothita Eyelash", "Deerling Hair",  "Foongus Spores", "Alomomola Mucus", "Tynamo Slime",  // 2041-2045
        "Axew Scales",    "Cubchoo Fur",    "Cryogonal Ice",  "Pawniard Blade", "Rufflet Feather",  // 2046-2050
        "Deino Scales",   "Larvesta Fuzz",  "Fletchling Feather", "Scatterbug Powder", "Litleo Tuft",  // 2051-2055
        "Flabebe Pollen", "Skiddo Leaf",    "Skrelp Kelp",    "Clauncher Claw", "Hawlucha Down",  // 2056-2060
        "Dedenne Fur",    "Goomy Goo",      "Klefki Key",     "Bergmite Ice",   "Noibat Fur",  // 2061-2065
        "Yungoos Fur",    "Crabrawler Shell", "Oricorio Feather", "Rockruff Rock",  "Mareanie Spike",  // 2066-2070
        "Mudbray Mud",    "Fomantis Leaf",  "Salandit Gas",   "Bounsweet Sweat", "Oranguru Fur",  // 2071-2075
        "Passimian Fur",  "Sandygast Sand", "Komala Claw",    "Mimikyu Scrap",  "Bruxish Tooth",  // 2076-2080
        "Chewtle Claw",   "Skwovet Fur",    "Arrokuda Scales", "Rookidee Feather", "Toxel Sparks",  // 2081-2085
        "Falinks Sweat",  "Cufant Tarnish", "Rolycoly Coal",  "Silicobra Sand", "Indeedee Fur",  // 2086-2090
        "Pincurchin Spines", "Snom Thread",    "Impidimp Hair",  "Applin Juice",   "Sinistea Chip",  // 2091-2095
        "Hatenna Dust",   "Stonjourner Stone", "Eiscue Down",    "Dreepy Powder",  nullptr,  // 2096-2100
        nullptr,          nullptr,          "Lechonk Hair",   "Tarountula Thread", "Nymble Claw",  // 2101-2105
        "Rellor Mud",     "Greavard Wax",   "Flittle Down",   "Wiglett Sand",   "Dondozo Whisker",  // 2106-2110
        "Veluza Fillet",  "Finizen Mucus",  "Smoliv Oil",     "Capsakid Seed",  "Tadbulb Mucus",  // 2111-2115
        "Varoom Fume",    "Orthworm Tarnish", "Tandemaus Fur",  "Cetoddle Grease", "Frigibax Scales",  // 2116-2120
        "Tatsugiri Scales", "Cyclizar Scales", "Pawmi Fur",      nullptr,          nullptr,  // 2121-2125
        "Wattrel Feather", "Bombirdier Feather", "Squawkabilly Feather", "Flamigo Down",   "Klawf Claw",  // 2126-2130
        "Nacli Salt",     "Glimmet Crystal", "Shroodle Ink",   "Fidough Fur",    "Maschiff Fang",  // 2131-2135
        "Bramblin Twig",  "Gimmighoul Coin", nullptr,          nullptr,          nullptr,  // 2136-2140
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 2141-2145
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 2146-2150
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 2151-2155
        "Tinkatink Hair", "Charcadet Soot", "Toedscool Flaps", "Wooper Slime",   "TM100",  // 2156-2160
        "TM101",          "TM102",          "TM103",          "TM104",          "TM105",  // 2161-2165
        "TM106",          "TM107",          "TM108",          "TM109",          "TM110",  // 2166-2170
        "TM111",          "TM112",          "TM113",          "TM114",          "TM115",  // 2171-2175
        "TM116",          "TM117",          "TM118",          "TM119",          "TM120",  // 2176-2180
        "TM121",          "TM122",          "TM123",          "TM124",          "TM125",  // 2181-2185
        "TM126",          "TM127",          "TM128",          "TM129",          "TM130",  // 2186-2190
        "TM131",          "TM132",          "TM133",          "TM134",          "TM135",  // 2191-2195
        "TM136",          "TM137",          "TM138",          "TM139",          "TM140",  // 2196-2200
        "TM141",          "TM142",          "TM143",          "TM144",          "TM145",  // 2201-2205
        "TM146",          "TM147",          "TM148",          "TM149",          "TM150",  // 2206-2210
        "TM151",          "TM152",          "TM153",          "TM154",          "TM155",  // 2211-2215
        "TM156",          "TM157",          "TM158",          "TM159",          "TM160",  // 2216-2220
        "TM161",          "TM162",          "TM163",          "TM164",          "TM165",  // 2221-2225
        "TM166",          "TM167",          "TM168",          "TM169",          "TM170",  // 2226-2230
        "TM171",          "TM172",          "TM173",          "TM174",          "TM175",  // 2231-2235
        "TM176",          "TM177",          "TM178",          "TM179",          "TM180",  // 2236-2240
        "TM181",          "TM182",          "TM183",          "TM184",          "TM185",  // 2241-2245
        "TM186",          "TM187",          "TM188",          "TM189",          "TM190",  // 2246-2250
        "TM191",          "TM192",          "TM193",          "TM194",          "TM195",  // 2251-2255
        "TM196",          "TM197",          "TM198",          "TM199",          "TM200",  // 2256-2260
        "TM201",          "TM202",          "TM203",          "TM204",          "TM205",  // 2261-2265
        "TM206",          "TM207",          "TM208",          "TM209",          "TM210",  // 2266-2270
        "TM211",          "TM212",          "TM213",          "TM214",          "TM215",  // 2271-2275
        "TM216",          "TM217",          "TM218",          "TM219",          "TM220",  // 2276-2280
        "TM221",          "TM222",          "TM223",          "TM224",          "TM225",  // 2281-2285
        "TM226",          "TM227",          "TM228",          "TM229",          nullptr,  // 2286-2290
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 2291-2295
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 2296-2300
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 2301-2305
        nullptr,          nullptr,          nullptr,          nullptr,          nullptr,  // 2306-2310
        "Picnic Set",     nullptr,          "Academy Bottle", "Academy Bottle", "Polka-Dot Bottle",  // 2311-2315
        "Striped Bottle", "Diamond Bottle", "Academy Cup",    "Academy Cup",    "Striped Cup",  // 2316-2320
        "Polka-Dot Cup",  "Flower Pattern Cup", "Academy Tablecloth", "Academy Tablecloth", "Whimsical Tablecloth",  // 2321-2325
        "Leafy Tablecloth", "Spooky Tablecloth", nullptr,          "Academy Ball",   "Academy Ball",  // 2326-2330
        "Marill Ball",    "Yarn Ball",      "Cyber Ball",     "Gold Pick",      "Silver Pick",  // 2331-2335
        "Red-Flag Pick",  "Blue-Flag Pick", "Pika-Pika Pick", "Winking Pika Pick", "Vee-Vee Pick",  // 2336-2340
        "Smiling Vee Pick", "Blue Poke Ball Pick", nullptr,          "Auspicious Armor", "Leader's Crest",  // 2341-2345
        nullptr,          nullptr,          "Pink Bottle",    "Blue Bottle",    "Yellow Bottle",  // 2346-2350
        "Steel Bottle (R)", "Steel Bottle (Y)", "Steel Bottle (B)", "Silver Bottle",  "Barred Cup",  // 2351-2355
        "Diamond Pattern Cup", "Fire Pattern Cup", "Pink Cup",       "Blue Cup",       "Yellow Cup",  // 2356-2360
        "Pikachu Cup",    "Eevee Cup",      "Slowpoke Cup",   "Silver Cup",     "Exercise Ball",  // 2361-2365
        "Plaid Tablecloth (Y)", "Plaid Tablecloth (B)", "Plaid Tablecloth (R)", "B&W Grass Tablecloth", "Battle Tablecloth",  // 2366-2370
        "Monstrous Tablecloth", "Striped Tablecloth", "Diamond Tablecloth", "Polka-Dot Tablecloth", "Lilac Tablecloth",  // 2371-2375
        "Mint Tablecloth", "Peach Tablecloth", "Yellow Tablecloth", "Blue Tablecloth", "Pink Tablecloth",  // 2376-2380
        "Gold Bottle",    "Bronze Bottle",  "Gold Cup",       "Bronze Cup",     "Green Poke Ball Pick",  // 2381-2385
        "Red Poke Ball Pick", "Party Sparkler Pick", "Heroic Sword Pick", "Magical Star Pick", "Magical Heart Pick",  // 2386-2390
        "Parasol Pick",   "Blue-Sky Flower Pick", "Sunset Flower Pick", "Sunrise Flower Pick", "Blue Dish",  // 2391-2395
        "Green Dish",     "Orange Dish",    "Red Dish",       "White Dish",     "Yellow Dish",  // 2396-2400
        "Fairy Feather",  "Syrupy Apple",   "Unremarkable Teacup", "Masterpiece Teacup", "Teal Mask",  // 2401-2405
        "Cornerstone Mask", "Wellspring Mask", "Hearthflame Mask", "Teal Style Card", "Crystal Cluster",  // 2406-2410
        "Health Mochi",   "Muscle Mochi",   "Resist Mochi",   "Genius Mochi",   "Clever Mochi",  // 2411-2415
        "Swift Mochi",    "Simple Chairs",  "Academy Chairs", "Academy Chairs", "Whimsical Chairs",  // 2416-2420
        "Leafy Chairs",   "Spooky Chairs",  "Plaid Chairs (Y)", "Plaid Chairs (B)", "Plaid Chairs (R)",  // 2421-2425
        "B&W Grass Chairs", "Battle Chairs",  "Monstrous Chairs", "Striped Chairs", "Diamond Chairs",  // 2426-2430
        "Polka-Dot Chairs", "Lilac Chairs",   "Mint Chairs",    "Peach Chairs",   "Yellow Chairs",  // 2431-2435
        "Blue Chairs",    "Pink Chairs",    "Ekans Fang",     "Sandshrew Claw", "Cleffa Fur",  // 2436-2440
        "Vulpix Fur",     "Poliwag Slime",  "Bellsprout Vine", "Geodude Fragment", "Koffing Gas",  // 2441-2445
        "Munchlax Fang",  "Sentret Fur",    "Hoothoot Feather", "Spinarak Thread", "Aipom Hair",  // 2446-2450
        "Yanma Spike",    "Gligar Fang",    "Slugma Lava",    "Swinub Hair",    "Poochyena Fang",  // 2451-2455
        "Lotad Leaf",     "Seedot Stem",    "Nosepass Fragment", "Volbeat Fluid",  "Illumise Fluid",  // 2456-2460
        "Corphish Shell", "Feebas Scales",  "Duskull Fragment", "Chingling Fragment", "Timburr Sweat",  // 2461-2465
        "Sewaddle Leaf",  "Ducklett Feather", "Litwick Soot",   "Mienfoo Claw",   "Vullaby Feather",  // 2466-2470
        "Carbink Jewel",  "Phantump Twig",  "Grubbin Thread", "Cutiefly Powder", "Jangmo-o Scales",  // 2471-2475
        "Cramorant Down", "Morpeko Snack",  "Poltchageist Powder", "Fresh-Start Mochi", "Roto-Stick",  // 2476-2480
        "Glimmering Charm", "Metal Alloy",    "Indigo Style Card", "Oddish Leaf",    "Tentacool Stinger",  // 2481-2485
        "Doduo Down",     "Seel Fur",       "Exeggcute Shell", "Tyrogue Sweat",  "Rhyhorn Fang",  // 2486-2490
        "Horsea Ink",     "Elekid Fur",     "Magby Hair",     "Lapras Teardrop", "Porygon Fragment",  // 2491-2495
        "Chinchou Sparks", "Snubbull Hair",  "Skarmory Feather", "Smeargle Paint", "Plusle Fur",  // 2496-2500
        "Minun Fur",      "Trapinch Shell", "Beldum Claw",    "Cranidos Spike", "Shieldon Claw",  // 2501-2505
        "Blitzle Mane Hair", "Drilbur Claw",   "Cottonee Fluff", "Scraggy Sweat",  "Minccino Fur",  // 2506-2510
        "Solosis Gel",    "Joltik Thread",  "Golett Shard",   "Espurr Fur",     "Inkay Ink",  // 2511-2515
        "Pikipek Feather", "Dewpider Thread", "Comfey Flower",  "Minior Shell",   "Milcery Cream",  // 2516-2520
        "Duraludon Tarnish", "Articuno Treat", "Zapdos Treat",   "Moltres Treat",  "Raikou Treat",  // 2521-2525
        "Entei Treat",    "Suicune Treat",  "Lugia Treat",    "Ho-Oh Treat",    "Latias Treat",  // 2526-2530
        "Latios Treat",   "Kyogre Treat",   "Groudon Treat",  "Rayquaza Treat", "Cobalion Treat",  // 2531-2535
        "Terrakion Treat", "Virizion Treat", "Reshiram Treat", "Zekrom Treat",   "Kyurem Treat",  // 2536-2540
        "Solgaleo Treat", "Lunala Treat",   "Necrozma Treat", "Kubfu Treat",    "Glastrier Treat",  // 2541-2545
        "Spectrier Treat", "Indigo Disk",    "Fiery Pick",     "Stellar Tera Shard", "Mythical Pecha Berry",  // 2546-2550
        "Blueberry Tablecloth", "Blueberry Chairs", "Synchro Machine", "Meteorite",      "Scarlet Book",  // 2551-2555
        "Violet Book",    "Briar's Book",   "Seed of Mastery", "Clefablite",     "Victreebelite",  // 2556-2560
        "Starminite",     "Dragoninite",    "Meganiumite",    "Feraligite",     "Skarmorite",  // 2561-2565
        "Froslassite",    "Heatranite",     "Darkranite",     "Emboarite",      "Excadrite",  // 2566-2570
        "Scolipite",      "Scraftinite",    "Eelektrossite",  "Chandelurite",   "Chesnaughtite",  // 2571-2575
        "Delphoxite",     "Greninjite",     "Pyroarite",      "Floettite",      "Malamarite",  // 2576-2580
        "Barbaracite",    "Dragalgite",     "Hawluchanite",   "Zygardite",      "Drampanite",  // 2581-2585
        "Zeraorite",      "Falinksite",     "Key to Room 202", "Super Lumiose Galette", "Lab Key Card A",  // 2586-2590
        "Lab Key Card B", "Lab Key Card C", nullptr,          nullptr,          "Pebble",  // 2591-2595
        "Cherished Ring", "Autographed Plush", "Tasty Trash",    "Revitalizing Twig", "Lida's Things",  // 2596-2600
        "Lumiosian Butter", "Nice Butter",    "Great Butter",   "Amazing Butter", "Supreme Butter",  // 2601-2605
        "Hyperspace Butter", "Hoennian Salt",  "Epice Noire",    "Arboliva Oil",   "Popping Candy",  // 2606-2610
        "Important Letter", "Cherished Ring", "Dirty Scarf",    nullptr,          nullptr,  // 2611-2615
        nullptr,          nullptr,          "Mega Shard",     "Colorful Screw", "Red Canari Plush",  // 2616-2620
        "Red Canari Plush", "Red Canari Plush", "Gold Canari Plush", "Gold Canari Plush", "Gold Canari Plush",  // 2621-2625
        "Pink Canari Plush", "Pink Canari Plush", "Pink Canari Plush", "Green Canari Plush", "Green Canari Plush",  // 2626-2630
        "Green Canari Plush", "Blue Canari Plush", "Blue Canari Plush", "Blue Canari Plush", "Raichunite X",  // 2631-2635
        "Raichunite Y",   "Chimechite",     "Absolite Z",     "Staraptite",     "Garchompite Z",  // 2636-2640
        "Lucarionite Z",  "Golurkite",      "Meowsticite",    "Crabominite",    "Golisopite",  // 2641-2645
        "Magearnite",     "Scovillainite",  "Baxcalibrite",   "Tatsugirinite",  "Glimmoranite",  // 2646-2650
        "Hyper Cheri Berry", "Hyper Chesto Berry", "Hyper Pecha Berry", "Hyper Rawst Berry", "Hyper Aspear Berry",  // 2651-2655
        "Hyper Oran Berry", "Hyper Persim Berry", "Hyper Lum Berry", "Hyper Sitrus Berry", "Hyper Pomeg Berry",  // 2656-2660
        "Hyper Kelpsy Berry", "Hyper Qualot Berry", "Hyper Hondew Berry", "Hyper Grepa Berry", "Hyper Tamato Berry",  // 2661-2665
        "Hyper Occa Berry", "Hyper Passho Berry", "Hyper Wacan Berry", "Hyper Rindo Berry", "Hyper Yache Berry",  // 2666-2670
        "Hyper Chople Berry", "Hyper Kebia Berry", "Hyper Shuca Berry", "Hyper Coba Berry", "Hyper Payapa Berry",  // 2671-2675
        "Hyper Tanga Berry", "Hyper Charti Berry", "Hyper Kasib Berry", "Hyper Haban Berry", "Hyper Colbur Berry",  // 2676-2680
        "Hyper Babiri Berry", "Hyper Chilan Berry", "Hyper Roseli Berry", "Canari Bread"  // 2681-2684
    };

    // Total entries in the table (includes index-0 nullptr slot).
    // Highest valid item ID = kItemNamesFullCount - 1.
    static constexpr int kItemNamesFullCount =
        static_cast<int>(sizeof(kItemNamesFull) / sizeof(kItemNamesFull[0]));

    // Return the English item name for the given item ID, or nullptr for 0 /
    // out-of-range / unused-slot so the caller can fall back to "Item #NNN".
    // Bounds-checked against kItemNamesFullCount.
    inline const char *item_name(const uint16_t id) {
        if (id > 0 && id < kItemNamesFullCount) {
            return kItemNamesFull[id];
        }
        return nullptr;
    }

}
