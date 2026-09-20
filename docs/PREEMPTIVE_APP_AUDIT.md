# Preemptive Application Audit

Date: 2026-08-26
Issue: [#477](https://github.com/salvogendut/geobench/issues/477)

This audit records how each GEOBENCH application fits the default preemptive
runtime. It does not assume that every application should become a scheduled
worker; most UI and I/O code remains a bounded root job.

## Responsiveness contract

GEOBENCH has three valid application execution models:

1. **Root callback**: short event, drawing, and lifecycle callbacks return to the
   window manager promptly.
2. **Bounded root job**: kernel, firmware, storage, network, bank-switching, and
   video operations remain on the root task, but long workflows advance by one
   bounded step per frame.
3. **Compute worker**: pure application-owned computation may be preempted. It
   must not call the kernel, firmware, storage, networking, bank-switching,
   drawing, modules, dialogs, or window-manager APIs.

An application is ready only when:

- ordinary input and close requests are handled between bounded work units;
- a pending operation can be cancelled or safely abandoned when its window
  closes;
- app-owned workers publish complete data units and cannot publish stale data
  after a new request or close;
- drive, directory, palette, fullscreen, sound, and interrupt state are restored
  on every exit path;
- cooperative and preemptive builds retain the same visible behavior;
- CPC, MSX2, and PCW have been exercised where the application is shipped.

There is no useful blanket conversion flag. Moving a callback that performs
kernel work into a worker would make the system unsafe rather than preemptible.

## Priority summary

| Priority | Area | Result |
|---|---|---|
| P0 | File Manager | Copy, directory preparation, and APP-icon probing are bounded root jobs and have cross-target smoke coverage. |
| P0 | Settings | Asset enumeration and icon-set validation are bounded root jobs; initial CPC/MSX2/PCW validation is complete. |
| P0 | Screensavers | All shipped savers use fixed or incremental frame budgets and have initial cross-target smoke coverage. |
| P1 | BASIC, Paint, Notepad, Icon Editor, Shell | Long graphics, document, and storage operations are bounded and have target smoke coverage. |
| P1 | Browser, WGET, Telnet | Network receive/parser and cancellation paths remain the main conversion work. |
| P1 | Viewer and Mahjong | Viewer is a bounded root renderer; Mahjong is event-driven. Resource/error feedback and worst-case measurement remain. |
| P2 | Clock and small utilities | Clock is bounded and smoke-tested; destructive and diagnostic utilities still need lifecycle edge tests. |

## P0: File Manager

Classification: **bounded root job required**. File Manager must not be a
compute worker because directory, file, icon, drag/drop, and drawing services
are kernel-owned.

Implemented:

- preemptive builds represent a drag/drop copy as app-owned state with a 24-bit
  offset and create/append/result state;
- the focused destination performs one complete read/write chunk per frame, so
  the shared transfer buffer is never left live across frame boundaries;
- the destination is raised and titled `Copying`; closing it cancels the job and
  removes the partial destination;
- other File Manager instances suspend storage operations while the copy owns
  the shared filesystem context;
- directory scans process at most four entries per frame and insert each entry
  directly into the sorted display order;
- PCW reads a short file's size from its first extent and scans the complete
  directory only when a full 16 KiB extent can have continuations;
- the free-space query runs as a separate frame step after enumeration;
- opening a drive or directory leaves the existing screen intact during the
  scan and publishes the completed title and listing in one window repaint;
- embedded `.APP` icons are probed and drawn one visible slot per frame through
  `GBAPICK.MOD`; repaint callbacks perform no storage I/O;
- PCW waits two quiet seconds before and between embedded-icon probes so real
  floppy input is not starved by continuous header reads;
- after the first successful PCW probe, File Manager retains the native icon in
  a lazily borrowed 16 KiB page. All 64 entries possible on a standard 180K
  disk fit in that cache, which is released on relist or window close;
- short scrollbar moves shift retained screen rows and paint only newly exposed
  entries; repainting a cached APP icon is a RAM blit rather than another file
  read;
- cooperative builds retain the original synchronous copy path.

Remaining risks:

- each directory entry and APP probe still requires one atomic backend
  operation on the root task; slow firmware cannot be preempted inside that
  operation;
- a missing `GBAPICK.MOD` leaves the generic APP icon, as intended, but needs a
  cross-target runtime check;
- multiple File Manager windows serialize scans and copies through the shared
  storage claim; icon probes run only while that claim is free. This still
  needs contention testing.

Required work:

1. Exercise the copy job on every storage backend, including cancellation and
   exact-multiple chunk sizes, before treating it as production-ready.
2. Exercise maximum-size directories and close a File Manager during each scan
   stage; verify the storage claim is always released.
3. Exercise generic, missing, single-codec, and dual-codec APP icons while
   scrolling and switching between list and icon views.

Acceptance tests:

- copy a 63 KiB file floppy-to-floppy, floppy-to-card, and card-to-floppy while
  moving the pointer and clock;
- cancel/close during copy, then open both source and destination directories;
- open a directory containing the maximum cached entries and embedded icons;
- repeat on CPC floppy, CPC M4/Albireo, MSX DOS drives, and PCW floppies.

## P0: Settings

Classification: **root-owned modal UI with bounded enumeration**. Settings
calls configuration storage, paged modules, live asset reload, palette changes,
and window-manager services, so it is not a worker candidate.

Implemented:

- selecting an asset or screensaver redraws only that selector as `Reading...`
  and returns to the managed-window frame loop;
- root-directory discovery and asset enumeration process at most four entries
  per frame across every available drive;
- icon-set pickers validate at most one `.IST` candidate per frame after the
  directory cursor is no longer needed;
- the completed list is handed to the existing modal popup, preserving the
  established selection, persistence, and live-reload behavior;
- closing Settings during enumeration restores the previous drive and releases
  the shared storage claim; cooperative builds retain synchronous enumeration.

Remaining risks:

- every `cfg_set()` rewrites `GEOBENCH.CFG` immediately;
- screensaver configuration modules and live titlebar/gadget/backdrop reloads
  execute atomically;
- the colour dialog owns a polling loop. This is intentional modal behavior,
  but all exits must restore modal and palette state.

Required work:

1. Validate picker cancellation and empty/missing asset directories on every
   target and storage backend.
2. Keep configuration writes and live reload calls on root, but make the UI
   explicit about the short commit operation and prevent re-entry.
3. Audit every modal exit, including ESC, missing module, disk error, and close,
   for cursor, modal, drive, palette, and repaint restoration.

Acceptance tests:

- open every picker with A/B/C present, missing, empty, and slow media;
- configure each configurable saver and cancel each dialog;
- change titlebar, gadgets, backdrop, wallpaper, colours, and MSX video mode;
- invoke Return to Defaults and close Settings at each intermediate state;
- verify the underlying File Manager remains correctly framed and opaque.

## P0: Screensavers

Screensavers are fullscreen and draw directly through kernel or target-native
video paths. They should remain **root-owned bounded frame renderers**. A worker
conversion would require a separate compute/display protocol and gives little
benefit while the desktop is intentionally hidden.

| Saver | Current bound | Audit result | Required action |
|---|---|---|---|
| SQUARES | One square per frame | Ready | Cross-target wake/exit smoke test. |
| ANT | Forty ant steps per frame; reset clears 256 grid bytes and 16 screen lines per frame | Converted | Cross-target reset and wake/exit test. |
| DECO | Four subdivision or four-row fill units per frame | Converted | Incremental stack traversal and leaf-band rendering; cross-target lifecycle test remains. |
| XMATRIX | 16 feeder columns, 512 cell updates, or 12 dirty glyph draws per frame | Converted | Cross-target speed, wake, and dense-screen test. |
| MOUNTAIN | Bounded clear, map generation, smoothing, noise, and cell drawing stages | Converted | Cross-target lifecycle and maximum-settings test remains. |
| FOREST | Four explicit recursion-frame transitions per frame | Converted | CPC/MSX lifecycle test remains. |
| STARFLD | At most 24 stars per frame with round-robin motion compensation | Converted | Test maximum stars and speed on each target. |
| FRACTALI | 16 clear lines, 192 points, four Koch expansions, or 16 segment copies per frame | Converted | Exercise both fractal types and cycle transitions. |
| MUNCH | At most 16 points per frame | Converted | Test the largest square and square transitions. |
| RORSCH | Explicit `PERFRAME` point budget | Ready | Cross-target wake/exit smoke test. |
| TRUCHET | Eight tiles per frame | Converted | Incremental tile generation; CPC/MSX lifecycle test remains. |
| LIGHTN | 32 clear lines or four main/fork bolt segments per frame | Converted | Exercise strike, fork, hold, clear, and wake transitions. |
| PYRO | 40 particles per frame with three compensated physics steps and O(1) free-slot allocation | Converted | Test maximum active-particle frame and burst reuse. |
| HELIX | Four curve segments per frame | Converted | Persistent curve state; CPC/MSX lifecycle test remains. |
| XROACH | Six roaches, updated every second frame | Ready | Cross-target movement, collision, and wake/exit test. |
| CATCLK | Fixed bitmap and hand/pupil update | Ready | Verify time, palette, and fullscreen restoration. |

All savers also need a common lifecycle test: launch-click suppression, keyboard
and mouse wake, palette/border restoration, fullscreen reset, and immediate
desktop repaint. The repeated keyboard-buffer drain loops are finite, but should
be covered by held-key tests on MSX2.

## P2: Clock

Classification: **root-owned bounded renderer; no worker required**.

- the focused-frame callback performs one time read and updates only when the
  displayed minute or second changes;
- a tick erases and redraws at most three hands plus the short digital readout;
- the Set Time form polls once per frame through the shared modal lifecycle and
  commits through the target clock service only after acceptance;
- full face construction is a fixed 30-segment rim plus 12 ticks and occurs only
  on launch, resize, fullscreen changes, or damage repaint;
- no Clock path performs filesystem, module, or network I/O, and no pure compute
  workload exists that would benefit from an app worker.

Required validation: set the time and cancel the dialog on CPC, MSX, and PCW;
toggle seconds, resize, enter/leave fullscreen, obscure/reveal the window, and
close from each state. Adding a worker or resident scheduler hook is explicitly
out of scope unless those bounded operations show measurable input latency.

## P1: Companion applications

### GB-BASIC

`BASIC.APP` remains an event-driven root-owned editor. `BASRUN.APP` executes at
most 24 ordinary statements and four console scrolls per frame. A complete
line-number or DATA scan consumes the rest of the current statement slice, so
repeated backward jumps and sparse DATA programs cannot multiply full-program
scans inside one frame.

`LINE` advances by eight pixels per frame, a filled box by four rows, outline
verticals by eight rows, and `CIRCLE` by two midpoint iterations. The graphics
state and dispatch vector live in the existing `BASRUN2.BIN` low-RAM overlay;
no kernel or resident scheduler space is used. Ctrl-C and window close remain
active while a graphics job is pending. DIM, expression, string, and INPUT
loops remain synchronous because their work is already capped by the fixed
40-element array pool, 192-byte expression arena, 25-character strings, and
56-byte input buffer.

Remaining validation: run mixed text/graphics programs on every target, abort
each graphics operation mid-draw, and exercise backward GOTO/GOSUB plus sparse
READ/RESTORE loops. Startup program/overlay loads remain atomic filesystem
operations by design.

### GB-PAINT

Paint owns banked picture storage and several windows, so it remains root-owned.
Its 20x20 editing tile keeps flood fill, shape, clipboard, and undo loops tightly
bounded. New, Load, Save, and Save As now advance in 512-byte root-owned jobs;
closing cancels the job and removes an incomplete output. Pane movement uses a
live title outline and damage-clipped final repaint. Bank mapping, drawing,
storage, and interactive drag handling remain correctly on the root task.

### Icon Editor

ICONED remains root-owned because its filesystem, borrowed app-page, picture
codec, and window operations all use kernel state. `.IST`, `.SPR`, and embedded
`.APP` documents now load and save in 512-byte jobs, retaining the full 16 KiB
application file while using low RAM only as a staging buffer. Save cancellation
removes partial output and releases the borrowed page on every close path.
Transfer status and completion repaint only the focused window unless editing
the active icon set requires the complete managed stack to be refreshed.

### Shell

SHELL remains root-owned because command execution, directory traversal, and
file transfer use the shared filesystem context. In preemptive builds, `ls`
enumerates at most four entries per frame, `cat` loads 512-byte chunks and
decodes at most 96 characters per frame, and `cp` performs one complete
512-byte read/write unit per frame. These jobs serialize through the shared
storage claim; `Esc`, `Ctrl-C`, or closing the window releases that claim, and a
cancelled or failed copy removes its partial destination. Cooperative builds
retain their original synchronous command paths.

Remaining validation: exercise empty files, exact 512-byte boundaries, long
text lines, cross-drive copies, full destinations, and cancellation on CPC
floppy/card storage, MSX DOS drives, and PCW floppies.

## P1: Remaining user applications

| Application | Classification | Main audit item |
|---|---|---|
| Browser | Bounded root network/parser job | Bound each receive, parse, image, redirect, and cache step; cancellation must close the channel. |
| WGET | Bounded root network/storage job | Verify one receive/write unit per frame and safe partial-file cleanup/resume. |
| Telnet | Bounded root network/serial job | Cap receive and ANSI parsing per frame; disconnect must cancel every transport state. |
| Viewer | Root-owned image renderer | Image-only; retains native banked rendering and falls back to bounded visible-row reads when picture banks are exhausted. |
| Notepad | Bounded root document job | Preemptive builds load/save 512 bytes per frame, serialize storage, clean partial saves, and cap input shifts to two characters per frame. Validate maximum files and deferred save actions. |
| Icon Editor | Bounded root document job | Loads and saves 512 bytes per frame while preserving borrowed-bank, preview, and active-icon-set reload state. |
| Mahjong | Root-owned game | Measure deal/shuffle/hint and full-board draws; tile interaction is otherwise event-driven. |
| Disk Utility | Root-owned destructive I/O | Formatting remains atomic kernel/module work; enforce modal state and error cleanup. |
| Nettest | Root-owned network state machine | Verify every transport state has a timeout and performs one bounded operation per frame. |
| Time Sync | Root-owned serial state machine | Verify bounded polling, timeout, cancellation, and clock-setting completion. |
| Browser Save | Root-owned storage helper | It writes all cached pages in one frame; convert to one page/chunk per frame. |
| Clock | Short root callbacks | Ready after set-time, close, and drag smoke tests. |
| Calculator | Short root callbacks | Ready after input and close smoke tests. |
| Sound Test | Short root sequencer | Ready; ensure close always stops sound. |
| Form Reference | Short root callbacks | Ready; retain as a modal/widget lifecycle diagnostic. |

## Scheduler-specific applications

- **Desktop** is the non-preemptible root task and owns input, composition,
  storage, modules, firmware, and policy. It requires scheduler lifecycle and
  interrupt restoration tests, not worker conversion.
- **TASKDEMO** is diagnostic only. It proves involuntary switching and must not
  be staged in normal preemptive images.
- **XAOS** is the reference compute-worker conversion. Its worker performs only
  fixed-point Mandelbrot computation and publishes complete rows; root code owns
  all drawing and lifecycle.

## Remaining order

1. Audit Browser, WGET, and Telnet receive/parser loops, transport cancellation,
   timeout, and partial-file behavior.
2. Finish Browser Save's chunked write path and the remaining Nettest, Time Sync,
   and Disk Utility lifecycle checks.
3. Exercise exact-boundary, cancellation, slow-media, and contention cases for
   the already bounded File Manager, Settings, BASIC, Paint, Notepad, Icon
   Editor, Shell, and screensaver paths.
4. Add explicit Viewer feedback/resource handling when the app-page pool cannot
   open a third picture window.
5. Maintain the CPC/MSX2/PCW smoke matrix as these follow-ups land.

All changes should remain app-linked or app-local. This audit identifies no
reason to add more resident kernel code before the application passes are
complete.
