; Shared guest ABI, in the MOS program's Z80 segment.
                include "../../lib/gbapp.inc"
PREEMPTIVE      equ 0
                include "../lowram.inc"
                include "input.inc"
                org GB_KERNEL
                jp AG_SYSTEM+15      ; enter the Agon workspace
                defs GB_TEXT-$,#c9
                jp draw_text
                defs GB_DIR1-$,#c9
                jp dir_first
                jp dir_next
                defs GB_POLL-$,#c9
                jp AG_POLL_HELD
                defs GB_FRAME-$,#c9   ; cursor show/hide unnecessary for VDP overlay
                jp draw_frame
                defs GB_GETARG-$,#c9
                jp AG_SYSTEM+36
                defs GB_ICON-$,#c9
                jp icon_full
                jp draw_fill
                defs GB_FSLOAD-$,#c9
                jp AG_SYSTEM+42
                jp AG_SYSTEM+45
                defs GB_GETKEY-$,#c9
                jp AG_SYSTEM+27
                defs GB_SETNAME-$,#c9
                jp AG_SYSTEM+39
                defs GB_RESTPAR-$,#c9
                jp AG_SYSTEM+21
                defs GB_WMOPEN-$,#c9
                jp AG_SYSTEM+30
                jp AG_SYSTEM+24
                defs GB_ISDIR-$,#c9
                jp AG_FILES+15
                jp AG_FILES+18
                jp AG_FILES+21
                jp entry_name
                jp no_result         ; no drag/drop
                defs GB_GETDRIVE-$,#c9
                jp no_result         ; single SD volume
                defs GB_WMLAUNCHAS-$,#c9
                jp AG_SYSTEM+33
                defs GB_FSFREE-$,#c9
                jp no_result         ; free space unknown
                jp icon_half
                defs GB_CLIPSET-$,#c9
                jp clip_set
                jp clip_get
                jp clip_size
                defs GB_UI-$,#c9
                jp AG_SYSTEM+48
                defs GB_WMMANAGED-$,#c9
                jp AG_SYSTEM+18
                defs GB_NET+3-$,#c9   ; reserve the complete public table

; Private calls from the resident workspace, after the public ABI.
                assert $ == #80c0
                jp paint_guest
                jp guest_footer
                jp unsupported
paint_guest     ld a,(AG_FILEMODE)
                or a
                jr z,chrome_done
                call AG_WM+15
chrome_done     jp window_title

; Little-endian rectangle in memory: x,y,w,h -> B,C,D,E.
unpack_rect     ld a,b
                ld b,c
                ld c,a
                ld a,d
                ld d,e
                ld e,a
                ret
window_title    ld bc,(MW_RECT)
                ld de,(MW_RECT+2)
                call unpack_rect
                push bc
                ld e,14
                ld a,2
                call draw_fill
                pop bc
                push bc
                ; Draw the motif only in the title lane. Opaque title glyphs
                ; cover it from the left; the right-hand controls stay clear.
                call title_right
                sub b
                sub 2
                ld d,a
                ld e,1
                inc b
                ld a,c
                add a,4
                ld c,a
                ld a,3
title_stripe    push af
                push bc
                push de
                ld a,1
                call draw_fill
                pop de
                pop bc
                inc c
                inc c
                pop af
                dec a
                jr nz,title_stripe
                pop bc
                push bc
                ld a,(MW_RECT+2)
                add a,b
                sub 3
                ld b,a
                inc c
                inc c
                inc c
                ld de,#0102
                ld hl,close_label
                call draw_text
                ld a,(AG_FILEMODE)
                cp 1
                jr nz,title_name
                pop bc
                push bc
                ld a,(MW_RECT+2)
                add a,b
                sub 11
                ld b,a
                inc c
                inc c
                inc c
                ld hl,view_label
                ld de,#0102
                call draw_text
title_name
                pop bc
                inc b
                inc c
                inc c
                inc c
                ld de,#0102           ; white font on black paper
                ld hl,(wnd_title)
                ld a,(hl)
                inc hl
                ld h,(hl)
                ld l,a
                push hl
                call title_right
                pop hl
                jp draw_text_limit

; First column reserved for the controls (leave a column of gutter).
title_right     ld a,(AG_FILEMODE)
                cp 1
                ld a,(MW_RECT)
                ld hl,MW_RECT+2
                jr nz,calc_title_right
                add a,(hl)
                sub 12
                ret
calc_title_right add a,(hl)
                sub 4
                ret

dir_first       call AG_FILES+6
                jr dir_result
dir_next        call AG_FILES+9
dir_result      ex de,hl
                ld a,h
                or l
                ret z
                scf
                ret
entry_name      call AG_FILES+12
                ex de,hl
                ret
no_read         ld bc,0
no_result       xor a
                ret
clip_set        ld a,d
                cp 2
                jr c,clip_set_n
                ld de,512
clip_set_n      ld (ag_clip_count),de
                ld b,d
                ld c,e
                ld de,#3e00
                jr clip_move
clip_get        ex de,hl
                ld bc,(ag_clip_count)
                or a
                sbc hl,bc
                jr nc,clip_get_n
                add hl,bc
                ld b,h
                ld c,l
clip_get_n      ld hl,#3e00
clip_move       ld a,b
                or c
                ret z
                push bc
                ldir
                pop bc
                ret
clip_size       ld bc,(ag_clip_count)
                ret
ag_clip_count   dw 0
icon_full       ld e,0
                jr icon_draw
icon_half       ld e,1
icon_draw       ld (AG_ICON_ARGS),a
                ld a,b
                ld (AG_ICON_ARGS+1),a
                ld a,c
                ld (AG_ICON_ARGS+2),a
                ld a,e
                ld (AG_ICON_ARGS+3),a
                jp AG_FILES+30

; Window state and load requests are owned by the resident workspace.
guest_footer    ld a,(AG_FILEMODE)
                or a
                ld hl,app_setup
                jr z,footer
                cp 3
                ld hl,note_setup
                jr z,footer
                cp 2
                ld hl,pic_setup
                jr z,footer
                cp 6
                ld hl,viewer_setup
                jr z,footer
                cp 5
                ld hl,xaos_setup
                jr z,footer
                ld hl,fm_setup
                jr footer
unsupported     ld hl,no_handler
footer          push hl
                inc hl
                inc hl
                ld a,(AG_VGA)
                or a
                ld a,29
                jr z,footer_row
                ld a,59
footer_row      ld (hl),a
                pop hl
                ld bc,42             ; TAB + 39 printable columns
                call send
                ld bc,0
                xor a
                ret

send            db #49                ; RST.LIS 18: Z80 caller -> MOS block output
                rst #18
                ret
close_label     db "X",0
view_label      db "View",0
app_setup       db 31,0,AG_STATUS_ROW," Calculator   Space: click  Esc: close "
app_setup_end
fm_setup        db 31,0,AG_STATUS_ROW," File Manager  V: view  Space: click   "
pic_setup       db 31,0,AG_STATUS_ROW," Picture preview            Esc: close "
note_setup      db 31,0,AG_STATUS_ROW," Shift+Space: click   Ctrl-W: switch   "
no_handler      db 31,0,AG_STATUS_ROW," Cannot open file on this Agon build   "
xaos_setup      db 31,0,AG_STATUS_ROW," XAOS   +/-: zoom  R: reset  Esc: close"
viewer_setup    db 31,0,AG_STATUS_ROW," Viewer   +/-: zoom         Esc: close "
                assert fm_setup+42 == pic_setup
                assert pic_setup+42 == note_setup
                assert note_setup+42 == no_handler
                assert no_handler+42 == xaos_setup
                assert xaos_setup+42 == viewer_setup
                assert viewer_setup+42 == $
wnd_title       equ #17e4
                include "draw.asm"
