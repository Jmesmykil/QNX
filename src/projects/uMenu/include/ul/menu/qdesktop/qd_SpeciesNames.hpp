// qd_SpeciesNames.hpp — full English National Dex species-name lookup.
//
// PURPOSE: give the W13-SAVE-PARSER save editor real species names
// (e.g. "Garchomp") instead of "Species #445" for every Pokemon up to the
// current National Dex maximum.  The save editor previously shipped only a
// 151-entry Gen-1 fallback table inline in qd_SaveEditorLayout.cpp; this
// header supersedes that with the complete Gen 1-9 list.
//
// SOURCE: public National Pokedex (Gen 1-9).  Species names are public,
// factual reference data (a numbered list of names).  They are reproduced
// here as plain ASCII strings.  No third-party code was copied; this is a
// clean-room data table authored by hand.
//
// ASCII NOTE: the on-device font has no CJK or accented glyphs, so every
// name is plain 7-bit ASCII.  Names that canonically use special characters
// are transliterated to ASCII:
//   - Gender symbols  -> "Nidoran-F" / "Nidoran-M"
//   - Apostrophes     -> kept as ASCII '  ("Farfetch'd", "Sirfetch'd")
//   - Accents dropped -> "Flabebe" (no accents)
//   - Periods kept    -> "Mr. Mime", "Mime Jr.", "Mr. Rime"
//   - Colon kept      -> "Type: Null"
//   - Hyphens kept    -> "Ho-Oh", "Porygon-Z", "Jangmo-o", "Kommo-o"
//
// FORMS: base species name only — regional / battle / cosmetic forms are
// intentionally not represented (the dex number maps to the base species).
//
// Self-contained, header-only.  Only <cstdint> is required.
// Compiles clean with -std=c++20 -Wall -Wextra.

#pragma once

#include <cstdint>

namespace ul::menu::qdesktop {

    // National Dex names indexed by dex number.
    //   index 0       = nullptr (invalid / "no species")
    //   index 1       = "Bulbasaur"
    //   ...
    //   index 1025    = "Pecharunt" (current National Dex maximum, Gen 9)
    static constexpr const char *kSpeciesNamesFull[] = {
        nullptr,            // 0 = invalid
        // --- Generation 1 (Kanto): 1-151 ---
        "Bulbasaur",   "Ivysaur",     "Venusaur",    "Charmander",  "Charmeleon",  // 1-5
        "Charizard",   "Squirtle",    "Wartortle",   "Blastoise",   "Caterpie",    // 6-10
        "Metapod",     "Butterfree",  "Weedle",      "Kakuna",      "Beedrill",    // 11-15
        "Pidgey",      "Pidgeotto",   "Pidgeot",     "Rattata",     "Raticate",    // 16-20
        "Spearow",     "Fearow",      "Ekans",       "Arbok",       "Pikachu",     // 21-25
        "Raichu",      "Sandshrew",   "Sandslash",   "Nidoran-F",   "Nidorina",    // 26-30
        "Nidoqueen",   "Nidoran-M",   "Nidorino",    "Nidoking",    "Clefairy",    // 31-35
        "Clefable",    "Vulpix",      "Ninetales",   "Jigglypuff",  "Wigglytuff",  // 36-40
        "Zubat",       "Golbat",      "Oddish",      "Gloom",       "Vileplume",   // 41-45
        "Paras",       "Parasect",    "Venonat",     "Venomoth",    "Diglett",     // 46-50
        "Dugtrio",     "Meowth",      "Persian",     "Psyduck",     "Golduck",     // 51-55
        "Mankey",      "Primeape",    "Growlithe",   "Arcanine",    "Poliwag",     // 56-60
        "Poliwhirl",   "Poliwrath",   "Abra",        "Kadabra",     "Alakazam",    // 61-65
        "Machop",      "Machoke",     "Machamp",     "Bellsprout",  "Weepinbell",  // 66-70
        "Victreebel",  "Tentacool",   "Tentacruel",  "Geodude",     "Graveler",    // 71-75
        "Golem",       "Ponyta",      "Rapidash",    "Slowpoke",    "Slowbro",     // 76-80
        "Magnemite",   "Magneton",    "Farfetch'd",  "Doduo",       "Dodrio",      // 81-85
        "Seel",        "Dewgong",     "Grimer",      "Muk",         "Shellder",    // 86-90
        "Cloyster",    "Gastly",      "Haunter",     "Gengar",      "Onix",        // 91-95
        "Drowzee",     "Hypno",       "Krabby",      "Kingler",     "Voltorb",     // 96-100
        "Electrode",   "Exeggcute",   "Exeggutor",   "Cubone",      "Marowak",     // 101-105
        "Hitmonlee",   "Hitmonchan",  "Lickitung",   "Koffing",     "Weezing",     // 106-110
        "Rhyhorn",     "Rhydon",      "Chansey",     "Tangela",     "Kangaskhan",  // 111-115
        "Horsea",      "Seadra",      "Goldeen",     "Seaking",     "Staryu",      // 116-120
        "Starmie",     "Mr. Mime",    "Scyther",     "Jynx",        "Electabuzz",  // 121-125
        "Magmar",      "Pinsir",      "Tauros",      "Magikarp",    "Gyarados",    // 126-130
        "Lapras",      "Ditto",       "Eevee",       "Vaporeon",    "Jolteon",     // 131-135
        "Flareon",     "Porygon",     "Omanyte",     "Omastar",     "Kabuto",      // 136-140
        "Kabutops",    "Aerodactyl",  "Snorlax",     "Articuno",    "Zapdos",      // 141-145
        "Moltres",     "Dratini",     "Dragonair",   "Dragonite",   "Mewtwo",      // 146-150
        "Mew",                                                                     // 151
        // --- Generation 2 (Johto): 152-251 ---
        "Chikorita",   "Bayleef",     "Meganium",    "Cyndaquil",   "Quilava",     // 152-156
        "Typhlosion",  "Totodile",    "Croconaw",    "Feraligatr",  "Sentret",     // 157-161
        "Furret",      "Hoothoot",    "Noctowl",     "Ledyba",      "Ledian",      // 162-166
        "Spinarak",    "Ariados",     "Crobat",      "Chinchou",    "Lanturn",     // 167-171
        "Pichu",       "Cleffa",      "Igglybuff",   "Togepi",      "Togetic",     // 172-176
        "Natu",        "Xatu",        "Mareep",      "Flaaffy",     "Ampharos",    // 177-181
        "Bellossom",   "Marill",      "Azumarill",   "Sudowoodo",   "Politoed",    // 182-186
        "Hoppip",      "Skiploom",    "Jumpluff",    "Aipom",       "Sunkern",     // 187-191
        "Sunflora",    "Yanma",       "Wooper",      "Quagsire",    "Espeon",      // 192-196
        "Umbreon",     "Murkrow",     "Slowking",    "Misdreavus",  "Unown",       // 197-201
        "Wobbuffet",   "Girafarig",   "Pineco",      "Forretress",  "Dunsparce",   // 202-206
        "Gligar",      "Steelix",     "Snubbull",    "Granbull",    "Qwilfish",    // 207-211
        "Scizor",      "Shuckle",     "Heracross",   "Sneasel",     "Teddiursa",   // 212-216
        "Ursaring",    "Slugma",      "Magcargo",    "Swinub",      "Piloswine",   // 217-221
        "Corsola",     "Remoraid",    "Octillery",   "Delibird",    "Mantine",     // 222-226
        "Skarmory",    "Houndour",    "Houndoom",    "Kingdra",     "Phanpy",      // 227-231
        "Donphan",     "Porygon2",    "Stantler",    "Smeargle",    "Tyrogue",     // 232-236
        "Hitmontop",   "Smoochum",    "Elekid",      "Magby",       "Miltank",     // 237-241
        "Blissey",     "Raikou",      "Entei",       "Suicune",     "Larvitar",    // 242-246
        "Pupitar",     "Tyranitar",   "Lugia",       "Ho-Oh",       "Celebi",      // 247-251
        // --- Generation 3 (Hoenn): 252-386 ---
        "Treecko",     "Grovyle",     "Sceptile",    "Torchic",     "Combusken",   // 252-256
        "Blaziken",    "Mudkip",      "Marshtomp",   "Swampert",    "Poochyena",   // 257-261
        "Mightyena",   "Zigzagoon",   "Linoone",     "Wurmple",     "Silcoon",     // 262-266
        "Beautifly",   "Cascoon",     "Dustox",      "Lotad",       "Lombre",      // 267-271
        "Ludicolo",    "Seedot",      "Nuzleaf",     "Shiftry",     "Taillow",     // 272-276
        "Swellow",     "Wingull",     "Pelipper",    "Ralts",       "Kirlia",      // 277-281
        "Gardevoir",   "Surskit",     "Masquerain",  "Shroomish",   "Breloom",     // 282-286
        "Slakoth",     "Vigoroth",    "Slaking",     "Nincada",     "Ninjask",     // 287-291
        "Shedinja",    "Whismur",     "Loudred",     "Exploud",     "Makuhita",    // 292-296
        "Hariyama",    "Azurill",     "Nosepass",    "Skitty",      "Delcatty",    // 297-301
        "Sableye",     "Mawile",      "Aron",        "Lairon",      "Aggron",      // 302-306
        "Meditite",    "Medicham",    "Electrike",   "Manectric",   "Plusle",      // 307-311
        "Minun",       "Volbeat",     "Illumise",    "Roselia",     "Gulpin",      // 312-316
        "Swalot",      "Carvanha",    "Sharpedo",    "Wailmer",     "Wailord",     // 317-321
        "Numel",       "Camerupt",    "Torkoal",     "Spoink",      "Grumpig",     // 322-326
        "Spinda",      "Trapinch",    "Vibrava",     "Flygon",      "Cacnea",      // 327-331
        "Cacturne",    "Swablu",      "Altaria",     "Zangoose",    "Seviper",     // 332-336
        "Lunatone",    "Solrock",     "Barboach",    "Whiscash",    "Corphish",    // 337-341
        "Crawdaunt",   "Baltoy",      "Claydol",     "Lileep",      "Cradily",     // 342-346
        "Anorith",     "Armaldo",     "Feebas",      "Milotic",     "Castform",    // 347-351
        "Kecleon",     "Shuppet",     "Banette",     "Duskull",     "Dusclops",    // 352-356
        "Tropius",     "Chimecho",    "Absol",       "Wynaut",      "Snorunt",     // 357-361
        "Glalie",      "Spheal",      "Sealeo",      "Walrein",     "Clamperl",    // 362-366
        "Huntail",     "Gorebyss",    "Relicanth",   "Luvdisc",     "Bagon",       // 367-371
        "Shelgon",     "Salamence",   "Beldum",      "Metang",      "Metagross",   // 372-376
        "Regirock",    "Regice",      "Registeel",   "Latias",      "Latios",      // 377-381
        "Kyogre",      "Groudon",     "Rayquaza",    "Jirachi",     "Deoxys",      // 382-386
        // --- Generation 4 (Sinnoh): 387-493 ---
        "Turtwig",     "Grotle",      "Torterra",    "Chimchar",    "Monferno",    // 387-391
        "Infernape",   "Piplup",      "Prinplup",    "Empoleon",    "Starly",      // 392-396
        "Staravia",    "Staraptor",   "Bidoof",      "Bibarel",     "Kricketot",   // 397-401
        "Kricketune",  "Shinx",       "Luxio",       "Luxray",      "Budew",       // 402-406
        "Roserade",    "Cranidos",    "Rampardos",   "Shieldon",    "Bastiodon",   // 407-411
        "Burmy",       "Wormadam",    "Mothim",      "Combee",      "Vespiquen",   // 412-416
        "Pachirisu",   "Buizel",      "Floatzel",    "Cherubi",     "Cherrim",     // 417-421
        "Shellos",     "Gastrodon",   "Ambipom",     "Drifloon",    "Drifblim",    // 422-426
        "Buneary",     "Lopunny",     "Mismagius",   "Honchkrow",   "Glameow",     // 427-431
        "Purugly",     "Chingling",   "Stunky",      "Skuntank",    "Bronzor",     // 432-436
        "Bronzong",    "Bonsly",      "Mime Jr.",    "Happiny",     "Chatot",      // 437-441
        "Spiritomb",   "Gible",       "Gabite",      "Garchomp",    "Munchlax",    // 442-446
        "Riolu",       "Lucario",     "Hippopotas",  "Hippowdon",   "Skorupi",     // 447-451
        "Drapion",     "Croagunk",    "Toxicroak",   "Carnivine",   "Finneon",     // 452-456
        "Lumineon",    "Mantyke",     "Snover",      "Abomasnow",   "Weavile",     // 457-461
        "Magnezone",   "Lickilicky",  "Rhyperior",   "Tangrowth",   "Electivire",  // 462-466
        "Magmortar",   "Togekiss",    "Yanmega",     "Leafeon",     "Glaceon",     // 467-471
        "Gliscor",     "Mamoswine",   "Porygon-Z",   "Gallade",     "Probopass",   // 472-476
        "Dusknoir",    "Froslass",    "Rotom",       "Uxie",        "Mesprit",     // 477-481
        "Azelf",       "Dialga",      "Palkia",      "Heatran",     "Regigigas",   // 482-486
        "Giratina",    "Cresselia",   "Phione",      "Manaphy",     "Darkrai",     // 487-491
        "Shaymin",     "Arceus",                                                   // 492-493
        // --- Generation 5 (Unova): 494-649 ---
        "Victini",     "Snivy",       "Servine",     "Serperior",   "Tepig",       // 494-498
        "Pignite",     "Emboar",      "Oshawott",    "Dewott",      "Samurott",    // 499-503
        "Patrat",      "Watchog",     "Lillipup",    "Herdier",     "Stoutland",   // 504-508
        "Purrloin",    "Liepard",     "Pansage",     "Simisage",    "Pansear",     // 509-513
        "Simisear",    "Panpour",     "Simipour",    "Munna",       "Musharna",    // 514-518
        "Pidove",      "Tranquill",   "Unfezant",    "Blitzle",     "Zebstrika",   // 519-523
        "Roggenrola",  "Boldore",     "Gigalith",    "Woobat",      "Swoobat",     // 524-528
        "Drilbur",     "Excadrill",   "Audino",      "Timburr",     "Gurdurr",     // 529-533
        "Conkeldurr",  "Tympole",     "Palpitoad",   "Seismitoad",  "Throh",       // 534-538
        "Sawk",        "Sewaddle",    "Swadloon",    "Leavanny",    "Venipede",    // 539-543
        "Whirlipede",  "Scolipede",   "Cottonee",    "Whimsicott",  "Petilil",     // 544-548
        "Lilligant",   "Basculin",    "Sandile",     "Krokorok",    "Krookodile",  // 549-553
        "Darumaka",    "Darmanitan",  "Maractus",    "Dwebble",     "Crustle",     // 554-558
        "Scraggy",     "Scrafty",     "Sigilyph",    "Yamask",      "Cofagrigus",  // 559-563
        "Tirtouga",    "Carracosta",  "Archen",      "Archeops",    "Trubbish",    // 564-568
        "Garbodor",    "Zorua",       "Zoroark",     "Minccino",    "Cinccino",    // 569-573
        "Gothita",     "Gothorita",   "Gothitelle",  "Solosis",     "Duosion",     // 574-578
        "Reuniclus",   "Ducklett",    "Swanna",      "Vanillite",   "Vanillish",   // 579-583
        "Vanilluxe",   "Deerling",    "Sawsbuck",    "Emolga",      "Karrablast",  // 584-588
        "Escavalier",  "Foongus",     "Amoonguss",   "Frillish",    "Jellicent",   // 589-593
        "Alomomola",   "Joltik",      "Galvantula",  "Ferroseed",   "Ferrothorn",  // 594-598
        "Klink",       "Klang",       "Klinklang",   "Tynamo",      "Eelektrik",   // 599-603
        "Eelektross",  "Elgyem",      "Beheeyem",    "Litwick",     "Lampent",     // 604-608
        "Chandelure",  "Axew",        "Fraxure",     "Haxorus",     "Cubchoo",     // 609-613
        "Beartic",     "Cryogonal",   "Shelmet",     "Accelgor",    "Stunfisk",    // 614-618
        "Mienfoo",     "Mienshao",    "Druddigon",   "Golett",      "Golurk",      // 619-623
        "Pawniard",    "Bisharp",     "Bouffalant",  "Rufflet",     "Braviary",    // 624-628
        "Vullaby",     "Mandibuzz",   "Heatmor",     "Durant",      "Deino",       // 629-633
        "Zweilous",    "Hydreigon",   "Larvesta",    "Volcarona",   "Cobalion",    // 634-638
        "Terrakion",   "Virizion",    "Tornadus",    "Thundurus",   "Reshiram",    // 639-643
        "Zekrom",      "Landorus",    "Kyurem",      "Keldeo",      "Meloetta",    // 644-648
        "Genesect",                                                                // 649
        // --- Generation 6 (Kalos): 650-721 ---
        "Chespin",     "Quilladin",   "Chesnaught",  "Fennekin",    "Braixen",     // 650-654
        "Delphox",     "Froakie",     "Frogadier",   "Greninja",    "Bunnelby",    // 655-659
        "Diggersby",   "Fletchling",  "Fletchinder", "Talonflame",  "Scatterbug",  // 660-664
        "Spewpa",      "Vivillon",    "Litleo",      "Pyroar",      "Flabebe",     // 665-669
        "Floette",     "Florges",     "Skiddo",      "Gogoat",      "Pancham",     // 670-674
        "Pangoro",     "Furfrou",     "Espurr",      "Meowstic",    "Honedge",     // 675-679
        "Doublade",    "Aegislash",   "Spritzee",    "Aromatisse",  "Swirlix",     // 680-684
        "Slurpuff",    "Inkay",       "Malamar",     "Binacle",     "Barbaracle",  // 685-689
        "Skrelp",      "Dragalge",    "Clauncher",   "Clawitzer",   "Helioptile",  // 690-694
        "Heliolisk",   "Tyrunt",      "Tyrantrum",   "Amaura",      "Aurorus",     // 695-699
        "Sylveon",     "Hawlucha",    "Dedenne",     "Carbink",     "Goomy",       // 700-704
        "Sliggoo",     "Goodra",      "Klefki",      "Phantump",    "Trevenant",   // 705-709
        "Pumpkaboo",   "Gourgeist",   "Bergmite",    "Avalugg",     "Noibat",      // 710-714
        "Noivern",     "Xerneas",     "Yveltal",     "Zygarde",     "Diancie",     // 715-719
        "Hoopa",       "Volcanion",                                                // 720-721
        // --- Generation 7 (Alola): 722-809 ---
        "Rowlet",      "Dartrix",     "Decidueye",   "Litten",      "Torracat",    // 722-726
        "Incineroar",  "Popplio",     "Brionne",     "Primarina",   "Pikipek",     // 727-731
        "Trumbeak",    "Toucannon",   "Yungoos",     "Gumshoos",    "Grubbin",     // 732-736
        "Charjabug",   "Vikavolt",    "Crabrawler",  "Crabominable","Oricorio",    // 737-741
        "Cutiefly",    "Ribombee",    "Rockruff",    "Lycanroc",    "Wishiwashi",  // 742-746
        "Mareanie",    "Toxapex",     "Mudbray",     "Mudsdale",    "Dewpider",    // 747-751
        "Araquanid",   "Fomantis",    "Lurantis",    "Morelull",    "Shiinotic",   // 752-756
        "Salandit",    "Salazzle",    "Stufful",     "Bewear",      "Bounsweet",   // 757-761
        "Steenee",     "Tsareena",    "Comfey",      "Oranguru",    "Passimian",   // 762-766
        "Wimpod",      "Golisopod",   "Sandygast",   "Palossand",   "Pyukumuku",   // 767-771
        "Type: Null",  "Silvally",    "Minior",      "Komala",      "Turtonator",  // 772-776
        "Togedemaru",  "Mimikyu",     "Bruxish",     "Drampa",      "Dhelmise",    // 777-781
        "Jangmo-o",    "Hakamo-o",    "Kommo-o",     "Tapu Koko",   "Tapu Lele",   // 782-786
        "Tapu Bulu",   "Tapu Fini",   "Cosmog",      "Cosmoem",     "Solgaleo",    // 787-791
        "Lunala",      "Nihilego",    "Buzzwole",    "Pheromosa",   "Xurkitree",   // 792-796
        "Celesteela",  "Kartana",     "Guzzlord",    "Necrozma",    "Magearna",    // 797-801
        "Marshadow",   "Poipole",     "Naganadel",   "Stakataka",   "Blacephalon", // 802-806
        "Zeraora",     "Meltan",      "Melmetal",                                  // 807-809
        // --- Generation 8 (Galar / Hisui): 810-905 ---
        "Grookey",     "Thwackey",    "Rillaboom",   "Scorbunny",   "Raboot",      // 810-814
        "Cinderace",   "Sobble",      "Drizzile",    "Inteleon",    "Skwovet",     // 815-819
        "Greedent",    "Rookidee",    "Corvisquire", "Corviknight", "Blipbug",     // 820-824
        "Dottler",     "Orbeetle",    "Nickit",      "Thievul",     "Gossifleur",  // 825-829
        "Eldegoss",    "Wooloo",      "Dubwool",     "Chewtle",     "Drednaw",     // 830-834
        "Yamper",      "Boltund",     "Rolycoly",    "Carkol",      "Coalossal",   // 835-839
        "Applin",      "Flapple",     "Appletun",    "Silicobra",   "Sandaconda",  // 840-844
        "Cramorant",   "Arrokuda",    "Barraskewda", "Toxel",       "Toxtricity",  // 845-849
        "Sizzlipede",  "Centiskorch", "Clobbopus",   "Grapploct",   "Sinistea",    // 850-854
        "Polteageist", "Hatenna",     "Hattrem",     "Hatterene",   "Impidimp",    // 855-859
        "Morgrem",     "Grimmsnarl",  "Obstagoon",   "Perrserker",  "Cursola",     // 860-864
        "Sirfetch'd",  "Mr. Rime",    "Runerigus",   "Milcery",     "Alcremie",    // 865-869
        "Falinks",     "Pincurchin",  "Snom",        "Frosmoth",    "Stonjourner", // 870-874
        "Eiscue",      "Indeedee",    "Morpeko",     "Cufant",      "Copperajah",  // 875-879
        "Dracozolt",   "Arctozolt",   "Dracovish",   "Arctovish",   "Duraludon",   // 880-884
        "Dreepy",      "Drakloak",    "Dragapult",   "Zacian",      "Zamazenta",   // 885-889
        "Eternatus",   "Kubfu",       "Urshifu",     "Zarude",      "Regieleki",   // 890-894
        "Regidrago",   "Glastrier",   "Spectrier",   "Calyrex",     "Wyrdeer",     // 895-899
        "Kleavor",     "Ursaluna",    "Basculegion", "Sneasler",    "Overqwil",    // 900-904
        "Enamorus",                                                                // 905
        // --- Generation 9 (Paldea / Kitakami / Blueberry): 906-1025 ---
        "Sprigatito",  "Floragato",   "Meowscarada", "Fuecoco",     "Crocalor",    // 906-910
        "Skeledirge",  "Quaxly",      "Quaxwell",    "Quaquaval",   "Lechonk",     // 911-915
        "Oinkologne",  "Tarountula",  "Spidops",     "Nymble",      "Lokix",       // 916-920
        "Pawmi",       "Pawmo",       "Pawmot",      "Tandemaus",   "Maushold",    // 921-925
        "Fidough",     "Dachsbun",    "Smoliv",      "Dolliv",      "Arboliva",    // 926-930
        "Squawkabilly","Nacli",       "Naclstack",   "Garganacl",   "Charcadet",   // 931-935
        "Armarouge",   "Ceruledge",   "Tadbulb",     "Bellibolt",   "Wattrel",     // 936-940
        "Kilowattrel", "Maschiff",    "Mabosstiff",  "Shroodle",    "Grafaiai",    // 941-945
        "Bramblin",    "Brambleghast","Toedscool",   "Toedscruel",  "Klawf",       // 946-950
        "Capsakid",    "Scovillain",  "Rellor",      "Rabsca",      "Flittle",     // 951-955
        "Espathra",    "Tinkatink",   "Tinkatuff",   "Tinkaton",    "Wiglett",     // 956-960
        "Wugtrio",     "Bombirdier",  "Finizen",     "Palafin",     "Varoom",      // 961-965
        "Revavroom",   "Cyclizar",    "Orthworm",    "Glimmet",     "Glimmora",    // 966-970
        "Greavard",    "Houndstone",  "Flamigo",     "Cetoddle",    "Cetitan",     // 971-975
        "Veluza",      "Dondozo",     "Tatsugiri",   "Annihilape",  "Clodsire",    // 976-980
        "Farigiraf",   "Dudunsparce", "Kingambit",   "Great Tusk",  "Scream Tail", // 981-985
        "Brute Bonnet","Flutter Mane","Slither Wing","Sandy Shocks","Iron Treads", // 986-990
        "Iron Bundle", "Iron Hands",  "Iron Jugulis","Iron Moth",   "Iron Thorns", // 991-995
        "Frigibax",    "Arctibax",    "Baxcalibur",  "Gimmighoul",  "Gholdengo",   // 996-1000
        "Wo-Chien",    "Chien-Pao",   "Ting-Lu",     "Chi-Yu",      "Roaring Moon",// 1001-1005
        "Iron Valiant","Koraidon",    "Miraidon",    "Walking Wake","Iron Leaves", // 1006-1010
        "Dipplin",     "Poltchageist","Sinistcha",   "Okidogi",     "Munkidori",   // 1011-1015
        "Fezandipiti", "Ogerpon",     "Archaludon",  "Hydrapple",   "Gouging Fire",// 1016-1020
        "Raging Bolt", "Iron Boulder","Iron Crown",  "Terapagos",   "Pecharunt",   // 1021-1025
    };

    // Total entries in the table (includes index-0 nullptr slot).
    // Highest valid dex number = kSpeciesNamesFullCount - 1.
    static constexpr int kSpeciesNamesFullCount =
        static_cast<int>(sizeof(kSpeciesNamesFull) / sizeof(kSpeciesNamesFull[0]));

    // Return the English National Dex name for the given dex number, or
    // nullptr for 0 / out-of-range so the caller can fall back to
    // "Species #NNN".  Bounds-checked against kSpeciesNamesFullCount.
    inline const char *species_name_full(const uint16_t dex) {
        if (dex > 0 && dex < kSpeciesNamesFullCount) {
            return kSpeciesNamesFull[dex];
        }
        return nullptr;
    }

}
