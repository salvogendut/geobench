; gbkern - GEOBENCH banked-kernel skeleton (Phase 2).
;
; Resident at GB_KERNEL (#8000), it owns the machine and the stack and never
; lives in the #4000-#7FFF window, so it can page apps and its own data buffers
; in and out. It exposes a fixed API jump table (see lib/gbapp.inc), loads a
; real app binary off disk into PAGE_APP0, and runs it there.
;
; Services with banked buffers follow the page-swap discipline: a service saves
; the caller's page (bank_cur), swaps to PAGE_DATA, touches its buffer, and
; restores the caller's page. gb_text_draw is the first real one - it renders
; the 6x8 font, whose glyphs live in PAGE_DATA. Strings come from the caller's
; page, so they are copied to a resident scratch BEFORE swapping to the font.
;
; Build: tools/build_kernel.sh   Run: 1984 --memory=128 --disk-a=build/gbkern.dsk --autostart=GBKERN

                include "../lib/gbapp.inc"
                include "../lib/firmware.inc"

; STORAGE_ALBIREO (#104): build-time drive-0 backend select. 0 (default) = the
; SYMBiFACE IDE FAT32 backend; 1 (pass -DSTORAGE_ALBIREO=1) = the Albireo CH376
; backend. The two are mutually exclusive - a machine has one card or the other -
; so only one is ever resident, which keeps the IDE FAT32 core off an Albireo
; build (the chip does FAT itself) and the budgets comfortable on both.
                ifndef STORAGE_ALBIREO
STORAGE_ALBIREO equ   0
                endif
                ifndef SPIKE                  ; #130: -DSPIKE=1 builds a minimal storage spike -
SPIKE           equ   0                       ; load DESKTOP.APP right after fs_init, report, hang
                endif
                ifndef GB_ROM_REQ             ; #152: -DGB_ROM_REQ=1 routes the offloadable drivers
GB_ROM_REQ      equ   0                       ; through GEOBENCH.ROM if present. Both the IDE and the
                endif                          ; Albireo build can offload (floppy read + the backend).
                if GB_ROM_REQ
GB_ROM          equ   1
                endif
; #156: the window maximize/restore + close-X title-bar gadgets are resident chrome, so they
; only build where there is headroom - the GB_ROM (offloaded) kernels, or the smaller Albireo
; kernel. The plain no-ROM IDE kernel is full, so it keeps the simple close box.
                if GB_ROM_REQ | STORAGE_ALBIREO
WM_GADGETS      equ   1
                endif
WM_MAXREQ       equ   #1309        ; #156: 1 = the maximize gadget was clicked; gb_doc_frame toggles
                                   ; (#130A is the app-side WM_FS fullscreen flag, #163)
PIC_PAGE        equ   #130B        ; #164 banked Viewer picture buffer: the borrowed app-pool page
PIC_WB          equ   #130C        ; (0 = none) + the parsed .PIC header the Viewer reads back:
PIC_H           equ   #130D        ; width in bytes, height in rows,
PIC_OFF         equ   #130E        ; and the bitmap offset (6 v1 / 14 v2).

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
; CLOCK.APP support (#72): a tiny time read-out (kept minimal - the BCD->binary
; conversion lives in the C app; the clock draws its hands by calling the firmware
; graphics directly, so no resident line primitive is needed).
GB_TIME_BUF     equ   #1240        ; raw time: +0 hours(&3F), +1 min, +2 sec, +3 binmode
BOOT_SP         equ   #1244        ; word: SP at kernel_main entry (GB_EXIT, #74)
GB_KSIZE        equ   #1246        ; word: resident kernel size in bytes (System>Ram Usage)
                                    ; (binmode 0 = BCD regs, nonzero = already binary)
BAR_HANDLER     equ   #1248        ; word: top-bar handler, run every WM frame in PAGE_APP0
                                    ; regardless of focus (0 = none); bar-as-app expt (#77)

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
REPAINT_HDLR    equ   #1340        ; per-depth full-repaint handlers (4 words), for
                                   ; restoring the background under a moved window (#43)

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
WM_DRAGMOV      equ   #142F        ; 1 once the pointer moved (a drag, not a click)
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
MW_MANAGED      equ   2            ; WM_FR_FLAGS bit: this window is kernel-managed (#146)
GHOST_W         equ   8            ; drag ghost outline box: 8 byte-cols x 16 lines
GHOST_H         equ   16           ; (an icon footprint); save-under in fs_secbuf
CUR_LOW         equ   #1500        ; low-RAM cursor sprite buffer (#65): DEFAULT.SPR is
                                   ; loaded here at boot so the bitmaps aren't resident
                                   ; (256B used; 512 reserved to #16FF for the sector copy)
CLIP_LEN        equ   #3E00        ; shared clipboard (#142): word = length, data #3E02..
CLIP_DATA       equ   #3E02        ; #3FFF (510 bytes). Non-banked low RAM above gb_copybuf
CLIP_CAP        equ   510          ; (which ends #3E00), so it survives app switches.
; Paged UI-dialog service (#142): apps marshal a request into this low-RAM block, the
; kernel pages in the GBUI module (#6000 in PAGE_DATA) and CALLs it; it renders the
; dialog (popup/prompt/file-picker) and writes the result back here.
UI_OP           equ   #1700        ; op: 1 popup, 2 prompt, 3 pickfile, 4 pickdir
UI_COL          equ   #1701        ; arg: column
UI_LINE         equ   #1702        ; arg: line
UI_N            equ   #1703        ; arg: popup label count / prompt maxlen
UI_RES          equ   #1704        ; result byte (popup row/0xFF, prompt 1/0, pick 1/0)
UI_MODAL        equ   #1705        ; 1 while the UI module runs -> menu_dispatch skips the app
UI_TEXT         equ   #1708        ; packed text: popup labels / prompt caption+buffer / exts
WM_FR_X         equ   1            ; entry field offsets (after +0 page):
WM_FR_FRAME     equ   5
WM_FR_REPAINT   equ   7
WM_FR_EVENT     equ   9
WM_FR_MENU      equ   11
WM_FR_FLAGS     equ   13
WM_FR_ARG       equ   14           ;   11-byte 8.3 file arg, captured at gb_wm_add

                org   GB_KERNEL
; --- fixed API jump table (order is the ABI; see lib/gbapp.inc) -----------
                jp    kernel_main            ; GB_INIT  #8000
                jp    k_cls                  ; GB_CLS   #8003
                jp    k_noop                 ; GB_PRINT #8006 (dead stub -> shared ret, #148)
                jp    k_noop                 ; GB_QUIT  #8009 (dead stub -> shared ret, #148)
                jp    gb_text_draw           ; GB_TEXT   #800C
                jp    gb_open_window         ; GB_WINDOW #800F
                jp    gb_fs_dir_first        ; GB_DIR1   #8012
                jp    gb_fs_dir_next         ; GB_DIRN   #8015
                jp    k_vsync                ; GB_BLITE  #8018 (removed: mapping->FM, #103)
                jp    cursor_show            ; GB_CURSHOW #801B (was jp k_curshow; #148)
                jp    k_poll                 ; GB_POLL    #801E
                jp    k_frame                ; GB_FRAME   #8021
                jp    cursor_erase           ; GB_CURHIDE #8024
                jp    k_noop                 ; GB_LAUNCH  #8027 (dead stub -> shared ret, #148)
                jp    focus_arg_ptr          ; GB_GETARG  #802A (k_getarg collapsed, #148)
                jp    launch_app             ; GB_RUN     #802D (k_run collapsed, #148)
                jp    k_icon                 ; GB_ICON    #8030
                jp    k_fill                 ; GB_FILL    #8033
                jp    k_saverect             ; GB_SAVERECT    #8036
                jp    k_restorerect          ; GB_RESTORERECT #8039
                ifdef WM_GADGETS
                jp    k_pic_open             ; GB_PICOPEN     #803C (#164; was dead GB_XORFRAME)
                else
                jp    k_ret0                 ; no banked picture support -> A=0 (Viewer falls back)
                endif
                jp    k_fsload               ; GB_FSLOAD      #803F
                jp    k_fssave               ; GB_FSSAVE      #8042
                jp    k_getkey               ; GB_GETKEY      #8045
                jp    k_vsync                ; GB_VSYNC       #8048
                jp    k_onevent              ; GB_ONEVENT     #804B
                jp    k_menu                 ; GB_MENU        #804E
                jp    k_setname              ; GB_SETNAME     #8051
                ifdef WM_GADGETS
                jp    k_pic_blit             ; GB_PICBLIT     #8054 (#164; was dead GB_ONREPAINT)
                else
                jp    k_noop                 ; (never called when GB_PICOPEN returned A=0)
                endif
                jp    wm_repaint_all         ; GB_RESTPAR     #8057 (was jp k_restore_parent; #148)
                jp    k_wm_run               ; GB_WMRUN       #805A
                jp    wm_register            ; GB_WMADD       #805D (was jp k_wm_add; #148)
                jp    k_wm_open              ; GB_WMOPEN      #8060
                jp    k_wm_close             ; GB_WMCLOSE     #8063
                jp    k_wm_setpos            ; GB_WMSETPOS    #8066
                ifdef WM_GADGETS
                jp    k_pic_close            ; GB_PICCLOSE    #8069 (#164; was dead GB_WMLAUNCH)
                else
                jp    k_noop                 ; (never called when GB_PICOPEN returned A=0)
                endif
                jp    k_isdir                ; GB_ISDIR       #806C
                jp    k_chdir                ; GB_CHDIR       #806F
                jp    k_back                 ; GB_BACK        #8072
                jp    k_entname              ; GB_ENTNAME     #8075
                jp    k_drag_start           ; GB_DRAGSTART   #8078
                jp    k_fs_delete            ; GB_FSDELETE    #807B
                jp    k_drive_poll           ; GB_DRIVES      #807E
                jp    fs_set_drive           ; GB_SETDRIVE    #8081 (A = drive)
                jp    k_get_drive            ; GB_GETDRIVE    #8084
                jp    k_copy_begin           ; GB_COPYBEGIN   #8087 (-> drag source ctx)
                jp    k_wm_launch_as         ; GB_WMLAUNCHAS  #808A (HL = app name)
                jp    k_time                 ; GB_TIME        #808D (-> GB_TIME_BUF h,m,s)
                jp    k_exit                 ; GB_EXIT        #8090 (leave -> AMSDOS)
                jp    k_copy_end             ; GB_COPYEND     #8093 (restore target ctx)
                jp    k_on_bar               ; GB_ONBAR       #8096 (HL = bar handler, #77)
                jp    k_wm_setsize           ; GB_WMSETSIZE   #8099 (A = w, L = h, #81)
                jp    k_vsync                ; GB_BLITEFULL   #809C (removed: mapping->FM, #103)
                jp    k_icon_half            ; GB_ICONHALF    #809F (A=slot B=x C=y, #103)
                jp    k_mxp                  ; GB_MXP         #80A2 -> HL = pointer pixel x (#114)
                jp    k_clip_set             ; GB_CLIPSET     #80A5 (HL=src, DE=len) shared clipboard (#142)
                jp    k_clip_get             ; GB_CLIPGET     #80A8 (HL=dst, DE=max) -> BC=copied
                jp    k_clip_len             ; GB_CLIPLEN     #80AB -> BC = clipboard length
                jp    k_ui                   ; GB_UI          #80AE run the paged dialog module -> BC=UI_RES
                jp    k_wm_managed           ; GB_WMMANAGED   #80B1 register a kernel-managed window (#146)
                jp    k_wm_damage            ; GB_WMDAMAGE    #80B4 set the repaint clip = a damage rect (#153)

; ---------------------------------------------------------------------------
kernel_main
                ld    (BOOT_SP),sp           ; entry SP, for GB_EXIT's clean return (#74)
                ld    hl,0                    ; #142: empty the shared clipboard at boot
                ld    (CLIP_LEN),hl          ; (else the first paste reads a garbage length)
                ld    a,1
                call  SCR_SET_MODE           ; mode 1, screen cleared
                call  TXT_CUR_DISABLE        ; no blinking firmware cursor blob
                call  TXT_CUR_OFF
                call  KM_DISARM_BREAK        ; ESC is ours, not a reset-to-BASIC key:
                ld    a,#C9                   ; disarm the break event AND patch the
                ld    (#BDEE),a               ; KM TEST BREAK indirection to RET (that's
                                              ; what actually resets on ESC; disarm alone
                                              ; doesn't touch it)
                call  disarm_joy_keys        ; joystick keys must not feed the char
                                              ; buffer, or moving the pointer floods
                                              ; gb_getkey (the keyboard is the joystick)
                call  set_palette            ; GEOBENCH 4-pen palette
                call  fs_init                ; pick storage backend (floppy here)
                ifdef GB_ROM                  ; #152: detect GEOBENCH.ROM before the first read
                call  gb_rom_probe
                endif
                ld    a,(fs_boot_drive)      ; #134: on the card (drive 0), point the system dir
                or    a                       ; at /GEOBENCH if present (else flat root); skip on
                call  z,fs_sys_resolve       ; a floppy boot drive (no subdirectories)
                if SPIKE
                ; #130 SPIKE harness (-DSPIKE=1): isolate the storage read - run the real
                ; boot's pre-load setup, load DESKTOP.APP into a plain low buffer (no banking),
                ; then HANG on the border (WHITE = loaded, RED = not found). Never reboots.
                call  input_init             ; faithful context: the same setup the real boot
                di                            ; runs between fs_init and its first load, so the
                call  mem_detect             ; mount lands correctly
                ei
                call  bank_normal            ; mem_detect leaves the bank config dirty
                ld    hl,spike_name
                ld    de,fs_req_name
                ld    bc,11
                ldir
                ld    hl,#3F00
                ld    (fs_load_max),hl
                ld    hl,#4000               ; plain low buffer (no bank paging)
                ld    (fs_load_dst),hl
                call  fs_load_file
                jr    nc,spike_fail
                ld    bc,#1A1A               ; WHITE held = DESKTOP.APP loaded OK in isolation
                call  SCR_SET_BORDER
spike_hang      jr    spike_hang
spike_fail
                ld    bc,#0606               ; RED held = file not found / load refused
                call  SCR_SET_BORDER
                jr    spike_hang
spike_name      db    "DESKTOP APP"  ; #130 SPIKE: the file to test-load from the card
                endif
                call  input_init             ; enable the SymbiFace mouse if present
                di                            ; probe RAM BEFORE PAGE_DATA is filled
                call  mem_detect             ; (it pokes every bank's #4000)
                ei
                ld    (md_banks),a           ; keep the raw bank count for app_pool_init (#84)
                ld    l,a                     ; total KB = 64 + banks*64
                ld    h,0
                add   hl,hl
                add   hl,hl
                add   hl,hl
                add   hl,hl
                add   hl,hl
                add   hl,hl
                ld    de,64
                add   hl,de
                ld    (KCFG_MEMKB),hl        ; -> KCFG_MEMSTR (formatted by GBCFG)
                call  cfg_boot               ; parse GEOBENCH.CFG (C module) ->
                                             ; KCFG_ICONS / KCFG_FONT (before the
                                             ; font/icon loaders consume them)
                call  font_init              ; load the font into PAGE_DATA
                call  icon_init              ; load the icon set into PAGE_DATA
                call  cursor_init            ; load the cursor sprite into low RAM (#65)
                call  clock_init             ; detect RTC + seed the software clock (#77:
                                              ; the desktop draws the bar, not the kernel)
                ld    hl,WM_NWIN            ; clear WM low-RAM state (counts, focus, table
                ld    de,WM_NWIN+1          ; incl. alive flags, z-order)
                ld    bc,WM_Z+WM_MAXWIN-WM_NWIN-1
                ld    (hl),0
                ldir
                call  app_pool_init          ; build the app-page pool from md_banks (#84)
                ld    a,#FF                   ; no previous focus yet -> first map_focus
                ld    (WM_FPREV),a           ; installs the desktop's menu
                ld    hl,kern_end-GB_KERNEL  ; resident kernel size -> GB_KSIZE (Ram Usage)
                ld    (GB_KSIZE),hl
                ld    hl,name_desktop        ; the desktop is the first app
                call  launch_app             ; run DESKTOP in PAGE_APP0 (the WM master loop
                                              ; runs forever; only GB_EXIT gets past here)
km_finish                                      ; reached by k_exit's longjmp
                ei
                ld    a,2                     ; back to BASIC's 80-col mode
                call  SCR_SET_MODE
                ret                           ; -> AMSDOS / BASIC

; k_exit (GB_EXIT #8090): leave GEOBENCH and return to AMSDOS. The WM master loop
; never returns, so longjmp out of the whole nested call chain by restoring SP to
; the boot value, then fall into km_finish's clean exit (#74).
k_exit
                di
                call  bank_normal             ; main RAM at #4000-#7FFF (for BASIC)
                ld    sp,(BOOT_SP)
                jp    km_finish
name_desktop    db    "DESKTOP APP"

; disarm_joy_keys: the CPC joystick is keyboard matrix row 9 (key numbers 72..77 =
; up/down/left/right/fire1/fire2). By default those keys translate to characters,
; so moving the pointer (= the joystick) floods the firmware key buffer and any
; app that calls gb_getkey (Notepad) redraws on every move. Set their normal /
; shift / control translation to &FF (no character). KM_GET_JOYSTICK / KM_TEST_KEY
; read the matrix directly and are unaffected, so the pointer still works.
disarm_joy_keys
                ld    c,72
djk_loop
                ld    a,c
                ld    b,#FF
                push  bc
                call  KM_SET_TRANSLATE
                pop   bc
                ld    a,c
                ld    b,#FF
                push  bc
                call  KM_SET_SHIFT
                pop   bc
                ld    a,c
                ld    b,#FF
                push  bc
                call  KM_SET_CONTROL
                pop   bc
                inc   c
                ld    a,c
                cp    78
                jr    c,djk_loop
                ret

; --- palette -------------------------------------------------------------
INK_DESKTOP     equ   1            ; blue  -> pen 0 (paper / backdrop)
INK_LIGHT       equ   26           ; white -> pen 1 (text)
INK_DARK        equ   0            ; black -> pen 2 (outlines / title bar)
INK_ACCENT      equ   6            ; red   -> pen 3 (accents)
set_palette
                ld    a,0
                ld    b,INK_DESKTOP
                ld    c,INK_DESKTOP
                call  SCR_SET_INK
                ld    a,1
                ld    b,INK_LIGHT
                ld    c,INK_LIGHT
                call  SCR_SET_INK
                ld    a,2
                ld    b,INK_DARK
                ld    c,INK_DARK
                call  SCR_SET_INK
                ld    a,3
                ld    b,INK_ACCENT
                ld    c,INK_ACCENT
                call  SCR_SET_INK
                ld    b,INK_DESKTOP
                ld    c,INK_DESKTOP
                jp    SCR_SET_BORDER

; --- input + cursor services ---------------------------------------------
; GB_CURSHOW draws the pointer (apps call it once after drawing their UI); the jump
; table goes straight to cursor_show now (#148, reclaimed the k_curshow wrapper).

; k_poll: frame-paced input poll. Moves the cursor by the held directions and
; redraws it; returns B = cursor byte col, C = cursor line, D flags: bit0 = a
; fresh click (fire just pressed), bit1 = quit (ESC). Interrupts must be on so
; the firmware scans the keyboard.
k_poll
                call  input_poll             ; in_dx/in_dy + in_fire/in_quit
                call  poll_move              ; -> DE = new x, HL = new y (clamped)
                call  MC_WAIT_FLYBACK        ; pace to 50 Hz; do the move in the blank
                call  cursor_move_to         ; erases+redraws ONLY if the position
                                             ; actually changed (re-drawing in place,
                                             ; e.g. holding a key into the edge clamp,
                                             ; corrupts the save-under). In the flyback
                                             ; blank so the move isn't seen mid-frame.
                                             ; MUST precede clock_tick: it consumes the
                                             ; DE/HL target that clock_tick would clobber.
                call  clock_tick             ; keep the top-bar clock live
                ld    hl,(cursor_x)          ; cursor byte col = cursor_x / 8
                srl   h
                rr    l
                srl   h
                rr    l
                srl   h
                rr    l
                ld    a,l
                ld    (poll_byte),a
                ld    hl,(cursor_y)          ; cursor line = 199 - cursor_y / 2
                srl   h
                rr    l
                ld    a,199
                sub   l
                ld    (poll_line),a
                ld    d,0
                ld    a,(in_fire)            ; click edge = fire & !last_fire
                ld    e,a
                ld    a,(poll_lastfire)
                cpl
                and   e
                jr    z,gp_noclick
                set   0,d
gp_noclick
                ld    a,e
                ld    (poll_lastfire),a
                ld    a,(in_quit)
                or    a
                jr    z,gp_noquit
                set   1,d
gp_noquit
                ld    a,(in_fire)            ; bit2 = fire currently held (for drag)
                or    a
                jr    z,gp_nohold
                set   2,d
gp_nohold
                call  menu_dispatch          ; kernel-owned top-bar click -> app
                ld    a,d                     ; publish the result to fixed low RAM so a
                ld    (POLL_FLAGS),a          ; WM callback can read it without re-polling
                ld    a,(poll_byte)           ; (gb_mx/gb_my/gb_flags read these)
                ld    (POLL_MX),a
                ld    b,a
                ld    a,(poll_line)
                ld    (POLL_MY),a
                ld    c,a
                ret
poll_lastfire   db    0
poll_byte       db    0
poll_line       db    0

; poll_move: apply the per-frame pointer delta (in_dx/in_dy, from input_poll -
; joystick step and/or SymbiFace mouse) to the cursor, clamped to the screen.
; Returns DE = new x, HL = new y. Does NOT write cursor_x/y - cursor_move_to does
; that, redrawing only when the position actually changes.
poll_move
                ld    hl,(cursor_x)
                ld    de,(in_dx)             ; signed delta this frame
                add   hl,de
                call  clamp638
                ld    (pm_newx),hl
                ld    hl,(cursor_y)
                ld    de,(in_dy)
                add   hl,de
                call  clamp398
                ld    de,(pm_newx)
                ret
pm_newx         dw    0
clamp638
                ld    de,638
                jr    clamp_hl
clamp398
                ld    de,398
clamp_hl                                       ; clamp HL to [0, DE]
                bit   7,h                     ; negative -> 0
                jr    z,ch_hi
                ld    hl,0
                ret
ch_hi
                push  hl
                or    a
                sbc   hl,de
                pop   hl
                ret   c                        ; HL < DE -> keep
                ex    de,hl                    ; clamp to DE
                ret

; fill_xywh: B=x C=y D=w E=h -> set fb_* then fill_block (fb_val preset). Shared
; rectangle/edge filler so k_frame and kwin_frame don't each repeat the fb_* stores.
fill_xywh
                ld    a,b
                ld    (fb_x),a
                ld    a,c
                ld    (fb_y),a
                ld    a,d
                ld    (fb_w),a
                ld    a,e
                ld    (fb_h),a
                jp    fill_block

; copy11: HL=src DE=dst -> copy 11 bytes (an 8.3 name). Shared by the many name-copy
; sites (fs_req_name / launch_arg / window arg) to drop the per-site ld bc,11 + ldir.
copy11
                ld    bc,11
                ldir
                ret

; k_frame (GB_FRAME): draw a 1px rectangle outline. B=x C=y D=w E=h (screen
; byte/line), A=pen (0..3). Screen only (no banked buffer), so no page swap.
k_frame
                ld    (gf_pen),a             ; save params FIRST: pen_to_byte below
                ld    a,b                     ; clobbers E (height), so capture it now
                ld    (gf_x),a
                ld    a,c
                ld    (gf_y),a
                ld    a,d
                ld    (gf_w),a
                ld    a,e
                ld    (gf_h),a
                ld    a,(gf_pen)
                call  pen_to_byte            ; pen -> Mode 1 fill byte
                ld    (fb_val),a
                ld    a,(gf_x)               ; top edge: (x, y, w, 1)
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    a,(gf_w)
                ld    d,a
                ld    e,1
                call  fill_xywh
                ld    a,(gf_x)               ; bottom edge: (x, y+h-1, w, 1)
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    a,(gf_h)
                add   a,c
                dec   a
                ld    c,a
                ld    a,(gf_w)
                ld    d,a
                ld    e,1
                call  fill_xywh
                ld    a,(gf_x)               ; left edge: (x, y, 1, h)
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    d,1
                ld    a,(gf_h)
                ld    e,a
                call  fill_xywh
                ld    a,(gf_x)               ; right edge: (x+w-1, y, 1, h)
                ld    b,a
                ld    a,(gf_w)
                add   a,b
                dec   a
                ld    b,a
                ld    a,(gf_y)
                ld    c,a
                ld    d,1
                ld    a,(gf_h)
                ld    e,a
                jp    fill_xywh
gf_x            db    0
gf_y            db    0
gf_w            db    0
gf_h            db    0
gf_pen          db    0

; --- firmware text (kernel boot messages) --------------------------------
k_cls
                ld    a,1
                jp    SCR_SET_MODE            ; mode 1 clears; returns to caller
k_noop                                         ; shared no-op for dead ABI slots (#148):
                ret                            ; GB_PRINT/QUIT/LAUNCH/XORFRAME/ONREPAINT/WMLAUNCH
k_ret0          xor   a                        ; shared "return 0" (e.g. GB_PICOPEN when a kernel
                ret                            ; has no banked-picture support, #164)

; to_data / from_data: save the caller's bank page and map PAGE_DATA (the font /
; icon page), then restore it. One shared save slot (dp_save) - these swaps are
; never nested (each service swaps, calls only non-swapping helpers, restores).
; Replaces the open-coded save/swap/restore at every PAGE_DATA service.
to_data
                ld    a,(bank_cur)
                ld    (dp_save),a
                ld    a,PAGE_DATA
                jp    bank_set
from_data
                ld    a,(dp_save)
                jp    bank_set
dp_save         db    0

; --- gb_text_draw: 6x8 font text service ---------------------------------
; B = byte col, C = line, D = pen, E = paper, HL = string (caller's page).
; Copy the string to resident scratch (caller page still mapped), set up the
; renderer, then swap to PAGE_DATA for the glyphs, draw, and restore.
gb_text_draw
                ld    a,b                     ; position
                ld    (tc_x),a
                ld    a,c
                ld    (tc_y),a
                ld    b,d                     ; pens: B = pen, C = paper
                ld    c,e
                push  hl
                call  set_text_pens
                pop   hl
                ld    de,gtd_scratch          ; copy string out of the caller's page
                call  gtd_copy
                call  to_data                 ; swap to the font page
                ld    hl,gtd_scratch
                call  draw_text
                jp    from_data               ; restore caller's page
gtd_copy                                       ; (HL) -> (DE) until NUL, cap 48
                ld    b,48
gtd_cloop
                ld    a,(hl)
                ld    (de),a
                or    a
                ret   z
                inc   hl
                inc   de
                djnz  gtd_cloop
                xor   a                        ; truncate + terminate
                ld    (de),a
                ret
gtd_scratch     defs  49

; --- gb_open_window: draw a window frame + title -------------------------
; B = x (byte col), C = y (line), D = w (bytes), E = h (lines), HL = title.
; The frame is plain fills (screen only); the title needs the font, so swap to
; PAGE_DATA for it. No save-under: closing a window redraws the desktop.
gb_open_window
                ld    a,b
                ld    (kw_x),a
                ld    a,c
                ld    (kw_y),a
                ld    a,d
                ld    (kw_w),a
                ld    a,e
                ld    (kw_h),a
                ld    de,kw_title            ; copy title out of the caller's page
                call  gtd_copy
                call  kwin_frame             ; frame: fills to screen, no font
                call  to_data                 ; title needs the font page
                ld    b,1                     ; white on black title bar
                ld    c,2
                call  set_text_pens
                ld    a,(kw_x)
                add   a,4
                ld    (tc_x),a
                ld    a,(kw_y)
                add   a,3
                ld    (tc_y),a
                ld    hl,kw_title
                call  draw_text
                ifdef WM_GADGETS
                ld    b,2                     ; close 'X' glyph: pen 2 (black) on pen 1 (white box)
                ld    c,1
                call  set_text_pens
                ld    a,(kw_x)
                inc   a
                ld    (tc_x),a
                ld    a,(kw_y)
                add   a,3
                ld    (tc_y),a
                ld    hl,gad_x_str
                call  draw_text
                endif
                jp    from_data
                ifdef WM_GADGETS
gad_x_str       db    "X",0
                endif

; kwin_frame: blue interior, black title bar + borders, white close gadget.
kwin_frame
                xor   a                       ; interior (blue): (x, y, w, h)
                ld    (fb_val),a
                ld    a,(kw_x)
                ld    b,a
                ld    a,(kw_y)
                ld    c,a
                ld    a,(kw_w)
                ld    d,a
                ld    a,(kw_h)
                ld    e,a
                call  fill_xywh
                ld    a,#F0                   ; title bar: white base (x, y, w, 14)
                ld    (fb_val),a
                ld    a,(kw_x)
                ld    b,a
                ld    a,(kw_y)
                ld    c,a
                ld    a,(kw_w)
                ld    d,a
                ld    e,14
                call  fill_xywh
                ld    a,#0F                   ; ... black horizontal stripes (1-line
                ld    (fb_val),a             ; fills, every other line; fb_x/fb_w
                ld    a,1                     ; stay kw_x/kw_w from the fill above)
                ld    (fb_h),a
                ld    a,(kw_y)
                ld    (kf_sy),a
                ld    b,7
kf_stripe       ld    a,(kf_sy)
                ld    (fb_y),a
                push  bc
                call  fill_block
                pop   bc
                ld    a,(kf_sy)
                add   a,2
                ld    (kf_sy),a
                djnz  kf_stripe
                ld    a,(kw_x)               ; black borders via k_frame (all 4 edges; the
                ld    b,a                     ; top edge coincides with the first title
                ld    a,(kw_y)               ; stripe, so it is behavior-neutral) - was
                ld    c,a                     ; three separate left/right/bottom fills
                ld    a,(kw_w)
                ld    d,a
                ld    a,(kw_h)
                ld    e,a
                ld    a,2                     ; pen 2 = black (#0F)
                call  k_frame
                ld    a,#F0                   ; close gadget (white): (x+1, y+2, 2, 10)
                ld    (fb_val),a
                ld    a,(kw_x)
                inc   a
                ld    b,a
                ld    a,(kw_y)
                add   a,2
                ld    c,a
                ld    d,2
                ld    e,10
                call  fill_xywh              ; (the 'X' glyph is drawn in gb_open_window, font)
                ifdef WM_GADGETS
; maximize gadget on the right: white box (x+w-4, y+2, 3, 10) + a centered black square.
; (3 bytes wide so the black square sits in the MIDDLE byte with white either side - a 2-byte
; box would have both edge bytes filled by k_frame, i.e. solid black. 1 byte = 4 px in mode 1.)
                ld    a,#F0
                ld    (fb_val),a
                ld    a,(kw_x)
                ld    hl,kw_w
                add   a,(hl)                  ; A = x + w
                sub   4
                ld    (kf_gx),a              ; gadget x (also reused by the hit-test)
                ld    b,a
                ld    a,(kw_y)
                add   a,2
                ld    c,a
                ld    d,3
                ld    e,10
                call  fill_xywh
                ld    a,#0F                   ; black (pen 2) filled square in the centre byte
                ld    (fb_val),a
                ld    a,(kf_gx)
                inc   a                        ; centre byte (gx+1)
                ld    b,a
                ld    a,(kw_y)
                add   a,5
                ld    c,a
                ld    d,1                       ; 4 px wide
                ld    e,4                       ; 4 px tall
                jp    fill_xywh
kf_gx           db    0            ; maximize-gadget x byte-col (set by kwin_frame)
                else
                ret                            ; plain (no-room) build: just the close box
                endif
kw_x            db    0
kw_y            db    0
kw_w            db    0
kw_h            db    0
kf_sy           db    0            ; kwin_frame: current title-bar stripe line
kw_title        defs  24

; --- directory enumeration services --------------------------------------
; Return CF set with HL -> a resident "NAME.EXT" string (0-term) for the entry,
; or NC at end of directory. fs_ent_* are resident, so no page swap is needed;
; the floppy backend is di-safe and uses its own resident buffers.
gb_fs_dir_first
                call  fs_dir_first
                jr    gdir_done
gb_fs_dir_next
                call  fs_dir_next
gdir_done
                ret   nc                       ; no/Last entry
                call  fmt_entry              ; fs_ent_name -> dir_namebuf
                ld    hl,dir_namebuf
                scf
                ret

; fmt_entry: fs_ent_name (8.3, space padded) -> dir_namebuf as the bare "NAME", 0.
; (Name only - the type is conveyed by the entry's icon; the old extension-display
; branch was dead, showext was never set.)
fmt_entry
                ld    de,dir_namebuf
                ld    hl,fs_ent_name
                ld    b,8
fe_name
                ld    a,(hl)
                cp    ' '
                jr    z,fe_end
                ld    (de),a
                inc   de
                inc   hl
                djnz  fe_name
fe_end
                xor   a
                ld    (de),a
                ret
dir_namebuf     defs  14

; --- k_icon_half (GB_ICONHALF): half-height (middle-band) blit of icon A=slot at
; B=x, C=y. The file->slot mapping now lives in the File Manager (#103); the kernel
; only blits a given slot (it owns PAGE_DATA where the .IST lives). The grid view
; uses k_icon (full icon), the list view uses this (compact 16px middle band).
k_icon_half
                ld    (be_slot),a            ; save the slot (A is reused below)
                ld    a,b
                ld    (be_x),a
                ld    a,c
                ld    (be_y),a
                call  to_data
                ld    a,(be_slot)
                call  icon_geom              ; sets bm_src/bm_w/bm_h/bm_x/bm_y
                call  blit_bitmap
                jp    from_data
be_x            db    0
be_y            db    0
be_slot         db    0
ig_w            db    0
ig_h            db    0

; icon_geom: A = slot -> from the .IST directory (DATA_ICONS) set up a
; half-height blit (middle band) at (be_x, be_y). PAGE_DATA must be mapped.
icon_geom
                ld    l,a                     ; dir entry = DATA_ICONS+16 + slot*4
                ld    h,0
                add   hl,hl
                add   hl,hl
                ld    de,DATA_ICONS+16
                add   hl,de
                ld    e,(hl)                  ; offset (word)
                inc   hl
                ld    d,(hl)
                inc   hl
                ld    a,(hl)                  ; width (bytes)
                ld    (ig_w),a
                inc   hl
                ld    a,(hl)                  ; height (rows)
                ld    (ig_h),a
                ld    hl,DATA_ICONS          ; bitmap base = DATA_ICONS + offset
                add   hl,de
                ld    a,(ig_h)               ; + (h/4)*w  (skip the top quarter)
                srl   a
                srl   a
                ld    b,a
                ld    a,(ig_w)
                ld    e,a
                ld    d,0
ig_skip
                ld    a,b
                or    a
                jr    z,ig_done
                add   hl,de
                dec   b
                jr    ig_skip
ig_done
                ld    (bm_src),hl
                ld    a,(ig_w)
                ld    (bm_w),a
                ld    a,(ig_h)               ; half height
                srl   a
                ld    (bm_h),a
                ld    a,(be_x)
                ld    (bm_x),a
                ld    a,(be_y)
                ld    (bm_y),a
                ret

; (file -> icon-slot mapping moved to the File Manager, #103 - the kernel now
; only blits a given slot via k_icon / k_icon_half; the .IST lives in PAGE_DATA.)

; --- config (C kernel module) --------------------------------------------
; cfg_boot: seed defaults, load GEOBENCH.CFG into the transfer area, then run the
; GBCFG C module (paged into a bank) to parse it into KCFG_ICONS / KCFG_FONT.
; Absent file -> length 0 -> the parser is a no-op and the defaults stand.
cfg_boot
                ld    hl,0                    ; default: no config text (module then
                ld    (KCFG_LEN),hl           ; emits the DEFAULT names itself)
                ld    hl,cfg_fname           ; fs_req_name = "GEOBENCH.CFG"
                ld    de,fs_req_name
                call  copy11
                ld    hl,#0200               ; cfg text buffer is 512 bytes
                ld    (fs_load_max),hl
                ld    hl,KCFG_TEXT
                ld    (fs_load_dst),hl
                call  fs_load_file
                jr    nc,cfgb_run            ; no file -> KCFG_LEN stays 0
                ld    hl,(fs_ent_size)        ; file <512 so the low word is the size
                ld    (KCFG_LEN),hl
cfgb_run
                jp    run_cfgmod
cfg_fname       db    "GEOBENCHCFG"          ; "GEOBENCH.CFG" (8.3)

; run_cfgmod: load GBCFG.BIN into PAGE_APP0 and CALL it (it parses the transfer
; area). Mirrors launch_app's load path; runs under DI (the module needs no
; interrupts) with no top-bar/ESC handling - this is boot time.
run_cfgmod
                ld    hl,cfgmod_name
                ld    de,fs_req_name
                call  copy11
                ld    a,(bank_cur)           ; save the current page
                push  af
                di
                ld    a,PAGE_APP0
                call  bank_set
                ld    hl,#3F00
                ld    (fs_load_max),hl
                ld    hl,APP_BASE
                ld    (fs_load_dst),hl
                call  fs_load_sys           ; #134: GBCFG.BIN lives in the /GEOBENCH system dir
                jr    nc,rcm_done            ; module missing -> keep defaults
                call  APP_BASE               ; crt0 _start -> main -> gb_cfg_parse
rcm_done
                pop   af                       ; restore the caller's page
                call  bank_set
                ei
                ret
cfgmod_name     db    "GBCFG   BIN"          ; 8.3, space-padded

; --- font in PAGE_DATA ----------------------------------------------------
; font_init: page PAGE_DATA in, load <FONT>.FNT into it, cache the geometry.
; The 8.3 filename was built by the GBCFG module (KCFG_FONTNAME); just copy it.
font_init
                di
                ld    a,PAGE_DATA
                call  bank_set
                ld    hl,KCFG_FONTNAME       ; fs_req_name = the config font name
                ld    de,fs_req_name
                call  copy11
                ld    hl,#1000               ; font fits easily
                ld    (fs_load_max),hl
                ld    hl,DATA_FONT           ; load into PAGE_DATA
                ld    (fs_load_dst),hl
                ld    hl,def_fnt             ; fall back to DEFAULT.FNT if missing
                call  load_or_default
                ld    hl,DATA_FONT           ; cache geometry (font_glyphs -> PAGE_DATA)
                call  font_apply_header
                call  bank_normal
                ei
                ret
def_fnt         db    "DEFAULT FNT"
def_ist         db    "DEFAULT IST"

; load_or_default: fs_req_name holds the wanted 8.3 name and fs_load_dst/max are
; set. Try it; if the file is missing (a bad ICONS=/FONT= value), retry with the
; 11-byte default name at HL so a typo can't leave the screen drawing garbage.
load_or_default                                ; HL = default 8.3 name (11 bytes)
                push  hl
                call  fs_load_sys             ; #134: fonts/icons/cursor live in /GEOBENCH
                pop   hl
                ret   c                         ; wanted file loaded
                ld    de,fs_req_name           ; missing -> use the default name
                call  copy11
                jp    fs_load_sys              ; (dst/max unchanged by the miss)

; icon_init: load <ICONS>.IST into PAGE_DATA at DATA_ICONS.
icon_init
                di
                ld    a,PAGE_DATA
                call  bank_set
                ld    hl,KCFG_ICONNAME       ; fs_req_name = the config icon name
                ld    de,fs_req_name
                call  copy11
                ld    hl,DATA_MODTOP-DATA_ICONS-#200 ; cap = the free icon region: from
                ld    (fs_load_max),hl               ; DATA_ICONS up to DATA_MODTOP (the write
                                                     ; write module also lives in PAGE_DATA),
                                                     ; less a sector. ~#1A00 (~25 icons),
                                                     ; derived so it never needs hand-tuning.
                ld    hl,DATA_ICONS
                ld    (fs_load_dst),hl
                ld    hl,def_ist             ; fall back to DEFAULT.IST if missing
                call  load_or_default
                call  bank_normal
                ei
                ret

; cursor_init (#65): load the cursor sprite (<CURSOR>.SPR from GEOBENCH.CFG, built
; by the GBCFG module into KCFG_CURSORNAME) into low RAM at CUR_LOW, so the 256-byte
; bitmaps need not be resident. Loads into low RAM (always mapped) so no PAGE_DATA
; swap. fs_load_max = 512 (the IDE reader copies a whole sector); CUR_LOW..+#1FF is
; reserved for that. Falls back to DEFAULT.SPR if the configured name is missing; if
; even that is absent, the buffer is blanked (empty cursor, never garbage).
cursor_init
                ld    hl,KCFG_CURSORNAME     ; fs_req_name = the config cursor name
                ld    de,fs_req_name
                call  copy11
                ld    hl,512
                ld    (fs_load_max),hl
                ld    hl,CUR_LOW
                ld    (fs_load_dst),hl
                ld    hl,def_spr            ; fall back to DEFAULT.SPR if missing
                di
                call  load_or_default
                ei
                ret   c
                ld    hl,CUR_LOW             ; missing -> blank the sprite buffer
                ld    de,CUR_LOW+1
                ld    bc,255
                ld    (hl),0
                ldir
                ret
def_spr         db    "DEFAULT SPR"

; --- app launch ----------------------------------------------------------
; launch_app: HL = 8.3 app name. Load it into the next bank page (PAGE_APP0 +
; launch depth) and run it; restore the caller's page when it quits. Apps nest
; (desktop -> filemgr -> viewer); the caller's page is kept on the stack so any
; depth restores correctly.
launch_app
                ld    de,fs_req_name         ; name -> fs_req_name (HL in caller page)
                call  copy11
                call  wm_alloc_page          ; grab a free bank page (not depth-based,
                or    a                       ; so it never collides with a co-resident
                ret   z                       ; WM window) -> A=page, 0 = none free
                ld    c,a                      ; C = target page
                push  bc                       ; save target page to free on return
                ld    a,(bank_cur)           ; save caller's page (per depth) on stack
                push  af
                ld    hl,(APP_HANDLER)       ; save the parent's event handler and clear
                push  hl                       ; it, so a top-bar click during the child
                ld    hl,0                     ; cannot call the parent's handler (whose
                ld    (APP_HANDLER),hl        ; page is not mapped while the child runs)
                ld    a,(MENU_DEF)           ; save & clear the parent's menu so the
                push  af                       ; child starts with a clean top bar
                xor   a
                ld    (MENU_DEF),a
                ld    hl,launch_depth
                inc   (hl)
                di
                ld    a,c
                call  bank_set
                ld    hl,#3F00
                ld    (fs_load_max),hl
                ld    hl,APP_BASE
                ld    (fs_load_dst),hl
                call  fs_load_sys           ; #134: app binaries live in the /GEOBENCH system dir
                jr    nc,la_done             ; missing -> just unwind
                ei
                call  APP_BASE
                di
la_done
                ld    hl,launch_depth
                dec   (hl)
                pop   af                       ; restore the parent's menu count
                ld    (MENU_DEF),a
                pop   hl                       ; restore the parent's event handler
                ld    (APP_HANDLER),hl
                pop   af                       ; caller's page
                call  bank_set
                pop   bc                       ; target page -> release it
                ld    a,c
                call  wm_free_page
                                              ; (#77: the desktop redraws the bar via its
                                              ; always-run handler, no draw_topbar here)
                ld    a,(WM_NWIN)            ; if WM windows are live underneath (a modal
                or    a                       ; app launched from one), repaint them so
                call  nz,wm_repaint_all      ; the modal app's screen area is restored
                ei
la_escwait                                     ; if the app quit on ESC, wait for the
                ld    a,66                     ; key to be released before the parent
                call  KM_TEST_KEY              ; runs - else the same press quits it too,
                jr    nz,la_escwait            ; cascading all the way back to BASIC
                ret
launch_depth    db    0

                ifdef WM_GADGETS
; --- #164 banked Viewer picture buffer ------------------------------------------------------
; Mirrors launch_app's load-into-a-bank: borrow a free app-pool page, map it at #4000, load the
; opened file into it, blit from it with restore_block (which reads its sb_buf in the #4000
; window). Lets the Viewer show a .PIC bigger than its own 16K page (up to a full-screen 320x200).
;
; k_pic_open (GB_PICOPEN): load the opened file into a borrowed bank + parse the .PIC header.
; -> A=1 with PIC_WB/PIC_H/PIC_OFF set if it's a .PIC loaded OK; A=0 (no free bank, or not a
; .PIC - the bank is released) so the Viewer falls back to its in-page buffer.
k_pic_open
                call  wm_alloc_page          ; A = a free app-pool page, or 0 = none free
                or    a
                ret   z                        ; -> A=0, caller uses its in-page buffer
                ld    (PIC_PAGE),a
                ld    a,(bank_cur)            ; save the Viewer's page
                push  af
                ld    a,(PIC_PAGE)
                call  bank_set               ; map the picture bank at #4000
                ld    hl,#3F00
                ld    (fs_load_max),hl
                ld    hl,#4000
                ld    (fs_load_dst),hl
                call  fs_load_file           ; load the opened file into the bank (#4000..)
                jr    nc,kpo_fail            ; not found
                ld    a,(#4000)              ; validate the "GBPC" magic
                cp    'G'
                jr    nz,kpo_fail
                ld    a,(#4001)
                cp    'B'
                jr    nz,kpo_fail
                ld    a,(#4002)
                cp    'P'
                jr    nz,kpo_fail
                ld    a,(#4003)
                cp    'C'
                jr    nz,kpo_fail
                ld    a,(#4004)              ; version: 2 = v2 (else v1), as the Viewer's parse_pic
                cp    2
                jr    nz,kpo_v1
                ld    hl,(#4006)            ; v2: pic_wb = (width + 3) >> 2
                inc   hl
                inc   hl
                inc   hl
                srl   h
                rr    l
                srl   h
                rr    l
                ld    a,l
                ld    (PIC_WB),a
                ld    a,(#4008)             ; height (rows)
                ld    (PIC_H),a
                ld    a,14                   ; bitmap offset
                ld    (PIC_OFF),a
                jr    kpo_ok
kpo_v1          ld    a,(#4004)             ; v1: byte width, then height, bitmap at +6
                ld    (PIC_WB),a
                ld    a,(#4005)
                ld    (PIC_H),a
                ld    a,6
                ld    (PIC_OFF),a
kpo_ok          pop   af                       ; restore the Viewer's page
                call  bank_set
                ld    a,1
                ret
kpo_fail        pop   af                       ; restore the Viewer's page, release the bank
                call  bank_set
                ld    a,(PIC_PAGE)
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE),a
                ret

; k_pic_blit (GB_PICBLIT): B=x C=y D=wbytes E=h HL=src_off. Map the picture bank and blit the
; region (#4000+src_off ..) to the screen at (x,y) via restore_block, then restore the page.
; The cursor is already down (the WM's cur_paintlock holds during the Viewer's on_draw).
k_pic_blit
                ld    a,b
                ld    (sb_x),a
                ld    a,c
                ld    (sb_y),a
                ld    a,d
                ld    (sb_w),a
                ld    a,e
                ld    (sb_h),a
                ld    bc,#4000
                add   hl,bc                   ; HL = #4000 + src_off (in the bank window)
                ld    (sb_buf),hl
                ld    a,(bank_cur)
                push  af
                ld    a,(PIC_PAGE)
                call  bank_set
                call  restore_block          ; reads sb_buf (#4000..bank), writes the screen
                pop   af
                call  bank_set
                ret

; k_pic_close (GB_PICCLOSE): release the borrowed picture bank.
k_pic_close
                ld    a,(PIC_PAGE)
                or    a
                ret   z
                call  wm_free_page
                xor   a
                ld    (PIC_PAGE),a
                ret
                endif

; app_pool_init (#84): build the app-page pool from the detected bank count
; (md_banks). Index 0..2 = bank 0 blocks 1..3 (#C5..#C7; block 0 is PAGE_DATA);
; then each further bank contributes all four blocks (&C4 + bank*8 + block), the
; same value lib/bank.asm's bank_page forms. Appends until the list reaches
; WM_MAXWIN entries or RAM runs out. Sets APP_NPAGES; zeroes APP_BUSY. So the
; desktop (first claim) always lands in APP_PAGES[0] = PAGE_APP0 = #C5, and a
; plain 128K machine yields exactly 3 pages (desktop + 2), as before.
app_pool_init
                ld    hl,APP_PAGES
                ld    (hl),#C5               ; bank 0 block 1
                inc   hl
                ld    (hl),#C6               ; bank 0 block 2
                inc   hl
                ld    (hl),#C7               ; bank 0 block 3
                inc   hl
                ld    c,3                      ; C = entries so far
                ld    a,(md_banks)
                ld    b,a                      ; B = detected bank count
                ld    d,1                      ; D = bank index, from 1
api_bank        ld    a,d
                cp    b
                jr    nc,api_done             ; bank index >= count -> done
                ld    a,d
                add   a,a
                add   a,a
                add   a,a                      ; A = bank*8
                add   a,#C4                     ; A = &C4 + bank*8 (this bank's block 0)
                ld    e,a                       ; E = current port value
                add   a,4
                ld    (api_end),a              ; sentinel = base + 4 (one past block 3)
api_blk         ld    a,c
                cp    WM_MAXWIN
                jr    nc,api_done             ; list full -> stop
                ld    (hl),e                    ; store this block's port value
                inc   hl
                inc   c
                inc   e
                ld    a,(api_end)
                cp    e
                jr    nz,api_blk               ; more blocks in this bank
                inc   d                          ; next bank
                jr    api_bank
api_done        ld    a,c
                ld    (APP_NPAGES),a
                ld    hl,APP_BUSY              ; mark every page free
                ld    de,APP_BUSY+1
                ld    bc,WM_MAXWIN-1
                ld    (hl),0
                ldir
                ret
api_end         db    0
md_banks        db    0

; wm_alloc_page: claim the lowest free app page -> A = its port value, or 0 if all
; APP_NPAGES pages are in use. wm_free_page: A = the port value to release. The pool
; serves both co-resident WM windows (wm_open_go) and modal launch_app calls.
wm_alloc_page
                ld    a,(APP_NPAGES)
                or    a
                ret   z                         ; (defensive) no pages at all
                ld    b,a                        ; B = page count
                ld    hl,APP_BUSY
                ld    de,APP_PAGES
wap_scan        ld    a,(hl)
                or    a
                jr    z,wap_take                ; busy flag clear -> free slot
                inc   hl
                inc   de
                djnz  wap_scan
                xor   a                          ; none free
                ret
wap_take        ld    (hl),1                     ; mark busy
                ld    a,(de)                      ; A = its port value
                ret
wm_free_page                                     ; A = port value to release
                ld    b,a                         ; B = target port
                ld    a,(APP_NPAGES)
                or    a
                ret   z
                ld    c,a                          ; C = page count
                ld    hl,APP_PAGES
                ld    de,APP_BUSY
wfp_scan        ld    a,(hl)
                cp    b
                jr    z,wfp_hit
                inc   hl
                inc   de
                dec   c
                jr    nz,wfp_scan
                ret                                ; not found (defensive)
wfp_hit         xor   a
                ld    (de),a                       ; clear the parallel busy flag
                ret

; GB_RUN (HL = 8.3 name in the caller's page): the slot jumps straight to
; launch_app (k_run wrapper collapsed, #148).

; k_launch (GB_LAUNCH): the old MODAL open-the-current-entry. Superseded by the
; co-resident open path and no longer called by any app; the slot -> k_noop (#148).

; Directory navigation (issue #54). The FAT backend enumerates the directory at
; fs_dir_clus; gb_chdir descends into the positioned entry (pushing the parent on
; a small stack) and gb_back pops it. On the floppy backend these touch FAT-only
; state the AMSDOS reader ignores, and gb_isdir is always 0 (no subdirectories),
; so the file manager simply never descends there.

; k_isdir (GB_ISDIR): A = 1 if the last-enumerated entry is a directory, else 0.
k_isdir
                ld    a,(fs_ent_attr)
                and   #10
                ret   z
                ld    a,1
                ret

; k_chdir (GB_CHDIR): descend into the positioned entry's directory. The IDE backend
; pushes the current dir cluster and sets it to the entry's start cluster; the
; Albireo backend (#104) appends "/<name>" to its path string instead. The next
; listing rewinds there. Refuses (no-op) when the stack/path is full.
k_chdir
                if STORAGE_ALBIREO
                jp    fsalb_chdir
                else
                ld    a,(fs_dir_sp)
                cp    4                          ; DIRSTACK depth
                ret   nc
                add   a,a                         ; slot = fs_dir_stack + sp*4
                add   a,a
                ld    e,a
                ld    d,0
                ld    hl,fs_dir_stack
                add   hl,de
                ex    de,hl                       ; DE = slot
                ld    hl,fs_dir_clus             ; push current dir
                ld    bc,4
                ldir
                ld    hl,fs_dir_sp
                inc   (hl)
                ld    hl,fs_ent_clus             ; descend: dir = entry's cluster
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
                endif

; k_back (GB_BACK): go to the parent directory (no-op at the top). IDE pops the dir
; cluster stack; Albireo (#104) truncates its path string at the last '/'.
k_back
                if STORAGE_ALBIREO
                jp    fsalb_back
                else
                ld    a,(fs_dir_sp)
                or    a
                ret   z
                dec   a
                ld    (fs_dir_sp),a
                add   a,a                         ; slot = fs_dir_stack + sp*4
                add   a,a
                ld    e,a
                ld    d,0
                ld    hl,fs_dir_stack
                add   hl,de
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
                endif

; k_entname (GB_ENTNAME): HL = the last-enumerated entry's raw 11-byte 8.3 name
; (space-padded, no dot) so an app can show the extension (gb_dir* return name-only).
k_entname
                ld    hl,fs_ent_name
                ret

; focus_arg_ptr: HL = the focused window's 11-byte file arg (per-window, so two
; editors keep separate files). Each window captured the pending launch_arg at
; gb_wm_add; getarg/setname/fsload/fssave all act on the focused window's copy.
focus_arg_ptr
                ld    a,(WM_FOCUS)
                call  wm_entry
                ld    de,WM_FR_ARG
                add   hl,de
                ret

; GB_GETARG (HL = the launch arg, the 8.3 file name the app opened): the slot
; jumps straight to focus_arg_ptr (k_getarg wrapper collapsed, #148).

; k_setname (GB_SETNAME): set the current file name so a later GB_FSLOAD/GB_FSSAVE
; targets it - how an app does New / Save As / open a picked file. HL = an 11-byte
; 8.3 name in the caller's page. Writes the focused window's per-window arg.
k_setname
                push  hl                       ; HL = src name
                call  focus_arg_ptr           ; HL = dst (focused window's arg)
                ex    de,hl                     ; DE = dst
                pop   hl                       ; HL = src
                call  copy11
                ret

; The shared clipboard (#142): a fixed low-RAM buffer (CLIP_LEN word + CLIP_DATA bytes)
; that survives app switches, so copy in one app and paste in another. Resident, so the
; code isn't duplicated into every app's bank. Low RAM is always mapped.

; k_clip_set (GB_CLIPSET): copy DE bytes from HL into the clipboard, clamped to CLIP_CAP.
; HL = src (caller page, mapped), DE = length.
k_clip_set
                or    a                        ; clamp DE to CLIP_CAP
                push  hl                        ; save src
                ld    hl,CLIP_CAP
                sbc   hl,de                     ; CLIP_CAP - len ; NC = len <= CLIP_CAP
                jr    nc,kcs_len
                ld    de,CLIP_CAP
kcs_len         pop   hl                        ; HL = src
                ld    (CLIP_LEN),de            ; store the length
                ld    b,d
                ld    c,e                        ; BC = count
                ld    a,b
                or    c
                ret   z                          ; nothing to copy
                ld    de,CLIP_DATA
                ldir                             ; src -> clipboard
                ret

; k_clip_get (GB_CLIPGET): copy up to DE bytes of the clipboard into HL. HL = dst (caller
; page), DE = max. Returns BC = bytes copied (the trampoline maps BC -> the C return).
k_clip_get
                ld    bc,(CLIP_LEN)            ; BC = stored length
                push  hl                        ; save dst
                ld    h,b
                ld    l,c
                or    a
                sbc   hl,de                     ; len - max ; CF = len < max
                jr    c,kcg_n                    ; copy len (BC already)
                ld    b,d
                ld    c,e                        ; else copy max
kcg_n           pop   de                         ; DE = dst
                ld    hl,CLIP_DATA              ; HL = src
                ld    a,b
                or    c
                ret   z                          ; count 0 -> BC=0
                push  bc                        ; save count
                ldir                             ; clipboard -> dst
                pop   bc                         ; BC = count (return)
                ret

; k_clip_len (GB_CLIPLEN): BC = the clipboard length (for sizing a paste).
k_clip_len
                ld    bc,(CLIP_LEN)
                ret

; k_ui (GB_UI #80AE): the paged dialog service (#142). The caller has marshalled its
; request into the low-RAM UI_* block; page in PAGE_DATA, load the GBUI module to
; DATA_MODTOP (#6000, above font+icons), raise UI_MODAL (so the dialog's own gb_poll
; loop doesn't dispatch top-bar clicks into the now-swapped-out app), CALL it to render
; + write UI_RES, then restore. Returns BC = UI_RES for the C trampoline. The dialog is
; modal: this call blocks until the user picks/cancels.
k_ui
                ld    a,1
                ld    (UI_MODAL),a
                ld    a,(bank_cur)
                push  af
                ld    a,PAGE_DATA
                call  bank_set
                ld    hl,gbui_modname         ; "GBUI    BIN" -> fs_req_name
                ld    de,fs_req_name
                ld    bc,11
                ldir
                ld    hl,#2000                ; module load cap (8 KB window to #8000)
                ld    (fs_load_max),hl
                ld    hl,DATA_MODTOP          ; #6000
                ld    (fs_load_dst),hl
                call  fs_load_sys            ; GBUI.BIN -> #6000 (boot drive, preserves browse dir)
                jr    nc,kui_done             ; missing -> leave UI_RES (caller pre-set a cancel)
                call  DATA_MODTOP            ; run the module: render, write UI_RES
kui_done
                pop   af
                call  bank_set
                xor   a
                ld    (UI_MODAL),a
                ld    a,(UI_RES)
                ld    c,a
                ld    b,0
                ret
gbui_modname    db    "GBUI    BIN"

; k_fsload (GB_FSLOAD): load the file the app was opened with (launch_arg) into a
; caller buffer. HL = dst (in the caller's page, which stays mapped through the
; FDC read - fsam_buf is resident, so no page swap), DE = max bytes. Returns
; BC = byte count (0 = missing / too big), CF set on success.
k_fsload
                ld    (fs_load_dst),hl
                ex    de,hl
                ld    (fs_load_max),hl
                call  focus_arg_ptr          ; load by the focused window's file name
                ld    de,fs_req_name
                call  copy11
                di
                call  fs_load_file
                ei
                ld    bc,0
                ret   nc                      ; not found / too big -> BC=0, NC
                ld    bc,(fs_ent_size)        ; content length (header already stripped)
                scf
                ret

; k_fssave (GB_FSSAVE): save the file the app was opened with (launch_arg). HL =
; src (caller's page, stays mapped), DE = byte count. Overwrites in place. CF set
; on success.
k_fssave
                ld    (fs_save_src),hl
                ex    de,hl
                ld    (fs_save_len),hl
                call  focus_arg_ptr          ; save by the focused window's file name
                ld    de,fs_req_name
                call  copy11
                di
                call  fs_save_file
                ei
                ret                            ; CF = saved

; k_getkey (GB_GETKEY): A = a typed character from the keyboard buffer, or 0 if
; none is waiting. Non-blocking (the firmware's IRQ scan fills the buffer).
; k_drive_poll (GB_DRIVES, #65): probe the drives GEOBENCH can reach and return a
; bitmask in A: bit0 = floppy A, bit1 = floppy B, bit2 = Disk C (the hard volume),
; bit3 = Disk C is an Albireo SD/USB card (vs IDE) so the desktop can pick its icon
; (#104). Probing a floppy spins the motor + recalibrates (slow) - call on demand.
k_drive_poll
                ld    c,0                          ; result bits
                if STORAGE_ALBIREO
                call  fsalb_present                ; Albireo -> Disk C (bit2) + SD flag (bit3)
                jr    nc,kdp_a
                set   2,c
                set   3,c                          ; bit3 = Disk C is an Albireo SD/USB card
                jr    kdp_a                          ; (so the desktop can pick the SD icon, #104)
                else
                call  fs_ide_present               ; IDE -> Disk C (bit2)
                jr    nc,kdp_a
                set   2,c
                endif
kdp_a
                push  bc
                xor   a                             ; floppy A = unit 0
                ld    (fsam_unit),a
                call  fsam_present
                pop   bc
                jr    nc,kdp_b
                set   0,c
kdp_b
                push  bc
                ld    a,1                           ; floppy B = unit 1
                ld    (fsam_unit),a
                call  fsam_present
                pop   bc
                jr    nc,kdp_done
                set   1,c
kdp_done
                xor   a                             ; leave the floppy backend on unit 0
                ld    (fsam_unit),a
                ld    a,c
                ret

; k_get_drive (GB_GETDRIVE, #65): A = the current active drive (0=IDE/C, 1=A, 2=B).
; A window reads this at startup to learn which drive it was opened on. (GB_SETDRIVE
; jumps straight to fs_set_drive.)
k_get_drive
                ld    a,(fs_cur_drive)
                ret

; Cross-drive copy (#65 phase 3): the orchestration moved into the File Manager (C)
; to reclaim resident space (#74). The kernel just flips the active drive+dir context
; between the dragged file's source (captured at drag start) and the drop target; the
; app does the load->save itself through the shared staging buffer (GBFAT_DATA).
;
; k_copy_begin (GB_COPYBEGIN): save the current (target) drive+dir, then switch to the
; source drive+dir, so the caller can load the dragged file. Pairs with k_copy_end.
k_copy_begin
                ld    a,(fs_cur_drive)        ; save the caller's (target) context
                ld    (cb_tdrv),a
                ld    hl,fs_dir_clus
                ld    de,cb_tdir
                ld    bc,4
                ldir
                ld    a,(WM_DRAGDRV)         ; switch to the drag source drive + dir
                call  fs_set_drive
                ld    hl,WM_DRAGDIR
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
; k_copy_end (GB_COPYEND): restore the saved (target) drive+dir, so the caller can
; re-list its own directory.
k_copy_end
                ld    a,(cb_tdrv)
                call  fs_set_drive
                ld    hl,cb_tdir
                ld    de,fs_dir_clus
                ld    bc,4
                ldir
                ret
cb_tdrv         db    0
cb_tdir         defs  4

; k_fs_delete (GB_FSDELETE, #62): HL = 11-byte 8.3 name in the caller page. Delete
; that file from the current directory (free clusters + clear the dir entry) via
; the paged GBFAT module. CF set = deleted. Used by drag-to-Trash.
k_fs_delete
                ld    de,fs_req_name
                call  copy11
                di
                call  fs_delete_file
                ei
                ret

k_getkey
                call  KM_READ_CHAR           ; CF + A = char, or NC = none. (Apps frame
                jr    nc,kgk_none            ; on IY now, so KM_READ_CHAR clobbering IX
                                              ; is harmless - see build_capp.sh.)
                ; The pointer is the joystick/cursor keys, and the firmware also
                ; buffers those as characters - so moving the pointer would flood a
                ; keyboard app (Notepad redrawing on every move). While any pointing
                ; direction is physically held, the "char" is the pointer, not
                ; typing: drop it. KM_TEST_KEY preserves B/DE/HL.
                ld    (kgk_char),a           ; save char in memory (KM_TEST_KEY may
                ld    hl,kgk_dirkeys         ; corrupt HL/BC, so don't trust regs)
                ld    b,(hl)
kgk_test
                inc   hl
                ld    a,(hl)
                push  hl
                push  bc
                call  KM_TEST_KEY
                pop   bc
                pop   hl
                jr    nz,kgk_none            ; a direction held -> discard the char
                djnz  kgk_test
                ld    a,(kgk_char)           ; a genuine typed character
                ret
kgk_none
                xor   a
                ret
kgk_char        db    0
kgk_dirkeys     db    10, 0,1,2,8, 72,73,74,75, 76,77 ; cursor + joystick dirs + fire
                                              ; (fire = the click; it also buffers a
                                              ; char, e.g. 'Z' - drop it while held)

; k_vsync (GB_VSYNC): no longer used - apps are frame-paced by the WM loop (on_frame
; runs once per k_poll) rather than driving their own gb_vsync loop. Stubbed to a
; sane "no ESC" return; ABI slot kept so the jump-table address stays fixed.
k_vsync
                xor   a
                ret

; k_onevent (GB_ONEVENT): register the calling app's event handler. HL = handler
; address (a void(void) C function in the app's page), 0 to unregister. The
; kernel calls it from menu_dispatch with a message in GB_MSG.
k_onevent
                ld    (APP_HANDLER),hl
                ret

; k_onrepaint (GB_ONREPAINT): now a no-op kept for ABI. Under the window manager
; an app's repaint handler lives in its gb_win_t (on_repaint); the old per-depth
; REPAINT_HDLR table is unused. The slot stays fixed and -> k_noop (#148).

; k_restore_parent (GB_RESTPAR): repaint everything behind the caller. Under the
; window manager (issue #45) the live windows below a modal app are exactly the WM
; windows, so this repaints them all bottom-up (wm_repaint_all) - e.g. a modal
; Notepad dragging its window restores the desktop + file manager underneath, then
; redraws itself on top. The jump table goes straight to wm_repaint_all now (#148,
; reclaimed the k_restore_parent wrapper).

; ---- cooperative window manager (issue #45, phase 2) --------------------------
; The kernel owns the master loop (wm_loop) and a table of co-resident windows.
; The root window (desktop) enters the loop via GB_WMRUN; further windows are
; opened non-blocking with GB_WMOPEN (load app -> its main calls GB_WMADD and
; returns). The loop polls, then on a fresh click hit-tests the windows top-down
; (z-order) and raises+focuses the one clicked, then calls the focused window's
; on_frame. A window closes itself with GB_WMCLOSE.
;
; A descriptor is { u8 x,y,w,h; on_frame(2); on_repaint(2); on_event(2) }.

; wm_register: HL = descriptor in the caller's page. Allocate a table slot, store
; page = caller, copy the descriptor, mark alive, push onto z-order top and focus.
; --- z-order manager (#148): the ONLY code that mutates WM_Z / WM_NWIN. ---------
; Three callers (register/close/raise) used to open-code the compaction; one held
; the slot in C across wm_free_page (which does ld c,a) and dropped the wrong
; window. Centralising it kills that bug class and dedups the loops.
;
; wm_z_append: A = slot -> WM_Z[WM_NWIN++] = slot (new z-top). Clobbers A,B,HL.
wm_z_append
                ld    b,a                          ; B = slot
                ld    hl,WM_NWIN
                ld    a,(hl)                       ; A = NWIN
                inc   (hl)                          ; NWIN++
                ld    hl,WM_Z
                add   a,l
                ld    l,a                           ; HL = &WM_Z[NWIN]
                ld    (hl),b
                ret

; wm_z_remove: C = slot -> compact WM_Z dropping slot C, dec NWIN once. Keeps C.
; Clobbers A,B,DE,HL (NOT C - callers may still need the slot).
wm_z_remove
                ld    hl,WM_Z
                ld    de,WM_Z
                ld    a,(WM_NWIN)
                ld    b,a
wzr_l           ld    a,(hl)
                cp    c
                jr    z,wzr_skip
                ld    (de),a
                inc   de
wzr_skip        inc   hl
                djnz  wzr_l
                ld    hl,WM_NWIN
                dec   (hl)
                ret

; wm_focus_top: WM_FOCUS = WM_Z[NWIN-1] (the live z-top). NWIN>=1 (desktop is [0]).
; The z-top is always alive (the manager keeps WM_Z == the live slots).
wm_focus_top
                ld    a,(WM_NWIN)
                dec   a
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)
                ld    (WM_FOCUS),a
                ret

wm_register
                ld    (wm_desc),hl
                call  wm_free_slot               ; A = first dead slot
                ld    (wm_slot),a
                call  wm_entry                    ; HL = WM_TABLE[slot]
                ld    a,(bank_cur)               ; +0 page = caller
                ld    (hl),a
                inc   hl
                ex    de,hl                       ; DE = entry+1
                ld    hl,(wm_desc)
                ld    bc,12                       ; x,y,w,h,on_frame,on_repaint,on_event,menu
                ldir
                ld    a,1                          ; entry+13 flags = alive
                ld    (de),a
                inc   de                            ; entry+14 = arg: capture the pending
                ld    hl,launch_arg               ; launch arg as this window's own file
                call  copy11
                ld    a,(wm_slot)                 ; focus + append the new window (z-top)
                ld    (WM_FOCUS),a
                jp    wm_z_append                  ; A = slot; tail-call (rets to wm_register's caller)

; ===== managed windows (#146): the kernel owns the chrome ====================
; k_wm_managed (GB_WMMANAGED): HL = a gb_mwin_t descriptor in the caller's page.
; Register a window the WM draws + drives: the descriptor pointer goes in WM_FR_FRAME
; and FLAGS gets MW_MANAGED, so wm_loop / wm_repaint_all route it to wm_chrome_frame /
; wm_chrome_draw instead of the app's on_frame/on_repaint. Descriptor layout:
;   +0 x +1 y +2 w +3 h +4 min_w +5 min_h +6 on_draw +8 on_click +10 on_frame
;   +12 on_close +14 on_event +16 title
k_wm_managed
                call  wm_register            ; HL = desc; register as a normal window (slot,
                                             ; page, focus, z-order, arg, flags=alive; copies
                                             ; desc[0..11] -> entry+1..12). wm_register stashed
                                             ; the desc ptr in wm_desc. Now patch for managed:
                ld    a,(WM_FOCUS)           ; the just-registered window
                call  wm_entry               ; HL = entry
                push  hl
                ld    de,WM_FR_FLAGS         ; FLAGS |= managed
                add   hl,de
                set   1,(hl)
                pop   hl
                push  hl
                ld    de,WM_FR_FRAME         ; WM_FR_FRAME (entry+5,6) = the descriptor ptr
                add   hl,de
                ld    de,(wm_desc)
                ld    (hl),e
                inc   hl
                ld    (hl),d
                pop   hl
                ld    de,WM_FR_EVENT         ; WM_FR_EVENT = desc.on_event (so bar clicks/drops
                add   hl,de                  ; reach the app's menu handler via the normal path)
                push  hl                     ; HL = entry+9
                ld    hl,(wm_desc)
                ld    de,6                   ; WM_FR_EVENT = desc.proc (#148): menu/drop come
                add   hl,de                  ; through here too, so every message reaches the
                ld    e,(hl)                 ; one WndProc (keyed by gb_msg.type)
                inc   hl
                ld    d,(hl)                 ; DE = proc ptr
                pop   hl                     ; HL = entry+9
                ld    (hl),e
                inc   hl
                ld    (hl),d                 ; REPAINT (entry+7,8) is garbage but the managed
                                             ; flag means wm_repaint_all skips it; MENU (entry+
                                             ; 11,12) is set by the app's gb_doc before the loop
                ld    a,(WM_FOCUS)           ; publish MW_RECT so the app can read gb_wm_x/y/w/h
                call  wm_entry               ; in main, but DON'T draw yet: the app loads its
                call  mw_publish             ; content then calls gb_restore_parent for the first
                                             ; paint, so a window never shows empty during a slow
                                             ; load (#146). A managed app MUST paint when ready.
                                             ; mw_publish preserves A (still = WM_FOCUS), so:
                jp    wm_set_clip            ; #153: pre-set the clip to the new window's rect so
                                             ; that first gb_restore_parent repaints only OUR area,
                                             ; not the whole desktop (the open "flash"); then
                                             ; wm_repaint_all restores the full-screen clip.

; mw_publish: HL = entry. Copy x,y,w,h -> MW_RECT (the app reads it via gb_wm_x/y/w/h);
; (mw_desc) = the descriptor pointer. HL preserved.
mw_publish
                push  hl
                inc   hl                     ; entry+1
                ld    de,MW_RECT
                ld    bc,4
                ldir                         ; MW_RECT = x,y,w,h ; HL = entry+5
                ld    e,(hl)
                inc   hl
                ld    d,(hl)
                ld    (mw_desc),de           ; desc ptr
                pop   hl
                ret

; mw_hook: A = a GB_MSG_* window message. Set gb_msg.type, then dispatch to the
; window's single proc (desc+6). The proc switches on the type. Clobbers HL,DE,A.
mw_hook
                ld    (GB_MSG),a              ; gb_msg.type = the message being delivered
                ld    hl,(mw_desc)
                ld    de,6
                add   hl,de                  ; HL = &desc.proc
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a                     ; HL = proc ptr
                ld    a,h
                or    l
                ret   z                       ; no proc -> skip (shouldn't happen)
                jp    md_call                 ; jp (hl); the proc rets to mw_hook's caller

; wm_chrome_draw: HL = entry. Draw frame+title (gb_open_window) then the content (on_draw).
wm_chrome_draw
                call  mw_publish
                ld    hl,(mw_desc)           ; title = *(desc+8)
                ld    de,8
                add   hl,de
                ld    e,(hl)
                inc   hl
                ld    d,(hl)
                ex    de,hl                  ; HL = title ptr (caller page)
                ld    a,(MW_RECT)
                ld    b,a                     ; x
                ld    a,(MW_RECT+1)
                ld    c,a                     ; y
                ld    a,(MW_RECT+2)
                ld    d,a                     ; w
                ld    a,(MW_RECT+3)
                ld    e,a                     ; h
                call  gb_open_window         ; frame + title
                ld    a,GB_MSG_DRAW          ; -> the proc draws the content
                jp    mw_hook

; wm_chrome_frame: HL = entry. The per-frame router: idle hook, then QUIT/close,
; close-gadget, content click. (Title drag + grip resize land in slice 2.)
wm_chrome_frame
                call  mw_publish
                ld    a,GB_MSG_FRAME         ; per-frame (idle/menus/tick)
                call  mw_hook
                ld    a,(POLL_FLAGS)
                bit   1,a                     ; GB_QUIT
                jp    nz,mw_do_close          ; (jp: mwf_max grew the block past jr range)
                ld    a,(POLL_FLAGS)
                bit   0,a                     ; GB_CLICK
                ret   z
                ld    a,(POLL_MY)            ; my - win_y in title band?
                ld    e,a
                ld    a,(MW_RECT+1)
                ld    d,a
                ld    a,e
                sub   d
                jr    c,mwf_content           ; my < win_y
                cp    14                       ; TITLE_H
                jr    nc,mwf_content           ; my >= win_y+14 -> content
                ld    a,(POLL_MX)            ; in title bar: which gadget?
                ld    e,a
                ld    a,(MW_RECT)
                add   a,5
                cp    e
                jr    c,mwf_notclose          ; win_x+5 < mx -> not the close gadget
                jr    z,mwf_notclose
                jr    mw_do_close             ; mx < win_x+5 -> close gadget
mwf_notclose
                ifdef WM_GADGETS
                ld    a,(MW_RECT)            ; maximize gadget? mx >= win_x + win_w - 4
                ld    hl,MW_RECT+2
                add   a,(hl)                  ; A = win_x + win_w
                sub   4
                cp    e
                jr    c,mwf_max              ; (win_x+win_w-4) < mx -> maximize/restore
                jr    z,mwf_max
                endif
mwf_title
                ld    a,GB_MSG_DRAG          ; otherwise a title-bar press -> drag the window
                jp    mw_hook
                ifdef WM_GADGETS
; mwf_max: the maximize/restore gadget does a WINDOWED maximize (chrome stays) - distinct from
; the borderless View>Fullscreen / 'F' (the app's on_fullscreen). Toggle the focused window
; between its saved size and full-below-the-bar (0,8 .. 80x192); flags bit2 (MW_MAXED) tracks
; which way. The pre-max geometry is one global, so restoring the earlier of two maximized
; windows would use the later one's size - a rare case not worth a per-window store.
mwf_max         ld    a,(WM_FOCUS)
                call  wm_entry               ; HL = focused entry
                push  hl
                ld    de,WM_FR_FLAGS
                add   hl,de
                bit   2,(hl)                  ; already maximized?
                jr    nz,mwf_unmax
                set   2,(hl)
                pop   hl                      ; HL = entry; save geom, then fill the screen
                push  hl
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_x),a
                ld    (hl),0
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_y),a
                ld    (hl),8
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_w),a
                ld    (hl),80
                inc   hl
                ld    a,(hl)
                ld    (wm_sav_h),a
                ld    (hl),192
                jr    mwf_max_paint
mwf_unmax       res   2,(hl)
                pop   hl                      ; HL = entry; restore the saved geometry
                push  hl
                inc   hl
                ld    a,(wm_sav_x)
                ld    (hl),a
                inc   hl
                ld    a,(wm_sav_y)
                ld    (hl),a
                inc   hl
                ld    a,(wm_sav_w)
                ld    (hl),a
                inc   hl
                ld    a,(wm_sav_h)
                ld    (hl),a
mwf_max_paint   pop   hl                      ; HL = entry
                call  mw_publish             ; refresh MW_RECT (the app reads gb_wm_x/y/w/h)
                call  clip_set_full
                jp    wm_repaint_all
wm_sav_x        db    0
wm_sav_y        db    0
wm_sav_w        db    0
wm_sav_h        db    0
                endif
mwf_content
                ld    a,GB_MSG_CLICK         ; content (incl. grip) -> a content press
                jp    mw_hook

; mw_do_close: a close was requested - deliver GB_MSG_CLOSE to the window's proc
; (it confirms + calls gb_wm_close). Every managed window has a proc, so there is
; no "no handler" path; mw_hook's null guard falls through to no-op if it's ever 0.
mw_do_close
                ld    a,GB_MSG_CLOSE
                jp    mw_hook

mw_desc         dw    0                       ; scratch: the focused managed window's descriptor

; wm_free_slot: -> A = lowest table slot whose alive flag is clear.
wm_free_slot
                ld    hl,WM_TABLE+WM_FR_FLAGS
                ld    de,WM_ESZ
                ld    c,0
wfs_l           ld    a,(hl)
                and   1
                jr    z,wfs_found
                add   hl,de
                inc   c
                ld    a,c
                cp    WM_MAXWIN
                jr    c,wfs_l
                ld    c,WM_MAXWIN-1               ; full (shouldn't happen) -> reuse last
wfs_found       ld    a,c
                ret

; k_wm_run (GB_WMRUN): register the caller (the root/desktop window) and enter the
; master loop. Never returns.
k_wm_run
                call  wm_register
wm_loop
                call  wm_map_focus               ; bank focused page; APP_HANDLER = its
                call  k_poll                      ; on_event, so a top-bar click reaches
                call  wm_focus_click             ; the right app. then route the click.
                call  wm_map_focus               ; focus may have changed
                ld    a,(WM_FOCUS)               ; call the focused window's on_frame
                call  wm_entry                   ; HL = entry
                push  hl                          ; #146: managed window -> kernel router
                ld    de,WM_FR_FLAGS
                add   hl,de
                bit   1,(hl)
                pop   hl                          ; HL = entry
                jr    z,wmf_legacy
                call  wm_chrome_frame
                jr    wmf_done
wmf_legacy
                ld    de,WM_FR_FRAME
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                call  md_call
wmf_done
                ld    hl,(BAR_HANDLER)            ; top-bar hook (#77): run the bar handler every
                ld    a,h                          ; frame in the desktop's page so the bar stays
                or    l                            ; live regardless of which window has focus.
                jr    z,wm_loop                    ; bar_draw only repaints lines 0-7 when the clock
                ld    a,PAGE_APP0                  ; minute or menu actually changes (and brackets
                call  bank_set                     ; its own redraws with gb_curhide/show), so the
                ld    hl,(BAR_HANDLER)            ; pointer over the bar is left untouched - do NOT
                call  md_call                      ; erase/show it every frame here: that landed the
                jr    wm_loop                      ; erase at the beam and dropped the bar pointer.

; k_on_bar (GB_ONBAR #77): HL = a handler the WM loop runs every frame in PAGE_APP0
; (the desktop's page) to draw the top bar, independent of focus. 0 to clear.
k_on_bar
                ld    (BAR_HANDLER),hl
                ret

; GB_WMADD registers the caller's window (legacy gb_wm_add); the jump table goes
; straight to wm_register now (#148, reclaimed the k_wm_add wrapper).

; k_wm_open (GB_WMOPEN): HL = 8.3 app name in the caller page. Load it into a free
; bank page and CALL its entry once (its main registers a window via GB_WMADD and
; returns), then restore the caller's page. Non-blocking: the app becomes a live
; window the master loop services, rather than running its own loop.
k_wm_open
                ld    de,fs_req_name
                call  copy11
                ld    hl,launch_arg               ; opened with no file -> blank the arg
                ld    b,11                          ; so the app starts file-less
                xor   a
kwo_blank       ld    (hl),a
                inc   hl
                djnz  kwo_blank
                jr    wm_open_go

; k_wm_launch (GB_WMLAUNCH): superseded. File-type -> app routing moved into the
; File Manager (C); it now calls GB_WMLAUNCHAS with the app it chose. The slot
; stays fixed (addresses) and -> k_noop (#148).

; k_wm_launch_as (GB_WMLAUNCHAS): HL = the 8.3 app name to open as a co-resident
; window. The current dir entry (fs_ent_name) becomes the new window's file arg, so
; a data file auto-opens in the app the caller chose (the File Manager picks it by
; extension - see apps/filemgr/main.c).
k_wm_launch_as
                push  hl                           ; app name
                ld    hl,fs_ent_name              ; current entry -> the launch file arg
                ld    de,launch_arg
                call  copy11
                pop   hl                           ; app name -> fs_req_name
                ld    de,fs_req_name
                call  copy11
wm_open_go
                call  wm_alloc_page
                or    a
                ret   z                            ; no free page
                ld    (wm_open_page),a
                ld    a,(bank_cur)
                ld    (wm_open_back),a
                di
                ld    a,(wm_open_page)
                call  bank_set
                ld    hl,#3F00
                ld    (fs_load_max),hl
                ld    hl,APP_BASE
                ld    (fs_load_dst),hl
                call  fs_load_sys                 ; app binary -> always the boot drive,
                jr    nc,wmo_fail                 ; not the window's browse drive (#65)
                ei
                call  APP_BASE                    ; main -> GB_WMADD + paint, then ret
                di
                ld    a,(wm_open_back)
                call  bank_set
                ei
                ret
wmo_fail
                ld    a,(wm_open_back)
                call  bank_set
                ld    a,(wm_open_page)
                call  wm_free_page
                ei
                ret

; k_wm_close (GB_WMCLOSE): close the focused (calling) window - free its page, drop
; it from the table and z-order, refocus the new z-top, and repaint. The caller's
; on_frame must return immediately after. Mapping is unchanged on return so the
; closing on_frame can still execute (its page content survives until reused).
k_wm_close
                ld    a,(WM_FOCUS)
                ld    c,a                          ; C = slot being closed
                call  wm_entry                    ; HL = entry
                ld    a,(hl)                       ; page
                push  af                           ; save it: wm_free_page clobbers everything
                ld    de,WM_FR_FLAGS
                add   hl,de
                ld    (hl),0                       ; mark dead (clear the alive flag)
                call  wm_z_remove                 ; drop slot C from the z-order (keeps C)
                call  wm_focus_top                ; refocus the new live z-top
                ld    a,#FF                        ; #156: force the next wm_map_focus to re-install
                ld    (WM_FPREV),a               ; the newly-focused (desktop) menu - else the bar
                                                  ; keeps the closed window's menu title (stale "View")
                pop   af                           ; A = the closed window's page
                call  wm_free_page                 ; release it (z-order already updated)
                call  clip_set_full              ; #156: full repaint so the desktop fully restores the
                                                  ; icons/labels the window had overlapped (clipping to
                                                  ; the window rect left truncated drive labels behind)
                jp    wm_repaint_all               ; repaint remaining windows + return

; k_wm_setpos (GB_WMSETPOS): A = x, L = y -> move the focused window's hit rect to
; (x,y), so click-to-focus follows a window the app has dragged. Also sets the fill
; clip to the damage = the bounding box of the old and new window rects, so the
; following gb_restore_parent only repaints there (no full-screen flicker).
k_wm_setpos
                ld    (sp_x),a                    ; new x
                ld    a,l
                ld    (sp_y),a                    ; new y
                ld    a,(WM_FOCUS)
                call  wm_entry                    ; HL = entry (+0); damage_axis keeps HL
                inc   hl                            ; +1 old x
                ld    b,(hl)
                inc   hl
                inc   hl                            ; +3 w
                ld    c,(hl)
                ld    a,(sp_x)
                call  damage_axis                 ; D = min(ox,nx), E = span
                ld    a,d
                ld    (clip_x),a
                ld    a,e
                ld    (clip_w),a
                dec   hl                            ; +2 old y
                ld    b,(hl)
                inc   hl
                inc   hl                            ; +4 h
                ld    c,(hl)
                ld    a,(sp_y)
                call  damage_axis
                ld    a,d
                ld    (clip_y),a
                ld    a,e
                ld    (clip_h),a
                dec   hl                            ; +4 -> +1 (x)
                dec   hl
                dec   hl
                ld    a,(sp_x)                     ; commit the new position
                ld    (hl),a
                inc   hl
                ld    a,(sp_y)
                ld    (hl),a
                ret
sp_x            db    0
sp_y            db    0

; k_wm_setsize (GB_WMSETSIZE, #81): A = new w, L = new h. Resize the focused window's
; rect (top-left stays put). Damage clip = its (x,y) covering max(old,new) size, so the
; following gb_restore_parent repaints any area a shrink vacated.
k_wm_setsize
                ld    (ss_w),a
                ld    a,l
                ld    (ss_h),a
                ld    a,(WM_FOCUS)
                call  wm_entry                    ; HL = entry (+0 page)
                inc   hl                            ; +1 x
                ld    a,(hl)
                ld    (clip_x),a                  ; clip x = window x (unchanged)
                inc   hl                            ; +2 y
                ld    a,(hl)
                ld    (clip_y),a
                inc   hl                            ; +3 w
                ld    b,(hl)                       ; old w
                ld    a,(ss_w)                     ; clip w = max(old, new)
                cp    b
                jr    nc,kss_w
                ld    a,b
kss_w
                ld    (clip_w),a
                ld    a,(ss_w)
                ld    (hl),a                       ; commit new w
                inc   hl                            ; +4 h
                ld    b,(hl)                       ; old h
                ld    a,(ss_h)
                cp    b
                jr    nc,kss_h
                ld    a,b
kss_h
                ld    (clip_h),a
                ld    a,(ss_h)
                ld    (hl),a                       ; commit new h
                ret
ss_w            db    0
ss_h            db    0

; damage_axis: B = old, A = new, C = size -> D = min(old,new), E = span =
; max(old,new) + size - min. The 1-D damage extent of a move. Preserves HL.
damage_axis
                cp    b
                jr    nc,dax_oldmin              ; new >= old -> min = old, max = new
                ld    d,a                           ; min = new
                ld    a,b                           ; max = old
                jr    dax_span
dax_oldmin      ld    d,b                           ; min = old (A = new = max)
dax_span        add   a,c                           ; max + size
                sub   d                             ; - min
                ld    e,a
                ret

; wm_map_focus: bank to the focused window's page and point APP_HANDLER at its
; on_event (so menu_dispatch in k_poll delivers top-bar clicks to the focused app).
; On a focus change, also swap the top-bar menu to the focused window's (or clear
; it) so the bar shows the right menu and clicks reach the right handler.
wm_map_focus
                ld    a,(WM_FOCUS)
                call  wm_entry                    ; HL = entry
                ld    a,(hl)                       ; page (+0)
                call  bank_set                     ; preserves HL
                push  hl
                ld    de,WM_FR_EVENT
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    (APP_HANDLER),hl
                pop   hl                            ; HL = entry
                ld    a,(WM_FOCUS)               ; focus changed since last frame?
                ld    de,WM_FPREV
                ld    a,(de)
                ld    b,a
                ld    a,(WM_FOCUS)
                cp    b
                ret   z                            ; no change -> leave the bar as is
                ld    (de),a                       ; WM_FPREV = focus
                ld    de,WM_FR_MENU               ; focused window's menu def ptr
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    a,h
                or    l
                jp    z,menu_clear                 ; no menu -> empty the bar
                jp    menu_install                 ; install the focused window's menu

; wm_focus_click: if a fresh click landed on a window other than the focused one,
; move focus there. App windows also rise to the z-top (and the stack repaints) if
; not already on top; the desktop (slot 0) stays pinned at the bottom. The click is
; not consumed - it is then delivered to the newly focused window's on_frame.
wm_focus_click
                ld    a,(POLL_FLAGS)
                bit   0,a
                ret   z                            ; no fresh click
                call  wm_hit_test                  ; CF set = no window hit (e.g. top bar)
                ret   c
                ld    b,a
                ld    a,(WM_FOCUS)
                cp    b
                ret   z                            ; already focused -> deliver
                ld    a,b
                ld    (WM_FOCUS),a               ; focus the clicked window
                or    a
                ret   z                            ; desktop: keep it at the bottom
                ld    c,a                          ; c = clicked app slot
                ld    a,(WM_NWIN)               ; already the z-top? then nothing to raise
                dec   a
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)
                cp    c
                ret   z
                ld    a,c
                call  wm_raise                     ; bring it to the front
                ld    a,(WM_FOCUS)               ; damage = the raised window's rect
                call  wm_set_clip
                jp    wm_repaint_all               ; restack the screen (clipped)

; wm_hit_test: -> A = slot of the top-most window whose rect contains the pointer
; (POLL_MX, POLL_MY), scanning z-order top->bottom; CF set if none.
wm_hit_test
                ld    a,(WM_NWIN)
                ld    (wm_hz),a
wht_l           ld    a,(wm_hz)
                or    a
                jr    z,wht_none
                dec   a
                ld    (wm_hz),a
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)                       ; slot = WM_Z[i]
                call  wm_entry                     ; HL = entry
                inc   hl                            ; +1 x
                ld    a,(POLL_MX)
                sub   (hl)
                jr    c,wht_l                       ; mx < x
                ld    c,a                            ; mx - x
                inc   hl                            ; +2 y
                inc   hl                            ; +3 w
                ld    a,c
                cp    (hl)
                jr    nc,wht_l                      ; mx-x >= w
                dec   hl                            ; +2 y
                ld    a,(POLL_MY)
                sub   (hl)
                jr    c,wht_l                       ; my < y
                ld    c,a                            ; my - y
                inc   hl                            ; +3 w
                inc   hl                            ; +4 h
                ld    a,c
                cp    (hl)
                jr    nc,wht_l                      ; my-y >= h
                ld    a,(wm_hz)                     ; hit -> recover slot
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)
                or    a                             ; clear CF
                ret
wht_none        scf
                ret

; k_drag_start (GB_DRAGSTART, #62): HL = 11-byte 8.3 name of the dragged item in
; the caller (source) page. Record it, then run a drag loop (the WM loop is
; suspended) that follows the pointer until the fire button is released. On release,
; if the pointer moved and the drop point is over a different live window that has
; an on_event handler, deliver a GB_MSG_DROP to it (its page mapped) - it reads the
; name from WM_DRAGNAME and the drop position from gb_mx/gb_my. Returns A = 1 if it
; was dropped on a target, A = 0 if it was just a click (no move / no target /
; dropped on itself) so the source can fall through to its normal click handling.
k_drag_start
                ld    de,WM_DRAGNAME              ; stash the dragged name
                call  copy11
                ld    a,(bank_cur)               ; remember the source page to restore
                ld    (wm_drag_pg),a
                ld    a,(WM_FOCUS)               ; source = the focused (calling) window
                ld    (WM_DRAGSRC),a
                ld    a,(fs_cur_drive)           ; capture the source drive + directory now
                ld    (WM_DRAGDRV),a             ; (active = the source FM's) for a later
                ld    hl,fs_dir_clus             ; cross-drive copy (#65 phase 3)
                ld    de,WM_DRAGDIR
                ld    bc,4
                ldir
                xor   a
                ld    (WM_DRAGMOV),a             ; moved = 0, ghost not shown yet
                ld    (ghost_on),a
                ld    a,(POLL_MX)                ; seed the last position
                ld    (WM_DRAGX0),a
                ld    a,(POLL_MY)
                ld    (WM_DRAGY0),a
ds_loop
                call  k_poll                      ; pace 50Hz + track pointer + POLL_*
                ld    a,(POLL_MX)                ; moved since last frame?
                ld    hl,WM_DRAGX0
                cp    (hl)
                jr    nz,ds_moved
                ld    a,(POLL_MY)
                ld    hl,WM_DRAGY0
                cp    (hl)
                jr    z,ds_held                   ; stationary -> leave the ghost as is
ds_moved        ld    a,(POLL_MX)                ; record the new last position
                ld    (WM_DRAGX0),a
                ld    a,(POLL_MY)
                ld    (WM_DRAGY0),a
                ld    a,1
                ld    (WM_DRAGMOV),a
                ld    a,(ghost_on)               ; ghost already up? -> move it
                or    a
                jr    nz,ds_gtrack
                call  cursor_erase                ; first movement: hide the arrow and
                ld    a,1                          ; show a box that follows the pointer
                ld    (cur_supp),a
                ld    (ghost_on),a
                ld    a,GHOST_W                   ; the box's save-under is fixed-size in
                ld    (sb_w),a                     ; the idle IDE sector buffer; set once
                ld    a,GHOST_H
                ld    (sb_h),a
                ld    hl,fs_secbuf
                ld    (sb_buf),hl
                jr    ds_gpaint
ds_gtrack       call  restore_block               ; erase the box at its old place
ds_gpaint       call  ghost_place
                call  ghost_draw
ds_held         ld    a,(POLL_FLAGS)            ; fire still held -> keep dragging
                bit   2,a
                jr    nz,ds_loop
                ld    a,(ghost_on)               ; released: tear the ghost down and
                or    a                            ; bring the arrow back
                jr    z,ds_norel
                call  restore_block               ; erase the box
                xor   a
                ld    (ghost_on),a
                ld    (cur_supp),a               ; A = 0 -> cursor visible again
                call  cursor_draw
ds_norel
                ld    a,(WM_DRAGMOV)            ; never moved -> a plain click
                or    a
                jr    z,ds_click
                call  wm_hit_test                 ; CF = nothing under the drop point
                jr    c,ds_click
                ld    b,a                          ; B = target slot
                ld    a,(WM_DRAGSRC)
                cp    b
                jr    z,ds_click                   ; dropped on itself -> no-op
                ld    a,b
                call  wm_entry                     ; HL = target entry
                ld    c,(hl)                       ; C = target page (+0)
                ld    de,WM_FR_EVENT               ; target's on_event handler
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    a,h
                or    l
                jr    z,ds_click                   ; no handler -> treat as a click
                ld    a,GB_MSG_DROP
                ld    (GB_MSG),a
                ld    a,c                           ; map the target's page, call it
                call  bank_set                      ; (it reads WM_DRAGNAME + POLL_MX/MY)
                call  md_call
                ld    a,(wm_drag_pg)              ; restore the source page
                call  bank_set
                ld    a,1
                ret
ds_click
                ld    a,(wm_drag_pg)              ; ensure the source page is mapped
                call  bank_set
                xor   a
                ret
wm_drag_pg      db    0

; ghost_place: sb_x/sb_y = the pointer (POLL_MX/MY), clamped so the GHOST_W x
; GHOST_H box stays on screen. sb_w/sb_h/sb_buf were set once when the ghost first
; appeared (they only change when the cursor save-under runs, which is suppressed
; during the drag), so erase = restore_block and draw = save_block + outline.
ghost_place
                ld    a,(POLL_MX)
                cp    80-GHOST_W+1
                jr    c,gpl_x
                ld    a,80-GHOST_W
gpl_x           ld    (sb_x),a
                ld    a,(POLL_MY)
                cp    200-GHOST_H+1
                jr    c,gpl_y
                ld    a,200-GHOST_H
gpl_y           ld    (sb_y),a
                ret

; ghost_draw: save the screen under the box, then draw a red outline there.
ghost_draw
                call  save_block
                ld    a,(sb_x)
                ld    b,a
                ld    a,(sb_y)
                ld    c,a
                ld    d,GHOST_W
                ld    e,GHOST_H
                ld    a,3
                jp    k_frame

ghost_on        db    0

; wm_raise: A = slot -> move it to z-order top and focus it (keeps the others'
; relative order). Compacts WM_Z removing the slot, then re-appends it at the end.
wm_raise
                ld    (WM_FOCUS),a
                ld    c,a
                call  wm_z_remove                 ; drop it from the z-order...
                ld    a,c
                jp    wm_z_append                  ; ...and re-append on top (tail-call)

; wm_repaint_all: repaint every window bottom-up (each in its own page, via its
; on_repaint), then restore the caller's page. The cursor is left to the handlers:
; each on_repaint redraws over the old pointer (a full backdrop fill, or a leading
; gb_curhide) and ends with gb_curshow, so the cursor stays correct with no double
; save-under here. WM_Z[0] is always the desktop, so its full paint runs first.
wm_repaint_all
                ld    a,(bank_cur)
                ld    (wm_rp_back),a
                ld    a,(cur_supp)              ; #148: hide the pointer before the repaint so it
                or    a                           ; can't pollute its save-under (else a later move
                call  z,cursor_erase             ; restores stale content = a pointer-sized hole).
                ld    a,1                         ; lock the cursor for the whole loop: app on_draw
                ld    (cur_paintlock),a           ; handlers also bracket with gb_curhide/show, and
                                                  ; that 2nd erase restores a stale save-under over
                                                  ; chrome a window drew earlier this pass (the XAOS
                                                  ; title hole, on File>New AND drag). Bracket ONCE.
                di                                ; Skip during a DnD ghost (cur_supp owns the screen)
                xor   a
                ld    (wm_rp_i),a
wra_l           ld    a,(wm_rp_i)
                ld    hl,WM_NWIN
                cp    (hl)
                jr    nc,wra_done
                ld    hl,WM_Z
                add   a,l
                ld    l,a
                ld    a,(hl)                       ; slot = WM_Z[i]
                call  wm_entry                     ; HL = entry
                push  hl                           ; #148 guard: never paint a dead slot
                ld    de,WM_FR_FLAGS
                add   hl,de
                ld    a,(hl)                       ; flags
                pop   hl                            ; HL = entry
                bit   0,a                          ; alive?
                jr    z,wra_next                   ; dead -> skip (z-order should exclude it)
                push  af                           ; keep flags across bank_set
                ld    a,(hl)                       ; page
                call  bank_set                     ; (preserves HL = entry)
                pop   af                            ; #146: managed -> kernel draws chrome
                bit   1,a                          ; managed?
                jr    z,wra_legacy
                call  wm_chrome_draw
                jr    wra_next
wra_legacy
                ld    de,WM_FR_REPAINT
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    a,h
                or    l
                jr    z,wra_next                   ; no handler
                call  md_call
wra_next        ld    a,(wm_rp_i)
                inc   a
                ld    (wm_rp_i),a
                jr    wra_l
wra_done        ld    a,(wm_rp_back)
                call  bank_set
                call  clip_set_full              ; repaints are clip-limited; restore the
                ei                                ; full-screen clip for normal drawing
                xor   a                           ; unlock: loop done, now show the pointer ONCE
                ld    (cur_paintlock),a           ; over the final composited screen (fresh save-under)
                ld    a,(cur_supp)              ; #148: ensure the pointer is up with a FRESH
                or    a                           ; save-under over the just-repainted content.
                call  z,cursor_show               ; GUARDED (#153): a handler that ends in gb_curshow
                ret                                ; (e.g. the desktop paint) already drew it - don't
                                                  ; redraw, or we'd save cur_bg OVER the cursor's own
                                                  ; pixels and stamp a ghost on the next move.

; k_wm_damage (GB_WMDAMAGE): set the repaint clip to a caller-supplied damage rect,
; so the next gb_restore_parent only repaints that region instead of the whole
; screen (#153 - kills the full-desktop flash after a dropdown menu). gb_popup
; passes its box UNION the focused window's rect, so the repaint covers both the
; dropdown's footprint (the part that overhangs other windows) and the window the
; menu action changed. Registers are pre-swapped by the trampoline for two word
; stores: C=x B=y (-> clip_x,clip_y) and E=w D=h (-> clip_w,clip_h).
k_wm_damage
                ld    (clip_x),bc                 ; clip_x = x, clip_y = y
                ld    (clip_w),de                 ; clip_w = w, clip_h = h
                ret

; clip_set_full: reset the fill clip to the whole screen (no clipping). Two word
; stores (clobbers BC,DE - the only caller reloads A and ignores BC/DE, #153).
clip_set_full
                ld    bc,0                       ; clip_x = 0, clip_y = 0
                ld    (clip_x),bc
                ld    de,#C850                   ; clip_w = 80 (#50), clip_h = 200 (#C8)
                ld    (clip_w),de
                ret

; wm_set_clip: A = slot -> set the fill clip to that window's rect, so the next
; wm_repaint_all only erases inside the window's area (its damage region).
wm_set_clip
                call  wm_entry                    ; HL = entry
                inc   hl                            ; +1 x
                ld    a,(hl)
                ld    (clip_x),a
                inc   hl                            ; +2 y
                ld    a,(hl)
                ld    (clip_y),a
                inc   hl                            ; +3 w
                ld    a,(hl)
                ld    (clip_w),a
                inc   hl                            ; +4 h
                ld    a,(hl)
                ld    (clip_h),a
                ret

; wm_entry: A = slot index -> HL = WM_TABLE + A*WM_ESZ. Clobbers A,B,DE.
wm_entry
                ld    hl,WM_TABLE
                or    a
                ret   z
                ld    b,a
                ld    de,WM_ESZ
we_add
                add   hl,de
                djnz  we_add
                ret

wm_desc         dw    0            ; wm_register: descriptor ptr
wm_slot         db    0            ; wm_register: chosen slot
wm_open_page    db    0            ; k_wm_open: page being loaded
wm_open_back    db    0            ; k_wm_open: caller's page to restore
wm_hz           db    0            ; wm_hit_test: z scan index
wm_rp_back      db    0            ; wm_repaint_all: caller's page
wm_rp_i         db    0            ; wm_repaint_all: z index

; menu_dispatch: called from k_poll. If a fresh click (D bit0) landed in the
; kernel-owned top bar and the app registered a handler, deliver a GB_MSG_MENU
; message (p0 = byte column) and consume the click. The app's page is mapped
; (it called GB_POLL), so the handler is a direct CALL - no bank trampoline.
menu_dispatch
                ld    a,(UI_MODAL)            ; a paged UI dialog is up? then the focused app's
                or    a                        ; bank is swapped out for the module - do NOT call
                ret   nz                        ; its handler. The dialog's own poll sees the click
                bit   0,d                     ; fresh click this frame?
                ret   z
                ld    a,(poll_line)
                cp    8                        ; inside the 8px top bar (rows 0..7)?
                ret   nc
                ld    hl,(APP_HANDLER)        ; handler registered?
                ld    a,h
                or    l
                ret   z
                res   0,d                      ; consume the click (the app's poll
                ld    a,GB_MSG_MENU            ; loop won't also see it)
                ld    (GB_MSG),a
                ld    a,(poll_byte)
                ld    (GB_MSG+1),a            ; p0 = clicked byte column
                push  de                       ; preserve the poll flags across the
                call  md_call                  ; C handler (call (hl))
                pop   de
                ret
md_call         jp    (hl)

; k_menu (GB_MENU): register the calling app's top-bar menu. HL = a blob in the
; app page: count, then per title { col (byte), 8-byte NUL/space-padded label }.
; Copied into resident MENU_DEF; the desktop's bar handler watches MENU_DEF and
; redraws the titles when it changes (#77). launch_app clears it for a child app.
k_menu                                          ; HL = def ptr in the focused app's page.
                ; Also record it as the focused window's menu, so a later focus change
                ; re-installs it instead of clearing the bar (#142: gb_menu must persist
                ; like a static gb_win_t.menu does).
                push  hl
                ld    a,(WM_FOCUS)
                call  wm_entry                  ; HL = focused window's WM entry
                ld    de,WM_FR_MENU
                add   hl,de                     ; -> its menu-def-ptr field
                pop   de                         ; DE = the def ptr
                ld    (hl),e
                inc   hl
                ld    (hl),d
                ex    de,hl                       ; HL = def ptr (fall into the copy)
menu_install                                    ; HL = menu def (mapped page) -> MENU_DEF
                ld    a,(hl)                  ; count
                cp    5
                ret   nc                       ; > 4 titles unsupported -> ignore
                ld    e,a                       ; len = count*9 + 1
                add   a,a
                add   a,a
                add   a,a
                add   a,e
                inc   a
                ld    c,a
                ld    b,0
                ld    de,MENU_DEF
                ldir
                ret
; menu_clear: empty the top-bar menu (focus moved to a window with no menu).
menu_clear
                xor   a
                ld    (MENU_DEF),a
                ret

; (menu titles are rendered by the desktop's bar handler now, from MENU_DEF - #77)

; (File-type -> app routing lives in the File Manager now; the kernel just opens
; the app the FM names, via GB_WMLAUNCHAS. The old resident app_for_ext + its name
; tables were removed to reclaim space - the kernel->C migration that funds the
; .APP launch model, #70.)
launch_arg      defs  11

; k_icon (GB_ICON): blit a full icon. A = slot, B = x, C = y. Reads the bitmap
; from the .IST in PAGE_DATA, so swap pages around it.
k_icon
                ld    (gi_slot),a
                ld    a,b
                ld    (gi_x),a
                ld    a,c
                ld    (gi_y),a
                call  to_data
                ld    a,(gi_slot)
                call  icon_full_geom
                call  blit_bitmap
                jp    from_data
icon_full_geom                                 ; A = slot -> bm_src/w/h, bm_x/y
                ld    l,a
                ld    h,0
                add   hl,hl
                add   hl,hl                     ; slot*4
                ld    de,DATA_ICONS+16
                add   hl,de
                ld    e,(hl)                  ; offset
                inc   hl
                ld    d,(hl)
                inc   hl
                ld    a,(hl)                  ; width
                ld    (bm_w),a
                inc   hl
                ld    a,(hl)                  ; height (full)
                ld    (bm_h),a
                ld    hl,DATA_ICONS
                add   hl,de
                ld    (bm_src),hl
                ld    a,(gi_x)
                ld    (bm_x),a
                ld    a,(gi_y)
                ld    (bm_y),a
                ret
gi_slot         db    0
gi_x            db    0
gi_y            db    0

; k_fill (GB_FILL): filled rectangle. B=x C=y D=w E=h A=pen. Capture the params
; before pen_to_byte (it clobbers E).
k_fill
                ld    (kf_pen),a
                ld    a,b
                ld    (fb_x),a
                ld    a,c
                ld    (fb_y),a
                ld    a,d
                ld    (fb_w),a
                ld    a,e
                ld    (fb_h),a
                ld    a,(kf_pen)
                call  pen_to_byte
                ld    (fb_val),a
                jp    fill_block
kf_pen          db    0

; k_saverect / k_restorerect (GB_SAVERECT / GB_RESTORERECT): save/restore a screen
; rectangle to/from a caller buffer. B=x C=y D=w E=h HL=buffer (w*h bytes, in the
; caller's page, mapped during the call). Thin wrappers over the cursor save-under
; primitives save_block/restore_block (lib/screen.asm), which manage the upper-ROM
; shadow when reading screen RAM. The sb_* params are shared with the cursor, so a
; caller must bracket with gb_curhide/gb_curshow (#114: PAINT canvas blit).
k_saverect
                call  rect_args
                jp    save_block
k_restorerect
                call  rect_args
                jp    restore_block
rect_args                                      ; B=x C=y D=w E=h HL=buf -> sb_*
                ld    a,b
                ld    (sb_x),a
                ld    a,c
                ld    (sb_y),a
                ld    a,d
                ld    (sb_w),a
                ld    a,e
                ld    (sb_h),a
                ld    (sb_buf),hl
                ret

; k_mxp (GB_MXP, #114): HL = the pointer's PIXEL x (0-319), for pixel-accurate
; drawing (gb_mx only gives the byte column). cursor_x is in 1/2-pixel units.
k_mxp
                ld    hl,(cursor_x)
                srl   h
                rr    l                          ; HL = cursor_x / 2 = pixel x
                ret

; k_xorframe (GB_XORFRAME): stub. The rubber-band XOR frame was never wired up
; (no app uses it, no gb_xorframe binding); body removed to reclaim resident
; space. The jump-table slot stays (addresses fixed) and -> k_noop (#148).

; ===========================================================================
; Top bar (kernel-owned): total RAM (left) + clock (right) on lines 0-7. The
; kernel keeps the clock ticking via clock_tick in k_poll, so it stays live
; whatever app is focused. Apps must keep clear of the top 8 lines.
; ===========================================================================
CLK_COL         equ   68
TICKS_PER_SEC   equ   50
RTC_ADDR        equ   #FD15        ; Dallas RTC on SymbiFace II / Cyboard
RTC_DATA        equ   #FD14

; clock_tick: once per frame from k_poll. Advances the software clock (when there is
; no RTC) every 50 frames, so gb_time stays correct. The desktop draws the bar now,
; so there is no clock drawing here (#77).
clock_tick
                ld    a,(clk_frames)
                inc   a
                cp    TICKS_PER_SEC
                jr    c,ct_keep
                xor   a
                ld    (clk_frames),a
                ld    a,(have_rtc)
                or    a
                call  z,sw_tick
                ret
ct_keep
                ld    (clk_frames),a
                ret

clock_init                                      ; detect the RTC + seed the software clock
                xor   a
                ld    (sw_sec),a
                ld    (sw_min),a
                ld    (sw_hour),a
                ld    (clk_frames),a
                call  rtc_detect
                ret
rtc_detect
                ld    e,#5A
                call  rtc_nvram_rw
                cp    #5A
                jr    nz,rd_none
                ld    e,#A5
                call  rtc_nvram_rw
                cp    #A5
                jr    nz,rd_none
                ld    a,1
                ld    (have_rtc),a
                ret
rd_none
                xor   a
                ld    (have_rtc),a
                ret
rtc_nvram_rw                                   ; E = value -> A = read-back of NVRAM 0x0E
                ld    a,#0E
                ld    bc,RTC_ADDR
                out   (c),a
                ld    bc,RTC_DATA
                out   (c),e
                ld    a,#0E
                ld    bc,RTC_ADDR
                out   (c),a
                ld    bc,RTC_DATA
                in    a,(c)
                ret
read_rtc_reg                                   ; A = reg -> A = value
                ld    bc,RTC_ADDR
                out   (c),a
                ld    bc,RTC_DATA
                in    a,(c)
                ret
; (read_time / put_bcd2 / bin_to_bcd removed: the desktop formats the clock from
; gb_time now - #77. sw_tick stays: it advances the software clock for gb_time.)
sw_tick
                ld    a,(sw_sec)
                inc   a
                cp    60
                jr    c,swt_sec
                xor   a
                ld    (sw_sec),a
                ld    a,(sw_min)
                inc   a
                cp    60
                jr    c,swt_min
                xor   a
                ld    (sw_min),a
                ld    a,(sw_hour)
                inc   a
                cp    24
                jr    c,swt_hour
                xor   a
swt_hour
                ld    (sw_hour),a
                ret
swt_min
                ld    (sw_min),a
                ret
swt_sec
                ld    (sw_sec),a
                ret
; k_time (GB_TIME #808D): current time -> GB_TIME_BUF (raw h, m, s + binmode). From
; the Dallas RTC (regs 4/2/0; binmode = reg B bit2) or the software clock (binary).
; The app converts BCD->binary when binmode is 0. Kept tiny (#72).
k_time
                ld    a,(have_rtc)
                or    a
                jr    z,kt_soft
                ld    a,#0B
                call  read_rtc_reg
                and   #04
                ld    (GB_TIME_BUF+3),a       ; binmode (0 = BCD)
                ld    a,#04
                call  read_rtc_reg
                and   #3F
                ld    (GB_TIME_BUF+0),a
                ld    a,#02
                call  read_rtc_reg
                ld    (GB_TIME_BUF+1),a
                ld    a,#00
                call  read_rtc_reg
                ld    (GB_TIME_BUF+2),a
                ret
kt_soft
                ld    a,(sw_hour)
                ld    (GB_TIME_BUF+0),a
                ld    a,(sw_min)
                ld    (GB_TIME_BUF+1),a
                ld    a,(sw_sec)
                ld    (GB_TIME_BUF+2),a
                ld    a,4                       ; nonzero -> already binary, no convert
                ld    (GB_TIME_BUF+3),a
                ret

; (fmt_mem migrated to the GBCFG C module: gb_fmt_mem in kernel/kc/kcfg.c. The
; kernel leaves the KB count in KCFG_MEMKB before cfg_boot; the module writes the
; "<decimal>K" string to KCFG_MEMSTR, which draw_topbar renders.)

; mem_detect: count present 64K expansion banks and return the total in A.
;
; Yarek (CPC4MB) banking (#138): a bank's 64K block 0 is paged into #4000-#7FFF
; by OUT (&7Fhi00),&C4|group<<3 (mode 4), where the port high byte selects the
; upper bank group INVERTED - &7F = banks 0-7 (DK'tronics), &7E = 8-15 (Yarek
; 1MB), &7D/&7C = 16-31. So bank b lives at port (&7F-(b>>3)), group (b&7);
; md_page forms that. We scan b=0.. writing a per-bank signature and reading it
; back, stopping at the first bank that is absent OR mirrors bank 0 (1984 and
; real Yarek alias absent banks down to the first expansion bank) - which keeps
; a 576K machine from being mis-counted as 1MB. MUST run resident and BEFORE
; PAGE_DATA is filled (it pokes every bank's #4000). Returns A = bank count.
mem_detect
                ld    hl,(#4000)             ; preserve the live #4000 word
                ld    (md_save),hl
                xor   a
                ld    (md_count),a
                ; --- 64K guard: confirm banked RAM is distinct from main memory.
                ; On a 64K machine the GA ignores banking, so every "bank" is just
                ; main #4000 and would be mis-counted; bail out reporting zero. ---
                ld    bc,#7FC0
                out   (c),c                  ; standard config: #4000 = main RAM
                ld    a,#A5
                ld    (#4000),a              ; marker in main memory
                xor   a
                call  md_page                ; page expansion bank 0 over #4000
                ld    a,#5A
                ld    (#4000),a              ; different marker into the bank
                ld    bc,#7FC0
                out   (c),c                  ; back to main
                ld    a,(#4000)
                cp    #A5                    ; main untouched -> banking works
                jr    nz,md_done             ; clobbered -> 64K, no expansion
                ; --- scan banks, writing/verifying a signature per bank.
                ; E = bank index (full_bg); md_page preserves D/E/H. ---
                ld    e,0
md_loop
                ld    a,e
                call  md_page                ; select bank E
                ld    a,e
                ld    (#4000),a              ; signature lo = index
                cpl
                ld    (#4001),a              ; signature hi = ~index
                xor   a
                call  md_page                ; re-select bank 0 (mirror reference)
                ld    a,(#4000)
                or    a                       ; bank 0 still holds its 0 ?
                jr    nz,md_done             ; corrupted -> bank E aliased bank 0
                ld    a,(#4001)
                inc   a                       ; bank 0 hi still #FF ? (inc #FF -> 0)
                jr    nz,md_done
                ld    a,e
                call  md_page                ; back to bank E
                ld    a,(#4000)
                cp    e                       ; bank E holds its own signature ?
                jr    nz,md_done
                ld    a,(#4001)
                ld    d,a
                ld    a,e
                cpl
                cp    d
                jr    nz,md_done
                ld    hl,md_count            ; bank E present and distinct
                inc   (hl)
                inc   e
                ld    a,e
                cp    16                      ; 16 banks = 1MB (Yarek/1984 cap)
                jr    nz,md_loop
md_done
                ld    bc,#7FC0               ; leave the standard 64K config selected
                out   (c),c
                ld    hl,(md_save)
                ld    (#4000),hl
                ld    a,(md_count)
                ret
; md_page: A = bank index (full_bg 0..15). Pages that 64K bank's block 0 into
; #4000-#7FFF (mode 4). Port hi = &7F-(idx>>3); value = &C4|(idx&7)<<3. Trashes
; A,B,C,L; preserves D/E/H so the scan loop's index survives.
md_page
                ld    c,a
                srl   a
                srl   a
                srl   a                       ; A = bank_high (idx>>3)
                ld    b,a
                ld    a,#7F
                sub   b                       ; A = &7F - bank_high (port high byte)
                ld    b,a
                ld    a,c
                and   7                        ; group = idx & 7
                add   a,a
                add   a,a
                add   a,a                       ; group<<3
                or    #C4                      ; value = %11 ggg 100 (mode 4)
                ld    l,a
                ld    c,0                       ; port lo = 00
                out   (c),l                    ; OUT (&7Fhi00), value
                ret

sw_sec          db    0            ; software clock (when no RTC); advanced by sw_tick,
sw_min          db    0            ; read by gb_time. The desktop draws/formats the bar.
sw_hour         db    0
clk_frames      db    0
have_rtc        db    0
md_save         dw    0
md_count        db    0

                include "../lib/screen.asm"
                include "../lib/text.asm"
                include "../lib/cursor_arrow.asm"
                include "../lib/cursor.asm"
                include "../lib/input.asm"
                include "../lib/fs.asm"
                ifdef GB_ROM                  ; #152: the GEOBENCH.ROM seam, shared by every backend
                include "../lib/fs_rom_seam.asm"
                endif
                if STORAGE_ALBIREO
                include "../lib/fs_albireo.asm"
                else
                include "../lib/fs_ide_fat.asm"
                endif
                include "../lib/fs_amsdos.asm"
                include "../lib/bank.asm"
kern_end                                        ; GBKERN.BIN = #8000..kern_end only.

; --- STABILITY GUARD: the resident kernel must not eat the stack -----------------
; The CPC runs on the UniDOS stack, which grows DOWN from HIMEM (&A288) toward
; kern_end - the only stack space is the gap between them. If the kernel grows into
; the reserve the stack smashes the resident image on a deep call + interrupt (a
; silent crash). This assert turns that into a LOUD build failure so a too-big
; kernel is caught here, not in the field. (Reclaim, or move scratch out, to fix.)
HIMEM           equ   #A288        ; UniDOS HIMEM (stack top); see docs/AMSDOS notes
STACK_RESERVE   equ   256          ; min bytes kept free below HIMEM for the stack (#95)
                if SPIKE                     ; #130: the throwaway spike hangs early (tiny stack)
                else
                assert HIMEM-kern_end>=STACK_RESERVE,"GBKERN too big - reclaim resident bytes (see #104)"
                endif

; --- scratch buffers live in LOW RAM (always the main bank, below the stack) -----
; They used to sit just above kern_end, but as the kernel grew they landed in the
; UniDOS stack's path: HIMEM &A288 grows DOWN toward kern_end, so a 512-byte sector
; write during a deep call (a Notepad save) smashed the return stack and the IDE
; loop ran away. Low RAM #1800+ (the retired GBFAT transfer area) is clear of both
; the descending stack and the resident image. Fixed addresses, not loaded data.
fs_secbuf       equ   #1800            ; IDE sector buffer / aliased AMSDOS write sector
fsam_buf        equ   #1A00            ; floppy whole-directory buffer
                                                ; The packaging incbins below are
                                                ; never loaded at runtime, only read
                                                ; by `save`. The payload outgrew a
                                                ; single free region, so it is split:
                                                ; the modules/fonts/assets sit ABOVE
                                                ; the kernel, the (larger) app binaries
                                                ; in low memory at #0100 - both within
                                                ; the 64K image and clear of #8000..
                                                ; kern_end (GBKERN.BIN).
cfg_img         incbin "../build/GBCFG.RAW"     ; config-parser C module, as GBCFG.BIN
cfg_imgend
fat_img         incbin "../build/GBFAT.RAW"     ; FAT16/IDE write module, as GBFAT.BIN
fat_imgend
flsv_img        incbin "../build/FLOPPYSV.RAW"  ; AMSDOS/floppy write module, as FLOPPYSV.BIN (#135)
flsv_imgend
font_img        incbin "../build/DEFAULT.FNT"   ; packaged on the disk as DEFAULT.FNT
font_imgend
cfont_img       incbin "../build/CLASSIC.FNT"   ; alternate 8x8 font (FONT=CLASSIC)
cfont_imgend
icon_img        incbin "../build/DEFAULT.IST"   ; packaged on the disk as DEFAULT.IST
icon_imgend
                ; STABILITY GUARD: the icon set loads into PAGE_DATA at DATA_ICONS and
                ; must stay below DATA_MODTOP (the write module shares PAGE_DATA) - else a
                ; save/delete/copy overwrites the end of the set (garbled icons, #88).
                assert icon_imgend-icon_img<=DATA_MODTOP-DATA_ICONS-#200,"DEFAULT.IST too big: would collide with the write module in PAGE_DATA - fewer icons or raise DATA_MODTOP"
wel_img         incbin "../assets/WELCOME.TXT"  ; a sample text file to open in VIEWER
wel_imgend
                include "../lib/cursor_data.asm" ; cur_spr_data..cur_spr_end -> DEFAULT.SPR
                include "../lib/cursor_hand_data.asm" ; cur_hand_data..end -> HAND.SPR
                ; Overflow apps are packaged by FURTHER rasm passes: this 64K image
                ; filled up, and the .dsk save accumulates across rasm invocations, so
                ; the largest binaries get their own passes - PAINT/XAOS/ICONED in
                ; pack_apps.asm (#114), and the gb_doc-grown VIEWER + FILEMGR in
                ; pack_apps2.asm (#142). Both are too big for this image's free regions.
                org   #0100                     ; --- app binaries, low region ---
dtp_img         incbin "../build/DESKTOP.RAW"   ; packaged on the disk as DESKTOP.APP
dtp_imgend
npd_img         incbin "../build/NOTEPAD.RAW"   ; packaged on the disk as NOTEPAD.APP
npd_imgend
clk_img         incbin "../build/CLOCK.RAW"     ; packaged on the disk as CLOCK.APP
clk_imgend
pist_img        incbin "../build/PAINT.IST"     ; PAINT toolchest set (#114): PAINT loads it,
pist_imgend                                     ; ICONED edits it. Packaging only - down here
                                                ; in the low region to keep the high region < #FFFF
                save  "GBKERN.BIN",GB_KERNEL,kern_end-GB_KERNEL,DSK,"build/gbkern.dsk"
                save  "DESKTOP.APP",dtp_img,dtp_imgend-dtp_img,DSK,"build/gbkern.dsk"
                save  "NOTEPAD.APP",npd_img,npd_imgend-npd_img,DSK,"build/gbkern.dsk"
                save  "CLOCK.APP",clk_img,clk_imgend-clk_img,DSK,"build/gbkern.dsk"
                save  "GBCFG.BIN",cfg_img,cfg_imgend-cfg_img,DSK,"build/gbkern.dsk"
                save  "GBFAT.BIN",fat_img,fat_imgend-fat_img,DSK,"build/gbkern.dsk"
                save  "FLOPPYSV.BIN",flsv_img,flsv_imgend-flsv_img,DSK,"build/gbkern.dsk"
                save  "DEFAULT.FNT",font_img,font_imgend-font_img,DSK,"build/gbkern.dsk"
                save  "CLASSIC.FNT",cfont_img,cfont_imgend-cfont_img,DSK,"build/gbkern.dsk"
                save  "DEFAULT.IST",icon_img,icon_imgend-icon_img,DSK,"build/gbkern.dsk"
                save  "PAINT.IST",pist_img,pist_imgend-pist_img,DSK,"build/gbkern.dsk"
                save  "WELCOME.TXT",wel_img,wel_imgend-wel_img,DSK,"build/gbkern.dsk"
                save  "DEFAULT.SPR",cur_spr_data,cur_spr_end-cur_spr_data,DSK,"build/gbkern.dsk"
                save  "build/DEFAULT.SPR",cur_spr_data,cur_spr_end-cur_spr_data
                save  "HAND.SPR",cur_hand_data,cur_hand_end-cur_hand_data,DSK,"build/gbkern.dsk"
                save  "build/HAND.SPR",cur_hand_data,cur_hand_end-cur_hand_data
                save  "build/GBKERN.RAW",GB_KERNEL,kern_end-GB_KERNEL
