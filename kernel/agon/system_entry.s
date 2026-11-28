        .module system_entry
        .globl _sys_init, _sys_event, _sys_open, _sys_text_focus, _cfg_handle
        .globl _ag_load, _ag_handle, _ag_name, _ag_setname, _ag_doc_read, _ag_doc_save, _ag_dialog
        .globl _ws_run, _ws_register, _ws_damage, _ws_close, _ws_key, _ws_request_app, _ws_request_doc
        .area _CODE
        jp _sys_init
        jp _sys_event
        jp _sys_open
        jp pixel_fence          ; compatibility fence, A = byte scanline
        jp _ag_load
        jp _ws_run
        jp _ws_register
        jp _ws_damage
        jp _ws_close
        jp _ws_key
        jp _ws_request_app
        jp _ws_request_doc
        jp get_name
        jp _ag_setname
        jp doc_read
        jp doc_save
        jp dialog
        jp _sys_text_focus
        .globl _ag_path, _ag_files_action
        jp _ag_path             ; a036: active guest's absolute path
        jp _ag_files_action     ; a039: managed Files action
        .globl _sys_probe_visible
        jp _ws_store
        jp _ws_fetch
        jp _sys_probe_visible
        jp 0x3b2c               ; a045: start a pure worker (SDCC: HL=fn, DE=progress)
_ag_worker_stop::
        jp 0x3b23               ; a048: cancel only the focused owner's worker
        .globl _ag_export_pic, _ag_validate_next
        jp _ag_export_pic
        jp _ag_validate_next
get_name:
        call _ag_name
        ex de,hl
        ret
doc_read:
        call _ag_doc_read
        ld b,d
        ld c,e
        ret
doc_save:
        call _ag_doc_save
        rrca                    ; bool -> saved in carry
        ret
dialog:
        call _ag_dialog
        ld c,a
        ld b,#0
        ret
_ws_input::  jp 0x0003
_ws_entry::  jp 0x4000
_ws_chrome:: jp 0x80c0
_ws_footer:: jp 0x80c3
_ws_error::  jp 0x80c6
_ws_target:: jp 0x8818
_ws_document:: jp 0x882a
_ws_selected:: jp 0x882d
; Three 16 KiB backing slots within the same next 64 KiB segment the
; bootstrap already used for its suspended parent. SP stays in MOS user RAM.
_ws_fetch::
        call page_addresses
        jr copy_page
_ws_store::
        call page_addresses
        .db 0x5b
        ex de,hl
copy_page:
        .db 0x5b,0x01,0,0x40,0,0x5b,0xed,0xb0
        ret
page_addresses:
        rrca
        rrca
        ld (backing+2),a
        .db 0xed,0x6e           ; LD A,MB
        ld (active+3),a
        inc a
        ld (backing+3),a
        .db 0x5b
backing: .db 0x21,0,0,0
        .db 0x5b
active:  .db 0x11,0,0x40,0
        ret
_ag_create_new::
        ld c,#6                 ; FA_CREATE_NEW | FA_WRITE
        jr ag_open
_ag_read_open::
        ld c,#1
ag_open:ld a,#0x0a
        jr ag_mos
_ag_read::
        ld a,#0x1a
        jr ag_io
_ag_write::
        ld a,#0x1b
; SDCC supplies a 16-bit count, but MOS consumes the full 24-bit DE.
ag_io:  push de
        .db 0x5b,0x11,0,0,0
        pop de
        push af
        ld a,(_ag_handle)
        ld c,a
        pop af
        jr ag_mos
_ag_close::
        ld c,a
        ld a,#0x0b
        jr ag_mos
_ag_rename::
        ld a,#6
        jr ag_mos
_ag_delete::
        ld a,#5
        jr ag_mos
_ag_mkdir::
        ld a,#7
        jr ag_mos
_ag_stat::
        ld a,#0x96
        jr ag_mos
_ag_dopen::
        ld a,#0x91
        jr ag_mos
_ag_dread::
        ld a,#0x93
        jr ag_mos
_ag_dclose::
        ld a,#0x92
ag_mos: .db 0x5b
        push ix
        .db 0x5b
        push iy
        .db 0x49
        rst 0x08
        .db 0x5b
        pop iy
        .db 0x5b
        pop ix
        ret
_sys_send::
        ld b,d
        ld c,e
        .db 0x49
        rst 0x18
        ret
_sys_rtc::
        ld a,#0x12
        jr mos
_cfg_open::
        ld c,#1
        jr open
_cfg_create::
        ld c,#0x0a              ; FA_CREATE_ALWAYS | FA_WRITE
open:   ld a,#0x0a
        jr mos
_cfg_read::
        ld a,#0x1a
        jr transfer
_cfg_write::
        ld a,#0x1b
transfer:
        .db 0x5b,0x11,8,0,0     ; MOS requires a 24-bit count
        push af
        ld a,(_cfg_handle)
        ld c,a
        pop af
        jr mos
_cfg_close::
        ld c,a
        ld a,#0x0b
mos:    .db 0x49
        rst 0x08
        ret
_layer_open::
        jp 0x0903
_layer_click::
        jp 0x0906
_layer_close::
        jp 0x0909

; Synchronous point query for MOS 2.x as well as 3.x. The tick deadline
; prevents a VDP with no response from trapping the desktop in the saver.
pixel_fence:
        ld l,a
        ld h,#0
_saver_pixel::
        ld (pixel_y),hl
        ld hl,#319
        ld a,(0x17eb)
        or a
        jr z,pixel_width
        ld hl,#639
pixel_width:
        ld (pixel_x),hl
        .db 0x5b
        push ix
        ld a,#8
        .db 0x49
        rst 0x08
        .db 0x5b
        res 2,4(ix)
        ld hl,#pixel_query
        ld bc,#7
        .db 0x49
        rst 0x18
        .db 0x5b
        ld b,0(ix)
pixel_wait:
        .db 0x5b
        bit 2,4(ix)
        jr nz,pixel_ready
        halt
        .db 0x5b
        ld a,0(ix)
        sub b
        cp #100
        jr c,pixel_wait
        ld a,#255
        jr pixel_done
pixel_ready:
        .db 0x5b
        ld a,0x16(ix)
pixel_done:
        .db 0x5b
        pop ix
        ret
pixel_query:
        .db 23,0,0x84
pixel_x: .dw 319
pixel_y: .db 0,0
