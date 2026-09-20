# Preemptive Multitasking

Issue [#477](https://github.com/salvogendut/geobench/issues/477) introduced the
incremental conversion from the callback-driven cooperative window manager to
preemptively scheduled application workers. Preemptive scheduling is now the
default for CPC, MSX, and PCW release builds. Explicit cooperative builds remain
available for regression testing.

The app-by-app scheduler classification, responsiveness contract, and migration
order are maintained in
[PREEMPTIVE_APP_AUDIT.md](PREEMPTIVE_APP_AUDIT.md). In particular, applications
that call storage, networking, firmware, bank-switching, drawing, or window
services remain on the root task and must split long work into bounded jobs;
they are not safe compute-worker candidates.

## Non-ROM Architecture

Preemption does not use `GEOBENCH.ROM`, the M4 ROM, or a ROM-backed CPC kernel.
`tools/build_scheduler.sh` assembles a target-specific scheduler payload of at
most 512 bytes. The preemptive build embeds that payload in `DESKTOP.APP`; the
desktop copies it to fixed RAM and initializes it before entering the window
manager:

- CPC and PCW: `#3C00-#3DFF`;
- MSX2: page-3 RAM at `#C900-#CAFF`.

The same CPC binary model therefore works with floppy, Albireo, and M4 storage.
`PREEMPTIVE=1` is deliberately rejected when `GB_ROM_REQ=1` so a development
build cannot accidentally acquire a GEOBENCH-ROM dependency.

The CPC timer adapter does use the computer's standard firmware interrupt
handler, as the cooperative system already does for input and timing. That is a
machine service, not scheduler storage: all GEOBENCH scheduler code remains in
RAM and arrived from disk with `DESKTOP.APP`.

## Execution Model

The desktop is the root task. It continues to own the compositor, input,
firmware calls, storage, paged modules, and window-manager policy. Once per WM
cycle it yields only when at least one worker is runnable.

An application opts in by placing a pure compute callback in
`gb_mwin_t.task_worker`, registering that managed window, completing its initial
paint, and calling `gb_task_enable()`. The normal managed-window `proc` remains
on the root task and continues to receive draw, frame, menu, click/drag, and
close messages. Lifecycle and UI code therefore never runs in the preemptible
worker context. Existing applications leave `task_worker` null and continue to
run cooperatively without source changes.

The worker may only operate on its own computation state. It must not call the
kernel, paged modules, firmware, storage, drawing, or other shared UI services.
The compositor-side `proc` samples worker state when it paints.

The CPC, MSX2, and PCW timers can interrupt a worker that never yields. They cannot
preempt the resident kernel, a paged module, firmware, storage code, or the
compositor. Blocking I/O therefore still blocks the system at this stage.

## Context And Stack

The Z80 has no protected mode or hardware task context. Every active task uses
the platform's existing fixed stack. At a switch, the scheduler saves all main
and alternate registers and copies only the live `BOOT_SP-SP` bytes into the
owning application bank at `#7F00-#7FFF`. Restoring a task copies those bytes
back before restoring its registers.

This design consumes 256 bytes in each participating application bank instead
of allocating eight fixed-RAM stacks. `tools/build_capp.sh` requires
`TASK_STACK_RESERVE=256` for task builds and rejects any linked image whose
code, data, or BSS reaches the reserved range.

Eight scheduler-state bytes reuse the retired `REPAINT_HDLR` block at
`#1340-#1347`. The scheduler's emergency stack overlays `#1450-#1480` only while
application execution is interruptible; that scratch is not live then.

## CPC Interrupt Path

The CPC adapter installs a three-byte jump in writable IM-1 vector RAM at
`#0038` when the app-carried scheduler initializes. A six-tick quantum at the CPC's
300 Hz interrupt rate gives a nominal 50 Hz scheduling slice.

On a worker interrupt, the adapter switches only when all of these checks pass:

- execution is in the mapped application page (`#4000-#7FFF`);
- the mapped bank still belongs to the current WM slot;
- scheduler/kernel work is not locked.

Fast paths tail-call the normal CPC firmware interrupt handler immediately. If
a switch is due, the scheduler first restores the selected task's registers and
stack, then tail-calls that same handler. Firmware therefore receives its exact
native interrupt stack contract on every tick; keyboard, pointer, clock, sound,
and firmware events continue at their normal rate. The scheduler also preserves
the alternate Z80 register set used internally by the CPC firmware. Exit to
BASIC/DOS restores the standard `JP #B941` IM-1 vector.

## MSX2 Interrupt Path

MSX-DOS leaves a writable IM-1 trampoline at `#0038`. The scheduler replaces
only that trampoline's JP target when it initializes and
saves the original DOS target for tail chaining. This runs before the BIOS maps
ROM into page 0 for H.TIMI, so GEOBENCH low RAM and mapper state remain visible.

A two-interrupt quantum gives nominal 25 Hz PAL or 30 Hz NTSC task slices. The
same application-page, mapper-bank, and scheduler-lock checks used by the CPC
adapter gate every switch. Fast paths and completed switches both tail-chain the
saved DOS handler, which continues through the normal BIOS/H.TIMI path; the
clock tick and UNAPI hook therefore keep running. Exit restores the saved DOS
trampoline before H.TIMI is restored and mapper segments are released.

## PCW Interrupt Path

Standalone GEOBENCH owns the PCW and keeps the floppy controller's interrupt
route disabled; its storage driver polls the raw FDC status instead. The
scheduler can therefore use the ASIC's independent 300 Hz maskable timer as its
sole interrupt source. A six-tick quantum gives a nominal 50 Hz task slice.

The adapter replaces the standard PCW MCU-bootstrap bytes at `#0038` with an
IM-1 jump and acknowledges each timer tick by reading port `#F4`. Fast and
switched paths return directly with `RETI`; no absent firmware or CP/M handler
is involved. Exit disables interrupts, restores the canonical `F0 21 F5`
bootstrap sequence, and then performs GEOBENCH's normal warm reboot through
`#0000`.

## Build And Diagnostic

Use `make cpc`, `make msx`, `make pcw`, or `make all` for the default
RAM-resident preemptive distributions. The direct `tools/build_kernel*.sh`
scripts also default to `PREEMPTIVE=1`. The scheduler is embedded automatically
in `DESKTOP.APP`; no scheduler file or ROM is required on the target media.
The compatibility targets `make cpc-preemptive`, `make msx-preemptive`, and
`make pcw-preemptive` produce the same builds. Use `make cpc-cooperative`,
`make msx-cooperative`, or `make pcw-cooperative` for regression images without
the scheduler. None of these targets stages or launches `TASKDEMO.APP`. The PCW
CF2 boot disk is already full, so its preemptive build omits Browser Save;
Browser remains on the Companion disk. The explicit diagnostic build also omits
the spare `IMPROVED.TBR`.

`TASKDEMO.APP` is the deterministic test worker. Its compute callback never
yields, so a responsive desktop while it runs proves timer preemption rather
than cooperative progress. Build it with `make taskdemo`; it is not staged in
normal or ordinary preemptive distributions. Use
`make msx-preemptive-diagnostic` or `make pcw-preemptive-diagnostic` to stage
and auto-open it for emulator stress tests.

`XAOS.APP` is the first production application with an opt-in worker. On CPC,
MSX2, and PCW the worker performs only fixed-point Mandelbrot calculations and
publishes complete rows. Its normal window callback remains on the root task
and owns row conversion, drawing, menus, input, files, and lifecycle. Starting
a new view increments a generation counter so a suspended calculation cannot
publish a stale pixel after zooming or panning. Cooperative builds retain the
original bounded per-frame renderer.

`VIEWER.APP` remains root-managed because filesystem calls, bank mapping and
target-native blitters are kernel-owned operations. It is image-only: the normal
path retains the native banked renderer, while bank exhaustion falls back to
bounded visible-row reads rather than requiring another picture bank. Text files
remain the responsibility of Notepad. Moving byte conversion alone into a worker
would add synchronization without removing kernel work from the root task.

`NOTEPAD.APP` is also root-managed. Its app-linked document framework loads and
saves one 512-byte chunk per focused frame in every build, including launch
files. The job owns the shared storage context, blocks edits while its buffer is
in transit, and removes a partial output if saving fails or the window closes.
Dirty-document Save-then-New, Save-then-Load, and Save-then-Close actions resume
only after a successful write. Its fixed 4 KiB text transformations remain
root-owned, typing is capped at two insert/delete shifts per frame, and its
app-linked compact scrollbar moves the view three lines per click.

`FILEMGR.APP` keeps storage on the root task but advances a drag/drop copy by one
complete read/write chunk per focused frame. The destination window is raised
and titled `Copying`; closing it cancels the operation and removes the partial
file. Other File Manager instances suspend storage actions while the job owns
the shared transfer context. Directory scans process at most four entries per
frame and insertion-order entries as they arrive; the free-space query is a
separate frame step. PCW retains the first four CP/M directory sectors in the
dedicated 2 KiB low-RAM directory buffer. Bounded enumeration, free-space
calculation, chunked file lookup, and APP-icon lookup therefore reuse those
sectors instead of restarting real floppy reads. The app-facing enumeration
path also skips exact multi-extent size calculation because no app consumes
that value; raw filesystem callers retain it, and file loads still process all
extents. The completed title and listing are published together in one repaint;
partial scan state is never exposed. Repaint callbacks use generic APP
placeholders and perform no storage I/O. `GBAPICK.MOD` then probes and draws at
most one visible embedded APP icon per frame. PCW waits briefly for the window
to appear, then completes a compact probe batch using one 512-byte data-sector
read per APP. Its first successful probe also stores the native four-colour icon
in a lazily borrowed 16 KiB page. A standard 180K PCW disk has at most 64
directory entries, so every icon fits; subsequent repaints and short scrollbar
moves blit retained RAM rather than reopening APP files. The cache is
invalidated when the directory changes and released when the window closes.
Normal cooperative builds retain their original synchronous File Manager path.

Publishing a newly opened opaque managed window uses `GB_REPAINTTOP`, so the
window is drawn without invoking Desktop and every lower window's repaint
handler. Closing a window damages its former rectangle plus one icon-cell guard
at the right (whole glyph/icon blits may cross a clip edge), then recomposes the
remaining layers inside that area. Moves, shrinking resizes, and modal popups
still use the bottom-up compositor because they expose content from more than
one layer.

Settings asset and screensaver pickers also remain root-owned. In preemptive
images they discover asset directories and enumerate at most four entries per
frame, then validate at most one icon set per frame before opening the existing
modal popup. A shared storage claim covers the job and is released on completion
or window close; normal images retain the synchronous picker path.

The MSX2 rotation and lifecycle checks run without a GUI:

```sh
MSX_HEADLESS=1 MSX_SCRIPT=debug/msx_preempt_probe.tcl tools/run_msx.sh QA/MSX/GBMSX.IMG
MSX_HEADLESS=1 MSX_SCRIPT=debug/msx_preempt_lifecycle.tcl tools/run_msx.sh QA/MSX/GBMSX.IMG
```

They write telemetry to `build/msx/preempt-probe.txt` and
`build/msx/preempt-lifecycle.txt` respectively.

The PCW diagnostic boots in `1985` with both workers already open:

```sh
../1985/1985 --config debug/1985-pcw.conf \
  --disk-a QA/PCW/Floppies/GEOBENCH.DSK
```

The emulator's keyboard pointer fallback is sufficient for lifecycle testing:
use the cursor keys to move and Space to click. Close each task independently,
then use System > Exit to exercise the warm-boot vector restoration.

## Budget

- Explicit cooperative `PREEMPTIVE=0` resident-kernel cost: **0 bytes**.
- CPC claimed-drop handoff in `PREEMPTIVE=1`: **12 resident bytes**. The current
  Albireo kernel retains one byte above the required stack reserve; the M4
  kernel sits exactly at the enforced 256-byte reserve.
- Scheduler image: **at most 512 bytes**, carried by the desktop and installed
  in fixed RAM. The current PCW adapter occupies the complete 512-byte slot;
  CPC and MSX2 retain a small amount of scheduler headroom.
- Scheduler state: **0 new low-RAM bytes**, reusing eight retired bytes.
- Participating application reserve: **256 bytes per app bank**.
- CPC preemptive transfer buffer: **6.5 KiB** (`#2200-#3BFF`) instead of the
  cooperative build's 7 KiB; arbitrary-size copy remains chunked.

Every preemptive payload records the largest copied fixed-stack context in
`SCHED_STACK_MAX` and latches `SCHED_FAULT` if a context exceeds 255 bytes. This
telemetry lives inside the already reserved scheduler state and does not add
resident-kernel bytes.

## Platform Status

- **CPC:** RAM-resident context engine and firmware-compatible timer adapter
  are implemented. Two simultaneous non-yielding workers have run for an
  extended emulator stress test with exact 300 Hz firmware-time progression,
  a 32-byte maximum observed context, and no stack fault.
- **MSX2:** the DOS IM-1 adapter, mapper-aware context switch, close/input
  lifecycle, and exit restoration are implemented. Two simultaneous
  non-yielding workers rotate with BIOS ticks advancing, a 32-byte maximum
  observed context, and no stack fault. Closing each worker independently
  returns focus and runnable count to the desktop before DOS-vector restoration.
- **PCW:** the ASIC 300 Hz timer adapter and warm-boot vector restoration are
  implemented. `1985` traces show deterministic root/task-A/task-B rotation
  while both workers remain in their non-yielding loops. Boot-time and app-load
  floppy I/O complete with the hook active. Closing task B leaves task A
  advancing with `Tasks=1` and `Fault=0`; closing task A returns cleanly to the
  desktop. A manual System > Exit smoke test also completed the expected PCW
  warm reboot after restoring the interrupt bootstrap.

The scheduler path and the XAOS compute-worker integration build on all three
targets. Preemption is the release default; applications are converted
individually where a pure worker or bounded root job improves responsiveness.
The cooperative targets remain regression profiles, not the normal
distribution.
