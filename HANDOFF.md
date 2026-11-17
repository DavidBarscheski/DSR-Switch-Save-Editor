# Handoff notes (read this first in a new session)

Project: Nintendo Switch homebrew (libnx/devkitPro) save editor for Dark Souls Remastered
(title 01004AB00A260000). Edits stats, inventory quantities, adds items, multi-character saves.

Read these files in order: README.md, research/SAVE_FORMAT.md (everything learned about the save
format), include/save.h, source/save.cpp, source/main.cpp, tests/test_core.cpp.

State: all host tests pass. Switch build not compiled in the cloud sandbox (no libnx); the user builds it.
Build the tests: g++ -std=c++17 -O1 -Iinclude -Isource tests/test_core.cpp source/save.cpp source/md5.cpp -o tests/test_core
Regenerate the icon: python3 -I tools/make_icon.py
Regenerate item tables: python3 -I research/gen_addable.py  (reads research/param_items.csv)

Done: Stats, Items, Add screens; key items (slots 0-63) and ammo; multi-character picker;
Debug screen was REMOVED (its items were placeholders); Elite Cleric + Mage Smith armor are hidden
behind ZL+ZR on the Add screen (research/gen_addable.py CUT_ARMOR -> source/cut_data.inc).
Hardware-verified by the owner: everything except key items and the cut armor (those stay labelled Experimental).

Open: the "broken item" icon on equipped added items could not be reproduced by the user (closed
unless it returns). Ideas, each to confirm first: custom items using the spare ids/slots, +N upgraded
gear, Estus Flask. Researching a possible GitHub release: add LICENSE, credit Paramdex (soulsmods),
add an online-ban warning, never commit game dumps or saves.

Key facts: BND4 save, entry data offset = u32 at 0x40+0x20*i+0x10; MD5 over next 0x60000 bytes;
inventory header 0x370/0x374 counters, capacity 0x800 at 0x378, slots at 0x37C stride 28;
handle = (sortKey<<12)|slot, sortKey = sortId (goods, rings) or sortId*100 (weapons, armor).
Param extraction: XCI -> NCA -> RomFS -> dvdbnd1.bhd5 -> gameparam.parambnd.dcx -> BND3 -> .param
(research/extract_params.py, paramlib.py). The user owns the cartridge and dumped it themselves.

Icon: icon.jpg (256x256) is drawn by tools/make_icon.py and picked up by APP_ICON in the Makefile.

Nov 16 review fixes (see CODE_REVIEW.md F01-F13): done F01-F08 (persistence moved to
include/persist.h + source/persist.cpp behind IFileSystem with a fake-FS test suite), F04, F05 (no-character
files are not opened), F06, F07, F09 (test Makefile tracks headers and data), F10 (.gitignore), F11
(research/paramlib.py dummy bitfields; research/test_paramlib.py), F12. Run tests with: make -C tests
(always runs). Still open: F13 doc cleanup (SAVE_FORMAT.md history vs current spec), the Add-screen
selection clamp after ZL+ZR hides cut items, file chooser when several DRAKS*.sl2 exist, sanitizer run, CI.
Switch build still not compiled in the cloud sandbox; main.cpp was only syntax-checked against a stub header.

Icons (Nov 16): the user wants game icons for a PERSONAL, LOCAL icon pack only (never committed or released).
tools/scan_archives.py surveys dvdbnd*.bhd5/.bdt (run on the user's PC; writes archive_index.csv with names/sizes only).
Next: read the CSV, find the item-icon TPFs, then write an exporter (TPF -> DDS -> PNG) the user runs locally.
Unknown: whether the Switch textures are PC-style DDS inside TPF (the survey's platform/format fields will say).

Icons, Nov 16 (done, tested on real data): the item icons are DS1R Switch TPF textures (platform 4, NVN block-linear
BC3) in dvdbnd1 entries 505a064c and c09f3eef. tools/tpf_switch.py decodes them; tools/export_icons.py writes
<type>_<id>.png for every item (all 708 addable items have an icon). Grid: 12 cols x 164x184 px cells; sheet start ids:
Icon00 0, Icon01 132, Icon10 1000, Icon11 1132, Icon12 1264, Icon20 2000, Icon21 2132, Icon30 3000, Icon40 4000.
research/param_items.csv now has an iconId column. The icons are the game's artwork: LOCAL USE ONLY, never in the repo
or a release (.gitignore has icons/, icons_out/). The GUI must fall back to original glyphs when the pack is missing.

## GUI (v1.0.0: built and run on hardware; controls, rounded corners and add amounts confirmed by the owner)
- The console text UI was replaced by an SDL2 GUI. Layers: `include/gfx.h` (drawing interface), `source/ui.cpp`
  (all screens, platform-independent, tested with a recording backend), `source/catalog.cpp` (Add list: categories,
  search, scrolling, Owned/Staged status), `source/gfx_sdl.cpp` (Switch SDL2 backend: system font via `pl`, icons from
  `sdmc:/switch/DSRSaveEditor/icons/<tag8hex>_<id>.png`, texture caches), `source/main.cpp` (flow, input, system keyboard).
- The last hardware-tested console version is v0.2.0 (the GitHub release): keep it as the fallback.
- Verified on hardware: build and link against libnx + SDL2, system font, swkbd search, layout, adding and saving.
- Controls (see README table): ZL/ZR switch tabs, L/R = category (Add) and ±1000 (Stats), hold-to-repeat/accelerate in `include/input.h`, sticks work, Add screen amount via Left/Right (resets to 1 per item), right stick = page/category, R3 = cut armor, L3 = search. Not yet run on hardware.
- Icons are the game's artwork: never commit or release them (`.gitignore` covers icons folders). A public release may
  want original category glyphs instead.
- Preview tooling: `DSR_DUMP=<dir> make -C tests` then `python3 -I tools/render_dump.py <dir> <out> [icons] [font]`.
