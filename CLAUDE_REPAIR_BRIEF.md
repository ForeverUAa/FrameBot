# FrameBot: Claude Repair Brief

**Repository:** [ForeverUAa/FrameBot](https://github.com/ForeverUAa/FrameBot)  
**Purpose:** Give a coding agent the context and concrete work order needed to audit the whole mod, repair existing problems, finish incomplete editor work, and verify the result. This is not a claim that every feature listed already works correctly.

## Instructions for Claude

Work in the repository itself. Do not stop after writing a review or recommendations. Inspect the code, reproduce issues where possible, implement fixes, build the project, and continue until the important correctness issues are resolved or an external blocker is documented.

1. Read this file, `mod.json`, `CMakeLists.txt`, current workflow files, `src/main.cpp`, `src/global.cpp`, `src/macro.hpp` / `src/macro.cpp`, the serialization code under `src/gdr/`, and the timeline/editor files under `src/ui/`.
2. Treat current C++ source as authoritative. `README.md` is empty. `ADVANCED_EDITOR_IMPLEMENTATION.md` is historical progress documentation and no longer accurately describes every implemented feature.
3. Make a concise audit checklist, then fix issues in priority order. Do not perform a giant cosmetic rewrite before addressing data corruption, playback state, autosave, or editor-selection problems.
4. Preserve existing macro compatibility. Existing macro files and playback are more important than new UI features. Prefer backward-compatible changes.
5. Build and run relevant checks after meaningful changes. Do not claim something works based only on reading the source.
6. Fix root causes rather than symptoms. Avoid unrelated dependencies. Keep Windows and Android support.
7. Update this file when behavior changes. In the final report, list fixes, changed files, exact build/test results, and anything still unverified.

## Project identity and build baseline

- Mod name: **xdBot**. Repository: `ForeverUAa/FrameBot`.
- Geode version declared in `mod.json`: **5.8.2**.
- Geometry Dash target declared in `mod.json`: **2.2081** for Windows and Android.
- C++ standard: **C++23**, configured in `CMakeLists.txt`.
- Build system: CMake + Geode SDK. `GEODE_SDK` must point to a compatible Geode SDK directory.
- Main CI workflow: `.github/workflows/multi-platform.yml`, which builds Windows, Android32, and Android64, then packages the outputs.
- Baseline verified on 2026-10-09: commit `35ff3fa75f1400844917914e5db461ee03f099e1`, “Autosave timeline edits”. [CI run 37918121354](https://github.com/ForeverUAa/FrameBot/actions/runs/37918121354) completed successfully for Windows, Android32, Android64, and Package builds. This proves that commit built in CI, not that every gameplay path is bug-free.
- The preceding build caught an ambiguous `CCPoint` assignment in the analyzer. It was fixed by explicitly constructing `cocos2d::CCPoint`; the following CI run passed.
- Version caveat: `mod.json` declares mod version `2.4.1`, while `src/gdr/gdr.hpp` currently declares `xdBotVersion = "v2.3.11"`. Investigate whether these intentionally represent different version schemes before changing either.

## What the bot currently contains

### 1. Macro recording and playback

Main implementation: `src/main.cpp`, `src/global.cpp`, `src/macro.hpp`, `src/macro.cpp`, `src/gdr/`, `src/practice_fixes/`.

The code handles recording and replaying input actions, including press/release, frame timing, player selection, two-player mode, and active-macro state. Runtime hooks route recorded and replayed inputs through Geometry Dash gameplay functions, track current action and frame-fix cursors, and reset state around level lifecycle events.

The project also contains:
- Macro metadata such as author, description, game version, level information, framerate, seed, coins, LDM state, and bot information.
- Save/load/import paths for GDR and JSON representations, plus legacy `.xd` import handling.
- Macro list, details, import, load, merge, and save UI.
- A legacy paginated macro editor with frame, button, press/release, and player changes, as well as add/remove/clear and merge/apply operations.
- Existing checkpoint/level autosave functionality for recorded macros.

Audit expectations:
- Malformed or incomplete files must fail safely with useful errors, not crash the game.
- Preserve old macro parsing, frame-offset behavior, player mapping, input order, frame fixes, and metadata unless a specific bug requires a change.
- Saving must not silently mutate the macro’s meaning or overwrite an unrelated file.
- Verify that importing then exporting a macro preserves supported fields and that old macros without subframe metadata still load.

### 2. Precise timing and CBF

Main implementation: `src/hacks/cbf.cpp/.hpp`, `src/macro.hpp/.cpp`, `src/main.cpp`, `src/ui/record_layer.cpp`.

The input type stores a whole frame plus a fractional subframe in the range from 0 to less than 1. The data model serializes non-zero subframes and stores macro subdivision metadata. The CBF engine schedules inputs at fractional tick positions, with a configurable substep divider. The timeline has a CBF/subframe editing toggle and frame/subframe readouts. The analyzer also trials fractional timings.

Audit expectations:
- Enforce one timing invariant: frame is non-negative, subframe is normalized to `[0, 1)`, and inputs are ordered by precise time.
- Check frame-zero edge cases, subframes near 1.0, negative adjustments, same-frame inputs, and rounding.
- Test macros with no subframes, old macros without a subdivision field, and macros whose subframes require different dividers.
- Ensure reset, pause, stop, macro switching, death, restart, and cancellation leave no stale CBF actions or divider overrides queued.

### 3. Timeline editor

Main implementation:
- `src/ui/macro_timeline_layer.cpp/.hpp`: UI and interaction.
- `src/ui/macro_timeline.cpp/.hpp`: timeline model and history.
- `src/ui/macro_inspector.cpp/.hpp`: selected-event properties.
- `src/ui/record_layer.cpp`: Timeline entry point.

Current visible controls/features include:
- Play, Pause, Stop, Step, CBF toggle, Zoom - / Zoom +, Undo, Redo, and Tasks.
- Separate player tracks and color-coded press/release events.
- Ruler, frame/subframe readout, playhead, click-to-select, ruler click to seek, horizontal panning, and event dragging.
- Inspector controls for frame, subframe, button, player, and press/release action.
- Timeline playback that operates on the current macro and attempts to keep game/pause state consistent.

The timeline model has helpers for insertion, deletion, duplication, event edits, sorting, history, range selection, navigation, zoom, and scroll. **Not all model helpers currently have matching timeline UI controls.** Multi-select is only partially represented in the model.

#### Priority timeline risks to investigate and fix

1. **Dragging while sorting the input vector.** `MacroTimeline::setEventFrame` and `setEventSubframe` sort the vector after editing. The drag handler continues to use the old vector index on the next mouse-move event. When the dragged event crosses another event, the same index can refer to a different action. Fix this with stable drag identity or by deferring/reconciling sorting during a drag; never rely on an index that changes after sorting.
2. **Selection/inspector identity after reorder.** The selected event and inspector store indexes. After sorting, those indexes may point at a different event. Keep selection attached to the actual event through edits, sort, undo, and redo.
3. **Undo history while dragging.** Setters push history on each call. Mouse movement can create many intermediate states, and a single drag may call both frame and subframe setters repeatedly. Make one edit gesture one coherent undo transaction, with a sensible history cap and correct redo invalidation.
4. **History correctness.** Verify empty macros, first edit, repeated edits, edit after undo, undo/redo after reorder, and undo/redo when a selected event no longer exists.
5. **Rendering cost.** `updateTimeline` runs on a recurring schedule and re-renders UI layers. Profile large macros, avoid unnecessary child destruction/recreation or redraws, and confirm off-screen events are culled.
6. **Editing completeness.** Consider adding timeline-level insert/add, delete, duplicate, and usable multi-select actions, wired to the model and inspector. The legacy editor has some of these operations, but the timeline should not expose dead model functionality as if it were usable.
7. **Touch/platform behavior.** Test event selection, dragging, panning, ruler seeking, small screens, varying aspect ratios, and Android touch handling. Verify the play/pause/close lifecycle never strands the level in the wrong state.

### 4. Alignment analyzer / frame tasks

Currently lives inside `src/ui/macro_timeline_layer.cpp`, including `FrameTaskPopup`, the alignment probe and gameplay hooks. This is a large responsibility for one UI translation unit; splitting it into dedicated source/header files may be appropriate after behavior is stable.

Current intended design:
- **TASK**: scan candidates for the selected input in a smaller range, currently configured for ±6 whole frames.
- **ANALYZE**: create tasks from macro inputs and scan a wider range, currently ±24 whole frames per input.
- Candidate timings include the original timing and offsets on both sides. Each candidate runs a trial, and gameplay callbacks collect observed interactions.
- Orb interactions use player Y minus orb Y. Portal interactions use player X minus portal X.
- Results are deduplicated and visualized with markers/text.
- STOP cancels testing. CLEAR clears accumulated results.
- The probe hooks `playerTouchedRing` and `playerTouchedTrigger`; portal detection is filtered through a hard-coded object-ID set.

This is a heuristic interaction/alignment scanner, not proof that every valid alignment or portal type is detected. The object-ID filter and actual coverage require verification against the correct Geometry Dash object IDs and game behavior.

#### Priority analyzer risks to investigate and fix

- Restore state after success, no result, STOP, level death, level completion, restart, popup close, scene change, and every early return. This includes the previous macro, playback state, action/frame-fix cursors, restart/attempt values, CBF queue/divider override, pause state, and probe active state.
- Make cancellation/cleanup idempotent. No test should leave a modified macro, stale input queue, dangling UI pointer, or permanently changed game state.
- Verify exactly which player and input changes per trial. Ensure a trial changes one intended event, preserves ordering, and cannot match the wrong event when duplicate inputs exist.
- Confirm the test horizon is sufficient and documented. The current trial limits observation to a window after the candidate and can stop before the next same-player input; establish whether this is intended.
- Verify portal IDs. Do not expand the allowlist with guessed IDs. Use authoritative project/game definitions where possible, add tests or a maintained mapping, and handle unknown objects safely.
- Distinguish “no alignment observed” from “no callbacks observed” and from “trial could not run”. Show useful status/error messages.
- Make long Analyze jobs cancellable and avoid recursive or rapid nested trial transitions that can overflow the stack or corrupt task indexes on a large macro.
- Define what the reported results mean. Raw samples, distinct object alignments, successful completions, and timing candidates are not interchangeable.
- Avoid stale task/result state between popup sessions or different macros.

### 5. Timeline autosave and recovery

The latest change adds `Macro::saveAutosave(Macro&, path)` and calls it after timeline inspector edits, completed event drags, Undo, and Redo. It writes a stable `timeline_autosave_<level ID>_<sanitized level name>.gdr` file under the configured **Auto Saves Location**, using a temporary file before replacement. It is designed to overwrite the same timeline-recovery file rather than create a numbered file for each edit.

Important: this recovery autosave does **not** automatically overwrite the user’s original named macro file. Preserve this distinction unless a deliberate, safe design replaces it. Recording-time/checkpoint autosaving is separate.

Audit/fix requirements:
- Verify the autosave folder exists or can be created, is writable, and handles Unicode level names and invalid path characters on Windows and Android.
- Verify all timeline edit categories are saved, including drag release, inspector changes, undo, and redo. Do not write to disk on every mouse-move event.
- Verify the file appears in the existing Autosaves UI and can be loaded back without losing subframes, subdivision, metadata, or supported frame-fix data.
- Protect against data loss on failed saves. On Windows, rename over an existing target may fail; the current fallback removes the old file before retrying rename. Improve replacement so a failed update does not unnecessarily destroy the last valid recovery file.
- Handle the empty-macro case intentionally. Do not advertise a useful recovery point if it cannot be loaded or has no inputs, unless empty recovery is deliberately supported.
- Avoid blocking gameplay excessively for large macros. Measure serialization/write cost; debounce only if a completed edit is still reliably flushed before the UI closes.
- Avoid collisions if two levels share a name. Include the level ID when known and have a stable fallback for unknown names.
- Test first save, overwrite, interrupted/failed write, missing folder, empty inputs, invalid path, and load-back round trip.

### 6. Other current feature groups

The project is larger than the timeline. Do not accidentally delete or disable these while repairing the editor.

- **Main menu/record layer**: multi-page menu, recording/playback toggles, macro save/load, legacy macro editor, timeline, macro details, render settings and presets, autosaves browser, keybinds and settings popups.
- **Speedhack and TPS bypass**: speed control, custom TPS, delta handling, optional speedhack audio and related runtime patches.
- **Frame stepper**: manual frame advance, pause/resume integration, and related drawing/particle update hooks.
- **CBF/substep timing**: fractional input scheduling and divider settings.
- **Practice fixes**: capture/restore player state around checkpoints and correct input/rotation-related edge cases.
- **NoClip**: collision/death avoidance configuration.
- **Show Trajectory**: trajectory visualization, fake-player movement, hitbox and portal handling.
- **Layout Mode**: level object-data transformation for a layout-focused view.
- **Coin Finder**: coin detection/visualization.
- **Clickbot**: simulated click sounds with volume and behavior settings.
- **Autoclicker**: automated button holding/releasing for players.
- **Input mirror**: options for mirroring player inputs and controls.
- **Safe Mode and gameplay toggles**: instant respawn, no death effect, no respawn flash, respawn-time handling, shader disabling, instant/no mirror portal options, and related display options.
- **Renderer**: frame capture and video rendering, render presets/settings, output folder, codec/bitrate/resolution/FPS, and audio options. The renderer can use an external FFmpeg executable on Windows or optional `eclipse.ffmpeg-api` when available; some options are platform-dependent.
- **Level-end and time/checkpoint autosaving**: recording autosaves must continue to work independently of timeline recovery.
- **Keybinds**: current implementation is guarded by `GEODE_IS_WINDOWS`. Do not claim the same hotkeys work on Android without implementing/testing that support.
- **Compatibility/UI settings**: pause-menu button, menu visibility/labels, editor keybind behavior, disabling keybinds, and settings-page restoration.

The public `mod.json` settings include these groups:
- Hotkeys: Open Menu (Alt+F), Record (Alt+G), Play (Alt+H), Speedhack (Alt+S), NoClip (Alt+N), Frame Stepper (Alt+C), Advance frame (V), Show Trajectory (Alt+T), Render (Alt+P). The manifest marks these keybinds as Windows-specific.
- Macro behavior: accuracy mode (Vanilla / Input Fixes / Frame Fixes), frame offset, and automatic stop after playback.
- Paths: macro save location, autosaves location, render location, FFmpeg path.
- UI/compatibility: endscreen button, menu color, editor keybinds, disable keybinds, recording-only keybinds, level-settings button, restore settings page, auto-disable speedhack, frame-fix limit, and lock delta.
- The menu also uses saved settings not listed as public `mod.json` options. Keep the settings map in `src/ui/record_layer.cpp` and all consumers in sync.

### 7. Important code map

| Path | Responsibility |
|---|---|
| `mod.json` | Geode metadata, target GD versions, public settings and keybinds |
| `CMakeLists.txt` | C++23 target and source list |
| `src/main.cpp` | Core gameplay hooks, recording/playback loops, resets and checkpoint behavior |
| `src/global.cpp`, `src/includes.hpp` | Global runtime state, frame/TPS helpers, seed logic, compatibility checks |
| `src/macro.hpp` / `src/macro.cpp` | Input/Macro model, save, subdivision inference and autosave logic |
| `src/gdr/gdr.hpp` / `src/gdr/gdr.cpp` / `src/gdr/json.hpp` | GDR structures and serialization/import/export |
| `src/hacks/cbf.cpp/.hpp` | CBF and fractional input scheduler |
| `src/practice_fixes/` | Player/input state correction for practice/checkpoints |
| `src/hacks/other.cpp`, `tps_bypass.cpp`, `frame_stepper.cpp` | Speed/TPS, frame stepping, death/respawn and other hooks |
| `src/hacks/show_trajectory.cpp/.hpp` | Trajectory visualization |
| `src/hacks/layout_mode.cpp/.hpp` | Layout-mode level-data transformation |
| `src/hacks/coin_finder.cpp/.hpp` | Coin detection |
| `src/hacks/clickbot.cpp/.hpp`, `autoclicker.cpp` | Clickbot audio and automated input |
| `src/renderer/renderer.cpp/.hpp`, `src/renderer/ffmpeg/` | Frame capture, audio and video encoding |
| `src/ui/record_layer.cpp/.hpp` | Main UI, settings pages and tool entry points |
| `src/ui/load_macro_layer.cpp/.hpp`, `save_macro_layer.hpp` | Macro discovery, load/import/merge and explicit save |
| `src/ui/macro_editor.cpp/.hpp` | Legacy paginated editor and macro application |
| `src/ui/macro_timeline.cpp/.hpp` | Timeline model and undo/redo state |
| `src/ui/macro_inspector.cpp/.hpp` | Selected-event inspection/editing |
| `src/ui/macro_timeline_layer.cpp/.hpp` | Timeline UI, task popup, alignment detection and trial execution |
| `src/ui/render_settings_layer.cpp/.hpp`, `render_presets_layer.hpp` | Renderer configuration |
| `src/keybinds.cpp` | Windows keybind dispatch |
| `.github/workflows/` | Builds and packaging |

## What needs to be done

Work through these priorities. If the audit discovers a more severe data-loss or crash bug, promote it and explain why.

### P0: correctness and data safety

- [ ] Reproduce/fix timeline dragging across another event. The edited input must remain the same input as timestamps change and sorting occurs.
- [ ] Keep selection and inspector bound to the correct input after edits, sorting, undo and redo.
- [ ] Make one drag equal one undo step. Keep history bounded, valid and intuitive.
- [ ] Verify macro serialization round trips: GDR, JSON, old files without subframes, `.xd` import, frame fixes, subdivision, two-player inputs, empty/invalid/corrupt files.
- [ ] Make autosave recovery safe and test load-back on Windows and Android. Preserve the last valid backup if a write fails.
- [ ] Audit analyzer cleanup on every exit and restore all global/game/CBF state.
- [ ] Fix confirmed playback, timing, restart/death, checkpoint, or scene-lifetime regressions.
- [ ] Handle invalid paths/files/metadata without uncaught exceptions, crashes, hangs or silent corrupt writes.

### P1: feature completeness and quality

- [ ] Complete timeline event editing: add/insert, delete, duplicate, clear selection/range and reasonable keyboard shortcuts. Controls must be wired and tested.
- [ ] Finish or remove misleading placeholder/unused model features, especially range selection and event-editing APIs.
- [ ] Make Task/Analyze result semantics reliable and clear.
- [ ] Verify portal object IDs against actual definitions and test representative portal types, orb types and both players.
- [ ] Make long scans responsive, cancellable and safe for large macros.
- [ ] Profile timeline refresh/rendering with large input lists.
- [ ] Show useful save/test status and errors where silent failure would mislead the user.
- [ ] Validate UI at common Windows and Android resolutions/aspect ratios.

### P2: maintainability and project hygiene

- [ ] Consider extracting task/analyzer logic from `macro_timeline_layer.cpp` after behavior is stable.
- [ ] Reduce duplicated logic for frame/subframe normalization, sorting, event lookup, state restoration and serialization.
- [ ] Audit overlapping Geode hooks so handlers compose safely and call base implementations correctly.
- [ ] Check for stale TODOs, dead controls, unsafe casts/indexing, unchecked filesystem operations, uncaught conversions, static-state leaks, and unnecessary allocation in per-frame code.
- [ ] `README.md` is empty. Add practical build/install/use instructions after behavior is stable.
- [ ] Update `ADVANCED_EDITOR_IMPLEMENTATION.md` or label it historical so future agents do not follow obsolete TODOs.
- [ ] Review CI: `multi-platform.yml` pushes to `main` and can be manually dispatched; explicitly restrict release creation to `refs/heads/main` so a manual run on another branch cannot publish a release accidentally. Review extra Android workflows tied to branches named `d` and `xD`; determine whether intentional before deleting/consolidating them.
- [ ] Keep manifest version, displayed mod version, macro format version and release version conceptually separate and documented.

## Required tests and acceptance criteria

Do not mark the work complete just because CI compiles.

### Build and CI
- [ ] Windows CMake/Geode build passes.
- [ ] Android32 and Android64 build/package jobs pass.
- [ ] No relevant compiler warnings/errors indicating unsafe behavior are ignored.
- [ ] Workflow packaging produces the expected artifact; release publishing is main-only.

### Macro and timing
- [ ] Save/load round-trip preserves frames, subframes, players, buttons, press/release, metadata and supported frame fixes.
- [ ] Old frame-only macros still load and play.
- [ ] Same-frame and close-subframe inputs remain deterministically ordered.
- [ ] Play, pause, stop, restart, death, checkpoint restore, close and macro switching do not leave stale input or game state.
- [ ] Recording-time autosave still works; timeline recovery autosave does not overwrite the original named macro.

### Timeline
- [ ] Dragging an input past multiple events never switches which input is being edited.
- [ ] Selection and inspector keep the same logical input after sorting.
- [ ] Each drag is one undo item; undo/redo reproduce exact previous/next input states.
- [ ] Frame, subframe, button, player and action changes save successfully.
- [ ] Undo and redo save the resulting state.
- [ ] A recovery file appears in Autosaves and loads successfully.
- [ ] Large macros remain responsive; mouse/touch input cannot leave a stuck drag state.

### Analyzer
- [ ] TASK scans the selected event over the configured smaller radius (currently ±6 frames).
- [ ] ANALYZE scans macro events over the broader radius (currently ±24 frames per event).
- [ ] P1/P2 and press/release tests do not alter unrelated inputs.
- [ ] Orb Y and portal X reports identify the correct interacted object and show signed offsets clearly.
- [ ] STOP, level exit, death/completion and popup close restore the exact pre-test state.
- [ ] No stale markers, callbacks, task references or CBF entries survive cleanup.
- [ ] Portal coverage claims match verified IDs.

### Regression coverage
- [ ] Smoke-test macro recording/playback, save/load/import, legacy editor, checkpoint/practice fixes, CBF timing, Windows keybinds, and renderer startup/shutdown.
- [ ] Add focused automated tests for timeline-model and serialization helpers where practical. If gameplay behavior can only be verified in-game, state exactly what was and was not tested.

## Implementation rules

- Prefer small fixes with clear invariants over a full rewrite.
- Keep code compatible with project Geode/GD targets and supported platforms.
- Do not silently discard frame fixes, subframe data, metadata or inputs.
- Do not guess object IDs or API signatures. Verify them in project definitions, bindings, existing hooks or authoritative references.
- Keep error handling explicit and user-facing when a failure would otherwise look like success.
- Do not add external dependencies without a concrete reason.
- Add comments for non-obvious lifecycle/state requirements, not comments that merely restate code.
- Every new control must have working behavior, correct disabled states and cleanup.
- If something cannot be completed, make the safest partial fix, explain the blocker, and leave precise reproduction steps.

**Definition of done:** critical correctness and data-loss risks are fixed; the existing feature set remains functional; affected paths have tests or explicit verification; Windows + Android CI passes; docs match implementation; and the final report separates verified behavior from anything still requiring in-game confirmation.
