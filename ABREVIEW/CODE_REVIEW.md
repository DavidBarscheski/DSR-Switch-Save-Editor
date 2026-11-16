# Dark Souls Remastered Switch Save Editor — code review

Reviewed November 16, 2026, at revision `4ed67e439fd9ce369df3141be77ead44d92d6ec8`.

## Assessment

This is a compact Nintendo Switch homebrew save editor with a reasonably organized, testable core and substantial save-format research. It is more than a UI around guessed offsets: it preserves a complete original byte buffer, validates character checksums, restricts inventory edits, supports multiple characters, and generates item definitions from parameter data. Those are useful foundations.

The main weakness is the persistence layer. The backup and write sequence has failure cases that undermine the application's safety claims. Backup filenames can collide and overwrite an earlier recovery copy; backup completion is not fully checked; and failed writes or commits can cause the application to adopt uncertain disk state as its new baseline. The core also contains reproducible state-management bugs that the existing tests miss.

**Recommendation:** address the backup/write/recovery findings before expanding supported edits or presenting the editor as dependable for valuable saves. Passing the host tests establishes internal behavior on synthetic fixtures; it does not establish game acceptance or reliable Switch filesystem recovery.

No application, test, data, build, or documentation fixes were applied. This report is the only intended repository addition. “Implementation” below means a proposed implementation, not a completed change.

## Scope and limits

Reviewed both READMEs, handoff and format notes, all C++ source and headers, the host tests and Makefiles, all Python scripts, generated item tables, CSV schemas/data counts, license, and the tracked build artifacts/dependency files. The initial working tree was clean. No repository-specific `AGENTS.md` was found in the project file inventory.

I compiled the host tests from source into `/tmp`, exercised additional synthetic cases there, and regenerated tables in a temporary copy. I did not edit or open a real game save for writing. No Switch, cartridge dump, Paramdex checkout, or devkitPro installation was available for independent hardware/build/parameter-extraction validation. Historical hardware-test claims in the documentation are evidence supplied by the project, not independently reproduced results. Prebuilt binaries were identified by file type; their correspondence to the current source was not established.

## What the project does and how it works

The `.nro` application runs on the Switch through Homebrew Menu. `source/main.cpp` provides controller input, console screens, account selection, save mounting, file discovery, backup creation, and save writing. It mounts title `01004AB00A260000` for the selected account and picks the alphabetically first matching `DRAKS*.sl2` file within its directory scan.

`source/save.cpp` and `include/save.h` implement the platform-independent model. A `Save` owns two complete copies of the file: the bytes originally loaded and the working bytes. It reads little-endian integers at fixed character-relative offsets. A character region has a 16-byte stored MD5 followed by `0x60000` bytes covered by that checksum. `listCharacters()` scans BND4 entry offsets and uses checksum validity plus inventory capacity to recognize candidate characters.

The three screens offer:

- **Stats:** souls, level, eight attributes, current/maximum HP, and current/maximum stamina. Attribute edits automatically update level using an offset derived from the loaded stats and level, falling back to 82.
- **Items:** quantities of recognized goods already present, including consumables, materials, spells, and key items represented in the generated goods list. This is broader than the older “Humanity and materials only” description.
- **Add:** new goods, rings, armor, weapons, and ammunition. New entries contain seven 32-bit fields: type, item ID, quantity, handle, flag, current durability, and durability hit counter. Key items use slots 0–63; other entries use the later region. Cut armor is hidden behind ZL+ZR.

Inventory capacity is 2,048 entries of 28 bytes. Handles combine a sort key and a slot component. Slot selection imitates observations from real saves, preferring certain freed slots and then appending after the last used slot. This is an empirical compatibility rule, not a complete specification of the game's inventory allocator.

On confirmation, `performSave()` writes the original buffer to SD, calls `finalize()` to update inventory counters and MD5, overwrites the mounted save, commits the device, and reads the file back. The editor then reloads the disk bytes as a new editing baseline.

`source/md5.cpp` contains a small standalone implementation of the game's checksum algorithm. MD5 is appropriate for reproducing this file format; replacing it with a stronger hash would break compatibility. Its use here does not authenticate a save's layout or provenance.

The research scripts decode `.param` files using externally supplied Paramdex XML definitions, write `research/param_items.csv`, and generate C++ include files. The present generator produces **217 goods, 41 rings, 232 normal armor pieces, 210 weapons/ammunition entries, and 8 hidden armor pieces**: 700 normal add choices and 708 total. The parameter CSV contains 272 goods, 1,245 weapon, 324 armor, and 41 ring rows before filtering.

## Findings

Priority meanings: **P1** should be fixed before trusting the save/recovery path; **P2** is a correctness or validation issue worth fixing next; **P3** is a lower-impact correctness/documentation issue. These are engineering priorities, not security vulnerability ratings.

### F01 — P1: Backups can overwrite earlier backups

**Location:** `DSRSaveEditor-src/source/main.cpp:163–173, 84–90, 350–352`.

The filename contains only the source basename and a local timestamp with one-second resolution. `writeNewFile()` opens it with `"wb"`, which truncates an existing file. Two backups for the same basename and timestamp therefore replace one another. Different accounts share the same backup directory and naming scheme. An incorrect/repeated clock value also increases collision risk.

This is especially harmful after a partial save failure: retrying within the same timestamp can replace the clean backup with the newly loaded, damaged file. No collision or overwrite warning is shown.

**Proposed implementation:** include an account identifier and a unique suffix; create with exclusive semantics and retry on collision. Never replace an existing backup. Preserve the first clean baseline separately from later recovery snapshots.

**Acceptance check:** force a fixed clock and create multiple backups for the same filename and different users. Every earlier copy must remain byte-identical.

### F02 — P1: The backup gate does not establish a complete, verified recovery copy

**Location:** `DSRSaveEditor-src/source/main.cpp:84–90, 350–354`.

`writeNewFile()` checks `fwrite()` and `fflush()` but discards `fclose()`'s result and never reads the backup back. It can report success even when closing reports an error. The source save is subsequently overwritten. `overwriteInPlace()` also ignores close failures.

This is a concrete error-handling gap. The extent of physical durability after flush/close depends on the target filesystem and storage; it was not tested here.

**Proposed implementation:** propagate write, flush, and close errors; reopen the completed backup and compare its full bytes with the intended original before touching the live save. Use the target's supported synchronization behavior where appropriate, and remove or clearly mark incomplete backup files.

**Acceptance check:** inject short writes, flush failures, close failures, and corrupted backup readback. Each must prevent a live-save write and retain the user's pending edits.

### F03 — P1: Save failure handling can discard edits or adopt uncertain state

**Location:** `DSRSaveEditor-src/source/main.cpp:357–365, 415–424`.

`wroteToDisk` becomes true before `overwriteInPlace()` even opens the file. If opening fails, the caller can reload the unchanged disk file and discard all pending edits despite no write occurring. If writing or committing fails, the caller still reloads any bytes it can read and treats them as the baseline. A readable view after a commit failure is not evidence that the changes persisted successfully.

Worse, if this reload fails, the old in-memory model remains editable. Another save attempt is allowed despite uncertain disk state. A successfully read partial file may become read-only if MD5 fails, but that does not resolve the recovery workflow or protect against the backup collision in F01.

**Proposed implementation:** return a structured outcome distinguishing backup failure, open failure, partial write, commit failure, verification failure, and verified success. Preserve pending edits when no write occurred. After uncertain writes, enter an explicit recovery state; retain the clean original and its backup path, and require a verified reload/remount or restoration before another normal save. Only verified success should replace the normal baseline.

**Acceptance check:** fault-inject every filesystem operation and check both disk state and retained in-memory edits. Verify that a failed commit never produces an ordinary editable “saved” baseline.

### F04 — P2: Readback verifies the default character instead of the edited character

**Location:** `DSRSaveEditor-src/source/main.cpp:368–370`.

`performSave()` calls `Save::checksumValid(check)` without passing `save.base()`. A character chosen from another entry is therefore written using one base and verified using the file's default base. An empty or invalid default character causes a false “Written file did not verify” message even when the edited character's checksum is valid.

**Reproduced:** a synthetic file with a valid second character and a damaged default region produced `selected_checksum=1 default_checksum=0` after editing/finalizing the second character. Byte equality with `out` still protects against ordinary readback differences; the specific defect is which region gets checksum verification.

**Proposed implementation:** verify `checksumValid(check, save.base())`. If validating other characters too, preserve their before/after validation status and report those separately.

**Acceptance check:** successfully save a valid second character while the default region is empty or checksum-invalid.

### F05 — P2: The fallback load path bypasses container/character recognition

**Location:** `DSRSaveEditor-src/source/save.cpp:77–87, 100–113, 125–140`; `source/main.cpp:630–631`.

`listCharacters()` checks BND4 magic and inventory capacity, but `Save::load()` checks only file length, an offset, and the MD5 region. If discovery finds no characters, the UI calls this less restrictive loader anyway. A checksum-valid region can become editable even if the file is not BND4 or its inventory layout is unrecognized. Stats remain writable when inventory recognition fails; an existing test explicitly expects that behavior.

**Reproduced:** changing a synthetic fixture's magic to `NOPE` yielded no listed characters but `LoadStatus::Ok` and `editable=true` from `load()`.

This does not demonstrate a particular incompatible game version corrupting a save. It demonstrates that the advertised checksum/layout gate is weaker than it sounds: MD5 consistency does not identify the meaning of the bytes.

**Proposed implementation:** separate structural compatibility from checksum validity, validate the supported container and character region before enabling edits, and make the UI fallback inspect-only. A reusable low-level region loader can remain available for explicit internal testing.

**Acceptance check:** correct checksums must not make wrong magic, invalid entry tables, overlapping/out-of-range regions, or unsupported layouts editable.

### F06 — P2: Finalize followed by undo leaves inventory counters stale

**Location:** `DSRSaveEditor-src/source/save.cpp:400–419`, especially 413–414.

Counters are updated only when a region's occupancy delta is nonzero. Once `finalize()` has raised a counter, undoing every added item makes the delta zero; a second `finalize()` does not restore the original counter. It then calculates a valid MD5 over the inconsistent result.

**Reproduced:** add → finalize → undo → finalize yielded equipment counter **257**, original **256**, and unequal file bytes. The existing idempotence test calls finalize twice without intervening edits, so it misses this case. Normal successful UI saves reload the model; this becomes relevant to reuse of the core and failure paths that retain the model after finalization.

**Proposed implementation:** always assign each counter from `original counter + current occupancy delta`, including zero delta. Consider building a serialized output copy so finalization does not leave generated metadata in the editable working state.

**Acceptance check:** add/finalize/undo/finalize must reproduce the original bytes; also test multiple additions and partial undo in both regions.

### F07 — P2: Invalid existing goods stacks can be duplicated

**Location:** `DSRSaveEditor-src/source/save.cpp:174–181, 221–225, 321–334`.

`indexItems()` excludes goods whose quantity is zero or above the item's limit. `findOwned()` searches only that filtered list. Stackable goods do not get the broader `ownsEntry()` guard used by single-instance items, so adding an excluded existing good creates a second inventory entry with the same type and ID.

**Reproduced:** the existing fixture includes Red Titanite Slab ID 1070 at quantity 150. `owns()` recognizes it, but `addItem()` returns `Added` and creates another stack. The Add screen also describes such goods as “not owned” because it uses the filtered lookup.

**Proposed implementation:** separate physical ownership lookup from editable-quantity lookup. Refuse ambiguous/invalid existing stacks with an explanatory result, or provide an explicit repair operation rather than silently duplicating them.

**Acceptance check:** test zero, excessive, and multiple pre-existing stacks. “Not editable” must not mean “not present.”

### F08 — P2: The documented test command can run no tests

**Location:** `DSRSaveEditor-src/tests/Makefile:4–6, 11`; tracked `tests/test_core`.

Tests run only as part of rebuilding the executable target. An up-to-date target causes Make to do nothing. The repository ships that executable as a Linux x86-64 ELF, which is also unsuitable for native execution on this Mac.

**Observed:** `make -n -C DSRSaveEditor-src/tests` reports `test_core is up to date`; it schedules neither compilation nor execution. This can give a developer or CI a successful Make exit status without testing anything.

**Proposed implementation:** add a phony `test` target that depends on the executable and always runs it; make that the documented/default entry point. Stop tracking the host binary.

**Acceptance check:** run the command twice; both invocations must execute the tests. A fresh checkout on a different host must build its own executable.

### F09 — P2: Host test builds ignore header and generated-data dependencies

**Location:** `DSRSaveEditor-src/tests/Makefile:4`.

Only three `.cpp` files are prerequisites. Changes to `save.h`, `md5.h`, the generated `.inc` files, or `cut_count.h` do not trigger recompilation. Even fixing F08 alone could cause an old executable to test an earlier item catalog or interface.

**Proposed implementation:** use compiler-generated dependencies with per-object compilation, or explicitly list all relevant headers/include data for this small build. Keep the always-run test target separate from compilation.

**Acceptance check:** modify a generated table or public header in a disposable checkout and verify Make rebuilds the affected executable.

### F10 — P2: Tracked Windows build dependencies break portable incremental builds

**Location:** `DSRSaveEditor-src/build/main.d:1`; `DSRSaveEditor-src/Makefile`, final `-include $(DEPENDS)`.

The repository tracks object files and dependency files generated under paths such as `C:/Users/Administrator/...` and `C:/devkitPro/...`. On this Mac's Make, those Windows drive-letter paths cause a parser error. The application Makefile includes these files when entering the existing build directory, so a user can encounter the failure before rebuilding them.

**Reproduced directly:** `make -f DSRSaveEditor-src/build/main.d -n` fails with `target pattern contains no '%'`. A complete Switch build was not possible here without devkitPro; this is a dependency-file parse reproduction, not a claim that every fresh clean build fails.

**Proposed implementation:** stop tracking `build/`, objects, dependency files, host binaries, and Python caches; add ignore rules. Publish intended `.nro` artifacts separately with source revision, toolchain version, and checksums. Document a clean build from source.

**Acceptance check:** fresh Linux/macOS/Windows checkouts with their supported toolchains must not inherit another machine's build paths.

### F11 — P2: The parameter parser mis-sizes dummy bitfields

**Location:** `DSRSaveEditor-src/research/paramlib.py:12–17`.

A `dummy8` field with a bit count goes through a placeholder `pass`, resets bit packing, and advances by a full byte. If it follows another bitfield in the same byte, subsequent offsets become wrong. Since this parser supplies the CSV used to generate durability, limits, and sort keys, silent layout mistakes deserve attention even though extraction is an offline development step.

**Reproduced with synthetic XML:** fields `u8 first:1`, `dummy8 pad:7`, `u8 next` place `next` at offset **2**, with total size **3**; packing the first eight bits into one byte requires offset **1**, size **2**.

The external Paramdex definitions were unavailable, so I did not establish that this defect affects any particular value in the committed CSV. Regeneration from that CSV is consistent (see validation).

**Proposed implementation:** handle dummy bitfields through the same allocation/packing machinery as other bitfields, discarding their value rather than their layout. Reject unsupported XML declarations; validate row boundaries, computed row size, parameter type, and a pinned definition revision before emitting CSV.

**Acceptance check:** cover mixed dummy/value bitfields, byte boundaries, larger integer bitfields, arrays, truncated rows, and known decoded parameter values.

### F12 — P3: Revert does not necessarily restore the original field value

**Location:** `DSRSaveEditor-src/source/save.cpp:374–379, 396–397`.

`revert()` passes the original value through `set()`, which clamps it to the editor's chosen limits and may recompute level. A readable, checksum-valid save can contain values outside those limits. Reverting such a field changes it to the limit instead of restoring the loaded bytes.

**Reproduced:** original Vitality 100 → edit to 50 → revert gives **99**, not 100. A separately edited level also interacts with automatic level recomputation when reverting an attribute.

**Proposed implementation:** define separate “set validated value” and “restore original value” operations. Restore exact original field bytes on revert, and specify the intended behavior for dependent level changes.

**Acceptance check:** revert loaded out-of-range values and attributes alongside a manually edited level. The UI should describe any dependent changes clearly.

### F13 — P3: Documentation and API contracts describe older behavior

**Location:** `DSRSaveEditor-src/README.md`; `HANDOFF.md`; `research/SAVE_FORMAT.md`; `include/save.h:47, 54–66, 128–146`; `source/main.cpp:333–334`.

Examples:

- The README says the Items screen only adjusts Humanity/materials, while all 217 generated goods are indexed when present with accepted quantities.
- It says owned Add-screen items are left alone, but stackable owned goods are set to the requested maximum (`SetExisting`).
- The Items screen advertises “X: set to 99,” although `setItemMax()` uses per-item limits, including 1.
- `kMaxStack` is described as the largest offered limit at 99 even though ammunition uses 999. Add API comments still describe the earlier 99-only behavior.
- The README says key-item slots are never touched in one section and describes adding key items in another. Format notes preserve successive, contradictory interpretations without a single authoritative current specification.
- Handoff/README statements that the Switch layer has never been compiled conflict with the presence of an ARM ELF, object files, and build map. Those artifacts show a build occurred somewhere, but do not prove the current source was built or hardware-tested.

**Proposed implementation:** publish a short current format/support specification and an explicit per-feature validation matrix. Move chronological research discoveries under a clearly historical section. Align the README, UI prompts, and header comments with the implementation.

**Acceptance check:** each advertised input action and ownership rule should match a core/UI test or an explicit documented limitation.

## Additional limitations and recommendations

These are design constraints or validation gaps, rather than additional reproduced bugs:

- **A valid checksum does not mean a valid game state.** HP/max HP, stamina/max stamina, level/attributes, inventory entries, and story/event flags can be mutually inconsistent while MD5 remains correct. Key-item insertion does not update story flags. Provide feature-specific guidance and only add consistency rules backed by verified format/game behavior.
- **The selected slot algorithm remains empirical.** The code deliberately rejects some empty slots and can return “no suitable slot” even when holes exist. Keep that conservative behavior until broader observations justify changes. Do not replace it with arbitrary hole-filling based solely on synthetic tests.
- **Transformed weapon handles need broader validation.** The generator assigns source-item keys to bracketed transformation variants. Existing observations support retained handles for transformations, but do not establish every generated variant's behavior. Test equip/use, durability loss, storage transfer, save/reload, and upgrade operations on actual hardware.
- **Character discovery hides checksum-invalid entries.** They are excluded rather than displayed as damaged/read-only slots. The picker numbers discovered characters consecutively, so “Character 1” can actually be BND4 entry 1 when entry 0 is empty. Show actual game-slot/entry identifiers and distinguish empty, unsupported, and damaged entries.
- **File selection is implicit.** Multiple matching filenames lead to the lexicographically first file, without a chooser. Prefer the known exact filename or require an explicit selection when candidates are ambiguous.
- **No external-change check precedes writing.** The backup contains the bytes originally loaded, not a freshly compared current file. Re-read and compare the live file with the baseline before overwriting so another writer or stale session cannot silently lose changes.
- **I/O assumes trusted file sizes.** `readFile()` ignores seek return values and allocates according to `ftell()` without a supported-size bound. Use checked size/seek operations and a documented upper limit with useful error details. This is primarily resilience against damaged/unexpected files, since the normal input is mounted local game data.
- **Recovery is manual.** There is no restore UI or documented, detailed restoration procedure. Make the recovery copy discoverable by account/character and provide a verified restore workflow. In-place overwrite avoids truncation, but does not by itself make a write atomic or safe from interruption.
- **UI edge case:** the Add screen computes `n` before toggling cut-item visibility. Hiding the cut list and pressing R in the same input frame can use the old larger count and leave selection beyond the visible list (`main.cpp:445, 480–489`). Recompute/clamp selection after every list-size change.
- **Provenance is incomplete.** Parameter extraction relies on external definitions/names and a private game dump, but the repository does not pin the definition revision or record a reproducible input manifest. Record hashes/tool versions and exact upstream references without committing private saves or game dumps. Audit third-party name/data attribution separately; this review does not determine their redistribution terms.
- **Build/release verification is absent.** There is no checked-in CI configuration, pinned Switch toolchain, or demonstrated source-to-binary reproducibility. A prebuilt executable's existence is not a substitute for compiling the reviewed source.
- **Performance is adequate at this scale but repetitive.** Ownership queries repeatedly validate all 2,048 slots, and goods indexing scans the whole goods definition list per matching entry. Cache validated layout state and use keyed lookup tables if larger catalogs or sluggish redraws become an issue; address persistence correctness first.

## Validation performed

| Check | Result | Interpretation |
|---|---|---|
| Fresh host build with Apple Clang 17, C++17, `-Wall -Wextra -Wpedantic -O2` | Passed; `All tests passed.`; no compiler diagnostics | Existing synthetic test suite passes from current source |
| AddressSanitizer + UndefinedBehaviorSanitizer build | Built; run did not finish during the review window and was interrupted | No sanitizer pass is claimed; cause of noncompletion was not diagnosed |
| Added temporary core probes | Reproduced F04–F07 and F12 | Probes used synthetic buffers, not real saves |
| Generator run in isolated copy | Passed | 217 goods, 41 rings, 232 armor, 210 weapons/ammunition; no skipped transforms |
| Compare regenerated normal/cut tables and cut count with committed files | All four identical byte-for-byte | Generator and committed CSV/tables agree |
| Parse all four Python scripts | Passed | Syntax only; not full extraction/icon runtime validation |
| Synthetic parameter XML packing probe | Reproduced F11 | Does not prove committed CSV values are wrong |
| Documented host Make command, dry run | Target up to date; no test execution scheduled | Reproduces F08 without replacing the tracked binary |
| Parse shipped Windows dependency file with Make | Failed with `target pattern contains no '%'` | Reproduces F10 |
| Native Switch build/hardware/game acceptance | Not performed | devkitPro/libnx and hardware unavailable |

Fresh host test command used from the repository root:

```sh
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 \
  -I DSRSaveEditor-src/include \
  DSRSaveEditor-src/tests/test_core.cpp \
  DSRSaveEditor-src/source/save.cpp \
  DSRSaveEditor-src/source/md5.cpp \
  -o /tmp/abreview-dsr/test_core_native
/tmp/abreview-dsr/test_core_native
```

The existing tests usefully cover MD5 vectors/padding, round trips, checksum mismatch read-only behavior, bounds/clamping, unsupported inventory capacity/tags, multiple character regions, equipment/key counters, addition/undo, ownership for normal stacks, slot exhaustion, and byte preservation. Their main blind spots are the entire Switch I/O/UI layer, operation sequences across serialization/failure/retry, rejected-but-present stacks, and independent game-format compatibility. Some synthetic counters and slot contents deliberately do not represent a fully coherent real inventory, so passing them cannot validate the current format assumptions end to end.

## Proposed implementation sequence

Nothing in this section has been implemented.

1. **Make recovery dependable (F01–F03).** Extract the persistence coordinator from controller/UI code behind a small injectable filesystem interface. Add exclusive backup creation, full backup verification, explicit write/commit outcomes, and a recovery state that preserves the clean original. Add focused failure-injection tests before using the path on a valuable save.
2. **Correct deterministic core issues (F04–F07, F12).** Use the selected base consistently, enforce supported structure before editability, recompute both counters unconditionally, distinguish ownership from editability, and restore original values exactly. Add the reproduced sequences as permanent regression tests.
3. **Make validation repeatable (F08–F11).** Separate compile/run Make targets, include dependency tracking, remove generated build debris from source control, pin the toolchain/parameter definitions, and test the parameter parser. Add host CI and a clean Switch cross-build job.
4. **Reconcile the specification and UI (F13).** Create a current format document, accurate controls, actual character slot identifiers, an edit summary before confirmation, and a practical recovery guide. Treat historical notes as history.
5. **Broaden hardware evidence before features.** Test each item category, multiple/empty/default-invalid character layouts, full inventories, undo, reload, and recoverable failures. Record source revision, game version, toolchain, expected changes, and results using private fixture hashes or sanitized fixtures. New upgraded gear, Estus handling, and custom items should wait for this evidence.

The most valuable next work is a small, verifiable save transaction and recovery layer, followed by the reproduced core fixes. The current architecture supports that improvement without requiring a wholesale rewrite.
