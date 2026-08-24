# apps/

Bundled GEOBENCH applications — programs that run on top of the kernel + desktop.
Each app is a separate **SDCC `-mz80` C binary** built by `tools/build_capp.sh`
to run in a 16 KB banked window (`#4000–#7FFF`), reaching the resident kernel only
through the shared `lib/gb` API (`gb_fill`, `gb_wm_*`, the `gb_doc` document
framework, the `gb_popup`/`gb_prompt` dialogs, and opt-in `gb_button`/`gb_field`/
`gb_vscroll` widgets). Apps are loaded from disk on demand (GEOS-style), not
resident; the kernel runs the cooperative window-manager loop and calls each
focused window's handlers (issue #45).

## The apps (one C source each, under `apps/<name>/main.c`)

| App | Disk file | What it is |
|-----|-----------|------------|
| desktop  | `DESKTOP.APP`  | the root window — drive/Clock/Trash icons, drag-and-drop, the top bar + System menu, the screensaver idle trigger |
| filemgr  | `FILEMGR.APP`  | per-drive file browser (list/icon views, type→app routing, `..`, Trash) |
| viewer   | `VIEWER.APP`   | text / `.PIC` viewer (banked load for big pictures) |
| notepad  | `NOTEPAD.APP`  | text editor (word-wrap, File/Edit/View, saves `.BAS` CR+LF) |
| iconed   | `ICONED.APP`   | `.IST` icon-set / `.SPR` cursor / embedded `.APP` icon editor |
| paint    | `PAINT.APP`    | portable GBPC v2 bitmap paint (toolchest, saves `.PIC`) |
| xaos     | `XAOS.APP`     | fractal generator, exports portable `.PIC` files |
| mahjong  | `MAHJONG.APP`  | Kana Mahjong solitaire with selectable Katakana/Hiragana tiles, solvable Turtle deals, Undo and Hint |
| calculator | `CALC.APP`   | fixed-point desktop calculator with basic arithmetic, percentage and square root |
| clock    | `CLOCK.APP`    | analog clock widget |
| settings | `SETTINGS.APP` | control panel — font / icons / cursor / backdrop / wallpaper / desktop colours on CPC/MSX2 / screensaver, friendly 4-colour / 16-colour next-boot selection on MSX2, and Return to Defaults; PCW omits palette controls because its display is fixed monochrome; persisted to `GEOBENCH.CFG` |
| telnet   | `TELNET.APP`   | ANSI/Telnet terminal: CPC TCP via Net4CPC/M4 plus serial, 78x22 windowed + Mode-2 80x25 fullscreen; MSX2 TCP/IP UNAPI in a 78x22 window; PCW PerryNet/PerryFi plus serial, 80x25 windowed + 90x28 fullscreen |
| nettest  | `NETTEST.APP`  | network diagnostic — DNS `example.com`, TCP connect, HTTP GET, and PASS/FAIL status; CPC uses the active GBNET backend, PCW uses PerryNet over PerryFi |
| formref  | `FORMREF.APP`  | development reference for app-linked form composition and the first `GBAP` embedded application icon, using the Daruma artwork |
| sndtest  | `SNDTEST.APP`  | non-blocking app-linked sound diagnostic: PSG scale/noise on CPC/MSX2 and DKsound-equipped PCWs, with stock-PCW beeper fallback |
| wget     | `WGET.APP`     | GUI HTTP downloader with bounded redirects and streamed writes to an automatically derived 8.3 filename; CPC continues exact-length partial files with HTTP Range, while PCW uses PerryNet and safely restarts CP/M-record files |
| browser  | `BROWSER.APP`  | fullscreen HTTP browser for CPC, MSX2, and PCW; demand-streams into a bounded borrowed-bank cache, renders compact GET forms, simple table grids, and sequential lazy GBPC images through one image slot, hides proxy targets behind highlighted link labels, and loads/saves offline `.HTM` files without retaining a DOM |
| brsave   | `BRSAVE.APP`   | transient Browser helper that writes captured HTML source to an `.HTM` file without displacing the Browser bank |
| shell    | `SHELL.APP`    | fullscreen command shell with `ls`, `cd`, `pwd`, `cat`, `cp`, `rm`, `clear`, `help`, and `exit`; supports A/B/C paths and streamed arbitrary-size file copies |
| timesync | `TIMESYNC.APP` | PCW desktop helper — reads PerryNet's firmware clock with `TIME_GET` when `TIMESYNC=true`, then applies `TIMEZONE=+/-H` |

## Screensavers (`.SAV`)

A screensaver is just an app shipped with a `.SAV` extension. The desktop's
idle timer launches the configured one (`SAVER=<module>` / `SAVERTIME=<minutes>`
in `GEOBENCH.CFG`, set from **Settings → Screensaver**) after the idle timeout, and
System → "Activate screensaver" runs it on demand. Each is a full-screen window
(`WM_FS`) that animates every frame and closes on any input.

The Main CPC boot floppy carries `SQUARES.SAV`; the CPC floppy set carries the
remaining savers across `COMPANION.DSK` and `EXTRAS.DSK` (`XROACH.SAV`,
`CATCLK.SAV`, and `HELIX.SAV` are on Extras to preserve Companion allocation
blocks). The Albireo/M4 card and MSX2 distributions carry all 16 savers together.

| Saver | Disk file | Effect |
|-------|-----------|--------|
| saver    | `SQUARES.SAV`  | random squares (the default saver) |
| deco     | `DECO.SAV`     | Art-Deco / Mondrian rectangle subdivision — ported from the SymbOS `symsav-deco` |
| xmatrix  | `XMATRIX.SAV`  | binary "Matrix" digital rain (white → red → black glow) — ported from `symsav-xmatrix` |
| mountain | `MOUNTAIN.SAV` | configurable isometric 3D terrain — MSX Screen 7 uses eight elevation colours; four-colour and monochrome targets use compact low/high shading |
| fractalic | `FRACTALI.SAV` | Sierpinski triangle + Koch snowflake (random each cycle) — ported from `symsav-fractalic` |
| starfield | `STARFLD.SAV`  | 3D star-field flying toward the viewer (blue → red → white, black border) — inspired by `symsav-starfield`, fresh `#C000` impl |
| xroach   | `XROACH.SAV`   | 16×16 cockroaches scuttle on the blue field and scatter from a red rogue roach — ported from `symsav-xroach`, direct `#C000` sprite blit |
| pyro     | `PYRO.SAV`     | fixed-point fireworks — rockets rise and burst into shrapnel showers — ported from xscreensaver |
| forest   | `FOREST.SAV`   | recursive fractal trees with red blossoms — ported from xscreensaver |
| helix    | `HELIX.SAV`    | woven harmonograph curves (sin-table) — ported from xscreensaver |
| catclock | `CATCLK.SAV`   | Kit-Cat Klock — embedded body bitmap (from `assets/catclockbody.png` via `tools/png2catclock.py`) with sliding pupils + real hour/minute hands from `gb_time()` |
| munch    | `MUNCH.SAV`    | "munching squares" XOR moiré sweeping a power-of-two square — ported from xscreensaver |
| rorschach | `RORSCH.SAV`  | 4-fold-symmetric random-walk ink-blots — ported from xscreensaver |
| truchet  | `TRUCHET.SAV`  | random diagonal-tile maze, re-tiled every few seconds — ported from xscreensaver |
| ant      | `ANT.SAV`      | Langton's ant on an 80×50 grid (4-state rule, all four pens) — inspired by xscreensaver |
| lightning | `LIGHTN.SAV`  | midpoint-displacement forked lightning bolts that flash and re-strike — ported from xscreensaver |

The Settings **Module** picker lists every `.SAV` in the system media folder for
the selected drive, so a new screensaver appears there automatically once it is
built and staged. Saver names in `GEOBENCH.CFG` may be drive-qualified
(`A:XMATRIX`, `C:CATCLK`) for mixed floppy/card setups.

A configurable saver may carry a same-stem paged companion: `XMATRIX.MOD`,
`STARFLD.MOD`, and `MOUNTAIN.MOD` currently implement their **Configure**
dialogs. Settings discovers the companion from the selected `.SAV`, runs it
through the existing module loader, and persists the bounded key/value result.
Mountain exposes drawing speed, peak count, and completed-landscape hold time.
Savers without a `.MOD` do not add code to Settings and continue to run normally.

## App contract

An app's `main()` runs in its bank, draws its initial content, and registers a
window (`gb_wm_add` for a legacy window, or `gb_wm_managed` for kernel-drawn
chrome), then returns to the opener. The kernel's master loop then drives it:
it polls input and calls the focused window's frame / repaint / event handlers;
the app reads input with `gb_flags`/`gb_mx`/`gb_my` and calls `gb_wm_close` to
quit. Settings and saver apps should keep persistent config/media policy in the
app layer and treat the kernel as a provider of storage, WM, and reload
primitives. See `lib/gb/gb.h` for the full API.

## Reusable widgets

Common non-modal controls live in opt-in `libgb` compilation units instead of the
resident kernel. Build with only the units an application uses:

- `BUTTON=1`: buttons and rectangle hit-testing
- `WIDGETS=1`: buttons, text-field frames and rectangle hit-testing
- `SCROLL=1`: byte-range vertical scrollbars and scrollbar-part hit-testing
- `SCROLL16=1`: 16-bit-range vertical/horizontal scrollbars and pointer-to-value mapping
- `TOGGLE=1`: checkbox/toggle rendering and hit-testing
- `STEPPER=1`: decrement/value/increment controls and part hit-testing
- `SELECTOR=1`: framed popup/list choices and hit-testing
- `SLIDER=1`: horizontal sliders, hit-testing and pointer-to-value mapping
- `FORM=1`: labelled field rows, action rows and modal lifecycle; requires
  `WIDGETS=1`
- `FORM_SELECT=1`: labelled selector rows; requires `FORM=1` and `SELECTOR=1`
- `TIMESET=1`: binary `gb_set_time()` support without adding resident kernel code
- `SOUND=1`: target sound primitives without adding resident kernel code

For example:

```bash
BUTTON=1 STEPPER=1 tools/build_capp.sh apps/myapp build/MYAPP.RAW
```

The helpers are deliberately stateless: the application owns focus, text buffers,
selected values, ranges, scroll positions and actions, then calls the shared renderer
from `GB_MSG_DRAW` and the matching hit-test from `GB_MSG_CLICK`. Text-bearing
controls draw the supplied display text without editing or clipping it, so the app
applies its own scrolling/truncation. Widgets consistently use logical pens 0–3 as
canvas, surface, edge/text and accent; the existing `INKS=` palette therefore
recolours them without platform-specific code. Buttons accept
`GB_WIDGET_PRESSED` and `GB_WIDGET_DISABLED`; `gb_button_hit` rejects disabled
controls. WGET is the button/field reference, Shell and File Manager are
scrollbar references, Viewer demonstrates large vertical/horizontal scrollbars,
Settings is the selector/stepper reference, and Clock combines steppers, a
standard action row, modal form lifecycle, and the opt-in time setter. XAOS uses
compact buttons, and Icon Editor demonstrates
pressed and disabled button states. Disk Utility uses a shared command button
for its destructive format workflow. `FORMREF.APP` is staged as a development
diagnostic and demonstrates form composition without moving any state or code
into the resident kernel.

## App-linked sound

Sound follows the same opt-in model as forms and widgets:

```bash
SOUND=1 tools/build_capp.sh apps/myapp build/MYAPP.RAW
```

`gb_sound_tone(note, volume)` plays a chromatic note from C3 through B6,
`gb_sound_noise(period, volume)` starts noise, and `gb_sound_stop()` silences
the app. `gb_sound_caps()` reports pitch/noise/volume support. CPC and MSX2 use
AY/YM PSG channel A. PCW probes the DK'tronics AY at runtime and preserves its
joystick port; without that board, both start calls map to the fixed 3.75 kHz
beeper. The app owns timing and sequencing, normally from `GB_MSG_FRAME`, and
must stop sound when its window closes. There is no resident timer, sequencer,
kernel jump-table entry, or kernel-space cost.

Build `make sndtest`. Run `SNDTEST.APP` from `DIAG/` on a CPC card,
`GBENCH/` on MSX2, or the PCW Companion disk. Its Scale, Noise, and Stop
actions are also available on the `T`, `N`, and `S` keys.
