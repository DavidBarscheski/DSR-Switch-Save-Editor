# Future state: a Dark Souls character workshop

Date: November 16, 2026. Status: proposed product and engineering plan.

This document expands the feature ideas discussed after [the code review](CODE_REVIEW.md). It describes how to implement and test them; it does not authorize or apply application changes. The accompanying [interactive screen concept](futureState-mockup.html) uses fictional sample data and simulated operations. It never reads, edits, or writes a game save.

## 1. Product direction

Turn the editor into a character workshop: inspect a character, understand its inventory, plan a build, stage changes, review the result, and return to a known snapshot when needed. Keep it useful as a read-only inspector even when a particular save cannot safely be edited.

The defining workflow should be:

1. Select a Switch account and an actual game character slot.
2. See what the editor recognizes and which operations are supported.
3. Create or select a verified recovery snapshot.
4. Explore inventory or plan a build without touching the mounted save.
5. Review a plain-language change list.
6. Save through a verified transaction with explicit recovery behavior.

Prioritize dependable recovery, discoverability, and meaningful information. Avoid adding more editable offsets until their semantics and game behavior are verified.

### What success looks like

- A player can locate an item by name/category instead of scrolling hundreds of entries.
- A user can explain every proposed change before confirming a write.
- A failed save preserves the clean baseline, the recovery copy, and as much pending work as possible.
- A user can restore a selected snapshot without leaving the application.
- Build experiments can be saved and previewed independently of game-save writes.
- Unsupported data is identified honestly; checksum success is never presented as proof of game compatibility.

## 2. Screen design and controller model

### Visual direction

Use a restrained charcoal/ember interface with large text, strong focus indication, and simple panels. Avoid elaborate artwork, animated backgrounds, or proprietary game imagery. Spend screen space on item names, actual values, and meaningful explanations.

The concept file demonstrates five views: **Character**, **Inventory**, **Build**, **Snapshots**, and **Review**. Tabs switch views, inventory selection changes the detail panel, staging adds entries to Review, build buttons stage attribute edits, snapshots show a simulated recovery preview, and the review action simulates a save. All numbers are examples. The concept is browser-based; the shipped Switch implementation would draw native homebrew UI rather than embed a browser.

Target a 16:9 canvas using a proposed 1280×720 design coordinate system. This is a layout target to validate on hardware, not an assumption that every display path exposes identical framebuffer behavior. Use safe outer margins, roughly 24–28 design-pixel body text, and at least 40–48 pixels per selectable row. Show approximately 7–9 rows with readable details rather than 24 cramped rows. Verify actual pixel scaling and font readability on the target Switch models and in docked output.

### Layout

- Persistent header: application name, account alias, actual slot number, character name, pending-change count.
- Tab strip: Character / Inventory / Build / Snapshots / Review.
- Main area: list on the left and selected details on the right where appropriate.
- Persistent footer: controller actions for the current view.
- Modal review/recovery panels replace or cover the content area with an opaque surface; essential actions must remain on screen.

Use both a focus marker and a contrasting selected background. Statuses also need words such as “Verified,” “Experimental,” or “Read-only”; color alone is insufficient.

### Proposed controls

| Input | Normal action | Exceptions |
|---|---|---|
| D-pad / left stick | Move focus through the current view | Directional navigation follows visible spatial order |
| A | Open/select or stage the focused operation | Never an immediate live-save write from a list |
| B | Back/cancel | Prompts when leaving would discard pending work |
| L / R | Previous/next main tab | Disabled while a blocking transaction is running |
| ZL / ZR | Page through a long list | View-specific footer explains any alternative |
| X | Search in Inventory; preview in Build | Clearly labeled in the footer |
| Y | Context action such as favorite/details | Undo is available through an explicit history action |
| + | Open Review | Final confirmation occurs only in Review |
| − | Help / operation details | Includes feature validation status |

Do not require touch, simultaneous button chords, or mouse-like precision. Touch may supplement controller input. Use the platform keyboard for text entry only after its lifecycle and applet behavior have been tested. The mockup's browser buttons stand in for these controller actions.

### UI implementation

Retain the existing console UI during the first correctness milestones. Introduce an `InputAction` layer that translates platform input into semantic actions such as `MoveDown`, `OpenReview`, and `Confirm`. A view model computes display state without filesystem calls. A rendering adapter draws it.

Before selecting a graphics library, build a small Switch proof of concept covering text, lists, scrolling, focus, fonts, and a confirmation panel. Compare dependency size, license, maintenance, build complexity, memory use, and input support. Keep platform-specific headers out of the core model. A complete graphics rewrite should not block safe persistence work.

**Tests:** scripted focus navigation, selection bounds after filtering, back/cancel behavior, long names, empty lists, missing character names, large values, readable error panels, and controller reconnect. Inspect handheld/docked layouts on hardware. The browser mockup can validate interaction intent but cannot validate Switch font rendering, controller timing, or applet lifecycle.

## 3. Architecture: extend the existing core in small steps

Suggested logical modules, introduced only as they become necessary:

| Module | Responsibility | Boundary |
|---|---|---|
| `SaveDocument` / existing `Save` | Original bytes, supported layout, selected character, validated edits | No Switch APIs or UI drawing |
| `SaveLayoutValidator` | Container, entry ranges, checksums, inventory-region support | Returns structured validation results |
| `ItemCatalog` | Typed definitions, names, categories, limits, provenance | Generated data plus explicit metadata |
| `EditSession` | Staged commands, history, undo/redo, semantic change list | Does not persist the mounted save |
| `SnapshotStore` | Exclusive snapshot files, manifests, discovery, verification | SD storage adapter |
| `SaveTransaction` | Baseline comparison, backup, write, commit, verification, recovery | Injected filesystem interface |
| `BuildPlanner` | Draft attributes, verified rules, equipment requirement checks | Operates on a draft; no hidden writes |
| `SaveDiff` | Typed before/after changes and unexplained byte ranges | Read-only |
| `SwitchPlatform` | Account, mount, controller, keyboard, storage, drawing | Owns libnx integration |

Do not split every existing function into a framework. Start by extracting persistence behind an interface and adding a command/history layer around the current setters.

### Edit commands

Use typed commands such as `SetAttribute`, `SetGoodsQuantity`, `AddItem`, and, only when supported, `SetEquipment`. Each command carries a stable character identity, target identity, requested value, and validated before/after state. Item identity must include category/type and ID; never rely on the current row number in a filtered UI.

For operations that move inventory slots or rebuild indices, keep enough before-state to undo the exact affected bytes. A composite command groups “add armor set” into one undo step. Redo must revalidate preconditions. New commands clear the redo branch. An explicit history limit prevents unbounded memory growth; do not copy the entire multi-megabyte save for every button press.

Serialization should produce an output buffer without silently changing edit history. Recompute derived counters and checksum from a clearly defined baseline every time. Dirty state should represent user changes, independent of checksum bytes generated during serialization.

**Tests:** commands compose predictably; undo restores exact original bytes; redo reproduces the same serialized result; batch operations undo atomically; failed commands add no history; changing selection does not retarget an existing command; save/reload starts a new baseline. Include add → serialize → undo → serialize, the counter sequence exposed in the review.

## 4. Save manager and recovery

### Named snapshots

Store immutable raw copies of the full save container. A snapshot may be named “Before respec” or “Before adding weapons,” but its filename should use a generated identifier independent of the label and wall clock.

Proposed directory layout:

```text
sdmc:/switch/DSRSaveEditor/
  snapshots/<account-key>/<snapshot-id>/
    save.sl2
    manifest.json
  presets/
  settings.json
```

The raw full-file snapshot preserves all characters and system data. A manifest records schema version, snapshot ID, creation time with an explicit unknown-clock state, account key, source title ID, original relative path, file length, full-file integrity digest, character entry/base information, validation results, app/source revision, and user label. The full-file digest is for corruption detection; the game's per-character MD5 remains unchanged as a format requirement. Do not put real account identifiers or save contents into external logs.

Choose an identifier with exclusive creation and a collision retry loop; correctness must not rely solely on randomness or the clock. Sanitize labels for display and keep them out of path construction. If a manifest is missing/corrupt, show an incomplete snapshot; do not interpret it as a verified recovery point.

### Transaction states

```text
Draft → Validate → Compare live baseline → Create snapshot → Verify snapshot
      → Write live file → Commit → Read back → Verify full bytes + selected region
      → Verified success

Failure before live write → retain draft, leave live file untouched
Failure after possible live write → Recovery required
```

Read and compare the current live file with the session baseline before writing. Reject stale sessions instead of overwriting unexpected changes. Preserve the original baseline and snapshot reference throughout the transaction. Check open, short write, flush, close, commit, and readback results individually. Validate the selected base, not just the default character.

Only a verified success may replace the session baseline. A commit failure or unreadable result must not return to an ordinary editable state. Avoid claiming an in-place write is atomic. Investigate the target save filesystem's supported transactional behavior on hardware before choosing any rename/staging strategy; do not assume desktop rename semantics apply.

### Restore workflow

1. Select a snapshot and view its account, title, date, characters, and integrity status.
2. Verify raw bytes and manifest; reject wrong-title/account restores by default.
3. Show a semantic preview of what will change, including other characters.
4. Create and verify a pre-restore snapshot of the current full container.
5. Restore through the same transaction coordinator.
6. Commit, read back, and verify exact full-file equality.

Make full-container restore explicit: restoring one character requires a separately validated selective-merge feature. Do not present full-file restoration as “restore this character” when it also replaces others. Exporting an edited copy writes a separate SD file and leaves the live save untouched; mark the copy as exported, not committed to the game.

Do not auto-delete snapshots initially. Later retention settings should preserve pinned copies and the only known clean baseline, show the deletion set, and require an explicit user action.

**Tests:** frozen/backward clock, duplicate IDs, multiple accounts with identical basenames, full SD card, read-only storage, directory failures, short writes, every flush/close failure, corrupt manifest, tampered raw bytes, interrupted snapshot creation, stale live baseline, partial live write, commit failure, failed reload, and restore readback mismatch. A fake filesystem records operation order and injects faults. Assert that no live write occurs before backup verification and no retry overwrites the only clean snapshot. Hardware checks use disposable/test saves with independently retained backups; do not deliberately interrupt a valuable save.

## 5. Character overview and inspection

Begin with existing verified fields: actual entry/slot identity, level, souls, eight attributes, HP, stamina, recognized inventory counts, and per-region validation. Show pending values alongside loaded values.

Add name, starting class, playtime, equipment, and attuned spells only after locating and validating their storage. Starting class should not be guessed from current attributes. Equipment lookup must resolve stored handles to physical inventory entries and report dangling references. Never derive equipped items merely from ownership.

Represent field knowledge explicitly: `Verified`, `Experimental`, `Unsupported`, or `Unavailable`. A checksum-invalid character remains visible as a read-only entry with a reason. A system-data entry must not become a character merely because its bytes look plausible. Keep actual slot numbering when earlier slots are empty or damaged.

Explain dependent values: whether HP/stamina edits persist, whether the game recomputes them, and which stat changes affect level. Do not claim relationships not yet tested. An inspect-only compatibility report should be available before any edit.

**Tests:** empty first slot with a valid second character; mixed valid/damaged entries; malformed/overlapping entry ranges; missing metadata; valid MD5 in an unsupported structure; duplicate candidate bases; name termination/encoding; dangling equipment handles; extreme playtime values; and unchanged nonselected characters after edits. Newly mapped fields require independent real-save observations and game load/re-save checks.

## 6. Searchable inventory and useful batch actions

### Catalog and browsing

Build a catalog indexed by `(type tag, item ID)`. Keep weapon upgrade/infusion identity distinct from the base item. Generated metadata includes category, name, stack maximum, fresh durability, key-item region, sort-key rule, provenance, and feature support status. Add curated armor-set membership separately rather than inferring it from adjacent numeric IDs.

Normalize names for search without changing display text. Start with case-insensitive substring/token matching and category filters. Maintain `All`, `Owned`, `Missing`, `Favorites`, and `Pending` views. Ownership scans must include invalid/uneditable existing entries; classify them as present-but-invalid instead of offering duplicate additions. Keep original item IDs in an optional details view to distinguish identically named goods.

Use stable item keys for selection. Filtering preserves the selected key if still visible; otherwise select a defined neighboring result. Query changes reset pagination safely. Cache catalog lookup and validated inventory indices rather than rescanning the entire save for each rendered row.

### Details and actions

The right panel shows owned quantity, staged quantity, allowed range, durability where applicable, validation status, and operation explanation. Provide `Stage add`, `Adjust quantity`, `Favorite`, and `Undo pending addition` only when meaningful.

Useful batches:

- Refill owned upgrade materials to their verified individual limits.
- Add missing pieces of a selected armor set.
- Apply selected additions from a saved inventory preset.
- Export a collection checklist.

Before applying a batch, validate all targets and slot availability on a temporary model. Show planned additions, existing items skipped, unsupported entries, and capacity failures. Default to all-or-nothing staging; optional partial staging must be explicit. Apply one composite history command. Reapplying the same preset must not create duplicates.

Upgraded/infused gear is a later extension. Validate parameter IDs, maximum reinforcement levels, handle behavior, durability, and storage/equipment references for each supported path. Estus remains excluded until its ID transformations and dependent data are understood.

**Tests:** mixed-case searches, whitespace, no matches, duplicate names, all category combinations, favorite persistence, invalid existing stacks, upgraded ownership, full key/main regions, batch preflight failure, repeated preset application, exact byte changes, undo after list resort, and cancel before confirmation. Hardware tests check obtaining, equipping, storing, retrieving, upgrading, and reloading representative items from every enabled category.

## 7. Build planner and respec workspace

### Draft first

Create a build draft from the selected character without editing `Save`. Display current → target values, derived level, and which changes are staged. A user explicitly applies a draft to the edit session; that still does not write the game save.

Support named attribute presets, inventory addition presets, and eventually equipment presets as separate concepts. An equipment preset may reference owned items without modifying inventory. If it needs missing items, offer a separate reviewed addition step. Do not invent a respec item or story event to imply the game itself performed a legal respec.

### Calculations and constraints

Put game rules in a versioned, testable rules module. The current app infers a level offset from loaded values; that is useful for preserving its observed relationship but insufficient as a universal build-planning rule. Validate class/level relationships against authoritative game data or independently checked examples before presenting a target level as exact. Keep “preserve loaded relationship” separate from a validated class-aware calculation.

Initially support attribute ranges and verified weapon requirement comparisons. Later add equipment weight/load ratios and related thresholds once all inputs and formulas are confirmed. Damage output, scaling, two-handed requirements, spell slots, and HP/stamina predictions each require their own verified calculation implementation and support label. Unknown modifiers should produce “estimate unavailable” rather than a precise-looking invented result.

Allow temporary out-of-budget drafts for planning, but prevent unsupported values from being applied. Do not automatically refill souls, grant equipment, or alter progression as a side effect of changing attributes.

### Preset schema

Use a versioned JSON format with a label, rules/catalog revision, target attributes, optional typed item requests, and optional notes. Include no raw memory offsets. Bound file size and list lengths, validate all IDs/types/ranges, reject unknown required schema versions, and ignore or explicitly migrate optional old fields. Imported presets cannot run code or access paths.

**Tests:** known build examples per supported class; exact threshold boundaries; manually mismatched levels; current versus target separation; cancel leaves original bytes unchanged; applying creates one undoable command group; export/import round trips; unsupported item IDs; malformed/oversized JSON; old schema migration; and no hidden changes to souls, world state, or other characters. Hardware checks establish which values the game recalculates on load.

## 8. Save comparison and research tools

Compare two buffers in two layers:

1. **Semantic diff:** character attributes, known inventory entries/quantities, equipment where supported, and recognized metadata.
2. **Raw diff:** coalesced byte ranges not explained by known edits, with file offset and character-relative offset where possible.

Match physical entries carefully. Handles can move or be retained by transformations, so do not assume slot index or handle alone is a universal identity. Preserve duplicate entries and show ambiguous matches instead of collapsing them by item ID. Explain matching confidence for research use. Keep checksum/counter changes separate from gameplay changes.

Implement comparison as a read-only core module usable by the Switch and a later desktop CLI. A desktop companion can accept local exported saves and produce JSON/Markdown reports without a cloud service. Do not upload saves by default. A structured research capture records before/after hashes, game version, source revision, observed player action, and an expected-change description.

**Tests:** identical files; one stat edit; one quantity edit; insertion/removal; inventory relocation; duplicate IDs; transformed IDs with retained handles; different character counts; changed default bases; malformed files; and unexplained changes. Ensure every changed byte is either covered by an explained range or reported as unknown, without double-counting overlapping ranges. Large unrelated files should be rejected or handled with bounded memory.

## 9. Collections, challenges, and personal notes

### Collection checklist

Compare recognized ownership with a versioned obtainable-item catalog. Keep “currently owned” distinct from “ever collected”: the save may not retain reliable historical evidence after an item is consumed or discarded. A user-maintained checklist can track history separately. Distinguish base items, upgrades, infusion variants, and cut content with explicit filters. Exclude cut items from normal completion totals.

**Tests:** duplicate ownership counts once for the chosen collection definition; unknown items do not inflate completion; catalog upgrades migrate keys; offline notes survive app updates; and an empty/unreadable inventory never displays a misleading completion percentage.

### Challenge presets

Offer transparent starting configurations such as selected attribute targets and an equipment checklist. Every preset lists its intended changes before application. Keep challenge rules as user guidance when the editor cannot enforce them. Do not claim that a save edit proves a legitimate run or that a rule is enforced continuously while the game runs.

**Tests:** applying a challenge changes only declared supported targets; unsupported actions are rejected; repeated application is predictable; undo works; imported presets remain data only.

### Notes and shareable summaries

Attach local notes to presets/snapshots and export a concise character/build summary. Exclude account IDs, full filesystem paths, raw saves, and private notes by default. Let users choose which notes to include. Sharing means creating a local export; it does not automatically post anywhere.

**Tests:** Unicode notes, long labels, escaping Markdown/JSON, missing metadata, privacy defaults, and correct current/target distinction in exported builds.

## 10. Progression editing and advanced features: later research

World flags, bonfire/warp state, NPC quests, boss state, and key-item possession are related but separate. A future progression tool needs documented flag mappings, dependencies, and actual before/after captures. Do not offer a broad “unlock everything” feature based on possession alone.

Expose research findings read-only first. A supported progression operation should declare all preconditions, touched fields, expected in-game result, known incompatibilities, and recovery requirements. Require scenario-specific test saves and game load/play/re-save verification. Hide unverified operations from the ordinary editing UI rather than presenting raw offsets as friendly controls.

Custom items, broad cross-platform conversion, and automated online features are outside the initial roadmap. They require substantially different compatibility evidence or infrastructure. A desktop inspector/diff tool is a more achievable early companion than cross-platform save conversion.

## 11. Testing strategy and release gates

### Host validation

- Fix the Make target so tests always run and header/generated-file changes rebuild the executable.
- Keep binaries, object files, dependency files, and Python caches out of source control; publish intentional releases separately.
- Run deterministic regression tests for the code-review defects and all newly introduced edit commands.
- Add table/schema validation and deterministic generation checks in an isolated directory.
- Use synthetic coherent containers for valid-path tests, plus deliberately malformed fixtures for rejection paths. Label which is which.
- Add generated operation sequences: apply/revert/serialize/reload combinations should preserve specified invariants. A seeded generator makes failures reproducible.
- Fuzz parsing/validation with strict size and runtime limits; use sanitizer builds where supported and investigate hangs instead of treating an unfinished run as success.
- Test persistence using an injected fake filesystem, asserting exact operation order and error-state transitions.

Useful invariants: unrelated character bytes are unchanged; serialized supported regions have valid checksums; counters agree with the chosen verified region semantics; untouched fields retain exact bytes; rejected operations leave the model unchanged; completed undo returns to the starting state; and failed persistence never destroys the only known clean snapshot.

### Switch validation

Cross-build reviewed source using recorded devkitPro/libnx versions. Test boot, account selection, mount failure, multiple profiles, game-not-closed conditions, storage failures, keyboard cancel, repeated input, suspend/close behavior, controller disconnect, and handheld/docked readability.

For every writable feature, verify game acceptance beyond “checksum passes”: load the game, inspect the intended state, exercise the item/build, save normally, reopen the editor, and compare the resulting file. Record source revision, toolchain, game version, fixture hashes, action, expected result, and actual result. Keep source saves private unless explicitly sanitized and authorized for distribution.

### Feature support matrix

Maintain a table per feature/category: mapped offsets/data, synthetic tests, observed real-save evidence, hardware load acceptance, exercise/re-save acceptance, and known limits. A label of “Verified” must reference a defined acceptance bar, not an anecdote. These are release gates for this proposed implementation; they are not claims about existing test coverage.

### Release readiness

A release enabling ordinary live-save editing should require:

1. P1 persistence findings resolved and all fault-injection tests passing.
2. Reproducible clean host tests and Switch cross-build.
3. Verified backup/restore round trip on disposable hardware fixtures.
4. Selected/nondefault character cases and other-character preservation verified.
5. Accurate feature matrix, controls, limitations, and restore instructions.
6. Release artifact linked to source revision/toolchain with published checksums.

## 12. Delivery milestones

| Milestone | Deliverable | Completion evidence |
|---|---|---|
| A: Trustworthy foundation | Review bug fixes, injectable persistence, immutable snapshots, explicit recovery | Core regressions and I/O fault matrix pass; hardware backup/restore verified |
| B: Useful daily workflow | Semantic edit review, undo/redo, searchable/filterable inventory, favorites | Controller flows and batch preflight tests pass; category checks documented |
| C: Character workspace | Readable overview, actual slots, metadata with support labels, graphics adapter | Handheld/docked review; mixed damaged/valid slot tests; no source-format guesses enabled |
| D: Build workshop | Draft planning, named presets, verified requirements/rules | Known examples match; draft/apply/undo tests pass; in-game values checked |
| E: Discovery tools | Snapshot diff, collection checklist, local exports and notes | Diff byte coverage, privacy defaults, catalog migration tests |
| F: Advanced editing | Selected reinforcement/equipment/progression features | Per-feature research evidence and full game exercise/re-save acceptance |

Avoid calendar estimates until the persistence extraction and a Switch UI proof of concept establish effort. Each milestone should remain useful on its own; graphics polish and advanced item support should not delay recovery fixes.

## 13. Mockup interpretation and validation

The [interactive concept](futureState-mockup.html) depicts the intended hierarchy and staged workflow. It uses a reduced sample catalog and a fictional character, account alias, and snapshots. “Verified” labels inside it are sample interface text, not a certification of existing features. It does not implement game rules, actual equipment calculations, real backup integrity, or filesystem transactions. Its save/restore actions are simulations.

The narrow browser layout stacks panels so the concept can be inspected in chat; the target Switch layout remains a fixed landscape composition. Any production build should use the controller model above and verify actual hardware scaling. Use the mockup to choose information hierarchy, selection behavior, wording, and review steps before committing to a graphics dependency.

No existing application, test, generated data, or build files were changed to produce this plan and concept.

### Checks performed on these deliverables

The concept's JavaScript passed Node's syntax check. A lightweight simulated-DOM harness exercised initial rendering, view switching, item staging, review contents, build drafting, the pending-edit restore guard, undo, restore preview, simulated save/reset, and an empty search. All passed. Relative document links resolve to existing files. These checks do not establish actual browser rendering or accessibility behavior.

Visual browser inspection could not be completed: local server binding was denied by the execution sandbox, and the browser tool's security policy rejected local-file navigation. No alternate browser route was used. Layout, focus visibility, light/dark appearance, and real screen sizing remain visual acceptance checks for the concept, followed by separate hardware checks for any production implementation.
