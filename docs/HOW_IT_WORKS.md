# How the editor works

Technical details for people who want to know exactly what the app reads and writes. The short version is in the
[README](../README.md); the raw save-format notes are in [research/SAVE_FORMAT.md](../research/SAVE_FORMAT.md).

## What it edits, in detail

Souls, Level, Vitality, Attunement, Endurance, Strength, Dexterity, Resistance, Intelligence,
Faith, HP, Max HP, Stamina, Max Stamina, for the character you pick. If a save file holds more than one character (some do), the app lists them with level and souls and asks which to edit; the first one is the game's slot 1.

The offsets and checksum scheme come from the community EdiZon config/script for this game
(marked "beta, first slot only" upstream).

**Items screen (press `ZR`):** change the quantity (1-99, capped at each item's own stack limit) of goods stacks you **already own**:
materials such as Titanite and Humanity, consumables and so on. Names and IDs come from the game's own parameter tables (originally from a
community item-ID spreadsheet, checked against a real save). The inventory layout was worked out from real saves: a header (slot size 0x1C,
2048 slots) followed by a table of slots, each holding a type tag, item id and quantity. This screen only changes that quantity; new
entries are created by the Add screen. It offers no items at all if the inventory header doesn't look exactly as expected.

**Add screen (press `ZR` twice).** Creates a new inventory entry for any regular item
you don't own: 217 goods (materials, consumables, 72 spells, 45 key items such as embers,
Lordvessel and Master Key), 41 rings, 232 armor pieces and 210 weapons/shields including 14 kinds of
ammunition (x999). Everything is the +0 version; upgraded or infused (+N) gear is not offered. Goods
stack to the game's own limit, spells and the rest are x1. The list scrolls and has category tabs (L/R) and a name search (press the left stick); X adds,
Y undoes an add made in this session. Anything you already own is left alone. KEY ITEMS can skip story
progress, so only add them if you know what they do. Not offered: Estus Flask (its upgrade path changes the
item's id), gestures and cut or unused rows (the unobtainable Elite Cleric and Mage Smith armor is hidden: click the right stick on the Add screen to show those 8 pieces at the end of the list, and again to hide them). Inventory space is limited, so a very large batch will stop
with "no free slot".

Every item's menu sort key, stack limit and durability now come from the game's own parameter tables
(`GameParam`, read from a cartridge dump): the sort key stored in an entry's handle is the item's
`sortId` (goods and rings) or `sortId x 100` (weapons and armor). This was checked against 1,700+
entries in six real Switch saves; the only exceptions are items the game created by transforming
another one (infusions, boss-soul weapons, Estus upgrades), which keep the SOURCE item's handle. So
weapons made from a boss soul, such as Greatsword of Artorias, are added with the source weapon's key.
Key items are stored in slots 0-63 with their own counter; the app keeps that counter right. Excluded: Estus
Flask, gestures, cut/unused rows, and infused or upgraded gear. `research/param_items.csv` holds the
decoded tables and `research/gen_addable.py` regenerates the item lists.

What "add" writes, copied from real game behaviour: a type/ID/quantity/handle entry in an empty slot
(the first freed slot after the first weapon/armor entry; if none is left, the slot after the last
used one, which is what the game did when 100 weapons were taken out of the box), plus +1 on the
header entry counter. Slots 0-63 are reserved for key items and only used when adding one (see below). Materials were confirmed to work on a
real console, and a save edited by an earlier build of this app loaded and played normally with all
of its added items intact. Weapons, ammunition, spells and consumables have since been added and played on a real console. Key items and the hidden cut armor are the least
tested (no real save shows the game adding a key item, so those use a freed slot, else the slot after the last used one), and the app labels them
Experimental. Try them offline with a backup first.

Notes:
- **Level follows your stats automatically.** Level = (sum of the eight stats) - 82, which holds for
  every class in the game; the offset is read from your own save. Changing a stat updates Level, and
  reverting the stat restores it. You can still set Level by hand afterwards; the next stat change
  recomputes it.
- Value limits (e.g. stats 1–99, level 1–802, HP up to 9999) are conservative guards chosen for this
  app, not limits copied from the game files.
