# Features — where GEOBENCH is now

The current GEOBENCH desktop running on the Amstrad CPC (1984 emulator): a Mode 1
backdrop, a top bar showing total RAM and a clock, and draggable multicolour
bitmap icons (Disk, Clock, Trash) driven by a joystick (or AMX mouse) / keyboard pointer.

![Current status](../initial.png)

The ICONED icon/cursor editor open on a `.IST` set (magnified canvas, pen palette,
Prev/Next, UNDO) over a File Manager window — all co-resident under the kernel WM:

![ICONED editor + file manager](../iconed.png)

Several apps running at once: the File Manager (sorted listing), the Viewer showing
`PENGUIN.PIC` (a photo converted with `tools/picconv.py`), and the analog Clock —
all co-resident windows under the kernel window manager:

![Multiple apps: file manager, picture viewer, clock](../geobench.png)

A short capture of GEOBENCH running in the 1984 emulator — the desktop, a patterned
backdrop, dragging icons, opening apps and menus:

![GEOBENCH demo](../screenshots/geobench-demo.gif)

## What works today

- **Desktop** — a selectable **patterned backdrop** (a repeating 16×16 Mode-1 tile
  via `BACKDROP=`, or a plain colour), top bar (RAM probe + clock), draggable
  labelled icons that erase cleanly back to the pattern, a System menu (Ram Usage /
  Refresh Media / Tidy Icons / Settings / Activate screensaver / About GEOBENCH /
  Exit to DOS).
  Settings opens from the System menu — it no longer needs its own desktop icon.
- **File manager** — double-click the Disk icon to open a window listing the
  drive; a type icon + name per file (**list** or **icon** view), entries **sorted
  by type then alphabetically**, a **scrolling** list, click to select, double-click
  to open (routed to the right app by a type→app table). A **`..` entry** (with an
  up-arrow icon) goes up a directory; a **View** menu toggles list/icons and
  maximises the window. Multi-drive, drag-and-drop, a Trash.
  Offline `.HTM` files use the normal text-file icon and open directly in
  Browser when double-clicked on CPC, MSX2, or PCW.
- **Notepad** — a text editor: type/edit, word-wrap, click or cursor keys to place
  the caret, **File / Edit / View** menus (New/Load/Save/Save As, copy/paste,
  Fullscreen). Saves `.BAS` with CR+LF so CPC BASIC can load them.
- **ICONED** — an icon/cursor editor for `.IST` sets, `.SPR` cursors, and optional
  icons embedded in `GBAP` `.APP` files (magnified canvas, pen palette, Prev/Next,
  undo, New/Load/Save/Save As, View > Fullscreen). Its Load dialog hides legacy
  `.APP` files that have no editable icon header. A v2 APP may carry portable
  four-colour and MSX Screen-7 sixteen-colour variants; ICONED exposes both in
  Screen 7. Saving the configured active `.IST` reloads it immediately and
  repaints the desktop; inactive sets take effect when selected in Settings.
  The Python editor also edits the canonical ASM sources directly.
- **Paint** — a Mode-1 paint app: a canvas + toolchest (pencil, square, circle,
  flood fill, undo), a 4-ink palette and pencil width, New/Load/Save to the `.PIC`
  format (a versioned bitmap with its own size + palette), View > Fullscreen. Tool
  icons are a normal `.IST` set, editable in ICONED.
- **Viewer** — open any file to peek at it: word-wrapped text, or a `.PIC` image
  rendered to a window sized to the picture. File > Load opens another file, View >
  Fullscreen maximises. Draggable, resizeable. A large picture (over ~8.5 KB, i.e.
  bigger than the in-window buffer) is loaded into a borrowed 16 KB RAM bank, so on a
  bare 128K machine — where the desktop, file manager and viewer already use every
  app bank — a big image shows an empty window; a 256K+ expansion (any spare bank)
  displays it. Small pictures always work.
- **Clock** — an analog clock window (Dallas RTC, else software); View > Fullscreen
  rescales the face to the whole screen, an Options menu sets the time / toggles seconds.
- **Settings** — a control panel for `GEOBENCH.CFG`: pick the **font** (`.FNT`),
  **icon set** (`.IST`), **cursor** (`.SPR`), standalone **title-bar theme**
  (`.TBR`), independently selectable **window gadgets** (`.GDT`), **backdrop** pattern (`.BDP`, or
  `SOLID`) and a centred **wallpaper** (`.PIC`) from the system folder, a **Colours**
  editor for the 4 Mode-1 pens + the screen border (`INKS=`) with a **live** preview
  — `-`/`+` recolours the whole desktop instantly — and a **Screensaver** section
  (**Module** picker, modular per-saver **Configure**, and idle **Timeout**).
  Settings pages in an optional same-stem `.MOD`, so adding saver controls does
  not grow Settings. Starfield controls speed and star count; XMatrix controls
  binary/Kana glyphs and speed, plus its main color on CPC and in MSX 16-color
  mode; Mountain controls speed, peak count, and hold time. The MSX selector
  includes a native-color swatch. On MSX2 the video
  choices are labelled
  **4 colors** and **16 colors** (Screen 6 and Screen 7).
  **Return to Defaults** restores the complete target-specific configuration.
  Media settings are stored as
  **drive-qualified names** such as `A:DARKER` or `C:XMATRIX`, so Settings can
  browse either floppy or Albireo content without ambiguity. Invalid media
  falls back safely to `SOLID` / `NONE` at boot instead of blocking startup. The
  icon-set picker can **filter by icon count**. Launches from the System menu.
- **WGET** — enter a plain HTTP URL, select an available floppy/card drive, and
  stream the response directly to an automatically derived 8.3 filename. The
  CPC build uses Net4CPC or M4 TCP; the PCW build uses PerryFi/PerryNet. HTTP
  content-length, connection-close, chunked response bodies, and up to four
  absolute or relative redirects are supported. On CPC, an existing partial
  file is continued only after the server validates its exact offset with a
  `206 Content-Range`; a server that ignores Range restarts the file safely.
  PCW downloads restart because CP/M records cannot preserve an arbitrary byte
  offset. HTTPS is intentionally out of scope without TLS.
- **Browser** — a small fullscreen HTTP browser for CPC, MSX2, and PCW. It accepts plain
  `http://` URLs, follows up to four redirects, parses headers and chunked bodies
  through the shared HTTP parser, and streams HTML through a bounded text renderer
  instead of keeping a DOM. Text, headings, lists, compact GET forms, link labels,
  inline-image records, and bounded table-row records are cached in one borrowed
  16K page: up to 208 fixed-width rows. Simple tables buffer up to four source
  cells at a time and display as a centered three-column grid on MSX2 or a
  two-column grid on CPC and PCW; wider rows reflow into another display row.
  Cell text and linked images remain independently clickable. Spanning cells,
  CSS layout, and nested-table geometry are not retained. Link destinations are retained separately
  from their visible labels, so proxy transport URLs are not printed in the page.
  Browser renders the first viewport, pauses the open TCP stream, and
  resumes from the exact retained byte only when the user scrolls downward;
  cached lines remain available immediately when scrolling upward. The
  proportional scrollbar includes a continuation segment while more data is
  available. Reaching the platform cache bound is explicitly reported as truncated.
  If no spare page exists, Browser reports limited cache mode and retains the
  latest seven lines. Link labels stand out
  as underlined rows and open when clicked, and Back retains one previous URL;
  visible images are fetched sequentially through one bounded GBPC v2 slot (up
  to 160x96 pixels each), drawn directly, and replaced as the user scrolls.
  On CPC and MSX2, a bounded second pass retries a transiently failed visible
  image. On MSX2 Screen 7, successful
  reservation of one additional app-pool page adds `X-GBPC: 7,1` to every page
  and image request, allowing a proxy to return a sixteen-colour mode-7 image.
  Screen 6, CPC, PCW, and low-memory Screen-7 sessions omit the header and keep
  the portable four-colour mode-1 path. CSS, JavaScript, POST forms,
  general image decoding and HTTPS are not implemented; a configured proxy can
  convert ordinary web images to GBPC. The File menu
  loads and saves offline `.HTM` source files; Save As always produces an 8.3
  `.HTM` name. Settings configures an optional HTTP proxy and writes `PROXY=` in
  `GEOBENCH.CFG`; Direct clears it. Source capture uses up to three available
  borrowed pages (48 KiB) while reserving a page for the save worker. An active
  Screen-7 image page reduces source capture to two pages (32 KiB) so that same
  save-worker reserve remains available. The proxy itself must use plain HTTP.
- **Telnet** — an ANSI/VT100 terminal with Telnet negotiation. CPC uses
  Net4CPC/M4, MSX2 uses TCP/IP UNAPI, and PCW offers PerryNet TCP or raw serial.
  The MSX2 terminal is a 78x22 window in either configured video mode; CPC additionally offers its
  direct-framebuffer 80x25 mode.
- **Shell** — a fullscreen command-line file shell with scrollback and familiar
  `ls`, `cd`, `pwd`, `cat`, `cp`, and `rm` commands. Paths accept A/B/C drive
  prefixes and 8.3 components; `cat` and `cp` stream files in chunks rather than
  limiting operations to the app's free RAM.
- **Calculator** — a compact windowed fixed-point calculator with keyboard and
  pointer input, basic arithmetic, percentage, square root, sign and clear
  operations, and a black display with red digits.
- **Kana Mahjong** — a fullscreen 144-tile Mahjong solitaire game using an
  original C engine and selectable 42-face Katakana or 36-face Hiragana tile
  sets. New deals are assigned along a known legal Turtle-layout removal order,
  guaranteeing a solution. Matching always uses identical Kana; Game provides
  New, Undo, Hint and Quit, while Tiles starts a new deal with either alphabet.
- **Screensavers** — self-contained apps shipped with a `.SAV` extension. The
  desktop's idle timer (global, so it fires over any app) launches the configured
  module after the timeout; any pointer move / click / key returns to the desktop.
  The Main CPC boot floppy carries the default **SQUARES** saver; the Companion
  and Extras floppies together, the Albireo/M4 card distribution, and the MSX2
  distribution carry the full set: **SQUARES** (random squares), **DECO**
  (Art-Deco panels), **XMATRIX**
  (configurable binary/Kana "Matrix" rain), **MOUNTAIN** (isometric 3D terrain), **FRACTALI**
  (Sierpinski + Koch), **STARFLD** (3D star-field), **XROACH** (scattering
  cockroaches), **MUNCH** (munching squares), **RORSCH** (symmetric ink-blots),
  **TRUCHET** (tile maze), **ANT** (Langton's ant), **LIGHTN** (forked lightning),
  **PYRO** (fireworks), **FOREST** (fractal trees), **HELIX** (harmonograph), and
  **CATCLK** (a Kit-Cat Klock with sliding pupils + real hour/minute hands). The
  Settings **Module** picker lists every `.SAV` in the system folder (scrolling
  when there are more than fit), so new ones appear automatically. **Configure**
  loads an optional same-stem `.MOD` and persists the module's returned options
  in `GEOBENCH.CFG`. STARFLD exposes speed and star count; XMATRIX exposes
  binary/Kana glyphs and speed; MOUNTAIN exposes speed, peak count, and hold
  time. Mountain uses eight height bands in MSX 16-color mode, a temporarily
  black display border, and restores the desktop palette on exit. CPC and MSX
  16-color mode also expose XMatrix's main glyph color; XMatrix restores the
  launch-time desktop palette when it exits and always uses a black background.
- **One menu system for the whole UI (`gb_doc`)** — every app gets the **same menus**
  from one place: it registers a small "document" (its buffer + new/open/save hooks)
  and the framework supplies a standard **File** menu (New / Load / Save / Save As),
  an **Edit** menu (Select All / Copy / Paste over a **shared cross-app clipboard**)
  and a **View** menu (**Fullscreen** + app-specific entries), plus a navigable
  Open/Save file dialog. Apps carry no menu or dialog code; even the desktop's
  **System** menu and the clock's **Options** menu render through the same path. The
  heavy dialog renderer lives in a **paged kernel module**, so it isn't duplicated
  into every bank.
- **Banked app model** — the desktop and every app are separate binaries, paged
  into expansion-bank slots and run co-resident with the kernel; shared window
  chrome (drag/resize) lives in `libgb`.
- **Hybrid implementation** — the kernel is Z80 assembly; **every app is C**.
