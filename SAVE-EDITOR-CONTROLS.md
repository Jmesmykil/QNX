# Q OS Save Editor — Controls & Observability (v3.6.5)

Read-only Pokémon save **viewer** (no write-back yet). Game support: **BDSP**
(Brilliant Diamond / Shining Pearl) fully; **SwSh** parser staged but not shipped
(needs a SwSh save to verify); other games show "no parser".

## Opening
Desktop → **Save Editor**. You land on the **Title Picker**.

## Title Picker (pick a game)
| Button | Action |
|--------|--------|
| ↑ / ↓ | move between games |
| **A** | open the selected game's newest SD save |
| **Y** | rescan the SD for saves |
| **X** | back up the focused game's save (JKSV-style) |
| **ZL** | restore the latest backup |
| **B** | close the editor → desktop |

## Inside a save — 4 tabs: `Party/Box · Items · Trainer · Back`
| Button | Action |
|--------|--------|
| **L / R** | **switch tabs (opens the tab directly)** |
| **B** | back to the Title Picker |

### Party/Box tab
| Button | Action |
|--------|--------|
| ↑ ↓ ← → | move between the 6 party slots (2×3 grid) |
| **A** | open the highlighted Pokémon's **full detail** |
| **Y** | switch to the **Box** view |

Detail view shows: species · level · gender · nature · shiny · ability · held
item · OT · TID/SID · PID · all 6 IVs · moves (real names). **B or A** = back.

### Box view (after Y in Party/Box)
| Button | Action |
|--------|--------|
| ↑ ↓ | select a box (collapsed list, counts shown) |
| **A** | open the selected box |
| (in a box) ↑ ↓ | select a slot · **A** detail · **B** collapse |
| **Y / B** | back to the party grid |

### Items tab
| Button | Action |
|--------|--------|
| ↑ ↓ | scroll the bag (item names + counts) |

### Trainer tab
View-only: OT name · Trainer ID · Secret ID · money · gender.

### Back tab
**A** = return to the Title Picker.

## Observability (log_uMenu.log on the SD)
Added 2026-06-13 so every session is fully traceable:
- **Tab switches** → `save-editor: TAB x->y mode=N save_loaded=… box_supported=… bag=…`
- **Data snapshot on every load** → `save-editor SNAPSHOT[BDSP] OT=… TID=… party=… bag=…`
  followed by one line per party slot (`party[i] sp=… lv=… shiny=… held=… nat=… abil=… ivs=…`)
  and one line per bag item (`bag id=… x…`).
- **Box view open** → per-occupied-slot dump (`box{n} slot{m} sp=… lv=…`).
- **A-press / detail open** → logged with the species.

To pull it: bring UMS up, then read `/Volumes/SWITCH SD/ulaunch/log_uMenu.log`.
