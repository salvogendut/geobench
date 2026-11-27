; Single managed guest, in the MOS program's Z80 segment. Install this image
; at GB_KERNEL before entering the separately loaded, unmodified CALC.APP.
                include "../../lib/gbapp.inc"
PREEMPTIVE      equ 0
                include "../lowram.inc"
                include "input.inc"
                org GB_KERNEL
                jp run_guest
                defs GB_TEXT-$,#c9
                jp draw_text
                defs GB_DIR1-$,#c9
                jp dir_first
                jp dir_next
                defs GB_POLL-$,#c9
                jp AG_POLL_HELD
                defs GB_FRAME-$,#c9   ; cursor show/hide unnecessary for VDP overlay
                jp draw_frame
                defs GB_ICON-$,#c9
                jp icon_full
                jp draw_fill
                defs GB_FSLOAD-$,#c9
                jp no_read
                jp no_result         ; read-only filesystem
                defs GB_GETKEY-$,#c9
                jp get_key
                defs GB_RESTPAR-$,#c9
                jp request_draw
                defs GB_WMOPEN-$,#c9
                jp open_child
                jp close_guest
                defs GB_ISDIR-$,#c9
                jp AG_FILES+15
                jp AG_FILES+18
                jp AG_FILES+21
                jp entry_name
                jp no_result         ; no drag/drop
                defs GB_GETDRIVE-$,#c9
                jp no_result         ; single SD volume
                defs GB_WMLAUNCHAS-$,#c9
                jp open_document
                defs GB_FSFREE-$,#c9
                jp no_result         ; free space unknown
                jp icon_half
                defs GB_UI-$,#c9
                jp unsupported
                defs GB_WMMANAGED-$,#c9
                jp register_window
                defs GB_NET+3-$,#c9   ; reserve the complete public table

run_guest       xor a
                ld (active),a
                ld (pending_key),a
                ld (need_draw),a
                call guest_footer
                call APP_BASE         ; app registers a descriptor, then RETs
event_loop      ld a,(active)
                or a
                jp z,finished
                ld a,(need_draw)
                or a
                jr z,await_input
                xor a
                ld (need_draw),a
                ld a,(AG_FILEMODE)
                or a
                jr z,draw_title
                ld bc,(MW_RECT)
                ld de,(MW_RECT+2)
                call unpack_rect
                xor a
                call draw_fill
                ld bc,(MW_RECT)
                ld de,(MW_RECT+2)
                call unpack_rect
                ld a,2
                call draw_frame
draw_title
                call window_title
                ld a,GB_MSG_DRAW
                call dispatch
                jr event_loop
await_input     call AG_WAIT_INPUT
                cp ' '
                jr nz,guest_key
                call AG_WM+6
                or a
                jr z,event_loop
                cp 1
                jr z,guest_client
                cp 2
                jr z,guest_close
                cp 3
                jr z,guest_view
                call request_draw
                jr event_loop
guest_view
                ld a,'v'
                jr guest_key
guest_close
                ld a,GB_MSG_CLOSE
                jr send_msg
guest_client    ld a,GB_MSG_CLICK
                jr send_msg
guest_key
                cp 27
                ld (pending_key),a
                ld a,GB_MSG_FRAME
                jr nz,send_msg
                ld a,GB_MSG_CLOSE
send_msg        call dispatch
                jp event_loop
finished        jp AG_WM+9

; The descriptor is in the guest page. Its proc handles DRAW/FRAME/CLOSE
; after the app's entry has returned, exactly as on the established targets.
register_window ld de,MW_RECT
                ld bc,4
                ldir
                push hl
                call AG_WM+3
                pop hl
                inc hl                ; min_w, min_h
                inc hl
                ld e,(hl)
                inc hl
                ld d,(hl)
                ld (wnd_proc),de
                inc hl
                ld (wnd_title),hl    ; FM sets its title pointer after registering
                ld a,1
                ld (active),a
request_draw    ld a,1
                ld (need_draw),a
                ret
close_guest     xor a
                ld (active),a
                ret
get_key         ld hl,pending_key
                ld a,(hl)
                ld (hl),0
                ret
dispatch        ld (GB_MSG),a
                ld hl,(wnd_proc)
                jp (hl)

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
title_right     ld a,(MW_RECT)
                ld hl,MW_RECT+2
                add a,(hl)
                ld hl,AG_FILEMODE
                bit 0,(hl)
                jr z,calc_title_right
                sub 12
                ret
calc_title_right sub 4
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

; A single suspended parent. The directory context and the CPU stack stay
; resident; restore the entire app page even if the child fails validation.
open_document   call AG_FILES+42     ; selected PIC, not the handler's executable path
                ld a,2
                jr enter_child
open_child      call AG_FILES+24     ; HL = selected raw name; DE = checked path
                xor a
enter_child     ld (child_mode),a
                ld a,d
                or e
                jp z,unsupported
                ld (AG_DOC_PATH),de
                push ix
                push iy
                call save_parent
                ld hl,(MW_RECT)
                push hl
                ld hl,(MW_RECT+2)
                push hl
                ld hl,(wnd_proc)
                push hl
                ld hl,(wnd_title)
                push hl
                ; Path is in MOS adapter RAM, outside the guest and its copy.
                ld a,(child_mode)
                or a
                jr nz,load_preview
                ld hl,(AG_DOC_PATH)
                call AG_LOAD_CALC
                jr child_loaded
load_preview    call AG_LOAD_PREVIEW
child_loaded
                push af
                jr nz,child_done
                ld a,(child_mode)
                ld (AG_FILEMODE),a
                call run_guest
child_done      pop af
                pop hl
                ld (wnd_title),hl
                pop hl
                ld (wnd_proc),hl
                pop hl
                ld (MW_RECT+2),hl
                pop hl
                ld (MW_RECT),hl
                push af               ; preserve the checked loader's Z result
                call restore_parent
                pop af
                pop iy
                pop ix
                ld a,1
                ld (AG_FILEMODE),a
                ld (active),a
                jr nz,unsupported
                ld (need_draw),a
guest_footer    ld a,(AG_FILEMODE)
                or a
                ld hl,app_setup
                jr z,footer
                cp 2
                ld hl,pic_setup
                jr z,footer
                ld hl,fm_setup
                jr footer
unsupported     ld hl,no_handler
footer          ld bc,42             ; TAB + 39 printable columns
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
no_handler      db 31,0,AG_STATUS_ROW," Cannot open file on this Agon build   "
                assert fm_setup+42 == pic_setup
                assert pic_setup+42 == no_handler
                assert no_handler+42 == $
active          db 0
pending_key     db 0
need_draw       db 0
wnd_proc        dw 0
wnd_title       dw 0
child_mode      db 0
; Use the next 64 KiB of Agon user RAM for the suspended guest. Both effective
; addresses are based on MOS's current MBASE, not a hardcoded load address.
save_parent     db #ed,#6e            ; LD A,MB
                ld (copy_from),a
                inc a
                jr copy_to_bank
restore_parent  db #ed,#6e
                ld (copy_to),a
                inc a
                ld (copy_from),a
                jr copy_guest
copy_to_bank    ld (copy_to),a
copy_guest      db #5b,#21            ; LD.LIL HL,bank:APP_BASE
                dw APP_BASE
copy_from       db 0
                db #5b,#11            ; LD.LIL DE,bank:APP_BASE
                dw APP_BASE
copy_to         db 0
                db #5b,#01,0,#40,0,#5b,#ed,#b0  ; LD.LIL BC,#4000 / LDIR.LIL
                ret
                include "draw.asm"
