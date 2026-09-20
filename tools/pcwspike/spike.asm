; -----------------------------------------------------------------------
; spike.asm - PCW driver testbench payload (#331 Phase 2)
;
; Loaded at #2000 by kernel/pcwboot.asm from GEOBENCH.DSK's reserved
; tracks. Exercises the real GEOBENCH PCW video stack - lib/pcw/
; screen.asm + text.asm + cursor.asm - end to end, headless. Expected
; screenshot:
;
;   cyan desktop (k_cls, GB pen 0), black letterbox strip at the bottom
;   three rects: white / black / magenta          (fill_block pens 1,2,3)
;   a cyan/magenta 16px checker field             (fill_pattern, BD_TILE)
;   a test glyph blitted TRANSPARENT over the checker (pen-0 shows it)
;   the same glyph OPAQUE on the desktop          (pen-0 = cyan box)
;   a black bar crossing y=120..135               (block 4/5 boundary)
;   a small magenta patch ONLY inside a clip window (clip honored)
;   NO magenta at the save/restore spot           (round trip works)
;   two text lines: black-on-white + white-on-desktop (6x8 font)
;   the test pointer over the checker (transparent corners show it),
;   and NO trace at its first position over the white rect (erase works)
;   "hello pcw 42" typed via --paste-event -> k_getkey -> draw_text
;
; Debug beacons (survive the payload reload on a crash-reboot):
;   #0F00 = last stage completed   #0F01 = boot-cycle count
;
; NOTE: the stack must NOT live in slot 3 (#C000+) - the driver remaps
; that window per drawing row. SP sits below #8000 instead.
; -----------------------------------------------------------------------

BACKDROP_TILE   equ   1
TITLEBAR_TILE   equ   0
CUR_LOW         equ   testspr
fs_secbuf       equ   #1800        ; the kernel's sector buffer home (lowram)

                org #2000    ; clear of the low-RAM contracts the drivers use
                             ; (cur_bg #1291, clip #1338, scratch #14xx)

                include "../../lib/pcw/glue.inc"

entry:
        di
        ld sp,#8000             ; NOT #F000 - slot 3 is the screen window
        ld a,(#0F01)            ; debug: count boot cycles (survives reload)
        inc a
        ld (#0F01),a
        ld a,0
        ld (#0F00),a            ; debug: stage beacon
        ld (#0F02),a            ; drive-B stepping mode (host-asserted)
        ld (#0F03),a            ; drive-B stage verdict (host-asserted)

        call pcw_video_init     ; roller table + cls + display on
        ld a,1
        ld (#0F00),a

        ; --- three solid rects: pens 1 / 2 / 3 -------------------------
        ld a,1
        ld bc,#0410             ; x=4  y=16
        ld de,#1420             ; w=20 h=32
        call fill_t
        ld a,2
        ld bc,#1E10             ; x=30
        ld de,#1420
        call fill_t
        ld a,3
        ld bc,#3810             ; x=56
        ld de,#1420
        call fill_t

        ; --- checker field via fill_pattern ----------------------------
        ld a,4                  ; x=4
        ld (fb_x),a
        ld a,64
        ld (fb_y),a
        ld a,60
        ld (fb_w),a
        ld a,48
        ld (fb_h),a
        ld a,2
        ld (#0F00),a
        call fill_pattern
        ld a,3
        ld (#0F00),a

        ; --- glyph: transparent over the checker, opaque on desktop ----
        ld a,#FF
        ld (bm_keep),a
        ld hl,glyph
        ld (bm_src),hl
        ld a,10
        ld (bm_x),a
        ld a,72
        ld (bm_y),a
        ld a,4
        ld (bm_w),a
        ld a,16
        ld (bm_h),a
        call blit_bitmap

        xor a
        ld (bm_keep),a
        ld hl,glyph
        ld (bm_src),hl
        ld a,40
        ld (bm_x),a
        ld a,72
        ld (bm_y),a
        ld a,4
        ld (bm_w),a
        ld a,16
        ld (bm_h),a
        call blit_bitmap
        ld a,4
        ld (#0F00),a

        ; --- bar crossing the block 4/5 boundary (y 120..135) ----------
        ld a,2
        ld bc,#0478             ; x=4 y=120
        ld de,#5010             ; w=80 h=16
        call fill_t

        ; --- save / scribble / restore: must leave no trace ------------
        ld a,56
        ld (sb_x),a
        ld a,160
        ld (sb_y),a
        ld a,8
        ld (sb_w),a
        ld a,16
        ld (sb_h),a
        ld hl,#1600             ; scratch buffer outside the payload
        ld (sb_buf),hl
        ld a,5
        ld (#0F00),a
        call save_block
        ld a,6
        ld (#0F00),a
        ld a,3                  ; scribble magenta over the saved area
        ld bc,#38A0             ; x=56 y=160
        ld de,#0810
        call fill_t
        ld a,7
        ld (#0F00),a
        call restore_block      ; and undo it
        ld a,8
        ld (#0F00),a

        ; --- clip window: fill way past it, only the window paints -----
        ld a,70
        ld (clip_x),a
        ld a,160
        ld (clip_y),a
        ld a,10
        ld (clip_w),a
        ld a,16
        ld (clip_h),a
        ld a,3
        ld bc,#3C96             ; x=60 y=150
        ld de,#1E28             ; w=30 h=40 - mostly outside the clip
        call fill_t
        xor a                   ; reset clip to full screen
        ld (clip_x),a
        ld (clip_y),a
        ld a,PCW_COLS
        ld (clip_w),a
        ld a,PCW_LINES
        ld (clip_h),a
        ld a,9
        ld (#0F00),a

        ; --- text: black-on-white box, then white on the desktop -------
        ld hl,fontblob
        call font_apply_header
        ld a,1                  ; white box behind the first line
        ld bc,#06B2             ; x=6 y=178
        ld de,#2E0C             ; w=46 h=12
        call fill_t
        ld b,2                  ; black text on white paper
        ld c,1
        call set_text_pens
        ld a,7
        ld (tc_x),a
        ld a,180
        ld (tc_y),a
        ld hl,msg1
        call draw_text
        ld b,1                  ; white text on the cyan desktop
        ld c,0
        call set_text_pens
        ld a,8
        ld (tc_x),a
        ld a,196
        ld (tc_y),a
        ld hl,msg2
        call draw_text
        ld a,10
        ld (#0F00),a

        ; --- pointer: show over the white rect, then move over the -----
        ; checker; the first spot must be restored perfectly
        ld hl,testspr           ; alias phase2 = phase0 (static test)
        ld de,testspr+128
        ld bc,128
        ldir
        ld de,60                ; first: over the white rect (px 30, line 25)
        ld (cursor_x),de
        ld hl,444               ; y = (247-25)*2
        ld (cursor_y),hl
        call cursor_show
        ld de,80                ; then: over the checker (px 40, line 80)
        ld hl,334               ; y = (247-80)*2
        call cursor_move_to
        ld a,11
        ld (#0F00),a

        ; --- k_line: three GB_LINE-style lines via the PCW_GLINE cells --
        ld hl,300               ; \ diagonal, pen 2 (black)
        ld (PCW_GLINE+0),hl
        ld hl,16
        ld (PCW_GLINE+2),hl
        ld hl,356
        ld (PCW_GLINE+4),hl
        ld hl,60
        ld (PCW_GLINE+6),hl
        ld a,2
        ld (PCW_GLINE+8),a
        call k_line
        ld hl,356               ; / diagonal, pen 3 (magenta)
        ld (PCW_GLINE+0),hl
        ld hl,16
        ld (PCW_GLINE+2),hl
        ld hl,300
        ld (PCW_GLINE+4),hl
        ld hl,60
        ld (PCW_GLINE+6),hl
        ld a,3
        ld (PCW_GLINE+8),a
        call k_line
        ld hl,300               ; horizontal, pen 1 (white)
        ld (PCW_GLINE+0),hl
        ld hl,38
        ld (PCW_GLINE+2),hl
        ld hl,356
        ld (PCW_GLINE+4),hl
        ld hl,38
        ld (PCW_GLINE+6),hl
        ld a,1
        ld (PCW_GLINE+8),a
        call k_line

        ; --- CP/M fs test: mount, list the directory, load HELLO.TXT ---
        call fs_init
        ld b,1                  ; white on cyan for the listing
        ld c,0
        call set_text_pens
        ld a,20
        ld (fst_y),a
        call fs_dir_first
fst_dir:
        jr nc,fst_load
        ld hl,fs_ent_name       ; 11 chars, not terminated - copy + NUL
        ld de,fst_name
        ld bc,11
        ldir
        xor a
        ld (de),a
        ld a,66
        ld (tc_x),a
        ld a,(fst_y)
        ld (tc_y),a
        ld hl,fst_name
        call draw_text
        ld a,(fst_y)
        add a,9
        ld (fst_y),a
        call fs_dir_next
        jr fst_dir
fst_load:
        ld hl,fst_hello         ; load HELLO.TXT and print its content
        ld de,fs_req_name
        ld bc,11
        ldir
        ld hl,#6000             ; phys block 1 upper half (table is at #4000)
        ld (fs_load_dst),hl
        ld hl,512
        ld (fs_load_max),hl
        call fs_load_file
        jr nc,fst_done
        ld hl,#6000             ; cut at CR / CP/M EOF pad
        ld b,64
fst_term:
        ld a,(hl)
        cp #1A
        jr z,fst_cut
        cp 13
        jr z,fst_cut
        inc hl
        djnz fst_term
fst_cut:
        ld (hl),0
        ld b,3                  ; magenta-accent text
        ld c,0
        call set_text_pens
        ld a,40
        ld (tc_x),a
        ld a,232
        ld (tc_y),a
        ld hl,#6000
        call draw_text
fst_done:
        ld a,12
        ld (#0F00),a

        ; --- fs WRITE tests (#331 Phase 5b): create, delete, and a
        ; chunked multi-extent copy (2KB chunks through FS_LOAD_OFS +
        ; append) - the host extracts the results from the .dsk after.
        call wt_run
        jr nc,wt_fail
        ld b,3
        ld c,0
        call set_text_pens
        ld a,64
        ld (tc_x),a
        ld a,232
        ld (tc_y),a
        ld hl,wt_okmsg
        call draw_text
        jr wt_end
wt_fail:
        ld b,2
        ld c,3
        call set_text_pens
        ld a,64
        ld (tc_x),a
        ld a,232
        ld (tc_y),a
        ld hl,wt_badmsg
        call draw_text
wt_end:
        ld a,13
        ld (#0F00),a

        ; --- drive B: browse B, then a boot-first system load (#331 -----
        ; "can't open Disk B": fs_load_sys must hit the BOOT drive even
        ; while the browse drive is B, then restore the browse drive)
        ld a,2
        call fs_set_drive       ; mount B (COMPANION-style data disc)
        ld a,(fdc_dbl)          ; expose the measured stepping mode and the
        ld (#0F02),a            ; stage verdict to the host harness (SNA)
        call fs_dir_first
        jp nc,db_fail           ; B must list something...
        ld hl,fs_ent_name       ; ...and it must be the REAL file, not the
        ld de,db_bname          ; disc spec misread off the wrong track
        ld b,11                 ; (#331: drive B needs double-stepping)
db_ncmp:
        ld a,(de)
        cp (hl)
        jp nz,db_fail
        inc hl
        inc de
        djnz db_ncmp
        ld hl,fst_hello         ; HELLO.TXT exists ONLY on the boot disc
        ld de,fs_req_name
        ld bc,11
        ldir
        ld hl,#6000
        ld (fs_load_dst),hl
        ld hl,512
        ld (fs_load_max),hl
        call fs_load_sys        ; boot-first: must load from A
        jp nc,db_fail
        ld a,(fs_cur_drive)     ; and the browse drive must survive
        cp 2
        jp nz,db_fail
        call wt_run             ; exercise writes on the mounted B geometry
        jp nc,db_fail
        ld a,#B0                ; B-stage verdict: pass
        ld (#0F03),a
        ld b,3
        ld c,0
        call set_text_pens
        ld a,4
        ld (tc_x),a
        ld a,232
        ld (tc_y),a
        ld hl,db_okmsg
        call draw_text
        jr db_end
db_fail:
        ld a,#BE                ; B-stage verdict: fail
        ld (#0F03),a
        ld b,2
        ld c,3
        call set_text_pens
        ld a,4
        ld (tc_x),a
        ld a,232
        ld (tc_y),a
        ld hl,db_badmsg
        call draw_text
db_end:
        ld a,1                  ; back to A for the typing test
        call fs_set_drive
        ld a,14
        ld (#0F00),a

        ; --- typing test: k_getkey -> append + redraw. input_poll runs
        ; every pass, proving the input layer's slot-3 keyboard remap
        ; coexists with drawing. Keys arrive via 1985 --paste-event.
        call input_init
        ld b,2                  ; black text on the cyan desktop
        ld c,0
        call set_text_pens
tloop:
        call input_poll
        call k_getkey
        or a
        jr z,tloop
        cp 13                   ; ignore enter/del in this test
        jr z,tloop
        cp 127
        jr z,tloop
        ld c,a
        ld hl,typebuf           ; append (cap 30 chars)
        ld b,30
tb_find:
        ld a,(hl)
        or a
        jr z,tb_put
        inc hl
        djnz tb_find
        jr tloop
tb_put:
        ld (hl),c
        inc hl
        ld (hl),0
        ld a,4
        ld (tc_x),a
        ld a,214
        ld (tc_y),a
        ld hl,typebuf
        call draw_text
        jr tloop

; wt_run: exercise create, delete, chunk-read, append, and a second extent on
; the currently mounted disk. CF set on success. The host verifies the files.
wt_run:
        ld hl,n_save            ; 1. create PCWSAVE.TST
        ld de,fs_req_name
        ld bc,11
        ldir
        ld hl,msg_wr
        ld (fs_save_src),hl
        ld hl,msg_wr_len
        ld (fs_save_len),hl
        xor a
        ld (FS_XFLAGS),a
        call fs_save_file
        ret nc
        ld hl,n_del             ; 2. create then delete PCWDEL.TST
        ld de,fs_req_name
        ld bc,11
        ldir
        ld hl,msg_wr
        ld (fs_save_src),hl
        ld hl,msg_wr_len
        ld (fs_save_len),hl
        call fs_save_file
        ret nc
        ld hl,n_del
        ld de,fs_req_name
        ld bc,11
        ldir
        call fs_delete_file
        ret nc
        ld hl,0                 ; 3. chunked copy BIGTEST.BIN -> COPYOUT.BIN
        ld (ctofs),hl
        ld a,1
        ld (ctfirst),a
ct_loop:
        ld hl,n_big
        ld de,fs_req_name
        ld bc,11
        ldir
        ld hl,(ctofs)
        ld (FS_LOAD_OFS),hl
        xor a
        ld (FS_LOAD_OFS+2),a
        ld a,1                  ; chunk-read
        ld (FS_XFLAGS),a
        ld hl,#6000
        ld (fs_load_dst),hl
        ld hl,2048
        ld (fs_load_max),hl
        call fs_load_file
        ret nc
        ld hl,(fs_ent_size)
        ld (ctgot),hl
        ld a,h
        or l
        jr z,ct_done            ; EOF
        ld hl,n_out
        ld de,fs_req_name
        ld bc,11
        ldir
        ld a,(ctfirst)
        or a
        jr nz,ct_create
        ld a,2                  ; append
        jr ct_fl
ct_create:
        xor a                   ; create/truncate
ct_fl:
        ld (FS_XFLAGS),a
        ld hl,#6000
        ld (fs_save_src),hl
        ld hl,(ctgot)
        ld (fs_save_len),hl
        call fs_save_file
        ret nc
        xor a
        ld (ctfirst),a
        ld hl,(ctofs)
        ld de,(ctgot)
        add hl,de
        ld (ctofs),hl
        ld hl,(ctgot)           ; short chunk = final
        ld de,2048
        or a
        sbc hl,de
        jr c,ct_done
        jp ct_loop
ct_done:
        scf
        ret

; fill_t: A = pen, B = x, C = y, D = w, E = h -> fill_block
fill_t:
        push de
        push bc
        call pen_to_byte        ; clobbers A,E only
        ld (fb_val),a
        pop bc
        ld a,b
        ld (fb_x),a
        ld a,c
        ld (fb_y),a
        pop de
        ld a,d
        ld (fb_w),a
        ld a,e
        ld (fb_h),a
        jp fill_block

; --- the GEOBENCH PCW video + input stack under test --------------------
                include "../../lib/pcw/screen.asm"
                include "../../lib/pcw/text.asm"
                include "../../lib/cursor_arrow.asm"
                include "../../lib/pcw/cursor.asm"
                include "../../lib/pcw/input.asm"
                include "../../lib/pcw/fdc.asm"
                include "../../lib/pcw/fs.asm"

typebuf:        ds 32
fst_name:       ds 12
fst_y:          db 0
fst_hello:      db "HELLO   TXT"
n_save:         db "PCWSAVE TST"
n_del:          db "PCWDEL  TST"
n_big:          db "BIGTEST BIN"
n_out:          db "COPYOUT BIN"
msg_wr:         db "PCW write path OK",13,10
msg_wr_len      equ $-msg_wr
wt_okmsg:       db "WRITE OK",0
wt_badmsg:      db "WRITE FAIL",0
db_bname:       db "BFILE   TXT"
db_okmsg:       db "DISK B OK",0
db_badmsg:      db "DISK B FAIL",0
ctofs:          dw 0
ctgot:          dw 0
ctfirst:        db 0

; --- test data ----------------------------------------------------------
; 16x16px checker tile, GB pens 0/3 (renders cyan/magenta)
BD_TILE:
        db #00,#00,#FF,#FF
        db #00,#00,#FF,#FF
        db #00,#00,#FF,#FF
        db #00,#00,#FF,#FF
        db #00,#00,#FF,#FF
        db #00,#00,#FF,#FF
        db #00,#00,#FF,#FF
        db #00,#00,#FF,#FF
        db #FF,#FF,#00,#00
        db #FF,#FF,#00,#00
        db #FF,#FF,#00,#00
        db #FF,#FF,#00,#00
        db #FF,#FF,#00,#00
        db #FF,#FF,#00,#00
        db #FF,#FF,#00,#00
        db #FF,#FF,#00,#00

; 16x16px test glyph, GB pen space: pen-0 corners (transparent when
; bm_keep=#FF), pen-2 black ring, pen-1 white fill, pen-3 red core.
glyph:
        db #00,#AA,#AA,#00      ; row 0:  ..RRRR..  (R = ring pen2)
        db #0A,#AA,#AA,#A0
        db #AA,#55,#55,#AA      ; ring around white fill
        db #AA,#55,#55,#AA
        db #A5,#55,#55,#5A
        db #A5,#5F,#F5,#5A      ; pen-3 core starts
        db #A5,#5F,#F5,#5A
        db #A5,#55,#55,#5A
        db #A5,#55,#55,#5A
        db #A5,#5F,#F5,#5A
        db #A5,#5F,#F5,#5A
        db #A5,#55,#55,#5A
        db #AA,#55,#55,#AA
        db #AA,#55,#55,#AA
        db #0A,#AA,#AA,#A0
        db #00,#AA,#AA,#00

; test pointer sprite, phase 0: interleaved (mask,data) pairs in HARDWARE
; space - black-bordered white 16x16 box with transparent corner bytes.
; (mask #FF,data #00) = keep background; (#00,#00) = black; (#00,#FF) = white.
testspr:
        db #FF,#00, #00,#00, #00,#00, #FF,#00   ; row 0: corners transparent
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #00,#00, #00,#FF, #00,#FF, #00,#00
        db #FF,#00, #00,#00, #00,#00, #FF,#00   ; row 15
        ds 128                                   ; phase-2 copy (filled at run time)

msg1:   db "GEOBENCH PCW",0
msg2:   db "the quick brown fox 0123",0

fontblob:
        incbin "../../build/DEFAULT.FNT"

spike_end:
        save"build/pcwspike.bin",#2000,spike_end-entry
