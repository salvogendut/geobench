# Bringing the rest of GEOBENCH to AgonLight2

**Baseline:** `port/agonlight2` at `911523f`. This is a proposed sequence, not a list of features already shipped. Use the tracked [MSX card](../QA/MSX/CARD/GBENCH) and [MSX2 behaviour](MSX2.md) as the acceptance reference: 20 applications (including helpers), 16 screensavers and 11 modules. The goal is the same desktop workflows and compatible **data**, not an MSX emulator or a promise that MSX executables run on Agon.

## What to keep

The [Agon card](../QA/AGON/CARD) already boots from MOS, shows the SD icon and wallpaper, and supports PS/2 mouse and keyboard pointer input. Its three guest binaries are `CALC.APP`, `FILEMGR.APP` and `PICVIEW.APP` (mode 1, 6 and 7 pictures). Windows can move; File Manager and picture preview can resize. The System menu, RTC clock, Settings, Squares and Blank work as resident accessories. Settings writes its own preferences; that is **not** a general filesystem write service. See the [current build and controls](BUILDING.md#experimental-agonlight2-target).

Use the MSX Screen 7 feature set (four desktop pens, sixteen-colour artwork), but keep Agon's working 320x240 layout. Do not copy 512x212 coordinates, V9938 commands, BIOS work-area addresses, DOS drive letters, UNAPI calls or MSX mapper ports. Reuse app sources, file formats, algorithms, menus and tests. Translate at the GEOBENCH service boundary. Calculator running unchanged is a useful ABI test, not a general compatibility guarantee.

## Milestones

Each milestone ends with a runnable `QA/AGON/CARD/`. Keep the previous working card available for board comparisons. M1-M3 are the critical path; the later application batches can land independently once their listed services exist.

| Milestone | Work | Board acceptance |
|---|---|---|
| **M0. Freeze the baseline** | Record board MOS/VDP versions alongside Fab Agon Emulator's firmware. Inventory the [public calls](../lib/gbapp.inc) against the [Agon runtime](../kernel/agon/runtime.asm): implemented, intentionally unsupported or silently stubbed. Record code/stack headroom, guest pages owned by GEOBENCH and VDP bitmap usage separately. Add a diagnostic build, not an auto-started test on the normal card. | Cold boot, move/resize Files with keyboard, open/close System over Files, wake the saver, exit and relaunch from MOS. Capture the same scenes and timings before further changes. |
| **M1. Open, edit and save one document** | Replace the three hard-coded guest cases and exact-build signatures with a validated target/ABI-aware app catalogue or header. Keep rejecting truncated, wrong-target and oversized guests without disturbing the parent. Back the app file calls with MOS: open/read/seek, create/write/close and rename/delete for safe replacement. Implement just the shared menus, file picker and clipboard calls needed by Notepad; use FORMREF to exercise the controls. Wire `.TXT` to NOTEPAD. | From Files open a scratch text file, edit, Save As, exit GEOBENCH and read the result in MOS. Reopen it after reboot. Cancel, full card and failed replacement must preserve the original; a failed launch must leave Files usable. |
| **M2. Independent windows and memory** | Replace the single suspended-parent path with per-instance app state, stacks, file contexts and owned memory handles. Prototype the fixed 16 KiB guest ABI on eZ80 RAM; compare copying on dispatch with segment-based execution before choosing. Measure two guests alternating callbacks on the board. Add focus, z-order, clipping, scroll damage, modal ownership and close cleanup. Keep bitmap restore for transient menus, not as the source of truth for overlapping windows that change beneath them. | Keep Files, an edited Notepad and a picture open together. Change focus, move windows, close the middle one, run the saver and recover the exact visible scene and each app's state. Scrolling must not reread icons or repaint the desktop. Resource exhaustion must report an error, not silently ignore a launch. |
| **M3. Responsive desktop and preemption** | Deliver frame events even without input, bounded root jobs and cancellation. Keep MOS, VDP, filesystem, modules and WM calls on the non-preemptible root; interrupt only pure app computation. Prove timer chaining, Z80/ADL context and stack preservation, MBASE handling, and return to MOS **before** enabling preemption by default. Use the existing [scheduler contract](PREEMPTIVE_APP_AUDIT.md), not a worker per app. | A compute-only TASKDEMO worker advances without yielding while input, time and Files still work. Then run XAOS, close it mid-render, exit and relaunch from MOS repeatedly. Keep the cooperative diagnostic build until both pass on the board. |
| **M4. Finish the everyday desktop** | Bring over shared Desktop, File Manager, Settings and Clock behaviour; retire the corresponding bootstrap UI only after replacement. Include app icons/associations, per-window directories, copy/cancel, rename, folders, Trash, full screen, RAM/About and Exit to MOS. Support fonts, cursors, icon sets, independent title motifs/gadgets, backdrop, wallpaper and live palette changes. Preserve current Agon preferences or migrate them once with defaults. | Copy between two directories without disturbing either view. Cancel a copy without leaving a partial file. Change a title motif over Files; only affected windows repaint. Settings survive reboot and invalid or missing assets fall back safely. |
| **M5. Documents, games and sound** | Replace PICVIEW with shared VIEWER: scroll, four zoom levels, 4/16-colour decoding and bounded visible-row fallback. Port SHELL, ICONED, Kana MAHJONG and app-linked sound/SNDTEST. Test normal and 16-colour embedded app icons. Keep the current preview available until Viewer passes. | Open three pictures when memory permits; hit a limit deliberately and get useful feedback. Compare a zoomed area with the same source pixels on MSX. Edit an icon and reopen it, run Shell copy/cat, match Kana tiles, and verify Scale/Noise/Stop including silence after close. |
| **M6. BASIC and Paint** | Make Agon backends in the owning GB-BASIC and GB-PAINT repos, then pin or stage matching builds here. Include BASIC editor, BASRUN and BASRUN2 overlay; Paint needs several independently managed windows, bounded file jobs, sixteen pens and PAINT.IST. Do not fork their editors into this repo. | Run and interrupt ART.BAS; re-edit and rerun it. Open four- and sixteen-colour PICs, fill an enclosed shape, undo, save and reopen on both Agon and MSX. Pixel edits should touch only the canvas and selection, not the toolchest. |
| **M7. External savers and their options** | Move from two built-ins to the common external saver lifecycle. Port the full set in batches (below), then the three same-name configuration modules. Keep Blank as a low-resource fallback. Reuse the verified save/restore ordering. | For each saver: manual start, idle start over an open document, wake by key and mouse, verify palette, pointer, menu and windows are restored. A missing module must not prevent launch; memory pressure must not destroy the desktop. |
| **M8. Browser, networking and disk tools** | Port offline HTML and BRSAVE against ordinary files first. Put online Browser/Telnet behind a tested Agon transport, not MSX UNAPI or an assumed Wi-Fi stack. First prove connect, bounded receive, timeout, cancel, disconnect and absence on the actual setup. Start DISKUTIL with read-only media info. No format of the running SD card; enable destructive operations only for a separately identified, disposable target. | Offline browsing and Save As work with no adapter. Confirm unavailable transport immediately; with supported hardware test plain HTTP, proxy/direct and Telnet with a disconnect mid-session. Exercise disk operations on disposable media only. |

## Coverage and release boundaries

- **Modules:** bring up GBUI and GBCFG for shared dialogs/configuration, GBAPICK and GBTITLE for icons/chrome, GBIMG for pictures, GBWEB for Browser, SPLASH and SPLASHD for startup. Port the *services* where an Agon implementation already exists; do not ship an unused MSX binary solely to match a filename. Keep STARFLD, MOUNTAIN and XMATRIX option panels outside Settings.
- **Savers:** start with SQUARES, DECO, MUNCH, RORSCH and TRUCHET to prove the loader and primitives. Follow with ANT, LIGHTN, PYRO, HELIX and FRACTALI. Finish with FOREST, MOUNTAIN, STARFLD, XMATRIX, XROACH and CATCLK; exercise native colours, bitmap glyphs and RTC where appropriate.
- **Shared assets:** stage both icon sets, fonts, cursors, title bars, gadgets, backdrop tiles, gallery and BASIC examples. Keep existing portable GBPC, IST and GBAP artwork readable by the desktop tools; tag native code compatibility separately from icon compatibility.
- **Beyond MSX parity:** NETTEST, WGET and time sync need a supported transport; schedule them after M8, not as prerequisites for the offline desktop. Do not stage MSX boot files, raw MSX diagnostics, floppy images or mapper drivers. Where a desktop action has no Agon equivalent, hide it or give an explicit reason.
- **Release gate:** build the card from a clean checkout (including declared companion-app inputs); check its inventory against the MSX list with explicit Agon substitutions. Run `make check` and `make test-agon`, boot a fresh card on the physical board, exercise writes on a disposable copy, and verify exit/relaunch restores MOS input, video and interrupts. Run existing-target smoke tests when shared code changes. Record emulator and hardware results separately.

## First work package

**M1/M2 in progress on the branch:** the Agon catalogue, Notepad association,
navigable Open/Save As picker and verified text-file replacement are on the
card. Three guest pages can be open together; only the focused guest receives
callbacks. **M4 Files:** independent directory windows, create folder, rename
and cancellable single-file copy/paste now use the MOS storage service. On a
scratch board card, copy a file between two open folders, then cancel another
transfer and check both directories in MOS. Physical-board persistence, focus/drag latency and restore under
memory pressure still need checking. FORMREF and general file services remain
outstanding. **M3 diagnostic:** a separate card now runs TASKDEMO or the XAOS
Mandelbrot worker on its own stack, including while Files has focus. Fab
checks the completed pixels against an independent Q4.12 reference, cancel
mid-render and relaunch. **AgonLight2 hardware check passed (2026-11-28,
user-tested):** XAOS continued rendering while browsing Files, closed
mid-render and reopened successfully. GEOBENCH returned to MOS and relaunched
with keyboard input working. **Normal-card rollout:** XAOS now uses the same
opt-in timer worker, without the diagnostic counter. If the timer is occupied,
it renders in bounded foreground batches. The normal card adds definition
Open/Save and portable PIC export. Fab verifies all 4,480 exported pixels,
reopening from Files, rejected input preserving the save target, and a
completed foreground render while TASKDEMO owns the timer. The diagnostic
hardware result does not cover those file workflows; retest them on a spare SD. This is one targeted
compute worker, not general app preemption. See the
[card instructions](BUILDING.md#experimental-agonlight2-target).

Start with the ABI/memory inventory and a tiny storage probe on a scratch SD card, not another resident accessory. Have File Manager launch a second build of the same Agon app without changing the kernel and reject an incompatible one. Then deliver the **Files -> Notepad -> Save As -> reopen in MOS** path. It is useful on its own and tests the loader, storage and UI contracts before BASIC or Paint starts depending on them.

**Hardware references:** the [AgonLight2 board specification](https://www.olimex.com/Products/Retro-Computers/AgonLight2/open-source-hardware) lists 512 KiB of external CPU SRAM; that is not a promise of 512 KiB of free app memory. Use the [MOS API](https://agonplatform.github.io/agon-docs/mos/API/) for file, RTC and interrupt capabilities and the [VDP mode reference](https://agonplatform.github.io/agon-docs/vdp/Screen-Modes/) for display choices. Measure available resources on the supported firmware rather than assuming emulator limits.
