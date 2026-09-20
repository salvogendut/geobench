; kernel/lowram.inc - fixed low-RAM contracts for the resident kernel.
;
; Low RAM stays in main memory while the #4000-#7FFF window is banked, so the
; kernel, apps, and paged modules use these absolute cells as shared contracts.
; Keep this synchronized with kernel/lowram.tsv and validate with
; tools/check_lowram_map.py.

WM_MAXREQ       equ   #1309        ; #156: 1 = the maximize gadget was clicked; gb_doc_frame toggles
                                   ; (#130A is the app-side WM_FS fullscreen flag, #163)
PIC_PAGE        equ   #130B        ; #164 banked Viewer picture buffer: the borrowed app-pool page
PIC_WB          equ   #130C        ; (0 = none) + the parsed .PIC header the Viewer reads back:
PIC_H           equ   #130D        ; width in bytes, height in rows (16-bit #130D/E - tall pics
PIC_OFF         equ   #130F        ; exceed 255, #166), and the bitmap offset (6 v1 / 14 v2).
PIC_PAGE2       equ   #1348        ; optional second picture bank for .PIC files >16K.
PIC_OFS         equ   #1349        ; transient k_pic_open chunk loader state; after open, file size.
PIC_EDIT_BUF    equ   #134B        ; GB_PICEDIT: app-side 100x100 tile buffer pointer.
PIC_EDIT_OFF    equ   #134D        ; GB_PICEDIT: source/destination bitmap offset in the banked .PIC.
BANK_CUR        equ   #134F        ; shadow of the current #4000-#7FFF page; low so PICEDITL can update it.
                ifdef PLATFORM_MSX
; The MSX hardware pointer has no software save-under, so its otherwise-unused
; #1291 cursor buffer holds the extended Viewer state. Four mapper segments let
; a full 512x212 4bpp Screen-7 picture (54,272 bytes plus header) fit.
PIC_PAGE3       equ   #1291
PIC_PAGE4       equ   #1292
PIC_MODE        equ   #1293        ; GBPC packing: 1 = canonical 2bpp, 7 = Screen-7 4bpp
PIC_STRIDE      equ   #1294        ; source bytes per picture row (word)
                endif

                ifdef GB_ROM                  ; #152: the floppy read backend (fs_amsdos) is offloaded
FS_RDIO_LOWRAM  equ   1                       ; to GEOBENCH.ROM; its scratch state must live in fixed
GB_ROM_STUBS    equ   1                       ; low RAM (ROM is read-only). This resident build replaces
                include "../lib/fs_rom_lowram.inc"  ; the fs_amsdos read code with thin gb_rom-call stubs.
                if STORAGE_ALBIREO == 0
; #152: share the FAT core state with GEOBENCH.ROM at #1C00 (IDE only - the CH376 owns its
; filesystem in firmware, so the Albireo backend has no fs_fat32_core state to relocate; its
; fs_dir_clus is its own resident defs, which fs_fat_lowram.inc would collide with). The IDE
; read + write paths both use it; the write's fs_mount restores fs_dir_clus to the browse dir
; so the resident's directory position survives a save. Reclaims the core-state defs too.
FS_STATE_LOWRAM equ   1
FS_STATE_BASE   equ   #1C00
                include "../lib/fs_fat_lowram.inc"  ; FAT core state addresses (shared with the ROM)
                else
; #152: the Albireo CH376 I/O backend is offloaded to GEOBENCH.ROM; its persistent path
; state (alb_path/fsalb_mounted) + per-call save args live in fixed low RAM the ROM agrees on.
FS_ALB_LOWRAM   equ   1
                include "../lib/fs_alb_lowram.inc"  ; alb_path #1293 + save args (shared with the ROM)
                endif
                endif

; Config transfer area - resident low RAM (stays main RAM under banking, so the
; paged-in GBCFG module can reach it). The kernel fills text/len, runs the
; module, then reads the parsed ICONS=/FONT= stems back. See kernel/kc/.
KCFG_TEXT       equ   #1000        ; GEOBENCH.CFG contents (<=512 bytes)
KCFG_LEN        equ   #1200        ; word: cfg-text byte count (0 = no file)
KCFG_ICONNAME   equ   #1202        ; 11-byte 8.3 icon filename, built by the module
KCFG_FONTNAME   equ   #120D        ; 11-byte 8.3 font filename, built by the module
KCFG_MEMKB      equ   #1218        ; word: total RAM in KB (kernel -> module)
KCFG_MEMSTR     equ   #121A        ; "<decimal>K" top-bar string (module -> kernel)
KCFG_CURSORNAME equ   #1221        ; 11-byte 8.3 cursor filename (CURSOR=), built by module
KCFG_INKS       equ   #122C        ; 5 bytes: the 4 Mode-1 pen inks + border (INKS=), built by module
KCFG_BDPNAME    equ   #1231        ; 11-byte 8.3 backdrop tile filename (BACKDROP=), built by module
KCFG_BDDRIVE    equ   #123C        ; 0/1/2 = C/A/B for drive-qualified BACKDROP=, #FF = boot/system
WM_OPEN_STRICT  equ   #123D        ; one-shot: next gb_wm_open loads from current drive's /GBENCH
BD_TILE         equ   #1250        ; 64 bytes: the loaded 16x16 backdrop tile (.BDP), or unused if solid
BD_SOLID        equ   #1290        ; 1 = plain pen-0 backdrop (BACKDROP=SOLID / absent / missing file)
; CLOCK.APP support (#72): a tiny time read-out (kept minimal - the BCD->binary
; conversion lives in the C app; the clock draws its hands by calling the firmware
; graphics directly, so no resident line primitive is needed).
GB_TIME_BUF     equ   #1240        ; raw time: +0 hours(&3F), +1 min, +2 sec, +3 binmode
BOOT_SP         equ   #1244        ; word: SP at kernel_main entry (GB_EXIT, #74)
GB_KSIZE        equ   #1246        ; word: resident kernel size in bytes (System>Ram Usage)
                                    ; (binmode 0 = BCD regs, nonzero = already binary)
BAR_HANDLER     equ   #1248        ; word: top-bar handler, run every WM frame in PAGE_APP0
                                    ; regardless of focus (0 = none); bar-as-app expt (#77)
TITLE_READY     equ   #124F        ; issue #458: paged GBTITLE renderer/tile is installed

; Callback API (issue #32) - kernel->app event delivery, state in low RAM so it
; costs no resident image bytes (low RAM stays main RAM under banking).
APP_HANDLER     equ   #1300        ; word: registered app event handler (0 = none)
GB_MSG          equ   #1302        ; message record: type, p0, p1, p2 (4 bytes)
GB_MSG_MENU     equ   1            ; message type: top-bar click, p0 = byte column
GB_MSG_DROP     equ   2            ; message type: a file was dropped on this window
GB_MSG_DRAW     equ   3            ; managed-window messages (#148): dispatched to the
GB_MSG_CLICK    equ   4            ; single gb_mwin_t.proc, keyed by gb_msg.type. The
GB_MSG_FRAME    equ   5            ; proc switches on it (one WndProc per window).
GB_MSG_CLOSE    equ   6
GB_MSG_DRAG     equ   7
                                   ; (DnD, #62); name at WM_DRAGNAME, pos at POLL_MX/MY
POLL_MX         equ   #1306        ; last poll: cursor byte column (mx)
POLL_MY         equ   #1307        ; last poll: cursor line (my)
POLL_FLAGS      equ   #1308        ; last poll: flags (bit0 click, bit1 quit, bit2 fire)
MENU_DEF        equ   #1310        ; app menu: count, then {col, 8-byte label} *count
                                   ; #1335..#1337: fs_cur_drive/fs_boot_drive/fls_browse
KCFG_FRAMEPEN   equ   #133C        ; Edge pen, or Text/Accent when Edge matches Paper
REPAINT_HDLR    equ   #1340        ; retired per-depth repaint handlers (8 bytes).
                                   ; PREEMPTIVE reuses this dead block for scheduler state,
                                   ; so the experimental scheduler consumes no new low RAM.
                if PREEMPTIVE
SCHED_LOCK      equ   #1340        ; non-zero while kernel/module code is atomic
SCHED_RESERVED  equ   #1341        ; reserved scheduler state byte
SCHED_CURRENT   equ   #1342        ; current WM/task slot, #FF before task dispatch
SCHED_QUANTUM   equ   #1343        ; ticks left in the current quantum
SCHED_RUNNABLE  equ   #1344        ; runnable task count (desktop/system task included)
SCHED_FLAGS     equ   #1345        ; scheduler-wide flags (reserved)
SCHED_STACK_MAX equ   #1346        ; largest sampled fixed-stack use (bytes)
SCHED_FAULT     equ   #1347        ; non-zero when a context exceeds the 255-byte reserve
                ifdef PLATFORM_MSX
SCHED_BASE      equ   #C900        ; fixed page-3 RAM, visible during H.TIMI/mapper calls
                else
SCHED_BASE      equ   #3C00        ; fixed low RAM; top 512B of old module transfer area
                endif
SCHED_INIT_ENTRY equ  SCHED_BASE
SCHED_YIELD_ENTRY equ SCHED_BASE+3
SCHED_TASK_ENABLE_ENTRY equ SCHED_BASE+6
SCHED_IRQ_UNINSTALL_ENTRY equ SCHED_BASE+9
                endif

; Window manager (issue #45) - the kernel owns the master loop and a table of
; co-resident windows; each app registers one and becomes callback-driven. State
; in low RAM (0 resident image bytes). Phase 2: multiple windows + click-to-focus.
WM_NWIN         equ   #1350        ; number of live windows
WM_FOCUS        equ   #1351        ; slot index of the focused window
WM_TABLE        equ   #1352        ; WM_MAXWIN * WM_ESZ-byte entries:
WM_ESZ          equ   25           ;   page,x,y,w,h, on_frame(2),on_repaint(2),
WM_MAXWIN       equ   8            ;   on_event(2), menu(2), flags(1), arg(11)
                                   ; #84: 8 = desktop + 7 windows (table #1352..#1419)
WM_Z            equ   #141A        ; z-order: WM_NWIN slot indices, [0]=bottom
WM_FPREV        equ   #1422        ; previously focused slot (menu-swap edge detect)
WM_DRAGNAME     equ   #1423        ; drag-and-drop (#62): dragged 11-byte 8.3 name,
                                   ; app-visible (the drop target reads it on GB_MSG_DROP)
WM_DRAGSRC      equ   #142E        ; source window slot of the active drag
WM_DRAGMOV      equ   #142F        ; bit0 = pointer moved; bit7 = preemptive drop job claimed
WM_DRAGX0       equ   #1430        ; last drag position (byte col / line); movement
WM_DRAGY0       equ   #1431        ; vs this drives both the ghost and click/drag tell
WM_DRAGDRV      equ   #1432        ; source drive of the active drag (for cross-drive copy)
WM_DRAGDIR      equ   #1433        ; source directory cluster (4 bytes), captured at start
; --- app bank-page pool (#84): built at boot from the detected RAM. The old fixed
; 3-bit page bitmap (first expansion bank only) is gone; the pool now spans every
; detected bank, capped at WM_MAXWIN. The desktop (first claim) always gets [0]. ---
APP_NPAGES      equ   #1437        ; usable app-page count (3 on 128K, up to WM_MAXWIN)
APP_PAGES       equ   #1438        ; WM_MAXWIN port values; [0] = PAGE_APP0 (#C5, desktop)
APP_BUSY        equ   #1440        ; WM_MAXWIN busy flags (0 free / 1 busy), parallel
MW_RECT         equ   #1448        ; managed window (#146): the WM publishes the focused
                                   ; managed window's live x,y,w,h here before each hook
                                   ; so the app reads its rect with gb_wm_x/y/w/h
; #188: relocated resident scratch pool #1450..#148F (free gap #144C..CUR_LOW #1500).
; gtd_scratch #1450 (49B), cb_tdir #1481 (4B), launch_arg #1485 (11B) live here so they
; cost 0 resident image bytes. Do not reuse this range for other low-RAM data.
; #196: extended #1490..#14BB for the bootsplash reclaim (more write-before-read scratch
; moved out of the resident image): fslf_blocks #1490 (16B). GBNET reuses the same
; #1490..#149F transfer selector block, with #149C..#149F holding the M4 socket/state
; across module reloads (#261). pm_newx #14A0 (2B), poll_byte
; #14A2, poll_line #14A3, then screen.asm fill/blit/save scratch #14A4..#14BB (bm_*, bl_*,
; fb_rows/cy, fbw_*, sb_rows/cy/ptr, fb_x/y/w/h), and text.asm renderer scratch
; #14BC..#14CC (br_*, dc_*). gf_* uses #14CD..#14D1; fs dispatch/request
; state uses #14D2..#14FE. video_mode_hint #14FF (high bit valid) lets paged
; modules preserve Mode 2 fullscreen when touching the Gate Array register.
MW_MANAGED      equ   2            ; WM_FR_FLAGS bit: this window is kernel-managed (#146)
                if PREEMPTIVE
WM_TASK_RUNNABLE equ  8            ; WM_FR_FLAGS bit 3: bank has a valid task snapshot
                endif
GHOST_W         equ   8            ; drag ghost outline box: 8 byte-cols x 16 lines
GHOST_H         equ   16           ; (an icon footprint); save-under in fs_secbuf
CUR_LOW         equ   #1500        ; low-RAM cursor sprite buffer (#65): DEFAULT.SPR is
                                   ; loaded here at boot so the bitmaps aren't resident
                                   ; (256B used; 512 reserved to #16FF for the sector copy)
PICEDIT_BASE    equ   #1600        ; CPC GB_PICEDIT helper, appended to shipped .SPR files.
CLIP_LEN        equ   #3E00        ; shared clipboard (#142): word = length, data #3E02..
CLIP_DATA       equ   #3E02        ; #3FFF (510 bytes). Non-banked low RAM above gb_copybuf
CLIP_CAP        equ   510          ; (which ends #3E00), so it survives app switches.
; Paged UI-dialog service (#142): apps marshal a request into this low-RAM block, the
; kernel pages in the GBUI module (#6000 in PAGE_DATA) and CALLs it; it renders the
; dialog (popup/prompt/file-picker) and writes the result back here. The file-write
; modules reuse #1700..#1712 for their control header after modal UI returns.
UI_OP           equ   #1700        ; op: 1 popup, 2 prompt, 3 pickfile, 4 pickdir, 24 About
UI_COL          equ   #1701        ; arg: column
UI_LINE         equ   #1702        ; arg: line
UI_N            equ   #1703        ; arg: popup label count / prompt maxlen
UI_RES          equ   #1704        ; result byte (popup row/0xFF, prompt 1/0, pick 1/0)
UI_MODAL        equ   #1705        ; 1 while the UI module runs -> menu_dispatch skips the app
; #1706..#1707 are transient GBAPICK PCW icon-cache page/slot arguments.
UI_TEXT         equ   #1708        ; packed text: popup labels / prompt caption+buffer / exts
WM_FR_X         equ   1            ; entry field offsets (after +0 page):
WM_FR_FRAME     equ   5
WM_FR_REPAINT   equ   7
WM_FR_EVENT     equ   9
WM_FR_MENU      equ   11
WM_FR_FLAGS     equ   13
WM_FR_ARG       equ   14           ;   11-byte 8.3 file arg, captured at gb_wm_add
