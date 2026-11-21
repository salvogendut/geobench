; kernel/boot.asm - GEOBENCH boot/exit flow.
;
; This file is included immediately after the fixed API table, so kernel_main
; remains the target of GB_INIT and k_exit remains the target of GB_EXIT.

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
                ld    hl,pal_def             ; seed the default inks so the first set_palette
                ld    de,KCFG_INKS           ; has values (the GBCFG module rewrites KCFG_INKS
                ld    bc,5                    ; from INKS= later, then we re-apply the palette)
                ldir
                ld    a,1                     ; default to a solid backdrop until the module
                ld    (BD_SOLID),a           ; sets BD_SOLID from BACKDROP= (#128)
                call  set_palette            ; GEOBENCH 4-pen palette (reads KCFG_INKS)
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
                ld    hl,APP_LOAD_MAX
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
                if SPIKE_STAGED
                ; -DSPIKE_STAGED=1 (+STORAGE_ALBIREO): the real-HW Albireo loads /GEOBENCH/GBCFG.BIN
                ; first and it FAILED (v1 magenta). This v2 isolates WHY the file OPEN fails by
                ; trying three FILE_OPEN forms and reporting which one succeeds (never reboots):
                ;   RED     = mount failed
                ;   WHITE   = absolute open "/GEOBENCH/GBCFG.MOD" works (the trim fix) -> if the
                ;             fix card still rebooted, the failure is the banked READ, not the open
                ;   GREEN   = cd "/GEOBENCH" then open "GBCFG.MOD" works -> stateful-subdir fix
                ;   MAGENTA = neither opened -> deeper
                call  input_init
                di
                call  mem_detect
                ei
                call  bank_normal
                call  alb_ensure_mount        ; mount
                ld    bc,#0606               ; RED
                jp    nc,sts_show
                ; --- form 1: absolute "/GEOBENCH/GBCFG.MOD" (the trimmed path the fix card uses) ---
                ld    hl,sts_abs
                call  sts_setname
                call  sts_fopen
                ld    bc,#1A1A               ; WHITE = absolute open works
                jr    z,sts_show
                ; --- form 2: cd "/GEOBENCH" then open "GBCFG.MOD" relative (stateful subdir) ---
                call  sts_close
                ld    hl,sts_dir             ; SET_FILE_NAME "/GEOBENCH" + FILE_OPEN (enter dir)
                call  sts_setname
                ld    a,ALBC_FILEOPEN
                call  alb_sendcmd
                call  alb_waitint            ; ERR_OPEN_DIR/SUCCESS - either means we're in it
                ld    hl,sts_rel             ; SET_FILE_NAME "GBCFG.MOD" (relative) + FILE_OPEN
                call  sts_setname
                call  sts_fopen
                ld    bc,#1212               ; GREEN = cd-first opens it (stateful-subdir fix)
                jr    z,sts_show
                ld    bc,#0808               ; MAGENTA = none of the three opened
sts_show        call  SCR_SET_BORDER
sts_hang        jr    sts_hang
; sts_setname: HL -> NUL-terminated string -> SET_FILE_NAME it.
sts_setname     ld    a,ALBC_SETNAME
                call  alb_sendcmd
sts_sn_lp       ld    a,(hl)
                out   (c),a
                or    a
                ret   z
                inc   hl
                jr    sts_sn_lp
; sts_fopen: FILE_OPEN the already-set name. ZF set = ALB_INT_SUCCESS (opened).
sts_fopen       ld    a,ALBC_FILEOPEN
                call  alb_sendcmd
                call  alb_waitint
                cp    ALB_INT_SUCCESS
                ret
; sts_close: FILE_CLOSE (reset handle between attempts).
sts_close       ld    a,ALBC_FILECLOSE
                call  alb_sendcmd
                xor   a
                out   (c),a
                call  alb_waitint
                ret
sts_cfg11       db    "GBCFG   MOD"          ; 11-byte 8.3 for alb_setname_req
sts_abs         db    "/GEOBENCH/GBCFG.MOD",0
sts_dir         db    "/GEOBENCH",0
sts_rel         db    "GBCFG.MOD",0
                endif
                if SPIKE_M4
                ; -DSPIKE_M4=1 (+STORAGE_M4): does sending commands the M4ROM way - with the real
                ; SIZE byte first (m4_cmd/m4_send) instead of a #00 lead byte - make a SUBDIR list?
                ; Sends C_CD "/", C_CD "GBENCH", C_DIRSETARGS "*", C_READDIR all M4ROM-framed, then
                ; checks the listing. Hangs (never reboots).
                ;   MAGENTA = /GBENCH still empty even with M4ROM's exact framing (size byte not it)
                ;   WHITE   = /GBENCH lists -> the size byte was it (rewrite the backend to use it)
                call  input_init
                di
                call  mem_detect
                ei
                call  bank_normal
                ld    hl,m4_sysdir           ; cwd -> /GBENCH (m4_cd_path; cd is confirmed working)
                ld    de,m4_path
                ld    bc,8
                ldir
                call  m4_cd_path
                ld    a,#25                   ; C_DIRSETARGS - EMPTY filter, M4ROM-exact [25][43][00]
                call  m4_cmd
                xor   a
                call  m4_pb                   ; the single NUL filter byte (not "*")
                call  m4_send
                call  m4_io_end
                ld    a,#06                   ; C_READDIR - NO args, M4ROM-exact [06][43] (no maxlen)
                call  m4_cmd
                call  m4_send
                ld    a,(M4_RESP)            ; #E800+0 == 2 means empty/end-of-directory
                ld    e,a
                call  m4_io_end
                ld    a,e
                cp    2
                ld    bc,#0808               ; MAGENTA = still empty with M4ROM's exact list commands
                jp    z,m4s_show
                ld    bc,#1A1A               ; WHITE = empty-filter + no-arg C_READDIR was it
m4s_show        call  SCR_SET_BORDER
m4s_hang        jr    m4s_hang
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
                call  set_palette            ; re-apply now KCFG_INKS holds INKS= (#129)
                call  clip_set_full          ; #273 moved the clip rect to bare low RAM
                                             ; (#1338): it boots 0 -> every fill clips to
                                             ; nothing, eating the splash bar. Set it FULL
                                             ; before the first fill.
                call  boot_splash            ; #196: lollipop + empty load bar
                call  boot_tick              ; #196: bar 1/4 (moves before the long assets load)
                call  assets_load            ; font + icons + cursor + backdrop (also GB_RELOAD, #185)
                call  boot_tick              ; #196: bar 2/4
                call  clock_init             ; detect RTC + seed the software clock (#77:
                                              ; the desktop draws the bar, not the kernel)
                call  boot_tick              ; #196: bar 3/4
                include "wm_boot_init.inc"
                ld    hl,kern_end-GB_KERNEL  ; resident kernel size -> GB_KSIZE (Ram Usage)
                ld    (GB_KSIZE),hl
                call  boot_tick              ; #196: bar 4/4 (full) just before the desktop loads
                call  boot_desktop           ; run DESKTOP in a borrowed page (the WM master loop
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
                if PREEMPTIVE_CONTEXT
                call  SCHED_IRQ_UNINSTALL_ENTRY ; restore the standard CPC IM-1 vector
                endif
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
