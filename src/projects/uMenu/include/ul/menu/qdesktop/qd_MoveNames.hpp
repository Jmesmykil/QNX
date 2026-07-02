// qd_MoveNames.hpp — full English move-name lookup.
//
// PURPOSE: give the W13-SAVE-PARSER save editor real move names
// (e.g. "Thunderbolt") instead of "Move #85" for every move up to the
// current maximum move ID.  Companion to qd_SpeciesNames.hpp; the save
// viewer reads a Pokemon's four move IDs and resolves each to a name.
//
// SOURCE: public move list (Gen 1-9).  Move names are public, factual
// reference data (a numbered list of names).  They are reproduced here as
// plain ASCII strings.  No third-party code was copied; this is a
// clean-room data table authored by hand.  Indices match the in-game move
// IDs (move ID 1 = "Pound", 33 = "Tackle", 85 = "Thunderbolt").
//
// ASCII NOTE: the on-device font has no CJK or accented glyphs, so every
// name is plain 7-bit ASCII.  Names that canonically use special
// characters are transliterated to ASCII:
//   - Typographic apostrophe -> ASCII '  ("King's Shield", "Forest's Curse",
//                                          "Land's Wrath", "Nature's Madness",
//                                          "Let's Snuggle Forever")
//   - Hyphens kept    -> "Double-Edge", "U-turn", "X-Scissor", "Freeze-Dry"
//   - Periods kept    -> (none in the current list)
//   - Colon kept      -> (none in the current list)
//   - Digits/commas kept -> "10,000,000 Volt Thunderbolt",
//                           "Soul-Stealing 7-Star Strike"
//
// No move name in the current list contains accents, gender symbols, or
// any other non-ASCII glyph; the only non-ASCII source character is the
// typographic apostrophe handled above.
//
// Self-contained, header-only.  Only <cstdint> is required.
// Compiles clean with -std=c++20 -Wall -Wextra.

#pragma once

#include <cstdint>

namespace ul::menu::qdesktop {

    // Move names indexed by in-game move ID.
    //   index 0    = nullptr (invalid / "no move" — the empty move slot)
    //   index 1    = "Pound"
    //   ...
    //   index 920  = "Nihil Light" (current maximum move ID)
    static constexpr const char *kMoveNamesFull[] = {
        nullptr,            // 0 = invalid / empty move slot
        // --- Generation 1: 1-165 ---
        "Pound",       "Karate Chop", "Double Slap", "Comet Punch", "Mega Punch",  // 1-5
        "Pay Day",     "Fire Punch",  "Ice Punch",   "Thunder Punch","Scratch",  // 6-10
        "Vise Grip",   "Guillotine",  "Razor Wind",  "Swords Dance","Cut",  // 11-15
        "Gust",        "Wing Attack", "Whirlwind",   "Fly",         "Bind",  // 16-20
        "Slam",        "Vine Whip",   "Stomp",       "Double Kick", "Mega Kick",  // 21-25
        "Jump Kick",   "Rolling Kick","Sand Attack", "Headbutt",    "Horn Attack",  // 26-30
        "Fury Attack", "Horn Drill",  "Tackle",      "Body Slam",   "Wrap",  // 31-35
        "Take Down",   "Thrash",      "Double-Edge", "Tail Whip",   "Poison Sting",  // 36-40
        "Twineedle",   "Pin Missile", "Leer",        "Bite",        "Growl",  // 41-45
        "Roar",        "Sing",        "Supersonic",  "Sonic Boom",  "Disable",  // 46-50
        "Acid",        "Ember",       "Flamethrower","Mist",        "Water Gun",  // 51-55
        "Hydro Pump",  "Surf",        "Ice Beam",    "Blizzard",    "Psybeam",  // 56-60
        "Bubble Beam", "Aurora Beam", "Hyper Beam",  "Peck",        "Drill Peck",  // 61-65
        "Submission",  "Low Kick",    "Counter",     "Seismic Toss","Strength",  // 66-70
        "Absorb",      "Mega Drain",  "Leech Seed",  "Growth",      "Razor Leaf",  // 71-75
        "Solar Beam",  "Poison Powder","Stun Spore",  "Sleep Powder","Petal Dance",  // 76-80
        "String Shot", "Dragon Rage", "Fire Spin",   "Thunder Shock","Thunderbolt",  // 81-85
        "Thunder Wave","Thunder",     "Rock Throw",  "Earthquake",  "Fissure",  // 86-90
        "Dig",         "Toxic",       "Confusion",   "Psychic",     "Hypnosis",  // 91-95
        "Meditate",    "Agility",     "Quick Attack","Rage",        "Teleport",  // 96-100
        "Night Shade", "Mimic",       "Screech",     "Double Team", "Recover",  // 101-105
        "Harden",      "Minimize",    "Smokescreen", "Confuse Ray", "Withdraw",  // 106-110
        "Defense Curl","Barrier",     "Light Screen","Haze",        "Reflect",  // 111-115
        "Focus Energy","Bide",        "Metronome",   "Mirror Move", "Self-Destruct",  // 116-120
        "Egg Bomb",    "Lick",        "Smog",        "Sludge",      "Bone Club",  // 121-125
        "Fire Blast",  "Waterfall",   "Clamp",       "Swift",       "Skull Bash",  // 126-130
        "Spike Cannon","Constrict",   "Amnesia",     "Kinesis",     "Soft-Boiled",  // 131-135
        "High Jump Kick","Glare",       "Dream Eater", "Poison Gas",  "Barrage",  // 136-140
        "Leech Life",  "Lovely Kiss", "Sky Attack",  "Transform",   "Bubble",  // 141-145
        "Dizzy Punch", "Spore",       "Flash",       "Psywave",     "Splash",  // 146-150
        "Acid Armor",  "Crabhammer",  "Explosion",   "Fury Swipes", "Bonemerang",  // 151-155
        "Rest",        "Rock Slide",  "Hyper Fang",  "Sharpen",     "Conversion",  // 156-160
        "Tri Attack",  "Super Fang",  "Slash",       "Substitute",  "Struggle",  // 161-165
        // --- Generation 2: 166-251 ---
        "Sketch",      "Triple Kick", "Thief",       "Spider Web",  "Mind Reader",  // 166-170
        "Nightmare",   "Flame Wheel", "Snore",       "Curse",       "Flail",  // 171-175
        "Conversion 2","Aeroblast",   "Cotton Spore","Reversal",    "Spite",  // 176-180
        "Powder Snow", "Protect",     "Mach Punch",  "Scary Face",  "Feint Attack",  // 181-185
        "Sweet Kiss",  "Belly Drum",  "Sludge Bomb", "Mud-Slap",    "Octazooka",  // 186-190
        "Spikes",      "Zap Cannon",  "Foresight",   "Destiny Bond","Perish Song",  // 191-195
        "Icy Wind",    "Detect",      "Bone Rush",   "Lock-On",     "Outrage",  // 196-200
        "Sandstorm",   "Giga Drain",  "Endure",      "Charm",       "Rollout",  // 201-205
        "False Swipe", "Swagger",     "Milk Drink",  "Spark",       "Fury Cutter",  // 206-210
        "Steel Wing",  "Mean Look",   "Attract",     "Sleep Talk",  "Heal Bell",  // 211-215
        "Return",      "Present",     "Frustration", "Safeguard",   "Pain Split",  // 216-220
        "Sacred Fire", "Magnitude",   "Dynamic Punch","Megahorn",    "Dragon Breath",  // 221-225
        "Baton Pass",  "Encore",      "Pursuit",     "Rapid Spin",  "Sweet Scent",  // 226-230
        "Iron Tail",   "Metal Claw",  "Vital Throw", "Morning Sun", "Synthesis",  // 231-235
        "Moonlight",   "Hidden Power","Cross Chop",  "Twister",     "Rain Dance",  // 236-240
        "Sunny Day",   "Crunch",      "Mirror Coat", "Psych Up",    "Extreme Speed",  // 241-245
        "Ancient Power","Shadow Ball", "Future Sight","Rock Smash",  "Whirlpool",  // 246-250
        "Beat Up",                                                                  // 251
        // --- Generation 3: 252-354 ---
        "Fake Out",    "Uproar",      "Stockpile",   "Spit Up",     "Swallow",  // 252-256
        "Heat Wave",   "Hail",        "Torment",     "Flatter",     "Will-O-Wisp",  // 257-261
        "Memento",     "Facade",      "Focus Punch", "Smelling Salts","Follow Me",  // 262-266
        "Nature Power","Charge",      "Taunt",       "Helping Hand","Trick",  // 267-271
        "Role Play",   "Wish",        "Assist",      "Ingrain",     "Superpower",  // 272-276
        "Magic Coat",  "Recycle",     "Revenge",     "Brick Break", "Yawn",  // 277-281
        "Knock Off",   "Endeavor",    "Eruption",    "Skill Swap",  "Imprison",  // 282-286
        "Refresh",     "Grudge",      "Snatch",      "Secret Power","Dive",  // 287-291
        "Arm Thrust",  "Camouflage",  "Tail Glow",   "Luster Purge","Mist Ball",  // 292-296
        "Feather Dance","Teeter Dance","Blaze Kick",  "Mud Sport",   "Ice Ball",  // 297-301
        "Needle Arm",  "Slack Off",   "Hyper Voice", "Poison Fang", "Crush Claw",  // 302-306
        "Blast Burn",  "Hydro Cannon","Meteor Mash", "Astonish",    "Weather Ball",  // 307-311
        "Aromatherapy","Fake Tears",  "Air Cutter",  "Overheat",    "Odor Sleuth",  // 312-316
        "Rock Tomb",   "Silver Wind", "Metal Sound", "Grass Whistle","Tickle",  // 317-321
        "Cosmic Power","Water Spout", "Signal Beam", "Shadow Punch","Extrasensory",  // 322-326
        "Sky Uppercut","Sand Tomb",   "Sheer Cold",  "Muddy Water", "Bullet Seed",  // 327-331
        "Aerial Ace",  "Icicle Spear","Iron Defense","Block",       "Howl",  // 332-336
        "Dragon Claw", "Frenzy Plant","Bulk Up",     "Bounce",      "Mud Shot",  // 337-341
        "Poison Tail", "Covet",       "Volt Tackle", "Magical Leaf","Water Sport",  // 342-346
        "Calm Mind",   "Leaf Blade",  "Dragon Dance","Rock Blast",  "Shock Wave",  // 347-351
        "Water Pulse", "Doom Desire", "Psycho Boost",                              // 352-354
        // --- Generation 4: 355-467 ---
        "Roost",       "Gravity",     "Miracle Eye", "Wake-Up Slap","Hammer Arm",  // 355-359
        "Gyro Ball",   "Healing Wish","Brine",       "Natural Gift","Feint",  // 360-364
        "Pluck",       "Tailwind",    "Acupressure", "Metal Burst", "U-turn",  // 365-369
        "Close Combat","Payback",     "Assurance",   "Embargo",     "Fling",  // 370-374
        "Psycho Shift","Trump Card",  "Heal Block",  "Wring Out",   "Power Trick",  // 375-379
        "Gastro Acid", "Lucky Chant", "Me First",    "Copycat",     "Power Swap",  // 380-384
        "Guard Swap",  "Punishment",  "Last Resort", "Worry Seed",  "Sucker Punch",  // 385-389
        "Toxic Spikes","Heart Swap",  "Aqua Ring",   "Magnet Rise", "Flare Blitz",  // 390-394
        "Force Palm",  "Aura Sphere", "Rock Polish", "Poison Jab",  "Dark Pulse",  // 395-399
        "Night Slash", "Aqua Tail",   "Seed Bomb",   "Air Slash",   "X-Scissor",  // 400-404
        "Bug Buzz",    "Dragon Pulse","Dragon Rush", "Power Gem",   "Drain Punch",  // 405-409
        "Vacuum Wave", "Focus Blast", "Energy Ball", "Brave Bird",  "Earth Power",  // 410-414
        "Switcheroo",  "Giga Impact", "Nasty Plot",  "Bullet Punch","Avalanche",  // 415-419
        "Ice Shard",   "Shadow Claw", "Thunder Fang","Ice Fang",    "Fire Fang",  // 420-424
        "Shadow Sneak","Mud Bomb",    "Psycho Cut",  "Zen Headbutt","Mirror Shot",  // 425-429
        "Flash Cannon","Rock Climb",  "Defog",       "Trick Room",  "Draco Meteor",  // 430-434
        "Discharge",   "Lava Plume",  "Leaf Storm",  "Power Whip",  "Rock Wrecker",  // 435-439
        "Cross Poison","Gunk Shot",   "Iron Head",   "Magnet Bomb", "Stone Edge",  // 440-444
        "Captivate",   "Stealth Rock","Grass Knot",  "Chatter",     "Judgment",  // 445-449
        "Bug Bite",    "Charge Beam", "Wood Hammer", "Aqua Jet",    "Attack Order",  // 450-454
        "Defend Order","Heal Order",  "Head Smash",  "Double Hit",  "Roar of Time",  // 455-459
        "Spacial Rend","Lunar Dance", "Crush Grip",  "Magma Storm", "Dark Void",  // 460-464
        "Seed Flare",  "Ominous Wind","Shadow Force",                              // 465-467
        // --- Generation 5: 468-559 ---
        "Hone Claws",  "Wide Guard",  "Guard Split", "Power Split", "Wonder Room",  // 468-472
        "Psyshock",    "Venoshock",   "Autotomize",  "Rage Powder", "Telekinesis",  // 473-477
        "Magic Room",  "Smack Down",  "Storm Throw", "Flame Burst", "Sludge Wave",  // 478-482
        "Quiver Dance","Heavy Slam",  "Synchronoise","Electro Ball","Soak",  // 483-487
        "Flame Charge","Coil",        "Low Sweep",   "Acid Spray",  "Foul Play",  // 488-492
        "Simple Beam", "Entrainment", "After You",   "Round",       "Echoed Voice",  // 493-497
        "Chip Away",   "Clear Smog",  "Stored Power","Quick Guard", "Ally Switch",  // 498-502
        "Scald",       "Shell Smash", "Heal Pulse",  "Hex",         "Sky Drop",  // 503-507
        "Shift Gear",  "Circle Throw","Incinerate",  "Quash",       "Acrobatics",  // 508-512
        "Reflect Type","Retaliate",   "Final Gambit","Bestow",      "Inferno",  // 513-517
        "Water Pledge","Fire Pledge", "Grass Pledge","Volt Switch", "Struggle Bug",  // 518-522
        "Bulldoze",    "Frost Breath","Dragon Tail", "Work Up",     "Electroweb",  // 523-527
        "Wild Charge", "Drill Run",   "Dual Chop",   "Heart Stamp", "Horn Leech",  // 528-532
        "Sacred Sword","Razor Shell", "Heat Crash",  "Leaf Tornado","Steamroller",  // 533-537
        "Cotton Guard","Night Daze",  "Psystrike",   "Tail Slap",   "Hurricane",  // 538-542
        "Head Charge", "Gear Grind",  "Searing Shot","Techno Blast","Relic Song",  // 543-547
        "Secret Sword","Glaciate",    "Bolt Strike", "Blue Flare",  "Fiery Dance",  // 548-552
        "Freeze Shock","Ice Burn",    "Snarl",       "Icicle Crash","V-create",  // 553-557
        "Fusion Flare","Fusion Bolt",                                              // 558-559
        // --- Generation 6: 560-621 ---
        "Flying Press","Mat Block",   "Belch",       "Rototiller",  "Sticky Web",  // 560-564
        "Fell Stinger","Phantom Force","Trick-or-Treat","Noble Roar",  "Ion Deluge",  // 565-569
        "Parabolic Charge","Forest's Curse","Petal Blizzard","Freeze-Dry",  "Disarming Voice",  // 570-574
        "Parting Shot","Topsy-Turvy", "Draining Kiss","Crafty Shield","Flower Shield",  // 575-579
        "Grassy Terrain","Misty Terrain","Electrify",   "Play Rough",  "Fairy Wind",  // 580-584
        "Moonblast",   "Boomburst",   "Fairy Lock",  "King's Shield","Play Nice",  // 585-589
        "Confide",     "Diamond Storm","Steam Eruption","Hyperspace Hole","Water Shuriken",  // 590-594
        "Mystical Fire","Spiky Shield","Aromatic Mist","Eerie Impulse","Venom Drench",  // 595-599
        "Powder",      "Geomancy",    "Magnetic Flux","Happy Hour",  "Electric Terrain",  // 600-604
        "Dazzling Gleam","Celebrate",   "Hold Hands",  "Baby-Doll Eyes","Nuzzle",  // 605-609
        "Hold Back",   "Infestation", "Power-Up Punch","Oblivion Wing","Thousand Arrows",  // 610-614
        "Thousand Waves","Land's Wrath","Light of Ruin","Origin Pulse","Precipice Blades",  // 615-619
        "Dragon Ascent","Hyperspace Fury",                                          // 620-621
        // --- Generation 7: 622-742 ---
        "Breakneck Blitz","Breakneck Blitz","All-Out Pummeling","All-Out Pummeling","Supersonic Skystrike",  // 622-626
        "Supersonic Skystrike","Acid Downpour","Acid Downpour","Tectonic Rage","Tectonic Rage",  // 627-631
        "Continental Crush","Continental Crush","Savage Spin-Out","Savage Spin-Out","Never-Ending Nightmare",  // 632-636
        "Never-Ending Nightmare","Corkscrew Crash","Corkscrew Crash","Inferno Overdrive","Inferno Overdrive",  // 637-641
        "Hydro Vortex","Hydro Vortex","Bloom Doom",  "Bloom Doom",  "Gigavolt Havoc",  // 642-646
        "Gigavolt Havoc","Shattered Psyche","Shattered Psyche","Subzero Slammer","Subzero Slammer",  // 647-651
        "Devastating Drake","Devastating Drake","Black Hole Eclipse","Black Hole Eclipse","Twinkle Tackle",  // 652-656
        "Twinkle Tackle","Catastropika","Shore Up",    "First Impression","Baneful Bunker",  // 657-661
        "Spirit Shackle","Darkest Lariat","Sparkling Aria","Ice Hammer",  "Floral Healing",  // 662-666
        "High Horsepower","Strength Sap","Solar Blade", "Leafage",     "Spotlight",  // 667-671
        "Toxic Thread","Laser Focus", "Gear Up",     "Throat Chop", "Pollen Puff",  // 672-676
        "Anchor Shot", "Psychic Terrain","Lunge",       "Fire Lash",   "Power Trip",  // 677-681
        "Burn Up",     "Speed Swap",  "Smart Strike","Purify",      "Revelation Dance",  // 682-686
        "Core Enforcer","Trop Kick",   "Instruct",    "Beak Blast",  "Clanging Scales",  // 687-691
        "Dragon Hammer","Brutal Swing","Aurora Veil", "Sinister Arrow Raid","Malicious Moonsault",  // 692-696
        "Oceanic Operetta","Guardian of Alola","Soul-Stealing 7-Star Strike","Stoked Sparksurfer","Pulverizing Pancake",  // 697-701
        "Extreme Evoboost","Genesis Supernova","Shell Trap",  "Fleur Cannon","Psychic Fangs",  // 702-706
        "Stomping Tantrum","Shadow Bone", "Accelerock",  "Liquidation", "Prismatic Laser",  // 707-711
        "Spectral Thief","Sunsteel Strike","Moongeist Beam","Tearful Look","Zing Zap",  // 712-716
        "Nature's Madness","Multi-Attack","10,000,000 Volt Thunderbolt","Mind Blown","Plasma Fists",  // 717-721
        "Photon Geyser","Light That Burns the Sky","Searing Sunraze Smash","Menacing Moonraze Maelstrom","Let's Snuggle Forever",  // 722-726
        "Splintered Stormshards","Clangorous Soulblaze","Zippy Zap",   "Splishy Splash","Floaty Fall",  // 727-731
        "Pika Papow",  "Bouncy Bubble","Buzzy Buzz",  "Sizzly Slide","Glitzy Glow",  // 732-736
        "Baddy Bad",   "Sappy Seed",  "Freezy Frost","Sparkly Swirl","Veevee Volley",  // 737-741
        "Double Iron Bash",                                                        // 742
        // --- Generation 8: 743-826 ---
        "Max Guard",   "Dynamax Cannon","Snipe Shot",  "Jaw Lock",    "Stuff Cheeks",  // 743-747
        "No Retreat",  "Tar Shot",    "Magic Powder","Dragon Darts","Teatime",  // 748-752
        "Octolock",    "Bolt Beak",   "Fishious Rend","Court Change","Max Flare",  // 753-757
        "Max Flutterby","Max Lightning","Max Strike",  "Max Knuckle", "Max Phantasm",  // 758-762
        "Max Hailstorm","Max Ooze",    "Max Geyser",  "Max Airstream","Max Starfall",  // 763-767
        "Max Wyrmwind","Max Mindstorm","Max Rockfall","Max Quake",   "Max Darkness",  // 768-772
        "Max Overgrowth","Max Steelspike","Clangorous Soul","Body Press",  "Decorate",  // 773-777
        "Drum Beating","Snap Trap",   "Pyro Ball",   "Behemoth Blade","Behemoth Bash",  // 778-782
        "Aura Wheel",  "Breaking Swipe","Branch Poke", "Overdrive",   "Apple Acid",  // 783-787
        "Grav Apple",  "Spirit Break","Strange Steam","Life Dew",    "Obstruct",  // 788-792
        "False Surrender","Meteor Assault","Eternabeam",  "Steel Beam",  "Expanding Force",  // 793-797
        "Steel Roller","Scale Shot",  "Meteor Beam", "Shell Side Arm","Misty Explosion",  // 798-802
        "Grassy Glide","Rising Voltage","Terrain Pulse","Skitter Smack","Burning Jealousy",  // 803-807
        "Lash Out",    "Poltergeist", "Corrosive Gas","Coaching",    "Flip Turn",  // 808-812
        "Triple Axel", "Dual Wingbeat","Scorching Sands","Jungle Healing","Wicked Blow",  // 813-817
        "Surging Strikes","Thunder Cage","Dragon Energy","Freezing Glare","Fiery Wrath",  // 818-822
        "Thunderous Kick","Glacial Lance","Astral Barrage","Eerie Spell",                // 823-826
        // --- Generation 9: 827-920 ---
        "Dire Claw",   "Psyshield Bash","Power Shift", "Stone Axe",   "Springtide Storm",  // 827-831
        "Mystical Power","Raging Fury", "Wave Crash",  "Chloroblast", "Mountain Gale",  // 832-836
        "Victory Dance","Headlong Rush","Barb Barrage","Esper Wing",  "Bitter Malice",  // 837-841
        "Shelter",     "Triple Arrows","Infernal Parade","Ceaseless Edge","Bleakwind Storm",  // 842-846
        "Wildbolt Storm","Sandsear Storm","Lunar Blessing","Take Heart","Tera Blast",  // 847-851
        "Silk Trap",   "Axe Kick",    "Last Respects","Lumina Crash","Order Up",  // 852-856
        "Jet Punch",   "Spicy Extract","Spin Out",    "Population Bomb","Ice Spinner",  // 857-861
        "Glaive Rush", "Revival Blessing","Salt Cure",   "Triple Dive", "Mortal Spin",  // 862-866
        "Doodle",      "Fillet Away", "Kowtow Cleave","Flower Trick","Torch Song",  // 867-871
        "Aqua Step",   "Raging Bull", "Make It Rain","Psyblade",    "Hydro Steam",  // 872-876
        "Ruination",   "Collision Course","Electro Drift","Shed Tail",   "Chilly Reception",  // 877-881
        "Tidy Up",     "Snowscape",   "Pounce",      "Trailblaze",  "Chilling Water",  // 882-886
        "Hyper Drill", "Twin Beam",   "Rage Fist",   "Armor Cannon","Bitter Blade",  // 887-891
        "Double Shock","Gigaton Hammer","Comeuppance", "Aqua Cutter", "Blazing Torque",  // 892-896
        "Wicked Torque","Noxious Torque","Combat Torque","Magical Torque","Blood Moon",  // 897-901
        "Matcha Gotcha","Syrup Bomb",  "Ivy Cudgel",  "Electro Shot","Tera Starstorm",  // 902-906
        "Fickle Beam", "Burning Bulwark","Thunderclap", "Mighty Cleave","Tachyon Cutter",  // 907-911
        "Hard Press",  "Dragon Cheer","Alluring Voice","Temper Flare","Supercell Slam",  // 912-916
        "Psychic Noise","Upper Hand",  "Malignant Chain","Nihil Light",                  // 917-920
    };

    // Total entries in the table (includes index-0 nullptr slot).
    // Highest valid move ID = kMoveNamesFullCount - 1.
    static constexpr int kMoveNamesFullCount =
        static_cast<int>(sizeof(kMoveNamesFull) / sizeof(kMoveNamesFull[0]));

    // Return the English name for the given move ID, or nullptr for 0 /
    // out-of-range so the caller can fall back to "Move #NNN".
    // Bounds-checked against kMoveNamesFullCount.
    inline const char *move_name(const uint16_t id) {
        if (id > 0 && id < kMoveNamesFullCount) {
            return kMoveNamesFull[id];
        }
        return nullptr;
    }

}
