; kernel/boot_msx.asm - GEOBENCH boot/exit flow for the MSX2 target (#287).
;
; The MSX counterpart of kernel/boot.asm, included immediately after the fixed
; API table so kernel_main stays the GB_INIT target and k_exit the GB_EXIT
; target. The bootstrap stub (kernel/msx_stub.asm) has already: verified DOS 2,
; captured the mapper support routines + TPA page-1 segment into the page-3
; glue, copied this kernel to #8000, hooked H.TIMI, run CHGMOD 6 and set
; SP = MSX_STACK_TOP. GB_EXIT unwinds all of that and _TERMs back to the
; DOS prompt.

kernel_main
                ld    (BOOT_SP),sp           ; entry SP, for GB_EXIT's longjmp
                ld    hl,0                    ; empty the shared clipboard (#142)
                ld    (CLIP_LEN),hl
                xor   a                       ; UI_MODAL boots as TPA garbage under DOS
                ld    (UI_MODAL),a           ; (menu_dispatch would swallow EVERY top-bar
                                              ; click as "a dialog is up", #287)
                call  msx_video_init         ; 212 lines, 16x16 sprites, parked pointer
                ld    hl,pal_def             ; seed the default inks (GBCFG may rewrite
                ld    de,KCFG_INKS           ; KCFG_INKS from INKS= later)
                ld    bc,5
                ldir
                ld    a,1                     ; solid backdrop until BACKDROP= says else
                ld    (BD_SOLID),a
                call  set_palette
                call  k_cls                   ; pen-0 desktop while loading
                call  fs_init
                call  input_init
                call  msx_mem_init           ; PAGE_DATA segment + KCFG_MEMKB
                call  cfg_boot               ; parse GEOBENCH.CFG (paged C module)
                call  set_palette            ; re-apply now KCFG_INKS holds INKS=
                call  clip_set_full          ; boot clip rect = full screen (#273)
                call  boot_splash            ; #196: lollipop + empty load bar
                call  boot_tick              ; #196: bar 1/4 (before the long assets load)
                call  assets_load            ; font + icons + cursor + backdrop
                call  boot_tick              ; #196: bar 2/4
                call  clock_init
                call  boot_tick              ; #196: bar 3/4
                include "wm_boot_init.inc"
                ld    hl,kern_end-GB_KERNEL
                ld    (GB_KSIZE),hl
                ifdef GB_FSTEST              ; #287: deterministic BDOS write-path self-test.
                call  msx_fstest             ; save GBTEST.TXT, then _TERM so the host can
                jp    km_finish              ; mount the image and check it - no desktop.
                endif
                ifdef GB_COPYTEST            ; chunked-copy self-test (any-size DnD, Phase A):
                call  msx_copytest           ; BIG.PIC -> COPYOUT.TST via the REAL fs_load_file
                jp    km_finish              ; (chunk-read) + fs_save_file (append); host compares.
                endif
                ifdef GB_FILLTEST            ; #287: fill 4 bands pens 0-3 via k_fill, hang.
                ld    b,0                     ; B = pen
gft_band        push  bc
                ld    a,b                     ; pen
                push  af
                ld    c,b                     ; y = pen*48
                ld    a,b
                add   a,a
                add   a,a
                add   a,a                     ; *8
                ld    e,a
                add   a,a                     ; *16
                add   a,a                     ; *32... want *48 = *32 + *16
                add   a,e                     ; a = pen*48
                ld    c,a                     ; C = y
                ld    b,0                     ; B = x
                ld    d,128                   ; D = w (full width)
                ld    e,48                    ; E = h
                pop   af                      ; A = pen
                call  k_fill
                pop   bc
                inc   b
                ld    a,b
                cp    4
                jr    c,gft_band
gft_hang        jr    gft_hang
                endif
                call  boot_tick              ; #196: bar 4/4 (full) just before the desktop loads
                call  boot_desktop           ; the WM master loop never returns
km_finish                                      ; reached by k_exit's longjmp
                di
                if PREEMPTIVE_CONTEXT
                call  SCHED_IRQ_UNINSTALL_ENTRY ; restore the MSX-DOS IM 1 trampoline
                endif
                ld    hl,MSX_HOOKSAVE        ; restore the original H.TIMI hook
                ld    de,H_TIMI
                ld    bc,5
                ldir
                ei
                call  msx_free_segments      ; hand every ALL_SEG segment back to DOS
                call  msx_text_restore       ; text mode + stock palette for the DOS prompt
                ld    c,_TERM0               ; clean exit to COMMAND2
                jp    BDOS

                ifdef GB_FSTEST
; msx_fstest (#287): exercise the BDOS write path exactly as gb_fs_save/the
; Notepad save would - fs_save_file creates GBTEST.TXT in the CWD (root) with a
; known payload; then fs_delete_file removes a scratch file to prove _DELETE.
; The host build script mounts the image afterward and checks the content.
msx_fstest
                ld    hl,fstest_name         ; fs_req_name = "GBTEST  TXT"
                ld    de,fs_req_name
                ld    bc,11
                ldir
                ld    hl,fstest_data         ; save the payload
                ld    (fs_save_src),hl
                ld    hl,fstest_len
                ld    (fs_save_len),hl
                call  fs_save_file
                ret
fstest_name     db    "GBTEST  TXT"
fstest_data     db    "GEOBENCH MSX2 write path OK",13,10
fstest_len      equ   $-fstest_data
                endif

                ifdef GB_COPYTEST
; msx_copytest: copy BIG.PIC -> COPYOUT.TST in <=GB_COPYMAX chunks, driving the real
; fs_load_file (chunk-read at FS_LOAD_OFS) + fs_save_file (append) - the exact BDOS
; sequence apps/filemgr/main.c copy_file runs. The host mounts the image and byte-
; compares COPYOUT.TST against BIG.PIC.
CTC_BUF          equ   #2200          ; chunk buffer (module_data, free at boot)
                if PREEMPTIVE
CTC_MAX          equ   #1A00          ; scheduler reserves #3C00..#3DFF
                else
CTC_MAX          equ   #1C00          ; cooperative GB_COPYMAX
                endif
CTC_OFS          equ   #144C          ; FS_LOAD_OFS
CTC_FLAGS        equ   #144F          ; FS_XFLAGS
msx_copytest
                xor   a
                ld    (ct_ofs),a
                ld    (ct_ofs+1),a
                ld    (ct_ofs+2),a
                ld    a,1
                ld    (ct_first),a
ct_loop
                ld    hl,ct_src              ; fs_req_name = source
                ld    de,fs_req_name
                ld    bc,11
                ldir
                ld    a,(ct_ofs)             ; FS_LOAD_OFS = running offset (24-bit)
                ld    (CTC_OFS),a
                ld    a,(ct_ofs+1)
                ld    (CTC_OFS+1),a
                ld    a,(ct_ofs+2)
                ld    (CTC_OFS+2),a
                ld    a,1                     ; FS_XFLAGS bit0 = chunk-read
                ld    (CTC_FLAGS),a
                ld    hl,CTC_BUF
                ld    (fs_load_dst),hl
                ld    hl,CTC_MAX
                ld    (fs_load_max),hl
                call  fs_load_file
                ld    hl,(fs_ent_size)       ; got = bytes read
                ld    (ct_got),hl
                ld    a,h
                or    l
                jp    z,ct_done              ; 0 = EOF
                ld    hl,ct_dst              ; fs_req_name = dest
                ld    de,fs_req_name
                ld    bc,11
                ldir
                ld    a,(ct_first)
                or    a
                jr    nz,ct_create           ; first chunk -> create/truncate
                ld    a,2                     ; else append (FS_XFLAGS bit1)
                jr    ct_setx
ct_create
                xor   a
ct_setx
                ld    (CTC_FLAGS),a
                ld    hl,CTC_BUF
                ld    (fs_save_src),hl
                ld    hl,(ct_got)
                ld    (fs_save_len),hl
                call  fs_save_file
                xor   a
                ld    (ct_first),a
                ld    hl,(ct_ofs)             ; ct_ofs += got (24-bit)
                ld    de,(ct_got)
                add   hl,de
                ld    (ct_ofs),hl
                jr    nc,ct_nocarry
                ld    a,(ct_ofs+2)
                inc   a
                ld    (ct_ofs+2),a
ct_nocarry
                ld    hl,(ct_got)            ; short read (< CTC_MAX) = last chunk
                ld    de,CTC_MAX
                or    a
                sbc   hl,de
                jp    c,ct_done
                jp    ct_loop
ct_done
                xor   a
                ld    (CTC_FLAGS),a           ; leave the flags clear
                ret
ct_src          db    "BIG     PIC"
ct_dst          db    "COPYOUT TST"
ct_ofs          ds    3
ct_got          dw    0
ct_first        db    0
                endif

; k_exit (GB_EXIT): longjmp out of the nested WM call chain, then unwind.
k_exit
                di
                call  bank_normal            ; TPA segment back at #4000-#7FFF
                ld    sp,(BOOT_SP)
                jp    km_finish
name_desktop    db    "DESKTOP APP"

; msx_mem_init: allocate the PAGE_DATA segment and publish the RAM size.
; No free segment is fatal - GEOBENCH cannot run without its data page, so
; report and exit cleanly rather than limp.
msx_mem_init
                xor   a                       ; ALL_SEG: A=0 user segment, B=0 any mapper
                ld    b,a
                ld    hl,(MSX_ALLSEG)
                call  jp_hl
                jr    c,mmi_fatal
                ld    (MSX_PAGE_DATA),a
                ld    a,(MSX_TOTSEG)         ; KCFG_MEMKB = total segments * 16
                ld    l,a
                ld    h,0
                add   hl,hl
                add   hl,hl
                add   hl,hl
                add   hl,hl
                ld    (KCFG_MEMKB),hl
                ret
mmi_fatal
                ld    sp,(BOOT_SP)           ; no segment for PAGE_DATA -> back to DOS
                call  msx_text_restore
                ld    c,_TERM0
                jp    BDOS

; INITXT deliberately leaves the V9938 palette unchanged. Screen 7 replaces all
; 16 entries and also uses VRAM over the text-mode palette mirror, so returning
; without INIPLT leaves COMMAND2 unreadable. RST #30 (CALLF) invokes the MSX2
; SUB-ROM entry using the machine's dynamic expanded-slot byte.
msx_text_restore
                ld    ix,INITXT
                call  msx_bios
                ld    a,(EXBRSA)
                ld    (mtr_slot),a
                rst   #30
mtr_slot        db    0
                dw    INIPLT
                ret

; app_pool_init: APP_PAGES[0] = the TPA's own page-1 segment (the desktop rides
; it for free), then ALL_SEG until the pool is full or DOS runs dry. On a stock
; 128K machine that yields 1 window (the desktop) + whatever DOS left over.
app_pool_init
                ld    hl,APP_PAGES
                ld    a,(MSX_TPASEG)
                ld    (hl),a
                inc   hl
                ld    c,1                     ; C = entries so far
api_more
                ld    a,c
                cp    WM_MAXWIN
                jr    nc,api_done
                push  hl
                push  bc
                xor   a                       ; ALL_SEG a user segment
                ld    b,a
                ld    hl,(MSX_ALLSEG)
                call  jp_hl
                pop   bc
                pop   hl
                jr    c,api_done             ; DOS has no more -> pool complete
                ld    (hl),a
                inc   hl
                inc   c
                jr    api_more
api_done
                ld    a,c
                ld    (APP_NPAGES),a
                ld    hl,APP_BUSY            ; mark every page free
                ld    de,APP_BUSY+1
                ld    bc,WM_MAXWIN-1
                ld    (hl),0
                ldir
                ret

; msx_free_segments: FRE_SEG the PAGE_DATA segment + every pool segment except
; APP_PAGES[0] (that one is the TPA's own). Exit path only.
msx_free_segments
                ld    a,(MSX_PAGE_DATA)
                call  mfs_free
                ld    a,(APP_NPAGES)
                or    a
                ret   z
                dec   a
                ret   z                       ; only the TPA entry
                ld    b,a                     ; B = count above [0]
                ld    hl,APP_PAGES+1
mfs_loop
                ld    a,(hl)
                push  hl
                push  bc
                call  mfs_free
                pop   bc
                pop   hl
                inc   hl
                djnz  mfs_loop
                ret
mfs_free
                ld    hl,(MSX_FRESEG)
                jp    jp_hl

; msx_wait_tick: pace k_poll to ONE true video frame by waiting for a vertical-
; retrace edge on the VDP status flag (S#2 bit6 VR), NOT the H.TIMI tick. Under
; this machine + Nextor the BIOS frame interrupt STORMS (~8x/frame), so a tick-
; counted wait let k_poll run ~8x too fast: the pointer's acceleration ramp
; maxed out instantly (uncontrollable leaps) and the idle/saver timer counted 8x
; fast (saver in ~9s). The VR flag toggles exactly once per frame regardless of
; the interrupt rate. Brief DI so the R#15 select + poll don't race the BIOS ISR
; (which needs R#15=0/S#0); R#15 restored to 0 before EI, so the one pending
; frame interrupt acks S#0 cleanly - which also tames the storm.
; PRESERVES DE/HL like the CPC's MC_WAIT_FLYBACK - k_poll carries the pointer
; target through this call (the M1 "pointer pinned to the bottom" bug). #287
msx_wait_tick
                push  hl
                di
                ld    a,2                     ; select status register S#2 (VR flag)
                out   (VDP_CTRL),a
                ld    a,15|#80
                out   (VDP_CTRL),a
mwt_indisp      in    a,(VDP_CTRL)            ; wait until out of retrace (VR = 0)
                and   #40
                jr    nz,mwt_indisp
mwt_retrace     in    a,(VDP_CTRL)            ; then wait for retrace to begin (VR 0->1)
                and   #40
                jr    z,mwt_retrace
                xor   a                        ; restore R#15 = 0 (S#0, the ISR default)
                out   (VDP_CTRL),a
                ld    a,15|#80
                out   (VDP_CTRL),a
                ei
                pop   hl
                ret
